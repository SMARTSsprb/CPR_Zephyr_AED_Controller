/**
 * @file    get_mac_address.ino
 * @brief   Instant ESP32-C3 MAC Address Reader (ESP-NOW & Bluetooth)
 * @details Flash to any ESP32-C3 to read its burned-in factory MAC address.
 */

#include <WiFi.h>
#include <esp_mac.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  Serial.println("\n==================================================");
  Serial.println("         ESP32-C3 MCU MAC ADDRESS READER          ");
  Serial.println("==================================================");

  // 1. Read Wi-Fi Station MAC (Used for ESP-NOW)
  uint8_t mac[6];
  WiFi.macAddress(mac);

  Serial.printf("Wi-Fi STA MAC (ESP-NOW): %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("C-Array format:          {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}\n\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // 2. Read Bluetooth MAC
  uint8_t bt_mac[6];
  esp_read_mac(bt_mac, ESP_MAC_BT);
  Serial.printf("Bluetooth LE MAC:        %02X:%02X:%02X:%02X:%02X:%02X\n",
                bt_mac[0], bt_mac[1], bt_mac[2], bt_mac[3], bt_mac[4], bt_mac[5]);
  Serial.println("==================================================\n");
}

void loop() {
  delay(5000);
}

