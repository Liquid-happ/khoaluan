#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include "Adafruit_SHT31.h"

#define MQ2_PIN     34
#define BAT_PIN     35
#define BUZZER_PIN  18
#define ESP_NOW_CHANNEL 11
#define WDT_TIMEOUT 15

Adafruit_SHT31 sht31 = Adafruit_SHT31();

/* --- ĐỊA CHỈ MAC --- */
uint8_t macNode2[] = {0xD4, 0xE9, 0xF4, 0xA4, 0xE9, 0x58}; 
uint8_t macNode1[] = {0xAC, 0x15, 0x18, 0xD7, 0xCD, 0x08}; 

const char *PMK_KEY = "SmartWareHouse88"; 
const char *LMK_KEY = "KhoaLuanIoT2026X"; 

/* --- KHÓA CHẶT 16 BYTE BỘ NHỚ --- */
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

volatile bool send_success = false;

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    send_success = (status == ESP_NOW_SEND_SUCCESS);
    unsigned long time_taken = millis() - send_start_time; 
    Serial.printf("📡 Trạng thái truyền: %s | ACK: %lu ms\n", 
                  send_success ? "✅ THÀNH CÔNG" : "❌ THẤT BẠI", time_taken);
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
  memcpy(peerInfo.peer_addr, macNode1, 6); esp_now_add_peer(&peerInfo);
  
  Serial.println("✅ ESP32-3 ĐÃ SẴN SÀNG");
}

void loop() {
  esp_task_wdt_reset(); 

  if (millis() - last_read_time >= 2000) {
      last_read_time = millis();
      
      float temp = sht31.readTemperature();
      float hum = sht31.readHumidity();
      int gas = analogRead(MQ2_PIN);
      
      // --- ĐỌC ĐIỆN ÁP PIN CHUẨN ---
      int adc_val = analogRead(BAT_PIN);
      float VOLTAGE_DIVIDER_RATIO = 3.0; 
      float CALIBRATION_FACTOR = 1.0; 
      
      float bat_vol = (adc_val / 4095.0) * 3.3 * VOLTAGE_DIVIDER_RATIO * CALIBRATION_FACTOR; 

      float bat_pct = ((bat_vol - 3.4) / (4.2 - 3.4)) * 100.0;
      if (bat_pct > 100.0) bat_pct = 100.0; 
      if (bat_pct < 0.0) bat_pct = 0.0;     

      if (isnan(temp)) temp = 0.0;

      if(temp >= 45.0 || gas >= 800) digitalWrite(BUZZER_PIN, HIGH);
      else digitalWrite(BUZZER_PIN, LOW);

      bool force_send = false;
      if (last_send_time == 0 || millis() - last_send_time >= SEND_INTERVAL) force_send = true; 
      if (temp >= 45.0 || gas >= 800) force_send = true;        

      if (force_send) {
          Serial.println("=================================================");
          Serial.printf("📦 NODE 3 ĐANG ĐỌC DỮ LIỆU...\n");
          Serial.printf("🌡 Nhiệt độ: %.2f °C | 💧 Độ ẩm: %.2f %%\n", temp, hum);
          Serial.printf("💨 Khí Gas: %d       | 🔋 ADC: %d -> Pin: %.2f V (~%d%%)\n", gas, adc_val, bat_vol, (int)bat_pct);

          myData.id = 3; 
          myData.temp = temp;
          myData.hum = hum;
          myData.gas = gas;
          myData.bat_vol = bat_vol;
          myData.is_relayed = 0; 

          send_start_time = millis();
          Serial.println("Đang gửi thẳng tới Node 2 (Pi)...");
          esp_now_send(macNode2, (uint8_t *) &myData, sizeof(myData));
          delay(60); 
          
          if (!send_success) {
              Serial.println("⚠️ Node 2 mất kết nối! Tự động định tuyến qua Node 1...");
              esp_now_send(macNode1, (uint8_t *) &myData, sizeof(myData));
          }
          
          Serial.println("=================================================\n");
          last_send_time = millis();
      }
  }
}
