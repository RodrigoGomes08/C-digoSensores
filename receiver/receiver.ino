/*
 * ======================================================
 *  IR RECEIVER + WiFi + RGB LED + Speaker + APDS-9960
 *  Arduino UNO R4 WiFi
 * ======================================================
 * Bibliotecas necessárias:
 *   - IRremote (v3.x)
 *   - WiFiS3 (já incluída no core do UNO R4 WiFi)
 *   - ChainableLED (instalar via Library Manager: "Grove Chainable RGB LED")
 *   - SparkFun_APDS9960 (instalar via Library Manager: "SparkFun APDS9960")
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
char host[] = "10.177.35.66";
int  port   = 8081;
WiFiClient client;

// ─── IR Receiver ──────────────────────────────────────
#define IR_RECEIVE_PIN 2
#define START_BYTE     0xFF
#define ADDR_ESPERADO  0x42
#define MAX_MATRICULA  16

// ─── RGB LED (Grove Chainable — porta D6) ─────────────
// D6 = CLK, D7 = DATA  (o Grove usa os 2 pinos da porta)
ChainableLED led(6, 7, 1);  // (CLK, DATA, nº de LEDs)

// ─── Speaker (porta D4) ───────────────────────────────
#define SPEAKER_PIN 4

// ─── APDS-9960 (porta I2C) ────────────────────────────
SparkFun_APDS9960 apds;
#define DIST_LIMITE  50  // valor de proximidade acima do qual considera "ocupado" (0-255)

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

// ──────────────────────────────────────────────────────
//  SETUP
// ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  while (!Serial) { delay(10); }
  Serial.println("=== RECEIVER A INICIAR ===");

  // Speaker
  pinMode(SPEAKER_PIN, OUTPUT);
  digitalWrite(SPEAKER_PIN, LOW);

  // LED
  led.init();
  setLED(LED_OFF);

  // APDS-9960
  Wire.begin();
  if (!apds.init()) {
    Serial.println("[APDS] Falha ao iniciar! Verifica ligacao I2C.");
  } else {
    apds.enableProximitySensor(false);
    Serial.println("[APDS] Sensor de proximidade OK");
  }

  // IR
  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  Serial.println("[IR] Receiver OK");

  // WiFi
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

    if (addr == ADDR_ESPERADO) {
      processarByte(cmd);
    }

    IrReceiver.resume();
  }

  // 2. Atualiza LED e buzzer (non-blocking)
  atualizarLED();
  atualizarBuzzer();
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
        Serial.print("[IR] Matricula recebida: "); Serial.println(matricula);
        Serial.print("[IR] id_lugar: ");           Serial.println(id_lugar);
        enviarMatriculaAPI(id_lugar, matricula);
      } else {
        Serial.println("[IR] Checksum invalido. A ignorar.");
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
//  API — Envia matrícula e processa resposta
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

  // Lê resposta
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

  // Extrai "resultado" do JSON
  String resultado = extrairCampoJSON(resposta, "resultado");
  Serial.print("[API] resultado = "); Serial.println(resultado);

  // Lê proximidade do APDS-9960
  uint8_t proximidade = 0;
  apds.readProximity(proximidade);
  bool lugarOcupado = (proximidade > DIST_LIMITE);
  Serial.print("[APDS] proximidade = "); Serial.print(proximidade);
  Serial.println(lugarOcupado ? " -> OCUPADO" : " -> VAZIO");

  // ─── Decide ação ────────────────────────────────────
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
//  LED RGB (Grove Chainable P9813)
// ──────────────────────────────────────────────────────
void setLED(EstadoLED estado) {
  switch (estado) {
    case LED_OFF:              led.setColorRGB(0, 0,   0,   0);   break;
    case LED_AZUL:             led.setColorRGB(0, 0,   0,   255); break;
    case LED_VERDE:            led.setColorRGB(0, 0,   255, 0);   break;
    case LED_VERMELHO_PISCAR:  led.setColorRGB(0, 255, 0,   0);   break;
  }
}

void atualizarLED() {
  if (estadoLED == LED_VERMELHO_PISCAR) {
    if (millis() - ultimoBlink > 400) {
      ultimoBlink = millis();
      blinkLigado = !blinkLigado;
      if (blinkLigado) led.setColorRGB(0, 255, 0, 0);
      else             led.setColorRGB(0, 0,   0, 0);
    }
  } else {
    setLED(estadoLED);
  }
}

// ──────────────────────────────────────────────────────
//  Buzzer (non-blocking)
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
//  Extrai valor de campo JSON
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