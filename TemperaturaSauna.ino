/**
 * Medir temperatura da sauna com um termistor ntc 
 * 16x2 LCD display apresenta valor
 *
 *    - usa esp12E (caixas casas)
 *    - usa um termistor NTC
 *    - us um 16x2 LCD display
 *
 *  Podemos acessar os dados via msg wifi udp broadcast porta 8555,  via pagina http e via msg MQTT
 *  
 *  Para primeira conexao com wifi usar senha 12345678 no Ap TemperaturaSauna http://192.168.4.1 
 *     usado para configurar a conexao com um novo roteador
 *  
 *  Podemos supervisionar status do sistema pelo link http://"ip da conexao"  port 80
 *    ou atraves do led azul da placa: acessa=aguardando_conexao_wifi  piscando=conectado_funcionamento_ok
 *  
 *  Permite que o software seja atualizado via web OTA (http://ip/update)  
 *
 *  Para reset do wifi (apaga a configuracao SSID / Senha)  enviar resetnet via http (http://ip/resetnet)
 *
 *  Pode receber msg udp na porta 4210 
 *      
 *
*/

//#include "thermistor.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>//Biblioteca do UDP.
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h> // Importa a Biblioteca PubSubClient MQTT
#include <time.h>
#include <ESP8266HTTPUpdateServer.h>

#ifndef APSSID
#define APSSID "TemperaturaSauna" 
#define APPSK  "12345678"
#endif

const char* ntpServer = "br.pool.ntp.org";
const long  gmtOffset_sec = -3L * 60L * 60L;; // fuso horario -3h em seg
const int   daylightOffset_sec = 0; // horario de verao

//create an object from the UpdateServer - atualizar o firmware via http
ESP8266HTTPUpdateServer httpUpdater;
ESP8266WebServer server(80);

WiFiManager wifiManager;

#define TOPICO_ENVIA   "jmmoraessilva/sitio/temperaturasauna"    //tópico MQTT de envio medicao da altura para Broker
#define TOPICO_RECEBE   "jmmoraessilva/sitio/configsauna"    //tópico MQTT de envio / recebimento de informações do Broker
#define ID_MQTT  "jmmoraessilva_sitio_sauna"     //id mqtt (para identificação de sessão)
                               //IMPORTANTE: este deve ser único no broker (ou seja, 
                               //            se um client MQTT tentar entrar com o mesmo 
                               //            id de outro já conectado ao broker, o broker 
                               //            irá fechar a conexão de um deles).
// MQTT
//const char* BROKER_MQTT = "broker.hivemq.com"; //URL do broker MQTT que se deseja utilizar
//const char* BROKER_MQTT = "mqtt.eclipseprojects.io"; //URL do broker MQTT que se deseja utilizar mqtt.eclipseprojects.io:1883
const char* BROKER_MQTT = "test.mosquitto.org"; //URL do broker MQTT que se deseja utilizar 
int BROKER_PORT = 1883; // Porta do Broker MQTT
WiFiClient espClient; // Cria o objeto espClient
PubSubClient MQTT(espClient); // Instancia o Cliente MQTT passando o objeto espClient

// Analog pin used to read the NTC
//#define NTC_PIN               A0   //arduino nano A!
//#define NTC_PIN                 0  //pino gpio00 do esp01

// Thermistor object
// THERMISTOR thermistor(NTC_PIN,        // Analog pin
//                       51000,          // Nominal resistance at 25 ºC 50k ohm
//                       3950,           // thermistor's beta coefficient 3950
//                       50800);         // Value of the series resistor 51k

// Global temperature reading
//uint16_t temp;
float temp;
OneWire barramento(D4);
DallasTemperature sensor(&barramento);

// configura 16x2 LCD display 
#define SDA_PIN 4 /* Define the SDA pin here A4  */
#define SCL_PIN 5 /* Define the SCL Pin here A5 */
LiquidCrystal_I2C lcd(0x27,16,2);  /* set the LCD address to 0x27 for a 16 chars and 2 line display */

// Define o caractere personalizado para o símbolo de grau Celsius
byte degree[8] = {
  B00111,
  B00101,
  B00111,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000
};

// servico de msg UDP
#define HOST_PORT   (8555)         // porta servidor udp
IPAddress broadcastIp(255,255,255,255);
IPAddress netmask(255, 255, 255, 0);
String StatusConexao=""; // iniciado , conectado , espera
WiFiUDP udp; //Cria um objeto da classe UDP.
IPAddress myIP ; // ip local
int localUdpPort = 4210;
String MsgRecebida;
char PacketRecebe[100];
char PacketEnvia[50] = "Msg enviada";
String MsgDebug1 = "Iniciando ...";
String MsgDebug2 = "";
String MsgDebug3 = "";
String MsgDebug4 = "";

// pisca led 
// ESP8622 (NodeMCU LED = gpio16 D0  /  ESP12 led = GPIO02 D4 )
#define led8622 2
//#define led8622 D4
long ultimapisca = -50000;
long intervalopisca = 2000; // 2 seg

// envia msg ok de tempos em tempos e ler nivel da caixa
long ultimoenviook = -50000;
long intervaloenviook = 10000; // 10 seg

//==============================================
void ApagaLed( ) {digitalWrite(led8622, HIGH);}
void LigaLed() {digitalWrite(led8622, LOW);}

//=======================================================================
String PageHTML() {
 String  txtpage = "";
 String  auxtxt = "";
  txtpage += "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">";
  txtpage += "<html>";
  txtpage += "<head>";
  txtpage += "  <meta http-equiv=\"content-type\" content=\"text/html; charset=windows-1252\"/>";
  txtpage += "  <title> Medidor da Temperatura da Sauna </title>";
  txtpage += "  <style type=\"text/css\"> p { margin-left: 0.75cm; line-height: 100%; background: transparent } </style>";
  txtpage += "</head>";
  txtpage += "<body lang=\"pt-BR\" dir=\"ltr\">";
//  <p style=\"margin-bottom: 0cm; background: #558b9d"; line-height: 100%\">";
//  txtpage += "<font color=\"#00008b\"><font size=\"5\" style=\"font-size: 14pt\"> Medidor Nivel da Caixa de Agua " + auxtxt + " </font></font></p>";
  txtpage += "<p style=\"margin-bottom: 0cm; background: #558b9d; line-height: 200%\"><font color=\"#ffffff\"><font size=\"6\" style=\"font-size: 16pt\">Medidor Temperatura Sauna " + auxtxt  + "</font></font></p>";
  //txtpage += "<p style=\"margin-bottom: 0cm; line-height: 100%\"><br/></p>";
  auxtxt = WiFi.SSID();
  txtpage += "<p style=\"margin-bottom: 0cm; line-height: 100%\"><font color=\"#000000\"><font size=\"4\" style=\"font-size: 12pt\">Status da conex&atilde;o  Conectado na rede Wifi: " + auxtxt  + "</font></font></p>";
  auxtxt = myIP.toString().c_str();
  txtpage += "<p style=\"margin-bottom: 0cm; line-height: 100%\"><font color=\"#000000\"><font size=\"4\" style=\"font-size: 12pt\"> IP do dispositivo: " + auxtxt + "</font></font></p>";
  auxtxt = String(localUdpPort) +"  |  envia broadcast: " + HOST_PORT;
  txtpage += "<p style=\"margin-bottom: 0cm; line-height: 100%\"><font color=\"#000000\"><font size=\"4\" style=\"font-size: 12pt\"> Porta UDP - recebe msg: " + auxtxt + "</font></font></p>";
  txtpage += "<p style=\"margin-bottom: 0cm; line-height: 100%\"><br/></p>";
  auxtxt = String(temp) + " &deg;C " ;
  txtpage += "<p style=\"margin-bottom: 0cm; line-height: 100%\"><font color=\"#000080\"><font size=\"6\" >  Temperatura interior sauna: " + auxtxt + "</font></font></p>";
  txtpage += "<p style=\"margin-bottom: 0cm; line-height: 100%\"><br/></p>";
  txtpage += "<p style=\"margin-bottom: 0cm; line-height: 100%\"><font color=\"#000000\"><font size=\"4\" style=\"font-size: 12pt\">Mensagens de status: </font></font></p>";
  txtpage += "<p style=\"margin-bottom: 0cm; margin-left: 0.9cm; line-height: 100%\"><font color=\"#000000\"><font size=\"4\" style=\"font-size: 12pt\">   Msg1 : " + MsgDebug1 + "</font></font></p>";
  txtpage += "<p style=\"margin-bottom: 0cm; margin-left: 0.9cm; line-height: 100%\"><font color=\"#000000\"><font size=\"4\" style=\"font-size: 12pt\">   Msg2 : " + MsgDebug2 + "</font></font></p>";
  txtpage += "<p style=\"margin-bottom: 0cm; margin-left: 0.9cm; line-height: 100%\"><font color=\"#000000\"><font size=\"4\" style=\"font-size: 12pt\">   Msg3 : " + MsgDebug3 + "</font></font></p>";
  txtpage += "<p style=\"margin-bottom: 0cm; margin-left: 0.9cm; line-height: 100%\"><font color=\"#000000\"><font size=\"4\" style=\"font-size: 12pt\">   Msg4 : " + MsgDebug4 + "</font></font></p>";
  auxtxt =  getTime() + "  || atualizar software - http://ip/update || reset dados wifi - http://ip/resetnet ";
  txtpage += "<p style=\"margin-bottom: 0cm; background: #558b9d; line-height: 180%\"><font color=\"#ffffff\"><font size=\"4\" style=\"font-size: 14pt\"> " + auxtxt + "</font></font></p>";
  txtpage += "</body>";
  txtpage += "</html>";  
  return txtpage;
}

String NovaMsgDebug( String msg) {
  MsgDebug1 = MsgDebug2;
  MsgDebug2 = MsgDebug3;
  MsgDebug3 = MsgDebug4;
  msg += " --> " + getTime();
  MsgDebug4 = msg;
  return msg;
}

//==============================================================================
void EnviaMsgUDP(String msg)  //char msg[50])
{ // envia uma msg texto via UDP broadcastIp
  udp.beginPacket(broadcastIp, HOST_PORT);//Inicializa o pacote de transmissao ao broadcastIp e PORTA.
  //Adiciona-se o valor ao pacote.
  char str_arraymsg[msg.length()+1];
  msg.toCharArray(str_arraymsg, msg.length()+1);
  int i = 0;
  while (str_arraymsg[i] != 0) 
    udp.write((uint8_t)str_arraymsg[i++]);
  udp.endPacket();//Finaliza o pacote e envia.
  // Serial.println(NovaMsgDebug("--- Enviada msg udp: " +msg) + " IP: " + myIP.toString());
}

//=======================================================================
void RecebeMsgUDP(String msg_recebida)
{ // recebe uma msg texto do tipo udp
  msg_recebida = "";
  int packetSize = udp.parsePacket();
  if (packetSize) {
    // recebe UDP packets
    //Serial.printf("Recebe %d bytes from %s, port %d\n", packetSize, udp.remoteIP().toString().c_str(), udp.remotePort());
    int len = udp.read(PacketRecebe, 100);
    if (len > 0) {
      PacketRecebe[len] = 0;
      uint8_t buffer[100] = {0};  
      for (uint32_t i = 0; i < len; i++) {
        msg_recebida += ((char)buffer[i]); // armazena os caracteres para string
      }
      Serial.print("Packet recebido: ");
      Serial.println(PacketRecebe);
      Serial.println(msg_recebida);
      Serial.println(NovaMsgDebug("--- Recebida msg udp: " + msg_recebida));// Serial.println(msg);
    }
  }
}

void handleRoot() {
  server.send(200, "text/html", PageHTML());
}

void ResetNet() {
  Serial.println(NovaMsgDebug("Recebida msg para reset do Wifi"));
  EnviaMsgUDP("Resete wifi"); 
  server.send(200, "text/html", "OK reset wifi");
  wifiManager.resetSettings();
  ESP.restart();
}


/**
 * setup
 */
void setup()
{
  Serial.begin(115200);
  Serial.println("Inicializando...");
  
  // modulo led ESP8622
  pinMode(led8622, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  LigaLed();

  //Wire.begin(SDA_PIN, SCL_PIN);     // pin sda , scl
  Wire.begin();                       // no nano os pin sda , scl sao fixos
  lcd.init();                       // Initialize the LCD
  lcd.backlight();                  // Turn on the backlight
  lcd.clear();                      // Clear the LCD screen
  lcd.createChar(0, degree);       // Carrega o caractere personalizado no CGRAM
  lcd.setCursor(0, 0);               // Set the cursor to the first column and first row
  lcd.print(" TEMP. INTERNA ");     // Print some text
  lcd.setCursor(2,1);
  lcd.print("Config...");

   sensor.begin(); 

  //============= inicializando wifi =======================================
  Serial.println();
  // reset settings - wipe stored credentials for testing - usar somente para teste
  // these are stored by the esp library
  //wifiManager.resetSettings();
  wifiManager.setConfigPortalTimeout(240);
  // it is a good practice to make sure your code sets wifi mode how you want it.
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  //Tenta conectar no SSID e senha salvos, se não consegue 
  //    cria um AP (Access Point) com: ("nome da rede", "senha da rede") e IP  192.168.4.1
  if (!wifiManager.autoConnect(APSSID, APPSK)) {
    Serial.println(F("Falha na conexao. Resetar e tentar novamente..."));
    delay(1000);
    ESP.restart();
    //delay(5000);
  }

  //conexao com roteador Ok
  myIP = WiFi.localIP();
  String aux =  WiFi.localIP().toString();
  Serial.println(NovaMsgDebug("Conectado na rede Wifi: " + WiFi.SSID() + "  /  Endereco IP: " + aux));
   
   // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // comunicacao broker MQTT 
  initMQTT();
  
  MDNS.begin(APSSID);
  server.on("/", handleRoot);
  server.on("/resetnet", ResetNet);
  httpUpdater.setup(&server); 
  server.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.println(NovaMsgDebug("HTTPserver e HttpUpdater foram inicializados"));
  
  // inciar servico UDP
  udp.begin(localUdpPort);
  Serial.print("UDP pronto IP: ");  Serial.println(WiFi.localIP().toString().c_str());
  Serial.print("UDP port: ");  Serial.println(localUdpPort);

}

/**
 * loop
 *
 * Arduino main loop
 */
void loop()
{
   // Serial.println("INICIO LOOP " ); 
  server.handleClient();
  MDNS.update();
  //garante funcionamento da conexao ao broker MQTT
  VerificaConexoesMQTT();
  //delay(500);
  MQTT.loop(); // recebe msg MQTT 
  //======================================================= 
  // envia ip periodicamente = keep alive e recebe msg udp
  // e mede o nivel da caixa - 10seg
  if (millis() - ultimoenviook > intervaloenviook) { // 10seg
    LerTemperatura();
    ultimoenviook = millis();
    // recebe msg udp
    //RecebeMsgUDP(MsgRecebida);
    //Serial.print(F("Recebeu msg udp: "));
    //Serial.println(MsgRecebida);
  }

  //======================================================= 
  // pisca led periodicamente 
  if (((millis() - ultimapisca) > (intervalopisca))) {
    LigaLed();
    ultimapisca = millis();
  } 
  else {
    if (((millis() - ultimapisca) > 400)) { ApagaLed(); }
  }
  
}


String getTime() { 
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return String((millis()/1000)/60) + " min";
  }
  char datahoraaux[20];
  strftime(datahoraaux,20, "%d/%m/%Y %H:%M:%S", &timeinfo); // %B
  return datahoraaux;
}

void LerTemperatura() {
  // mede a temperatura no interior da sauna
  //temp = thermistor.read();   // Read temperature
  //temp = temp / 10;
  //Serial.print("Temperatura em °C : ");
  //Serial.println(temp);
  // Manda comando para ler temperaturas
  sensor.requestTemperatures(); 
  temp = sensor.getTempCByIndex(0);

  lcd.clear();  
  lcd.setCursor(0, 0);               // Set the cursor to the first column and first row
  lcd.print(" TEMP. INTERNA ");     // Print some text
  lcd.setCursor(4,1);
  lcd.print(temp);
  lcd.print(" ");     // Print some text
  lcd.write(byte(0));              // Exibe o símbolo de grau Celsius
  lcd.print("C");     // Print some text
     
  //Enviar msg de texto com o valor da temperatura para o servidor MQTT / msg UDP / debug serial
  //Serial.printf("Distance (cm): %d. %d%%\r\n", distance, waterLevelAsPer); 
  String textoMsg = "Temperatura sauna: " + String(temp) + " C" ;
  EnviaMsgUDP("http://" + myIP.toString() + "/ " + textoMsg );
  Serial.println(NovaMsgDebug(textoMsg));
  textoMsg = String(temp) + "°C --> " + getTime();
  MQTT.publish(TOPICO_ENVIA, textoMsg.c_str(),true);  // retem msg no mqtt

}

// ======= MQTT servico =============================

//Função: inicializa parâmetros de conexão MQTT(endereço do 
//        broker, porta e seta função de callback)
void initMQTT() 
{
    MQTT.setServer(BROKER_MQTT, BROKER_PORT);   //informa qual broker e porta deve ser conectado
    MQTT.setCallback(mqtt_callback);            //atribui função de callback (função chamada quando qualquer informação de um dos tópicos subescritos chega)
}

//Função: verifica o estado das conexao ao broker MQTT. 
//        Em caso de desconexão a conexão é refeita.
void VerificaConexoesMQTT()
{
    if (!MQTT.connected()) 
        reconnectMQTT(); //se não há conexão com o Broker, a conexão é refeita    
}
 
//Função: reconecta-se ao broker MQTT (caso ainda não esteja conectado ou em caso de a conexão cair)
//        em caso de sucesso na conexão ou reconexão, o subscribe dos tópicos é refeito.
void reconnectMQTT() 
{
    //while (!MQTT.connected()) 
    //{
        Serial.print("* Tentando se conectar ao Broker MQTT: ");
        Serial.println(BROKER_MQTT);
        if (MQTT.connect(ID_MQTT)) 
        {
            Serial.println(NovaMsgDebug("Conectado com sucesso ao broker MQTT!"));
            //MQTT.subscribe(TOPICO_RECEBE); // recebe msg do broker mqtt
            MQTT.publish(TOPICO_RECEBE,  myIP.toString().c_str(),true);  // retem msg no mqtt do end ip
        } 
        else
        {
            Serial.println(NovaMsgDebug("Falha ao reconectar no broker."));
            Serial.println("Havera nova tentatica de conexao em 2s");
            //delay(2000);
        }
    //}
}

//Função: função de callback 
//        esta função é chamada toda vez que uma informação de 
//        um dos tópicos subescritos chega)
void mqtt_callback(char* topic, byte* payload, unsigned int length) 
{
    String msg;
     //obtem a string do payload recebido
    for(int i = 0; i < length; i++) {
       char c = (char)payload[i];
       msg += c;
    }
   
    //toma ação dependendo da string recebida:
    if (msg.equals("IP")) {
        Serial.println(NovaMsgDebug("Broker MQTT ENVIOU IP "));
        MQTT.publish(TOPICO_ENVIA, myIP.toString().c_str());    
    }    
}
