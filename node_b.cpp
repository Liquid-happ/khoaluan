#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_wifi.h> 
#include "Adafruit_SHT31.h"

Adafruit_SHT31 sht31 = Adafruit_SHT31();

/* --- ĐỊA CHỈ MAC ĐÍCH (NODE A) --- */
uint8_t macNodeA[] = {0xD4, 0xE9, 0xF4, 0xA4, 0xE9, 0x58}; 

/* --- CẤU TRÚC GÓI TIN ESP-NOW --- */
typedef struct struct_message {
    float temp;
    float hum;
} struct_message;

struct_message myData;
unsigned long lastTime = 0;
const long timerDelay = 5000;

void setup() {
  Serial.begin(115200);

  // 1. KHỞI TẠO CẢM BIẾN I2C
  Wire.begin(21, 22);
  if (!sht31.begin(0x44)) {
    Serial.println("❌ Không tìm thấy SHT30. Đang thử địa chỉ 0x45...");
    if (!sht31.begin(0x45)) {
      Serial.println("❌ Vẫn không thấy SHT30! Vui lòng kiểm tra lại dây cắm.");
    }
  }

  // 2. KHỞI TẠO WI-FI & ÉP CỨNG CHANNEL 11
  WiFi.mode(WIFI_STA);
  
  int hardcodedChannel = 11; // 🎯 ÉP CỨNG ĐÚNG CHANNEL CỦA NODE A
  Serial.printf("Đang ép cứng Wi-Fi Channel sang: %d\n", hardcodedChannel);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(hardcodedChannel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  WiFi.disconnect(); 

  // 3. KHỞI TẠO ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Lỗi khởi tạo ESP-NOW");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, macNodeA, 6);
  peerInfo.channel = hardcodedChannel;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Không thể kết nối với Node A");
    return;
  }

  Serial.println("🚀 Node B đã sẵn sàng. Đang đọc và gửi dữ liệu...");
}

void loop() {
  if (millis() - lastTime >= timerDelay) {
    lastTime = millis();

    float t = sht31.readTemperature();
    float h = sht31.readHumidity();

    if (isnan(t) || isnan(h)) {
      Serial.println("⚠️ Lỗi SHT30, gửi mã lỗi -99");
      myData.temp = -99.0;
      myData.hum = -99.0;
    } else {
      Serial.printf("Nhiệt độ: %.2f °C | Độ ẩm: %.2f %%\n", t, h);
      myData.temp = t;
      myData.hum = h;
    }

    esp_err_t result = esp_now_send(macNodeA, (uint8_t *) &myData, sizeof(myData));
    
    if (result == ESP_OK) {
      Serial.println("✅ Gửi thành công sang Node A");
    } else {
      Serial.println("❌ Lỗi đường truyền ESP-NOW");
    }
  }
}