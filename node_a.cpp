#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <esp_now.h>
#include <WiFiManager.h> 
#include "Adafruit_SHT31.h"

#define MQ2_PIN     35
#define BUZZER_PIN  18

Adafruit_SHT31 sht31 = Adafruit_SHT31();

/* --- CẤU HÌNH MQTT --- */
const char* mqtt_server = "10.0.39.29"; 
const int   mqtt_port   = 1883;
const char* mqtt_user   = "quyet";
const char* mqtt_pass   = "quyet123";

WiFiClient espClient;
PubSubClient client(espClient);

/* --- MAC ADDRESS CHO CÁC NODE PHỤ --- */
uint8_t macESP2[6] = {0xD4, 0xE9, 0xF4, 0xA4, 0xF2, 0xB8};
uint8_t macESP3[6] = {0xAC, 0x15, 0x18, 0xD7, 0xCD, 0x08};

unsigned long lastSend = 0;
const long interval = 10000;
unsigned long lastReconnectAttempt = 0; 

/* --- CẤU TRÚC GÓI TIN ESP-NOW --- */
typedef struct struct_message {
    float temp;
    float hum;
} struct_message;

struct_message incomingReadings;
float referenceTemp = 0.0;
float referenceHum = 0.0;

/* --- HÀM HIỆU CHỈNH NHIỆT ĐỘ --- */
float calibrateTemp(float localT, float refT) {
    if (refT == 0.0 || refT == -99.0) return localT; 
    if (abs(localT - refT) > 2.0) return refT;
    return localT;
}

/* --- ESP-NOW NHẬN DỮ LIỆU --- */
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) {
  if (len == 0) return;

  Serial.printf("\n🔥 [DEBUG ESP-NOW] Nhận gói tin: %d bytes\n", len);

  if (len == sizeof(struct_message)) {
    memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
    referenceTemp = incomingReadings.temp;
    referenceHum = incomingReadings.hum;
    
    if (referenceTemp != -99.0) {
      Serial.printf("📥 DỮ LIỆU CHUẨN -> Temp Node B: %.2f | Hum Node B: %.2f\n", referenceTemp, referenceHum);
    } else {
      Serial.println("⚠️ Node B báo lỗi cảm biến SHT30!");
    }
  } 
  else {
    char command[32];
    memcpy(command, incomingData, len > 31 ? 31 : len);
    command[len > 31 ? 31 : len] = '\0';

    if (strcmp(command, "STOP_BUZZER") == 0) {
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("ESP-NOW: Đã nhận lệnh STOP_BUZZER!");
    }
  }
}

/* --- MQTT CALLBACK --- */
void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];

  if (message == "STOP_BUZZER") {
    digitalWrite(BUZZER_PIN, LOW);
    const char* cmd = "STOP_BUZZER";
    esp_now_send(macESP2, (uint8_t*)cmd, strlen(cmd) + 1);
    esp_now_send(macESP3, (uint8_t*)cmd, strlen(cmd) + 1);
  }
}

/* --- MQTT NON-BLOCKING --- */
void connectMQTT() {
  if (millis() - lastReconnectAttempt > 5000) {
    lastReconnectAttempt = millis();
    Serial.print("Attempting MQTT connection...");
    
    if (client.connect("ESP32_FIRE_NODE1", mqtt_user, mqtt_pass)) {
      Serial.println("✅ connected to Broker");
      client.subscribe("warehouse/alert");
    } else {
      Serial.print("failed, rc="); Serial.print(client.state());
      Serial.println(" (Thử lại sau 5s...)");
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(21, 22);
  if (!sht31.begin(0x44)) Serial.println("SHT30 not found on Node A!");

  WiFiManager wm;
  bool res = wm.autoConnect("ESP32_Config_SmartWarehouse", "12345678");

  if(!res) {
    Serial.println("Failed to connect WiFi or hit timeout");
  } else {
    Serial.println("✅ WiFi Connected via WiFiManager!");
    WiFi.setSleep(false); 
    Serial.printf(">>> Node A đang chạy ở Wi-Fi Channel: %d <<<\n", WiFi.channel());
  }

  if (esp_now_init() != ESP_OK) Serial.println("Error initializing ESP-NOW");
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = WiFi.channel(); 
  peerInfo.encrypt = false;

  memcpy(peerInfo.peer_addr, macESP2, 6); esp_now_add_peer(&peerInfo);
  memcpy(peerInfo.peer_addr, macESP3, 6); esp_now_add_peer(&peerInfo);

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) connectMQTT(); 
    else client.loop(); 
  }

  if (millis() - lastSend >= interval) {
    lastSend = millis();
    
    float localTemp = sht31.readTemperature();
    float hum  = sht31.readHumidity();
    int   gas  = analogRead(MQ2_PIN);

    if (isnan(localTemp) || isnan(hum)) {
      Serial.println("❌ Lỗi đọc SHT31 Node A!");
      localTemp = 0.0; hum = 0.0;
    }

    float finalTemp = calibrateTemp(localTemp, referenceTemp);
    if (gas > 600) digitalWrite(BUZZER_PIN, HIGH);

    if (WiFi.status() == WL_CONNECTED && client.connected()) {
      char payload[200];
      snprintf(payload, sizeof(payload),
               "{\"temperature\":%.2f,\"local_temp\":%.2f,\"ref_temp\":%.2f,\"humidity\":%.2f,\"gas\":%d}",
               finalTemp, localTemp, referenceTemp, hum, gas);
      client.publish("esp32/sensor", payload);
    }

    Serial.println("\n------------------------------------");
    Serial.printf("Temp: %.2f °C (Local: %.2f | Node B Ref: %.2f)\n", finalTemp, localTemp, referenceTemp);
    Serial.printf("Humidity: %.2f %% | Gas: %d\n", hum, gas);
    Serial.printf("Broker MQTT: %s\n", (client.connected() ? "Connected" : "Disconnected"));
    Serial.println("------------------------------------");
  }
}