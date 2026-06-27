/*
 * Controlo de LEDs com Sensor de Gestos APDS-9960
 * Hardware:
 *   - Arduino UNO R4 WiFi
 *   - Grove Base Shield
 *   - Grove APDS-9960 RGB/Gesture Sensor (ligado ao conector I2C)
 *   - 3x LED branco 5mm Grove Module
 *     · LED ESQUERDO  → pino D2
 *     · LED CENTRO    → pino D3
 *     · LED DIREITO   → pino D4
 *
 * Comportamento:
 *   - Gesto para a DIREITA → LED direito (D4) acende + LED centro (D3) acende
 *   - Gesto para a ESQUERDA → LED esquerdo (D2) acende + LED centro (D3) acende
 *   - Os LEDs ficam acesos 1 segundo e depois apagam
 */

#include <Wire.h>
#include <SparkFun_APDS9960.h>

// ── Pinos dos LEDs ──────────────────────────────────────────────────────────
#define LED_ESQUERDO  2   // Grove socket D2
#define LED_CENTRO    3   // Grove socket D3
#define LED_DIREITO   4   // Grove socket D4

// Tempo (ms) que os LEDs ficam acesos após um gesto
#define TEMPO_LED_ACESO 1000

// ── Objecto do sensor ───────────────────────────────────────────────────────
SparkFun_APDS9960 apds;

// ── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  Serial.println(F("=== Controlo de LEDs com APDS-9960 ==="));

  // Configura pinos dos LEDs como saída e garante que estão apagados
  pinMode(LED_ESQUERDO, OUTPUT);
  pinMode(LED_CENTRO,   OUTPUT);
  pinMode(LED_DIREITO,  OUTPUT);
  todosApagados();

  // Inicia comunicação I2C
  Wire.begin();

  // Inicia o sensor APDS-9960
  if (!apds.init()) {
    Serial.println(F("ERRO: Sensor APDS-9960 nao encontrado!"));
    Serial.println(F("Verifica a ligacao I2C no Base Shield."));
    while (true); // Para aqui — verifica a ligação
  }

  // Activa o motor de detecção de gestos
  if (!apds.enableGestureSensor(true)) {
    Serial.println(F("ERRO: Nao foi possivel activar o sensor de gestos!"));
    while (true);
  }

  Serial.println(F("Sensor pronto. Faz um gesto!"));
}

// ── Loop principal ──────────────────────────────────────────────────────────
void loop() {
  if (apds.isGestureAvailable()) {
    int gesto = apds.readGesture();

    switch (gesto) {
      case DIR_RIGHT:
        Serial.println(F("Gesto: DIREITA"));
        todosApagados();
        digitalWrite(LED_DIREITO, HIGH);
        digitalWrite(LED_CENTRO,  HIGH);
        delay(TEMPO_LED_ACESO);
        todosApagados();
        break;

      case DIR_LEFT:
        Serial.println(F("Gesto: ESQUERDA"));
        todosApagados();
        digitalWrite(LED_ESQUERDO, HIGH);
        digitalWrite(LED_CENTRO,   HIGH);
        delay(TEMPO_LED_ACESO);
        todosApagados();
        break;

      case DIR_UP:
        Serial.println(F("Gesto: CIMA (ignorado)"));
        break;

      case DIR_DOWN:
        Serial.println(F("Gesto: BAIXO (ignorado)"));
        break;

      default:
        // Gesto não reconhecido — não faz nada
        break;
    }
  }
}

// ── Função auxiliar ─────────────────────────────────────────────────────────
void todosApagados() {
  digitalWrite(LED_ESQUERDO, LOW);
  digitalWrite(LED_CENTRO,   LOW);
  digitalWrite(LED_DIREITO,  LOW);
}
