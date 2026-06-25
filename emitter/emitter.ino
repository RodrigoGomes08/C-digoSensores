/*
 * =====================================================
 *  IR EMITTER + VL53L0X + SERVO (Cancela)
 * =====================================================
 * Bibliotecas necessárias:
 *   - IRremote (v3.x)
 *   - Adafruit_VL53L0X
 *   - Servo
 * =====================================================
 */

#include <Wire.h>
#include "Adafruit_VL53L0X.h"
#include <Servo.h>
#include <IRremote.hpp>

Adafruit_VL53L0X lox = Adafruit_VL53L0X();
Servo cancela;

#define IR_SEND_PIN       3
#define START_BYTE        0xFF
#define MAX_MATRICULA     16
#define DELAY_ENTRE_BYTES 80
#define DELAY_APOS_START  200

const int servoPin        = 5;
const int distanciaLimite = 100;

bool cancelaAberta    = false;
bool objetoDetectado  = false;

// ─── Dados a enviar ───────────────────────────────────
uint8_t id_lugar  = 5;
String  matricula = "AA-00-BB";
// ──────────────────────────────────────────────────────

void setup() {
  Serial.begin(9600);
  while (!Serial) { delay(10); }

  Serial.println("=== EMITTER INICIADO ===");

  cancela.attach(servoPin);
  cancela.write(0);
  Serial.println("Servo OK — cancela fechada");

  IrSender.begin(IR_SEND_PIN, DISABLE_LED_FEEDBACK);
  Serial.println("IR Sender OK");

  if (!lox.begin()) {
    Serial.println("[ERRO] Falha ao iniciar VL53L0X!");
    while (1);
  }
  Serial.println("VL53L0X OK");
  Serial.println("Sistema pronto. A monitorizar...");
}

void loop() {
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);

  if (measure.RangeStatus != 4) {
    int distancia = measure.RangeMilliMeter;
    Serial.print("Distancia: ");
    Serial.print(distancia);
    Serial.println(" mm");

    if (distancia <= distanciaLimite) {
      if (!objetoDetectado) {
        objetoDetectado = true;

        if (!cancelaAberta) {
          Serial.println(">> Carro detetado — Abrindo cancela...");
          cancela.write(90);
          cancelaAberta = true;

          Serial.println(">> A enviar dados via IR...");
          enviarDados(id_lugar, matricula);
          Serial.println(">> Dados enviados.");
        } else {
          Serial.println(">> Fechando cancela...");
          cancela.write(0);
          cancelaAberta = false;
        }
      }
    } else {
      if (objetoDetectado) Serial.println(">> Objeto saiu da zona.");
      objetoDetectado = false;
    }
  } else {
    Serial.println("Leitura invalida (fora de alcance)");
  }

  delay(100);
}

void enviarDados(uint8_t idLugar, String mat) {
  uint8_t len = (uint8_t)mat.length();
  if (len > MAX_MATRICULA) { Serial.println("[ERRO] Matricula longa!"); return; }

  uint8_t trama[3 + MAX_MATRICULA + 1];
  uint8_t idx = 0;

  trama[idx++] = START_BYTE;
  trama[idx++] = idLugar;
  trama[idx++] = len;

  uint8_t checksum = START_BYTE ^ idLugar ^ len;
  for (uint8_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)mat[i];
    trama[idx++] = c;
    checksum ^= c;
  }
  trama[idx++] = checksum;

  for (uint8_t i = 0; i < idx; i++) {
    IrSender.sendNEC(0x42, trama[i], 0);
    delay(i == 0 ? DELAY_APOS_START : DELAY_ENTRE_BYTES);
  }

  Serial.println("[IR] Trama enviada.");
}