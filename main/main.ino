#include <WiFi.h>
#include <ESP32Servo.h>

#define RELAY_PIN               27      // Pin do relay conectado ao ESP32.
#define SERVO_PIN               13      // Pin do servomotor conectado ao ESP32.
#define MAX_IRRIGATION_TIMES    3       // Quantidade de vezes que o servomotor irá direcionar a mangueira de um ângulo a outro.  
#define uS_TO_S_FACTOR          1000000 // Conversão do fator de microsegundos para segundos.
#define TIME_TO_SLEEP           43200   // Tempo que o ESP32 irá dormir em segundos - Valor atual equivale a 12 horas.
#define THRESHOLD               40      // Sensibilidade do toque no T8 para ativar ESP32 por um botão ou toque no jumper.

RTC_DATA_ATTR int bootCount = 0;
touch_pad_t touchPin;

const char* ssid = "<INSIRA_NOME_DA_REDE_AQUI>";
const char* password = "<INSIRA_SENHA_DA_REDE_AQUI>";

uint32_t notConnectedCounter = 0;

float servoPosition = 0;  // Variável para armazenar a posição do servo.
uint32_t irrigationTimesCounter = 0;

WiFiServer server(80);

String httpHeader;

Servo myServo;  // Cria o objeto para controlar o servo.


// Variável auxiliar pra controlar a saída da porta 27.
bool outputPin27StateOn = false;

// Tempo atual da requisição.
unsigned long currentTime = millis();

// Tempo anterior da requisição e duração da requisição.
unsigned long previousTime = 0;
unsigned long elapsedTime = 0;

// Define o timeout em milisegundos (exemplo 2000ms = 2s).
const long timeoutTime = 2000;

void setup() {
  Serial.begin(115200);
  delay(1000);

  ++bootCount;
  Serial.println("Boot n.: " + String(bootCount));

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);  // Define em quanto tempo o ESP32 irá acordar.
  touchAttachInterrupt(T8, callback, THRESHOLD);  // Define que o ESP32 irá ser ativado ao tocar em T8.
  esp_sleep_enable_touchpad_wakeup();
  // Inicializa as variáves de saída
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  myServo.attach(SERVO_PIN);  // Anexa o servomotor ao pin SERVO_PIN

  // Conecta ao Wi-Fi
  Serial.print("Conectando em: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".\n");
    notConnectedCounter++;
    if(notConnectedCounter > 50) {
      Serial.println("Reiniciando devido ao WiFi estar desconectado...");
      ESP.restart();
    } else if(notConnectedCounter > 100) {
      break;
    }
  }
  // Imprime o IP local da conexão e inicia o servidor Web
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi conectado");
    Serial.println("Endereço IP: ");
    Serial.println(WiFi.localIP());
    server.begin();
  }

  turnOnIrrigation();
  turnOffIrrigation();
  Serial.println("Indo dormir agora");
  delay(1000);
  Serial.flush(); 
  esp_deep_sleep_start(); // Coloca o ESP32 para dormir.
}

void loop() {
  enableServer();
}

void turnOnIrrigation() {
  Serial.println("GPIO 27 on");
  outputPin27StateOn = true;
  digitalWrite(RELAY_PIN, HIGH);
  while(irrigationTimesCounter < 3){
    rotation(); 
    irrigationTimesCounter++;
  }
  irrigationTimesCounter = 0;
}

void turnOffIrrigation() {
  Serial.println("GPIO 27 off");
  outputPin27StateOn = false;
  digitalWrite(RELAY_PIN, LOW);            
}

void enableServer() {
  // Criar um servidor http usando a biblioteca WiFi.h.
  WiFiClient client = server.available();

  if(client) {
    currentTime = millis();
    previousTime = currentTime;
    
    Serial.println("Novo cliente.");
    String currentLine = "";  // Variável de controle para leitura da requisição HTTP.

    // Calcula a duração da requisição.
    elapsedTime = currentTime - previousTime;
    // Caso o cliente esteja conectado e não tenha ocorrido um timemout.
    while(client.connected() && elapsedTime <= timeoutTime) {
      currentTime = millis();
      elapsedTime = currentTime - previousTime;
      if(client.available()) {
        char c = client.read();
        Serial.write(c);
        httpHeader += c;
        if(c == '\n') {
          // Define o HTTP response.
          if(currentLine.length() == 0) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // Verifica a URL acessada pelo cliente utilizando o protocolo HTTP.
            if(httpHeader.indexOf("GET /27/on") >= 0){
              turnOnIrrigation();
            } else if(httpHeader.indexOf("GET /27/off") >= 0){
              turnOffIrrigation();
            }

            // Escreve o cabeçalho da página HTML a ser exibida na resposta ao cliente.
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #555555;}</style></head>");
            
            // Título da página.
            client.println("<body><h1>ESP32 Web Server</h1>");
            
            // Se o estado da variável outputPin27StateOn estiver false, exibde o botão ON       
            if (outputPin27StateOn==false) {
              // Exibe o estado atual do pin, e os botões ON/OFF para o RELAY_PIN  
              client.println("<p>GPIO 27 - Status - OFF </p>");
              client.println("<p><a href=\"/27/on\"><button class=\"button\">ON</button></a></p>");
            } else {
              client.println("<p>GPIO 27 - Status - ON </p>");
              client.println("<p><a href=\"/27/off\"><button class=\"button button2\">OFF</button></a></p>");
            } 
            client.println("</body></html>");
            
            // A resposta HTTP finaliza com uma outra linha em branco
            client.println();
            break;
          } else { 
            currentLine = "";
          }
        } else if (c != '\r') {  
          currentLine += c;      
        }
      }
    }
    // Limpa a variável de cabeçalho
    httpHeader = "";
    // Fecha a conexão
    client.stop();
    Serial.println("Cliente desconectado.");
    Serial.println("");
  }
}

// Função usada para rotacionar o servomotor e a mangueira acoplada a ele.
void rotation() {
  // Vai de 45 até 135 graus passo-a-passo de 0,3 em 0,3 grau.
  for (servoPosition = 45; servoPosition <= 135; servoPosition += 0.3) {
    myServo.write(servoPosition);   // diz ao servo para ir para a posição da variável servoPosition
    delay(15);                      // aguarda 15ms para o servo atualizar a posição
  }
  // Rotação inversa.
  for (servoPosition = 135; servoPosition >= 45; servoPosition -= 0.3) {
    myServo.write(servoPosition);              
    delay(15);                      
  }
}

// Função chamada quando o circuito é acionado por botão ou toque no jumper.
void callback() {
  turnOnIrrigation();
  turnOffIrrigation();
}
