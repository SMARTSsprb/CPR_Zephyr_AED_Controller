/**
 * @file    pcb_hardware_test.ino
 * @brief   Arduino Hardware Diagnostics & Soldered PCB Verification
 * @target  ESP32-C3 Super Mini
 * 
 * HARDWARE PIN MAPPING:
 *  - GPIO 3 : RGB LED Red   (via 220-330 Ohm resistor)
 *  - GPIO 4 : RGB LED Green (via 220-330 Ohm resistor)
 *  - GPIO 5 : RGB LED Blue  (via 220-330 Ohm resistor)
 *  - GPIO 7 : Hardware Sync Pulse Out (to Manikin MCU)
 *  - GPIO 8 : Onboard Blue LED (Active-LOW)
 *  - GPIO 9 : Shock Button (Momentary tactile to GND, Active-LOW)
 *  - GPIO 6 : Optional Power Button (Tactile to GND, Active-LOW)
 * 
 * SERIAL COMMANDS (115200 Baud):
 *  - 'r' : Test RED LED
 *  - 'g' : Test GREEN LED
 *  - 'b' : Test BLUE LED
 *  - 'y' : Test AMBER / YELLOW (Red + Green)
 *  - 'w' : Test WHITE (All 3 Channels)
 *  - 'o' : Turn OFF all LEDs
 *  - 's' : Fire Hardware Sync Pulse on GPIO 7
 *  - 't' : Run automatic RGB color cycle test
 *  - '?' : Print Help Menu & MAC Addresses
 */

#include <WiFi.h>
#include <esp_mac.h>

/* =============================================================================
 * HARDWARE CONFIGURATION
 * ============================================================================= */
#define PIN_RGB_RED      3
#define PIN_RGB_GREEN    4
#define PIN_RGB_BLUE     5
#define PIN_SYNC_OUT     7
#define PIN_STATUS_LED   8   /* Onboard Blue LED (Active-LOW on ESP32-C3) */
#define PIN_SHOCK_BTN    9   /* BOOT / Shock button (Active-LOW) */
#define PIN_POWER_BTN    6   /* Optional logic switch (Active-LOW) */

/* Set to true if using Common Anode (+3.3V) RGB LED.
 * Set to false if using Common Cathode (GND) RGB LED (Standard). */
const bool RGB_COMMON_ANODE = false;

/* =============================================================================
 * HELPER FUNCTIONS
 * ============================================================================= */
void setRgb(bool r, bool g, bool b) {
  bool valR = RGB_COMMON_ANODE ? !r : r;
  bool valG = RGB_COMMON_ANODE ? !g : g;
  bool valB = RGB_COMMON_ANODE ? !b : b;

  digitalWrite(PIN_RGB_RED,   valR ? HIGH : LOW);
  digitalWrite(PIN_RGB_GREEN, valG ? HIGH : LOW);
  digitalWrite(PIN_RGB_BLUE,  valB ? HIGH : LOW);
}

void fireSyncPulse(uint32_t durationMs = 50) {
  Serial.printf("[SYNC OUT] Firing %u ms HIGH pulse on GPIO 7...\n", durationMs);
  digitalWrite(PIN_SYNC_OUT, HIGH);
  delay(durationMs);
  digitalWrite(PIN_SYNC_OUT, LOW);
  Serial.println("[SYNC OUT] Pulse complete (GPIO 7 LOW).");
}

void printMenu() {
  Serial.println("\n--------------------------------------------------");
  Serial.println("  INTERACTIVE SERIAL COMMANDS (Send character):");
  Serial.println("   r -> Red LED ON");
  Serial.println("   g -> Green LED ON");
  Serial.println("   b -> Blue LED ON");
  Serial.println("   y -> Yellow / Amber ON (Red + Green)");
  Serial.println("   w -> White ON (Red + Green + Blue)");
  Serial.println("   o -> Turn OFF all LEDs");
  Serial.println("   s -> Fire 50ms Sync pulse on GPIO 7");
  Serial.println("   t -> Run automated RGB test sequence");
  Serial.println("   ? -> Show this menu & hardware status");
  Serial.println("--------------------------------------------------\n");
}

void runSelfTestCycle() {
  Serial.println("\n>>> [SELF-TEST] Testing RGB Channels & Status LED <<<");

  // 1. Red
  Serial.println(" -> Red Channel (GPIO 3) ON");
  setRgb(true, false, false);
  digitalWrite(PIN_STATUS_LED, LOW); // Onboard LED ON
  delay(800);

  // 2. Green
  Serial.println(" -> Green Channel (GPIO 4) ON");
  setRgb(false, true, false);
  digitalWrite(PIN_STATUS_LED, HIGH); // Onboard LED OFF
  delay(800);

  // 3. Blue
  Serial.println(" -> Blue Channel (GPIO 5) ON");
  setRgb(false, false, true);
  digitalWrite(PIN_STATUS_LED, LOW);
  delay(800);

  // 4. Amber / Yellow
  Serial.println(" -> Amber/Yellow (Red + Green) ON");
  setRgb(true, true, false);
  delay(800);

  // 5. White
  Serial.println(" -> White (R + G + B) ON");
  setRgb(true, true, true);
  delay(800);

  // 6. Off
  Serial.println(" -> All LEDs OFF");
  setRgb(false, false, false);
  digitalWrite(PIN_STATUS_LED, HIGH);
  delay(300);

  // 7. Test Sync Pulse
  fireSyncPulse(50);

  Serial.println(">>> [SELF-TEST] Complete! Monitoring buttons & serial input... <<<\n");
}

/* =============================================================================
 * ARDUINO SETUP
 * ============================================================================= */
void setup() {
  Serial.begin(115200);
  delay(1200);

  // Initialize GPIOs
  pinMode(PIN_RGB_RED,    OUTPUT);
  pinMode(PIN_RGB_GREEN,  OUTPUT);
  pinMode(PIN_RGB_BLUE,   OUTPUT);
  pinMode(PIN_SYNC_OUT,   OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);

  pinMode(PIN_SHOCK_BTN,  INPUT_PULLUP);
  pinMode(PIN_POWER_BTN,  INPUT_PULLUP);

  // Default states
  setRgb(false, false, false);
  digitalWrite(PIN_SYNC_OUT, LOW);
  digitalWrite(PIN_STATUS_LED, HIGH); // Off for active-low

  // Read MAC Addresses
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  uint8_t sta_mac[6];
  WiFi.macAddress(sta_mac);

  uint8_t bt_mac[6];
  esp_read_mac(bt_mac, ESP_MAC_BT);

  Serial.println("\n==================================================");
  Serial.println("    ESP32-C3 SOLDERED PCB HARDWARE DIAGNOSTICS   ");
  Serial.println("==================================================");
  Serial.printf(" Wi-Fi STA MAC (ESP-NOW): %02X:%02X:%02X:%02X:%02X:%02X\n",
                sta_mac[0], sta_mac[1], sta_mac[2], sta_mac[3], sta_mac[4], sta_mac[5]);
  Serial.printf(" Bluetooth LE MAC:        %02X:%02X:%02X:%02X:%02X:%02X\n",
                bt_mac[0], bt_mac[1], bt_mac[2], bt_mac[3], bt_mac[4], bt_mac[5]);
  Serial.println("--------------------------------------------------");
  Serial.println(" Pinout Configuration:");
  Serial.println("  - GPIO 3 : RGB RED");
  Serial.println("  - GPIO 4 : RGB GREEN");
  Serial.println("  - GPIO 5 : RGB BLUE");
  Serial.println("  - GPIO 7 : Hardware Sync Pulse Out");
  Serial.println("  - GPIO 8 : Onboard Blue LED (Active-LOW)");
  Serial.println("  - GPIO 9 : Shock Button (BOOT / External to GND)");
  Serial.println("  - GPIO 6 : Power Pin / Button (to GND)");
  Serial.println("==================================================");

  printMenu();

  // Run initial self-test
  runSelfTestCycle();
}

/* =============================================================================
 * ARDUINO MAIN LOOP
 * ============================================================================= */
static bool last_shock_btn_state = HIGH;
static bool last_pwr_btn_state   = HIGH;
static uint32_t last_shock_time  = 0;
static uint32_t last_pwr_time    = 0;

void loop() {
  uint32_t now = millis();

  // 1. Process Serial Commands
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') return; // ignore newlines

    switch (c) {
      case 'r':
      case 'R':
        Serial.println("[CMD] RED LED ON");
        setRgb(true, false, false);
        break;

      case 'g':
      case 'G':
        Serial.println("[CMD] GREEN LED ON");
        setRgb(false, true, false);
        break;

      case 'b':
      case 'B':
        Serial.println("[CMD] BLUE LED ON");
        setRgb(false, false, true);
        break;

      case 'y':
      case 'Y':
      case 'a':
      case 'A':
        Serial.println("[CMD] AMBER / YELLOW LED ON");
        setRgb(true, true, false);
        break;

      case 'w':
      case 'W':
        Serial.println("[CMD] WHITE LED ON (All 3 Channels)");
        setRgb(true, true, true);
        break;

      case 'o':
      case 'O':
        Serial.println("[CMD] All LEDs OFF");
        setRgb(false, false, false);
        digitalWrite(PIN_STATUS_LED, HIGH);
        break;

      case 's':
      case 'S':
        fireSyncPulse(50);
        break;

      case 't':
      case 'T':
        runSelfTestCycle();
        break;

      case '?':
      case 'h':
      case 'H':
        printMenu();
        break;

      default:
        Serial.printf("[?] Unknown command '%c'. Send '?' for help.\n", c);
        break;
    }
  }

  // 2. Monitor Shock Button (GPIO 9) with Debouncing
  bool shock_btn = digitalRead(PIN_SHOCK_BTN);
  if (shock_btn != last_shock_btn_state && (now - last_shock_time > 50)) {
    last_shock_time = now;
    last_shock_btn_state = shock_btn;

    if (shock_btn == LOW) {
      // Button Pressed!
      Serial.println("\n*** [BUTTON EVENT] SHOCK BUTTON PRESSED (GPIO 9)! ***");
      
      // Flash Red Hazard Indication & fire Sync Pulse
      setRgb(true, false, false);
      digitalWrite(PIN_STATUS_LED, LOW);
      fireSyncPulse(50);
      
      // Return to Green after 500ms
      delay(500);
      setRgb(false, true, false);
      digitalWrite(PIN_STATUS_LED, HIGH);
      Serial.println("*** [BUTTON EVENT] Shock Action Finished. LED set to Green. ***\n");
    } else {
      Serial.println("[BUTTON EVENT] Shock Button Released (GPIO 9 HIGH).");
    }
  }

  // 3. Monitor Power Pin / Button (GPIO 6) with Debouncing
  bool pwr_btn = digitalRead(PIN_POWER_BTN);
  if (pwr_btn != last_pwr_btn_state && (now - last_pwr_time > 50)) {
    last_pwr_time = now;
    last_pwr_btn_state = pwr_btn;

    if (pwr_btn == LOW) {
      Serial.println("\n*** [BUTTON EVENT] POWER PIN / SWITCH PRESSED (GPIO 6 LOW) ***");
      // Blink Blue to indicate power button
      setRgb(false, false, true);
      delay(200);
      setRgb(false, false, false);
    } else {
      Serial.println("[BUTTON EVENT] Power Pin / Switch Released (GPIO 6 HIGH).");
    }
  }

  delay(10); // Small loop tick
}
