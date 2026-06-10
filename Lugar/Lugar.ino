#include <WiFi.h>

// ===== WiFi =====
char ssid[] = "casa";
char pass[] = "EmCasaRod";

char host[] = "192.168.0.103";
int port = 8081;

WiFiClient client;

// ===== FSR =====
const int FSR_PIN = A0;
const int FSR_MIN = 0;
const int FSR_MAX = 4095;

int ultimaPercentagem = -1;
const int LIMIAR_MUDANCA = 3;

// --------------------------------------------------

bool ipValido(IPAddress ip) {
  return !(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
}

void ligarWiFi() {
  Serial.println("[WiFi] A ligar...");
  int status = WiFi.begin(ssid, pass);

  while (status != WL_CONNECTED) {
    delay(2000);
    status = WiFi.status();
    Serial.println("[WiFi] A ligar...");
  }

  while (!ipValido(WiFi.localIP())) {
    delay(1000);
  }

  Serial.println("[WiFi] Ligado");
  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());
}

void enviarPressao(int percentagem) {
  if (!client.connect(host, port)) {
    Serial.println("[ERRO] Falha ao conectar.");
    return;
  }

  // Body JSON
  char body[40];
  snprintf(body, sizeof(body), "{\"valor\":%d}", percentagem);
  int bodyLen = strlen(body);

  client.print("POST /contentores/pressao/1 HTTP/1.1\r\n");
  client.print("Host: ");
  client.print(host);
  client.print("\r\n");
  client.print("Content-Type: application/json\r\n");
  client.print("Content-Length: ");
  client.print(bodyLen);
  client.print("\r\n");
  client.print("Connection: close\r\n");
  client.print("\r\n");
  client.print(body);

  unsigned long timeout = millis();
  while (client.connected() || client.available()) {
    while (client.available()) {
      Serial.print((char)client.read());
      timeout = millis();
    }
    if (millis() - timeout > 10000) {
      Serial.println("[ERRO] Timeout.");
      break;
    }
  }

  client.stop();
  Serial.println("\n[HTTP] Ligacao fechada.");
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  ligarWiFi();
  Serial.println("[SISTEMA] Pronto a ler contentor.");
}

void loop() {
  int valorBruto = analogRead(FSR_PIN);
  int percentagem = map(valorBruto, FSR_MIN, FSR_MAX, 0, 100);
  percentagem = constrain(percentagem, 0, 100);

  Serial.print("Bruto: ");
  Serial.print(valorBruto);
  Serial.print(" | Percentagem: ");
  Serial.print(percentagem);
  Serial.println("%");

  if (abs(percentagem - ultimaPercentagem) >= LIMIAR_MUDANCA) {
    enviarPressao(percentagem);
    ultimaPercentagem = percentagem;
  }

  delay(2000);
}