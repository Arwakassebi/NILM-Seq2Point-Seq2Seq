/*
 * ESP32 + PZEM-004T v3.0 (Modbus RTU via MAX485) -> MQTT
 * Lit V, I, P, E, f, PF et publie un JSON sur "home/pzem".
 * Projet NILM - ENIT 2025/2026
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

HardwareSerial Pzemserial(2);

#define RXD2 16
#define TXD2 17
#define MAX485_DE 21
#define MAX485_RE 22

// ===== Identifiants (Wi-Fi / MQTT) =====
// Copier secrets.h.example en secrets.h et renseigner vos valeurs.
// secrets.h est ignoré par git : ne jamais le versionner.
#include "secrets.h"
// Le topic de publication est home/pzem (voir homeassistant/configuration.yaml)

WiFiClient espClient;
PubSubClient client(espClient);

// ===== CRC FUNCTION =====
unsigned short crc16(unsigned char *buf, int len) {
  unsigned short crc = 0xFFFF;
  for (int pos = 0; pos < len; pos++) {
    crc ^= (unsigned short)buf[pos];
    for (int i = 0; i < 8; i++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

// ===== WIFI =====
void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP ESP32: ");
  Serial.println(WiFi.localIP());
}

// ===== MQTT reconnect =====
void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");

    String clientId = "ESP32_PZEM_" + String((uint32_t)ESP.getEfuseMac());

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("connected ");
    } else {
      Serial.print("failed rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 3s...");
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  Pzemserial.begin(9600, SERIAL_8N1, RXD2, TXD2);

  pinMode(MAX485_DE, OUTPUT);
  pinMode(MAX485_RE, OUTPUT);
  digitalWrite(MAX485_DE, 0);
  digitalWrite(MAX485_RE, 0);

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setKeepAlive(60);

  configTime(0, 0, "pool.ntp.org");

  Serial.println("ESP32 Boot OK");
}

void loop() {

  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.loop();

  // Flush buffer
  while (Pzemserial.available()) Pzemserial.read();
  delay(200);
  while (Pzemserial.available()) Pzemserial.read();

  // Modbus request
  unsigned char request[8] = {
    0x01, 0x04, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00
  };

  unsigned short crc = crc16(request, 6);
  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  digitalWrite(MAX485_DE, 1);
  digitalWrite(MAX485_RE, 1);
  delay(10);
  Pzemserial.write(request, 8);
  Pzemserial.flush();
  delay(10);
  digitalWrite(MAX485_DE, 0);
  digitalWrite(MAX485_RE, 0);

  unsigned char buffer[60] = {0};
  int total = 0;
  unsigned long start = millis();

  while (total < 60 && (millis() - start) < 5000) {
    if (Pzemserial.available()) {
      buffer[total++] = Pzemserial.read();
    }
  }

  int startIdx = -1;
  for (int i = 0; i <= total - 3; i++) {
    if (buffer[i] == 0x01 && buffer[i+1] == 0x04 && buffer[i+2] == 0x14) {
      startIdx = i;
      break;
    }
  }

  if (startIdx == -1 || startIdx + 25 > total) {
    Serial.println("Failed to read PZEM!");
    delay(2000);
    return;
  }

  unsigned char *response = buffer + startIdx;

  unsigned short rcrc = crc16(response, 23);
  if (response[23] != (rcrc & 0xFF) || response[24] != (rcrc >> 8)) {
    Serial.println("CRC error!");
    delay(2000);
    return;
  }

  // ===== PARSE DATA =====
  float voltage = ((response[3] << 8) | response[4]) * 0.1f;

  uint32_t raw_current = ((uint32_t)response[7] << 24) |
                         ((uint32_t)response[8] << 16) |
                         ((uint32_t)response[5] << 8) |
                         response[6];
  float current = raw_current * 0.001f;

  uint32_t raw_power = ((uint32_t)response[11] << 24) |
                       ((uint32_t)response[12] << 16) |
                       ((uint32_t)response[9] << 8) |
                       response[10];
  float power = raw_power * 0.1f;

  uint32_t raw_energy = ((uint32_t)response[15] << 24) |
                        ((uint32_t)response[16] << 16) |
                        ((uint32_t)response[13] << 8) |
                        response[14];
  float energy = raw_energy;

  float frequency = ((response[17] << 8) | response[18]) * 0.1f;
  float pf = ((response[19] << 8) | response[20]) * 0.01f;

  Serial.println("------------------------------");
  Serial.print("Voltage: "); Serial.println(voltage);
  Serial.print("Current: "); Serial.println(current);
  Serial.print("Power: "); Serial.println(power);
  Serial.print("Energy: "); Serial.println(energy);
  Serial.print("Frequency: "); Serial.println(frequency);
  Serial.print("PF: "); Serial.println(pf);
  Serial.println("------------------------------");

  // ===== TIMESTAMP =====
  time_t now;
  time(&now);

  // ===== MQTT PAYLOAD =====
  String payload = "{";
  payload += "\"voltage\":" + String(voltage, 1) + ",";
  payload += "\"current\":" + String(current, 3) + ",";
  payload += "\"power\":" + String(power, 1) + ",";
  payload += "\"energy\":" + String(energy, 0) + ",";
  payload += "\"frequency\":" + String(frequency, 1) + ",";
  payload += "\"pf\":" + String(pf, 2) + ",";
  payload += "\"ts\":" + String((long)now);
  payload += "}";

  bool ok = client.publish("home/pzem", payload.c_str());

  if (ok) {
    Serial.println("MQTT publish OK ");
  } else {
    Serial.println("MQTT publish FAILED ");
    Serial.print("MQTT state: ");
    Serial.println(client.state());
  }

  delay(2000);
}
