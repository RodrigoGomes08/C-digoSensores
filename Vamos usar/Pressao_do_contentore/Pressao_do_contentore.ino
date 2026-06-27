#include <WiFi.h>

// ===== WiFi =====
char ssid[] = "casa";
char pass[] = "EmCasaRod";
char host[] = "192.168.0.103";  // CORRIGIDO
int port = 8081;

WiFiClient client;

// ===== FSR =====
const int FSR_PIN = A0;
const int FSR_MIN = 0;
const int FSR_MAX = 1000;

int ultimaPercentagem = -1;
const int LIMIAR_MUDANCA = 3;

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
  Serial.print("[HTTP] A conectar a ");
  Serial.print(host);
  Serial.print(":");
  Serial.println(port);

  if (!client.connect(host, port)) {
    Serial.println("[ERRO] Falha ao conectar.");
    return;
  }

  char body[32];
  snprintf(body, sizeof(body), "{\"valor\":%d}", percentagem);
  int bodyLen = strlen(body);

  Serial.print("[HTTP] Body: ");
  Serial.println(body);

  client.print("POST /api/contentores/pressao/1 HTTP/1.1\r\n");  // só UMA vez
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
  bool recebeuAlgo = false;

  while (client.connected() || client.available()) {
    while (client.available()) {
      char c = client.read();
      Serial.print(c);
      recebeuAlgo = true;
      timeout = millis();
    }
    if (millis() - timeout > 10000) {
      Serial.println();
      Serial.println("[ERRO] Timeout a ler resposta.");
      break;
    }
  }

  if (!recebeuAlgo) {
    Serial.println("[ERRO] Nenhum byte recebido.");
  }

  client.stop();
  Serial.println();
  Serial.println("[HTTP] Ligacao fechada.");
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

  Serial.print("Pressao bruta: ");
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