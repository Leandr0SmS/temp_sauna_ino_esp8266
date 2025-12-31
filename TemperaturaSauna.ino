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
  String txtpage = "";
  txtpage += "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0 Transitional//EN\">";
  txtpage += "<html>";
  txtpage += "<head>";
  txtpage += "  <meta http-equiv=\"content-type\" content=\"text/html; charset=windows-1252\"/>";
  txtpage += "  <title> Sauna </title>";
  txtpage += "  <style type=\"text/css\">";
  txtpage += "    * {margin: 0;padding: 0;box-sizing: border-box;font-family: monospace,'Courier New', Courier;}";
  txtpage += "    body {padding: 1em;background-color: rgb(245, 253, 253);}";
  txtpage += "    .title {text-align: center;font-size: 4em;}";
  txtpage += "    #termometro-container {";
  txtpage += "      display: flex;";
  txtpage += "      justify-content: center;";
  txtpage += "      align-items: center;";
  txtpage += "    }";
  txtpage += "    #termometro-div {";
  txtpage += "      font-size: 2.5em;";
  txtpage += "      border-radius: 45%;";
  txtpage += "      background: radial-gradient(circle at center, #fb853c 22%, #fcff47);";
  txtpage += "      box-shadow: 0 0 6rem 2rem #fff018;";
  txtpage += "      backdrop-filter: blur(8px);";
  txtpage += "      -webkit-backdrop-filter: blur(8px);";
  txtpage += "      padding: 1em;";
  txtpage += "      color: white;";
  txtpage += "      display: flex;";
  txtpage += "      justify-content: center;";
  txtpage += "      align-items: center;";
  txtpage += "      font-weight: bold;";
  txtpage += "      height: 17rem;";
  txtpage += "      width: 17rem;";
  txtpage += "    }";
  txtpage += "    #status{";
  txtpage += "      display: none;";
  txtpage += "      margin-top: 1.5rem;";
  txtpage += "      font-size: 0.8em;";
  txtpage += "    }";
  txtpage += "    .space {padding-left: 2rem;}";
  txtpage += "    .center{text-align: center;}";
  txtpage += "    .sub-title{font-weight: 700;}";
  txtpage += "    .hide {";
  txtpage += "      opacity: 0;";
  txtpage += "    }";
  txtpage += "    .show {";
  txtpage += "      opacity: 1;";
  txtpage += "    }";
  txtpage += "    #termometro-container, #status {";
  txtpage += "      transition: opacity 0.3s;";
  txtpage += "    }";
  txtpage += "    #optionsIcon {";
  txtpage += "      opacity: 0.6;";
  txtpage += "    }";
  txtpage += "        #optionsIcon:hover{";
  txtpage += "      cursor: pointer;";
  txtpage += "      opacity: 0.9;";
  txtpage += "      transform: scale(1.1);";
  txtpage += "    }";
  txtpage += "  </style>";
  txtpage += "</head>";
  txtpage += "<body lang=\"pt-BR\" dir=\"ltr\">";
  txtpage += "  <svg id=\"optionsIcon\" height=\"2rem\" viewBox=\"0 0 20 20\" version=\"1.1\" xmlns=\"http://www.w3.org/2000/svg\">";
  txtpage += "    <g id=\"layer1\">";
  txtpage += "      <path d=\"M 9.9980469 0 L 9.328125 0.0234375 L 8.6621094 0.08984375 L 8 0.203125 L 8 2.2539062 L 7.4628906 2.4121094 L 6.9375 2.609375 L 6.4277344 2.8398438 L 5.9375 3.1074219 L 4.4863281 1.6582031 L 3.9375 2.046875 L 3.4199219 2.4707031 L 2.9296875 2.9296875 L 2.4726562 3.4179688 L 2.046875 3.9394531 L 1.6582031 4.484375 L 3.1074219 5.9375 L 2.8417969 6.4296875 L 2.609375 6.9394531 L 2.4140625 7.4628906 L 2.2539062 8 L 0.203125 8 L 0.091796875 8.6621094 L 0.0234375 9.3300781 L 0 10 L 0.0234375 10.669922 L 0.091796875 11.339844 L 0.203125 12 L 2.2539062 12 L 2.4140625 12.539062 L 2.609375 13.060547 L 2.8417969 13.570312 L 3.1074219 14.064453 L 1.6582031 15.515625 L 2.046875 16.060547 L 2.4726562 16.582031 L 2.9296875 17.070312 L 3.4199219 17.529297 L 3.9375 17.953125 L 4.4863281 18.341797 L 5.9375 16.892578 L 6.4277344 17.160156 L 6.9375 17.390625 L 7.4628906 17.587891 L 8 17.746094 L 8 19.796875 L 8.6621094 19.910156 L 9.328125 19.978516 L 9.9980469 20 L 10.671875 19.978516 L 11.337891 19.910156 L 12 19.796875 L 12 17.746094 L 12.537109 17.587891 L 13.0625 17.390625 L 13.572266 17.160156 L 14.0625 16.892578 L 15.513672 18.341797 L 16.058594 17.953125 L 16.580078 17.529297 L 17.070312 17.070312 L 17.527344 16.582031 L 17.953125 16.060547 L 18.341797 15.515625 L 16.888672 14.064453 L 17.158203 13.570312 L 17.390625 13.060547 L 17.585938 12.539062 L 17.746094 12 L 19.796875 12 L 19.908203 11.339844 L 19.976562 10.669922 L 20 10 L 19.976562 9.3300781 L 19.908203 8.6621094 L 19.796875 8 L 17.746094 8 L 17.585938 7.4628906 L 17.390625 6.9394531 L 17.158203 6.4296875 L 16.888672 5.9375 L 18.341797 4.484375 L 17.953125 3.9394531 L 17.527344 3.4179688 L 17.070312 2.9296875 L 16.580078 2.4707031 L 16.058594 2.046875 L 15.513672 1.6582031 L 14.0625 3.1074219 L 13.572266 2.8398438 L 13.0625 2.609375 L 12.537109 2.4121094 L 12 2.2539062 L 12 0.203125 L 11.337891 0.08984375 L 10.671875 0.0234375 L 9.9980469 0 z M 9.6640625 1.0058594 L 10.333984 1.0058594 L 11 1.0566406 L 11 3.0722656 L 11.572266 3.1796875 L 12.130859 3.3320312 L 12.677734 3.5332031 L 13.207031 3.7773438 L 13.710938 4.0644531 L 14.191406 4.3925781 L 15.617188 2.96875 L 16.123047 3.4042969 L 16.595703 3.875 L 17.03125 4.3828125 L 15.605469 5.8085938 L 15.933594 6.2871094 L 16.222656 6.7949219 L 16.466797 7.3222656 L 16.666016 7.8671875 L 16.820312 8.4296875 L 16.925781 8.9980469 L 18.943359 8.9980469 L 18.994141 9.6660156 L 18.994141 10.333984 L 18.943359 11.001953 L 16.925781 11.001953 L 16.820312 11.570312 L 16.666016 12.132812 L 16.466797 12.679688 L 16.222656 13.208984 L 15.933594 13.712891 L 15.605469 14.193359 L 17.03125 15.617188 L 16.595703 16.125 L 16.123047 16.597656 L 15.617188 17.03125 L 14.191406 15.607422 L 13.710938 15.935547 L 13.207031 16.222656 L 12.677734 16.46875 L 12.130859 16.667969 L 11.572266 16.820312 L 11 16.927734 L 11 18.943359 L 10.333984 18.994141 L 9.6640625 18.994141 L 9 18.943359 L 9 16.927734 L 8.4277344 16.820312 L 7.8671875 16.667969 L 7.3222656 16.46875 L 6.7929688 16.222656 L 6.2890625 15.935547 L 5.8085938 15.607422 L 4.3828125 17.03125 L 3.8769531 16.597656 L 3.4042969 16.125 L 2.96875 15.617188 L 4.3945312 14.193359 L 4.0664062 13.712891 L 3.7773438 13.208984 L 3.5332031 12.679688 L 3.3339844 12.132812 L 3.1796875 11.570312 L 3.0703125 11.001953 L 1.0566406 11.001953 L 1.0058594 10.333984 L 1.0058594 9.6660156 L 1.0566406 8.9980469 L 3.0703125 8.9980469 L 3.1796875 8.4296875 L 3.3339844 7.8671875 L 3.5332031 7.3222656 L 3.7773438 6.7949219 L 4.0664062 6.2871094 L 4.3945312 5.8085938 L 2.96875 4.3828125 L 3.4042969 3.875 L 3.8769531 3.4042969 L 4.3828125 2.96875 L 5.8085938 4.3925781 L 6.2890625 4.0644531 L 6.7929688 3.7773438 L 7.3222656 3.5332031 L 7.8671875 3.3320312 L 8.4277344 3.1796875 L 9 3.0722656 L 9 1.0566406 L 9.6640625 1.0058594 z M 9.9980469 6.0019531 L 9.5175781 6.0292969 L 9.0429688 6.1171875 L 8.5820312 6.2617188 L 8.140625 6.4589844 L 7.7285156 6.7070312 L 7.3476562 7.0078125 L 7.0058594 7.3496094 L 6.7070312 7.7265625 L 6.4570312 8.1425781 L 6.2597656 8.5820312 L 6.1152344 9.0429688 L 6.0292969 9.5195312 L 6 10 L 6.0292969 10.484375 L 6.1152344 10.957031 L 6.2597656 11.417969 L 6.4570312 11.859375 L 6.7070312 12.273438 L 7.0058594 12.654297 L 7.3476562 12.996094 L 7.7285156 13.292969 L 8.140625 13.541016 L 8.5820312 13.742188 L 9.0429688 13.882812 L 9.5175781 13.970703 L 9.9980469 14.001953 L 10.482422 13.970703 L 10.957031 13.882812 L 11.417969 13.742188 L 11.859375 13.541016 L 12.271484 13.292969 L 12.652344 12.996094 L 12.994141 12.654297 L 13.291016 12.273438 L 13.542969 11.859375 L 13.740234 11.417969 L 13.884766 10.957031 L 13.970703 10.484375 L 14 10 L 13.970703 9.5195312 L 13.884766 9.0429688 L 13.740234 8.5820312 L 13.542969 8.1425781 L 13.291016 7.7265625 L 12.994141 7.3496094 L 12.652344 7.0078125 L 12.271484 6.7070312 L 11.859375 6.4589844 L 11.417969 6.2617188 L 10.957031 6.1171875 L 10.482422 6.0292969 L 9.9980469 6.0019531 z M 9.796875 7.0078125 L 10.203125 7.0078125 L 10.611328 7.0625 L 11.003906 7.1738281 L 11.380859 7.3359375 L 11.730469 7.5488281 L 12.046875 7.8085938 L 12.326172 8.1054688 L 12.5625 8.4414062 L 12.751953 8.8046875 L 12.888672 9.1914062 L 12.972656 9.59375 L 12.998047 10 L 12.972656 10.410156 L 12.888672 10.808594 L 12.751953 11.195312 L 12.5625 11.558594 L 12.326172 11.894531 L 12.046875 12.193359 L 11.730469 12.451172 L 11.380859 12.664062 L 11.003906 12.828125 L 10.611328 12.9375 L 10.203125 12.992188 L 9.796875 12.992188 L 9.3886719 12.9375 L 8.9941406 12.828125 L 8.6191406 12.664062 L 8.2695312 12.451172 L 7.9511719 12.193359 L 7.6738281 11.894531 L 7.4375 11.558594 L 7.2480469 11.195312 L 7.1113281 10.808594 L 7.0273438 10.410156 L 7.0019531 10 L 7.0273438 9.59375 L 7.1113281 9.1914062 L 7.2480469 8.8046875 L 7.4375 8.4414062 L 7.6738281 8.1054688 L 7.9511719 7.8085938 L 8.2695312 7.5488281 L 8.6191406 7.3359375 L 8.9941406 7.1738281 L 9.3886719 7.0625 L 9.796875 7.0078125 z " style=\"fill:#222222; fill-opacity:1; stroke:none; stroke-width:0px;\"/>";
  txtpage += "    </g>";
  txtpage += "  </svg>";
  txtpage += "  <div id=\"termometro-container\" class=\"show\">";
  txtpage += "    <div id=\"termometro-div\">";
  txtpage += String(temp,1) + " &deg;C";
  txtpage += "    </div>";
  txtpage += "  </div>";
  txtpage += "  <?xml version=\"1.0\" ?>";
  txtpage += "  <div id=\"status\" class=\"hide\">";
  txtpage += "    <h3>Status da conex&atilde;o:</h3>";
  txtpage += "    <p>Rede Wifi: " + WiFi.SSID() + "</p>";
  txtpage += "    <p>IP: " + myIP.toString() + "</p>";
  txtpage += "    <p>Get Time: " + getTime() + "</p>";
  txtpage += "    <div>";
  txtpage += "      <h3>Porta UDP:</h3>";
  txtpage += "      <p class=\"space\">recebe msg: " + String(localUdpPort) + "</p>";
  txtpage += "      <p class=\"space\">envia broadcast: " + String(HOST_PORT) + "</p>";
  txtpage += "    </div>";
  txtpage += "    <h3>Mensagens de status: </h3>";
  txtpage += "    <p class=\"space\">Msg1: " + MsgDebug1 + "</p>";
  txtpage += "    <p class=\"space\">Msg2: " + MsgDebug2 + "</p>";
  txtpage += "    <p class=\"space\">Msg3: " + MsgDebug3 + "</p>";
  txtpage += "    <p class=\"space\">Msg4: " + MsgDebug4 + "</p>";
  txtpage += "    <p>Atualizar software: <a href=\"http://" + myIP.toString() + "/update\" target=\"_blank\">http://" + myIP.toString() + "/update</a></p>";
  txtpage += "    <p>Reset dados wifi: <a href=\"http://" + myIP.toString() + "/resetnet\" target=\"_blank\">http://" + myIP.toString() + "/resetnet</a></p>";
  txtpage += "  </div>";
  txtpage += "  <script>";
  txtpage += "    const status = document.getElementById(\"status\");";
  txtpage += "    const optionsIcon = document.getElementById(\"optionsIcon\");";
  txtpage += "    const tempContainer = document.getElementById(\"termometro-container\");";
  txtpage += "    const toggleStatus = () => {";
  txtpage += "      if (status.className === \"hide\") {";
  txtpage += "        status.className = \"show\";";
  txtpage += "        tempContainer.className = \"hide\";";
  txtpage += "        setTimeout(() => {";
  txtpage += "          status.style.display = \"block\";";
  txtpage += "          tempContainer.style.display = \"none\";";
  txtpage += "        }, 300);";
  txtpage += "      } else if ((status.className === \"show\")) {";
  txtpage += "        status.className = \"hide\";";
  txtpage += "        tempContainer.className = \"show\";";
  txtpage += "        setTimeout(() => {";
  txtpage += "          status.style.display = \"none\";";
  txtpage += "          tempContainer.style.display = \"flex\";";
  txtpage += "        }, 300);";
  txtpage += "      } else {";
  txtpage += "        status.className = \"show\";";
  txtpage += "        tempContainer.className = \"show\";";
  txtpage += "        setTimeout(() => {";
  txtpage += "          status.style.display = \"block\";";
  txtpage += "          tempContainer.style.display = \"flex\";";
  txtpage += "        }, 300);";
  txtpage += "      }";
  txtpage += "    };";
  txtpage += "    optionsIcon.addEventListener(\"click\", toggleStatus);";
  txtpage += "  </script>";
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
  
  MDNS.begin("sauna");
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
  temp = (temp / 113) * 100;  // reduz 13% do valor para corrigir erro
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
