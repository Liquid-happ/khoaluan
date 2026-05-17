#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "Adafruit_SHT31.h"

#define MQ2_PIN     35
#define BAT_PIN     34
#define ESP_NOW_CHANNEL 11

Adafruit_SHT31 sht31 = Adafruit_SHT31();

/* --- ĐỊA CHỈ MAC CỦA NODE NHẬN --- */
uint8_t macReceiver[] = {0xD4, 0xE9, 0xF4, 0xA4, 0xE9, 0x58};

/* --- CẤU TRÚC GÓI TIN --- */
typedef struct struct_message {
    int id;
    float temp;
    float hum;
    int gas;
    float bat_vol;
} struct_message;

struct_message myData;
esp_now_peer_info_t peerInfo;

unsigned long send_start_time = 0;

/* --- CALLBACK KIỂM TRA TRẠNG THÁI GỬI & THỜI GIAN ACK --- */
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  unsigned long time_taken = millis() - send_start_time; // Tính thời gian phản hồi
  
  Serial.print("📡 Trạng thái truyền: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.print("✅ THÀNH CÔNG");
  } else {
    Serial.print("❌ THẤT BẠI");
  }
  Serial.printf(" | Thời gian ACK: %lu ms\n", time_taken);
  Serial.println("=================================================\n");
}

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  if (!sht31.begin(0x44)) {
    Serial.println("⚠️ Không tìm thấy cảm biến SHT31!");
  }

  // Khởi tạo Wi-Fi Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Ép kênh Wi-Fi
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESP_NOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  // Khởi tạo ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Lỗi khởi tạo ESP-NOW");
    return;
  }

  // Đăng ký hàm Callback khi gửi xong
  esp_now_register_send_cb(OnDataSent);

  // Đăng ký Node Nhận
  memcpy(peerInfo.peer_addr, macReceiver, 6);
  peerInfo.channel = ESP_NOW_CHANNEL;  
  peerInfo.encrypt = false; 
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ Lỗi thêm thiết bị nhận");
    return;
  }
  Serial.println("✅ HỆ THỐNG ESP32-1 ĐÃ SẴN SÀNG (KÊNH 11)!");
}

void loop() {
  // 1. Đọc cảm biến
  float temp = sht31.readTemperature();
  float hum = sht31.readHumidity();
  int gas = analogRead(MQ2_PIN);
  float bat_vol = (analogRead(BAT_PIN) / 4095.0) * 3.3 * 2; 

  if (isnan(temp)) temp = 0.0; // Reset về 0 nếu lỗi I2C

  // 2. In thông số ra màn hình để theo dõi
  Serial.println("=================================================");
  Serial.printf("📦 NODE 1 ĐANG ĐỌC DỮ LIỆU...\n");
  Serial.printf("🌡 Nhiệt độ: %.2f °C | 💧 Độ ẩm: %.2f %%\n", temp, hum);
  Serial.printf("💨 Khí Gas: %d       | 🔋 Pin: %.2f V\n", gas, bat_vol);
  Serial.println("Đang gửi gói tin đi...");

  // 3. Gán dữ liệu vào struct
  myData.id = 1;
  myData.temp = temp;
  myData.hum = hum;
  myData.gas = gas;
  myData.bat_vol = bat_vol;

  // 4. Lưu mốc thời gian bắt đầu gửi và phát lệnh
  send_start_time = millis();
  esp_now_send(macReceiver, (uint8_t *) &myData, sizeof(myData));

  // 5. CHỐT CHẶN: Tạm dừng hoàn toàn vòng lặp
  // 5000 = 5 giây. Bạn có thể thay đổi số này để kéo dài hoặc rút ngắn thời gian giữa các lần gửi.
  delay(5000); 
}
