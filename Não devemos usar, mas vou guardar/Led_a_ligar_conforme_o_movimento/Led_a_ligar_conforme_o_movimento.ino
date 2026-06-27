#include <Wire.h>
#include <SparkFun_APDS9960.h>

SparkFun_APDS9960 apds;

const int LED_PIN = 4;

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!apds.init()) {
    Serial.println("Erro ao inicializar APDS-9960");
    while (1);
  }

  if (!apds.enableGestureSensor(true)) {
    Serial.println("Erro ao ativar sensor de gestos");
    while (1);
  }

  Serial.println("Sensor pronto!");
}

void loop() {

  if (apds.isGestureAvailable()) {

    int gesture = apds.readGesture();

    switch (gesture) {

      case DIR_UP:
        Serial.println("Movimento: CIMA");
        digitalWrite(LED_PIN, LOW);
        break;

      case DIR_DOWN:
        Serial.println("Movimento: BAIXO");
        digitalWrite(LED_PIN, LOW);
        break;

      case DIR_LEFT:
        Serial.println("Movimento: ESQUERDA");

        digitalWrite(LED_PIN, HIGH);
        delay(1000);
        digitalWrite(LED_PIN, LOW);

        break;

      case DIR_RIGHT:
        Serial.println("Movimento: DIREITA");

        digitalWrite(LED_PIN, HIGH);
        delay(1000);
        digitalWrite(LED_PIN, LOW);

        break;

      default:
        Serial.println("Outro gesto");
        break;
    }
  }
}