#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include "Adafruit_SHT31.h"

#define MQ2_PIN     35
#define BAT_PIN     34
#define BUZZER_PIN  18
#define ESP_NOW_CHANNEL 11
#define WDT_TIMEOUT 15

Adafruit_SHT31 sht31 = Adafruit_SHT31();

/* --- ĐỊA CHỈ MAC --- */
uint8_t macNode2[] = {0xD4, 0xE9, 0xF4, 0xA4, 0xE9, 0x58}; // Đích (Pi)
uint8_t macNode3[] = {0xD4, 0xE9, 0xF4, 0xA4, 0xF2, 0xB8}; // Cần đăng ký để giải mã

const char *PMK_KEY = "SmartWareHouse88"; 
const char *LMK_KEY = "KhoaLuanIoT2026X"; 

/* --- KHÓA CHẶT 16 BYTE BỘ NHỚ CHO MỌI LOẠI CHIP ESP32 --- */
typedef struct __attribute__((packed)) struct_message {
    uint8_t id;
    float temp;
    float hum;
    uint16_t gas;
    float bat_vol;
    uint8_t is_relayed;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

unsigned long send_start_time = 0;
unsigned long last_read_time = 0;
unsigned long last_send_time = 0;
const unsigned long SEND_INTERVAL = 60000; // 1 phút = 60.000 ms

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  unsigned long time_taken = millis() - send_start_time; 
  Serial.print("📡 Trạng thái truyền: ");
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? "✅ THÀNH CÔNG" : "❌ THẤT BẠI");
  Serial.printf(" | Thời gian ACK: %lu ms\n", time_taken);
}

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
    if (len == sizeof(struct_message)) {
        struct_message relayData;
        memcpy(&relayData, incomingData, sizeof(relayData));
        
        if (relayData.id != 1) { // Không tự Relay chính mình
            relayData.is_relayed = 1; // 1 = Đi đường vòng
            
            Serial.println("\n-------------------------------------------------");
            Serial.printf("🔄 ĐANG LÀM CẦU NỐI (RELAY) CHO NODE %d...\n", relayData.id);
            
            send_start_time = millis();
            esp_now_send(macNode2, (uint8_t *) &relayData, sizeof(relayData));
            Serial.println("-------------------------------------------------\n");
        }
    }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  esp_task_wdt_init(WDT_TIMEOUT, true); esp_task_wdt_add(NULL);

  Wire.begin(21, 22); sht31.begin(0x44);

  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) return;

  esp_now_set_pmk((uint8_t *)PMK_KEY);
  esp_now_register_send_cb(OnDataSent);
  
  peerInfo.channel = ESP_NOW_CHANNEL;  
  peerInfo.encrypt = true; 
  memcpy(peerInfo.lmk, LMK_KEY, 16);
  
  memcpy(peerInfo.peer_addr, macNode2, 6); esp_now_add_peer(&peerInfo);
  memcpy(peerInfo.peer_addr, macNode3, 6); esp_now_add_peer(&peerInfo);

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("✅ ESP32-1 ĐÃ SẴN SÀNG (KIÊM RELAY)");
}

void loop() {
  esp_task_wdt_reset(); 

  if (millis() - last_read_time >= 2000) {
      last_read_time = millis();

      float temp = sht31.readTemperature();
      float hum = sht31.readHumidity();
      int gas = analogRead(MQ2_PIN);
      
      float VOLTAGE_DIVIDER_RATIO = 3.0; // Mạch phân áp 200k/100k
      float bat_vol = (analogRead(BAT_PIN) / 4095.0) * 3.3 * VOLTAGE_DIVIDER_RATIO; 
      if (bat_vol > 4.2) bat_vol = 4.2; 
      if (bat_vol < 3.4) bat_vol = 3.4;

      float bat_pct = ((bat_vol - 3.4) / (4.2 - 3.4)) * 100.0;
      if (bat_pct > 100.0) bat_pct = 100.0; 
      if (bat_pct < 0.0) bat_pct = 0.0;     

      if (isnan(temp)) temp = 0.0;

      if(temp >= 45.0 || gas >= 800) digitalWrite(BUZZER_PIN, HIGH);
      else digitalWrite(BUZZER_PIN, LOW);

      bool time_to_send = false;
      if (last_send_time == 0 || millis() - last_send_time >= SEND_INTERVAL) time_to_send = true;
      if (temp >= 45.0 || gas >= 800) time_to_send = true; // Nguy hiểm gửi luôn

      if (time_to_send) {
          Serial.println("=================================================");
          Serial.printf("📦 NODE 1 ĐANG ĐỌC DỮ LIỆU...\n");
          Serial.printf("🌡 Nhiệt độ: %.2f °C | 💧 Độ ẩm: %.2f %%\n", temp, hum);
          Serial.printf("💨 Khí Gas: %d       | 🔋 Pin: %.2f V (~%d%%)\n", gas, bat_vol, (int)bat_pct);

          myData.id = 1;
          myData.temp = temp;
          myData.hum = hum;
          myData.gas = gas;
          myData.bat_vol = bat_vol; 
          myData.is_relayed = 0; // 0 = Đi trực tiếp

          send_start_time = millis();
          Serial.println("Đang gửi gói tin đi...");
          esp_now_send(macNode2, (uint8_t *) &myData, sizeof(myData));

          Serial.println("=================================================\n");
          last_send_time = millis(); 
      }
  }
}
