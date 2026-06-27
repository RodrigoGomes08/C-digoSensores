/*
 * ======================================================
 * IR RECEIVER + WiFi + GROVE RGB LED + Speaker + APDS-9960
 * Arduino UNO R4 WiFi — Correção de Cor por Proximidade Local
 * ======================================================
 */

#include <IRremote.hpp>
#include <WiFiS3.h>
#include <SparkFun_APDS9960.h>
#include <Wire.h>

// ─── WiFi ─────────────────────────────────────────────
char ssid[] = "OPPO A78 5G"; //Coloca o mesmo wifi do arduino
char pass[] = "i5y644cy";

// 1. Liga o pc que estás á mesma rede que vais ligar o arduino, ou seja a que está em cima
// 2. Abre o terminal e escreve ipconfig e copia o IP que está aqui Wireless LAN adapter Wi-Fi
// 3. Cola esse IP na linha abaixo(host)
char host[] = "10.197.187.66"; 
int  port   = 8081;            
WiFiClient client;

// 4. Depois de fazeres isso vai ao VSCode e na pasta mydev.techurbis.com clica com o btn direito e clica no open a integrated terminal e cola este comando php composer.phar install
// 5. Ainda no mesmo terminal cola este comando php -S 0.0.0.0:8081 -t public e a resposta tem de ser parecida com esta [Sat Jun 27 11:25:30 2026] PHP 8.3.30 Development Server (http://0.0.0.0:8081) started
// 6. Liga o arduino e abre o serial monitor que é aquela lupinha em cima á esquerda e confirma se está tudo a correr bem
  
// ─── IR Receiver ──────────────────────────────────────
#define IR_RECEIVE_PIN 2
#define START_BYTE     0xFF
#define ADDR_ESPERADO  0x42
#define MAX_MATRICULA  16

// ─── Grove Chainable RGB LED (Porta D6) ────────────────
#define CLK_PIN   6
#define DATA_PIN  7

// ─── Speaker (porta D4) ───────────────────────────────
#define SPEAKER_PIN 4

// ─── APDS-9960 ────────────────────────────────────────
SparkFun_APDS9960 apds;
#define LIMITE_ALTO      60    
#define LIMITE_BAIXO     35    
#define INTERVALO_PROX   2000  

// ─── id do lugar (atualizado quando chega IR) ─────────
uint8_t id_lugar_atual = 4;  

// ─── Máquina de estados IR ────────────────────────────
enum EstadoTrama {
  AGUARDA_START,
  AGUARDA_ID_LUGAR,
  AGUARDA_LEN,
  AGUARDA_CHARS,
  AGUARDA_CHECKSUM
};

EstadoTrama estado    = AGUARDA_START;
uint8_t     id_lugar  = 0;
uint8_t     len_mat   = 0;
uint8_t     chars_rec = 0;
char        matricula[MAX_MATRICULA + 1];
uint8_t     checksum  = 0;

// ─── Estado LED e Buzzer ──────────────────────────────
enum EstadoLED { LED_OFF, LED_AZUL, LED_VERDE, LED_VERMELHO_PISCAR, LED_VERMELHO_FIXO };
EstadoLED     estadoLED    = LED_OFF;
unsigned long ultimoBlink  = 0;
bool          blinkLigado  = false;
bool          buzzerAtivo  = false;
unsigned long ultimoBuzzer = 0;
bool          buzzerLigado = false;

// ─── Controlo de envio de proximidade ─────────────────
unsigned long ultimoEnvioProx   = 0;
int           ultimoEstadoLugar = -1; 

// Protótipos de funções
void atualizarLED();
void atualizarBuzzer();
void enviarEstadoLugar();
void processarByte(uint8_t b);
void resetarEstado();
void enviarMatriculaAPI(uint8_t idLugar, const char* mat);
void ligarWiFi();
String extrairCampoJSON(String json, String campo);
void sendGroveByte(uint8_t b);
void setGroveLEDColor(uint8_t r, uint8_t g, uint8_t b);

// ──────────────────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  while (!Serial) { delay(10); }
  Serial.println("=== RECEIVER A INICIAR ===");

  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);

  pinMode(CLK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);
  digitalWrite(CLK_PIN, LOW);
  digitalWrite(DATA_PIN, LOW);
  
  delay(500); 

  // Inicia explicitamente em VERDE (Livre)
  estadoLED = LED_VERDE; 
  atualizarLED(); 

  Wire.begin();
  if (!apds.init()) {
    Serial.println("[APDS] Falha ao iniciar!");
  } else {
    apds.enableProximitySensor(false);
    Serial.println("[APDS] OK");
  }

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  Serial.println("[IR] OK");

  ligarWiFi();
  Serial.println("=== RECEIVER PRONTO ===");
}

// ──────────────────────────────────────────────────────
//  LOOP
// ──────────────────────────────────────────────────────
void loop() {
  // 1. Recebe IR, ele recebe a mensagem aos poucos, não recebe tudo de uma vez
  if (IrReceiver.decode()) {
    uint16_t addr = IrReceiver.decodedIRData.address;
    uint8_t  cmd  = (uint8_t)IrReceiver.decodedIRData.command;
    if (addr == ADDR_ESPERADO) processarByte(cmd);
    IrReceiver.resume();
  }

  // 2. Envia estado do lugar periodicamente via proximidade
  if (millis() - ultimoEnvioProx >= INTERVALO_PROX) {
    ultimoEnvioProx = millis();
    enviarEstadoLugar();
  }

  // 3. Atualiza LED e buzzer
  atualizarLED();
  atualizarBuzzer();
}

// ──────────────────────────────────────────────────────
//  APDS-9960 — Determina a cor e envia estado para a API
// ──────────────────────────────────────────────────────
void enviarEstadoLugar() {
  uint8_t proximidade = 0;
  apds.readProximity(proximidade);
  
  int ocupado = ultimoEstadoLugar;
  if (proximidade > LIMITE_ALTO) {
    ocupado = 1;
  } else if (proximidade < LIMITE_BAIXO) {
    ocupado = 0;
  }
  
  if (ocupado == -1) ocupado = 0;

  Serial.print("[APDS] proximidade=");
  Serial.print(proximidade);
  Serial.print(" -> ");
  Serial.println(ocupado ? "OCUPADO" : "VAZIO");

  // CORREÇÃO: Altera o estado do LED localmente de forma imediata!
  if (ocupado == 1) {
    // Se a API não mudou o LED para azul (reserva) nem para piscar (erro), aplica Vermelho Fixo
    if (estadoLED != LED_AZUL && estadoLED != LED_VERMELHO_PISCAR) {
      estadoLED = LED_VERMELHO_FIXO;
    }
  } else {
    // Se o lugar está vazio, volta sempre a ficar Verde
    estadoLED = LED_VERDE;
  }

  // Só avança para o envio Wi-Fi se o estado mudou na base de dados
  if (ocupado == ultimoEstadoLugar) return;
  ultimoEstadoLugar = ocupado;

  if (!client.connect(host, port)) {
    Serial.println("[LUGAR][ERRO] Falha ao conectar.");
    return;
  }

  char rota[50];
  if (ocupado) {
    snprintf(rota, sizeof(rota), "/api/lugar/ocupar/%d", id_lugar_atual);
  } else {
    snprintf(rota, sizeof(rota), "/api/lugar/desocupar/%d", id_lugar_atual);
  }

  client.print("GET "); client.print(rota); client.print(" HTTP/1.1\r\n");
  client.print("Host: "); client.print(host); client.print("\r\n");
  client.print("Connection: close\r\n");
  client.print("\r\n");

  unsigned long timeout = millis();
  while (client.connected() || client.available()) {
    while (client.available()) {
      client.read(); 
      timeout = millis();
    }
    if (millis() - timeout > 5000) break;
  }
  client.stop();

  Serial.print("[LUGAR] Enviado -> ");
  Serial.println(rota);
}

// ──────────────────────────────────────────────────────
//  IR — Máquina de estados
// ──────────────────────────────────────────────────────
// Cada byte que chega passa por esta função
void processarByte(uint8_t b) {
  switch (estado) {
    case AGUARDA_START: // encontrou o inicio
      if (b == START_BYTE) {
        checksum = START_BYTE;
        estado   = AGUARDA_ID_LUGAR;
        Serial.println("[IR] Start recebido.");
      }
      break;

    case AGUARDA_ID_LUGAR: // guardou o id_lugar
      id_lugar  = b;
      checksum ^= b;
      estado    = AGUARDA_LEN;
      Serial.print("[IR] id_lugar = "); Serial.println(id_lugar);
      break;

    case AGUARDA_LEN: // Sabe quantas letras veem e vê se o comprimento é inválido ou não
      if (b == 0 || b > MAX_MATRICULA) {
        Serial.println("[IR] Comprimento invalido. A resetar.");
        resetarEstado();
        return;
      }
      len_mat   = b;
      checksum ^= b;
      chars_rec = 0;
      estado    = AGUARDA_CHARS;
      break;

    case AGUARDA_CHARS: // monta a matricula letra a letra
      matricula[chars_rec++] = (char)b;
      checksum ^= b;
      if (chars_rec >= len_mat) {
        matricula[chars_rec] = '\0';
        estado = AGUARDA_CHECKSUM;
      }
      break;

    case AGUARDA_CHECKSUM: // como a mensagem é valida envia para a API
      if (b == checksum) {
        Serial.print("[IR] Matricula: "); Serial.println(matricula);
        Serial.print("[IR] id_lugar: "); Serial.println(id_lugar);
        id_lugar_atual = id_lugar; 
        enviarMatriculaAPI(id_lugar, matricula);
      } else {
        Serial.println("[IR] Checksum invalido.");
      }
      resetarEstado();
      break;
  }
}

void resetarEstado() {
  estado    = AGUARDA_START;
  id_lugar  = 0;
  len_mat   = 0;
  chars_rec = 0;
  checksum  = 0;
  memset(matricula, 0, sizeof(matricula));
}

// ──────────────────────────────────────────────────────
//  API — Envia matrícula
// ──────────────────────────────────────────────────────
void enviarMatriculaAPI(uint8_t idLugar, const char* mat) {
  Serial.println("[API] A enviar matricula...");

  // Liga ao servidor
  if (!client.connect(host, port)) {
    Serial.println("[API][ERRO] Falha ao conectar.");
    return;
  }

  // Cria o JSON, da mesma forma como a API está pronta para o receber
  // Exemplo: {"id_lugar":4,"matricula":"12-AB-34"}
  char body[60];
  snprintf(body, sizeof(body), "{\"id_lugar\":%d,\"matricula\":\"%s\"}", idLugar, mat);
  int bodyLen = strlen(body);

  //Envia o pedido HTTP manualmente
  client.print("POST /api/sensor/valor-matricula HTTP/1.1\r\n");
  client.print("Host: "); client.print(host); client.print("\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: "); client.print(bodyLen); client.print("\r\n"); // tamanho do body
  client.print("Connection: close\r\n");
  client.print("\r\n");
  client.print(body);// envia o json

  String resposta = "";
  unsigned long timeout = millis();
  while (client.connected() || client.available()) {
    while (client.available()) {
      char c = client.read();
      resposta += c;
      timeout = millis();
    }
    if (millis() - timeout > 5000) break;
  }
  client.stop();

  Serial.println("[API] Resposta:");
  Serial.println(resposta);

  String resultado = extrairCampoJSON(resposta, "resultado");
  Serial.print("[API] resultado = "); Serial.println(resultado);

  if (resultado == "sem_reserva") {
    Serial.println("[ACAO] Sem reserva -> LED verde");
    estadoLED   = LED_VERDE;
    buzzerAtivo = false;
  } else if (resultado == "match") {
    Serial.println("[ACAO] Matricula correta -> LED azul");
    estadoLED   = LED_AZUL;
    buzzerAtivo = false;
  } else if (resultado == "mismatch") {
    Serial.println("[ACAO] Matricula errada -> LED vermelho + buzzer");
    estadoLED   = LED_VERMELHO_PISCAR;
    buzzerAtivo = true;
  } else {
    Serial.println("[ACAO] Resposta desconhecida.");
    estadoLED   = LED_OFF;
    buzzerAtivo = false;
  }
}

// ──────────────────────────────────────────────────────
//  MÉTODO DE SUPORTE GROVE LED (P9813) — EMULAÇÃO UNO R4
// ──────────────────────────────────────────────────────
void sendGroveByte(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(DATA_PIN, (b >> i) & 0x01);
    delayMicroseconds(2); 
    digitalWrite(CLK_PIN, HIGH);
    delayMicroseconds(2);
    digitalWrite(CLK_PIN, LOW);
    delayMicroseconds(2);
  }
}

void setGroveLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t checksum = 0xC0;
  if ((b & 0x80) == 0) checksum |= 0x20;
  if ((b & 0x40) == 0) checksum |= 0x10;
  if ((g & 0x80) == 0) checksum |= 0x08;
  if ((g & 0x40) == 0) checksum |= 0x04;
  if ((r & 0x80) == 0) checksum |= 0x02;
  if ((r & 0x40) == 0) checksum |= 0x01;

  for (int i = 0; i < 4; i++) sendGroveByte(0x00);
  sendGroveByte(checksum);
  sendGroveByte(b); 
  sendGroveByte(g);
  sendGroveByte(r);
  for (int i = 0; i < 4; i++) sendGroveByte(0x00);
}

// ──────────────────────────────────────────────────────
//  GERENCIAMENTO LED RGB
// ──────────────────────────────────────────────────────
EstadoLED ultimoEstadoLED = LED_OFF; 

void atualizarLED() {
  if (estadoLED == LED_VERMELHO_PISCAR) {
    if (millis() - ultimoBlink > 400) {
      ultimoBlink = millis();
      blinkLigado = !blinkLigado;
      if (blinkLigado) {
        setGroveLEDColor(255, 0, 0); 
      } else {
        setGroveLEDColor(0, 0, 0);   
      }
    }
    ultimoEstadoLED = LED_VERMELHO_PISCAR;

  } else {
    if (estadoLED != ultimoEstadoLED) {
      ultimoEstadoLED = estadoLED;
      switch (estadoLED) {
        case LED_OFF:           setGroveLEDColor(0, 0, 0);     break;
        case LED_AZUL:          setGroveLEDColor(0, 0, 255);   break;
        case LED_VERDE:         setGroveLEDColor(0, 255, 0);   break;
        case LED_VERMELHO_FIXO: setGroveLEDColor(255, 0, 0);   break; // Adicionado Vermelho Fixo local
        default: break;
      }
    }
  }
}

// ──────────────────────────────────────────────────────
//  Buzzer
// ──────────────────────────────────────────────────────
void atualizarBuzzer() {
  if (!buzzerAtivo) {
    digitalWrite(SPEAKER_PIN, LOW);
    buzzerLigado = false;
    return;
  }
  if (millis() - ultimoBuzzer > 500) {
    ultimoBuzzer = millis();
    buzzerLigado = !buzzerLigado;
    digitalWrite(SPEAKER_PIN, buzzerLigado ? HIGH : LOW);
  }
}

// ──────────────────────────────────────────────────────
//  WiFi
// ──────────────────────────────────────────────────────
void ligarWiFi() {
  Serial.println("[WiFi] A ligar...");
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    delay(2000);
    Serial.println("[WiFi] A tentar...");
  }
  Serial.print("[WiFi] Ligado. IP: ");
  Serial.println(WiFi.localIP());
}

// ──────────────────────────────────────────────────────
//  Extrai campo JSON
// ──────────────────────────────────────────────────────
String extrairCampoJSON(String json, String campo) {
  String chave = "\"" + campo + "\":\"";
  int idx = json.indexOf(chave);
  if (idx == -1) return "";
  idx += chave.length();
  int fim = json.indexOf("\"", idx);
  if (fim == -1) return "";
  return json.substring(idx, fim);
}