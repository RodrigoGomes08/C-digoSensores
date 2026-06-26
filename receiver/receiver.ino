/*
 * ======================================================
 *  IR RECEIVER + WiFi + RGB LED + Speaker + APDS-9960
 *  Arduino UNO R4 WiFi
 * ======================================================
 * Bibliotecas necessárias:
 *   - IRremote (v3.x)
 *   - WiFiS3 (já incluída no core do UNO R4 WiFi)
 *   - ChainableLED ("Grove Chainable RGB LED")
 *   - SparkFun_APDS9960
 * ======================================================
 */

#include <IRremote.hpp>
#include <WiFiS3.h>
#include <ChainableLED.h>
#include <SparkFun_APDS9960.h>
#include <Wire.h>

// ─── WiFi ─────────────────────────────────────────────
char ssid[] = "OPPO A78 5G";
char pass[] = "i5y644cy";
char host[] = "10.197.187.66";
int  port   = 8081;
WiFiClient client;

// ─── IR Receiver ──────────────────────────────────────
#define IR_RECEIVE_PIN 2
#define START_BYTE     0xFF
#define ADDR_ESPERADO  0x42
#define MAX_MATRICULA  16

// ─── RGB LED (Grove Chainable — porta D6) ─────────────
ChainableLED led(6, 7, 1);

// ─── Speaker (porta D4) ───────────────────────────────
#define SPEAKER_PIN 4

// ─── APDS-9960 ────────────────────────────────────────
SparkFun_APDS9960 apds;
#define PROX_LIMITE      50    // acima disto = ocupado
#define INTERVALO_PROX   2000  // envia estado a cada 2s (igual ao FSR)

// ─── id do lugar (atualizado quando chega IR) ─────────
uint8_t id_lugar_atual = 4;  // valor por defeito

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
enum EstadoLED { LED_OFF, LED_AZUL, LED_VERDE, LED_VERMELHO_PISCAR };
EstadoLED     estadoLED    = LED_OFF;
unsigned long ultimoBlink  = 0;
bool          blinkLigado  = false;
bool          buzzerAtivo  = false;
unsigned long ultimoBuzzer = 0;
bool          buzzerLigado = false;

// ─── Controlo de envio de proximidade ─────────────────
unsigned long ultimoEnvioProx  = 0;
int           ultimoEstadoLugar = -1; // -1 = nunca enviado

// ──────────────────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  while (!Serial) { delay(10); }
  Serial.println("=== RECEIVER A INICIAR ===");

  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);

  led.init();
  setLED(LED_OFF);

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
  // 1. Recebe IR
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
//  APDS-9960 — envia estado do lugar para a API
// ──────────────────────────────────────────────────────
void enviarEstadoLugar() {
  uint8_t proximidade = 0;
  apds.readProximity(proximidade);
  int ocupado = (proximidade > PROX_LIMITE) ? 1 : 0;

  Serial.print("[APDS] proximidade=");
  Serial.print(proximidade);
  Serial.print(" -> ");
  Serial.println(ocupado ? "OCUPADO" : "VAZIO");

  // Só envia se o estado mudou (igual ao LIMIAR_MUDANCA do FSR)
  if (ocupado == ultimoEstadoLugar) return;
  ultimoEstadoLugar = ocupado;

  if (!client.connect(host, port)) {
    Serial.println("[LUGAR][ERRO] Falha ao conectar.");
    return;
  }

  char body[40];
  snprintf(body, sizeof(body), "{\"ocupado\":%d}", ocupado);
  int bodyLen = strlen(body);

  char rota[40];
  snprintf(rota, sizeof(rota), "/api/lugar/ocupar/%d", id_lugar_atual);
  // Se ocupado envia /lugar/ocupar/X, se vazio envia /lugar/desocupar/X
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
  String resposta = "";
  while (client.connected() || client.available()) {
    while (client.available()) {
      char c = client.read();
      resposta += c;
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
void processarByte(uint8_t b) {
  switch (estado) {
    case AGUARDA_START:
      if (b == START_BYTE) {
        checksum = START_BYTE;
        estado   = AGUARDA_ID_LUGAR;
        Serial.println("[IR] Start recebido.");
      }
      break;

    case AGUARDA_ID_LUGAR:
      id_lugar  = b;
      checksum ^= b;
      estado    = AGUARDA_LEN;
      Serial.print("[IR] id_lugar = "); Serial.println(id_lugar);
      break;

    case AGUARDA_LEN:
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

    case AGUARDA_CHARS:
      matricula[chars_rec++] = (char)b;
      checksum ^= b;
      if (chars_rec >= len_mat) {
        matricula[chars_rec] = '\0';
        estado = AGUARDA_CHECKSUM;
      }
      break;

    case AGUARDA_CHECKSUM:
      if (b == checksum) {
        Serial.print("[IR] Matricula: "); Serial.println(matricula);
        Serial.print("[IR] id_lugar: "); Serial.println(id_lugar);
        id_lugar_atual = id_lugar; // atualiza o lugar ativo
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

  if (!client.connect(host, port)) {
    Serial.println("[API][ERRO] Falha ao conectar.");
    return;
  }

  char body[60];
  snprintf(body, sizeof(body), "{\"id_lugar\":%d,\"matricula\":\"%s\"}", idLugar, mat);
  int bodyLen = strlen(body);

  client.print("POST /api/sensor/valor-matricula HTTP/1.1\r\n");
  client.print("Host: "); client.print(host); client.print("\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: "); client.print(bodyLen); client.print("\r\n");
  client.print("Connection: close\r\n");
  client.print("\r\n");
  client.print(body);

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
//  LED RGB
// ──────────────────────────────────────────────────────
void setLED(EstadoLED e) {
  switch (e) {
    case LED_OFF:             led.setColorRGB(0, 0,   0,   0);   break;
    case LED_AZUL:            led.setColorRGB(0, 0,   0,   255); break;
    case LED_VERDE:           led.setColorRGB(0, 0,   255, 0);   break;
    case LED_VERMELHO_PISCAR: led.setColorRGB(0, 255, 0,   0);   break;
  }
}

void atualizarLED() {
  if (estadoLED == LED_VERMELHO_PISCAR) {
    if (millis() - ultimoBlink > 400) {
      ultimoBlink = millis();
      blinkLigado = !blinkLigado;
      led.setColorRGB(0, blinkLigado ? 255 : 0, 0, 0);
    }
  } else {
    setLED(estadoLED);
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