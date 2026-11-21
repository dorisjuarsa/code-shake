/*
 * COKE_SHAKE.ino
 *
 * Interactive Coca-Cola Bottle Controller
 * Hardware: Waveshare ESP32-S3-Touch-LCD-1.83 (ST7789P + CST816T + QMI8658 + AXP2101 + ES8311)
 *
 * BLE Game Protocol:
 * - Device Names: COCA-COLA-BOTTLE-01 / COCA-COLA-BOTTLE-02
 * - Service UUID:        19b10000-e8f2-537e-4f6c-d104768a1214
 * - Characteristic UUID: 19b10001-e8f2-537e-4f6c-d104768a1214 (NOTIFY | READ)
 * - Payload Format:      SHAKE,<id>,<intensity> (e.g. SHAKE,1,75)
 *
 * Features:
 * 1. Omni-directional Peak-to-Peak Bipolar Shake Filter (Zero false triggers).
 * 2. Tuned for natural 30cm human hand shaking (Sensitivity 0.10g - 2.00g).
 * 3. Dual Sliders: FORCE (0.10g - 2.00g) & SPEED (Ultra-Fast 5ms - Normal 100ms).
 * 4. Inverted-contrast slider text rendered directly inside bars to maximize screen area.
 * 5. Nokia Pager / Smartwatch Carousel Settings (5-page carousel with < PREV & NEXT >).
 * 6. Interactive Touch Drag Slider for Brightness (PWM 5-255).
 * 7. Mini Oscilloscope with 100% full-width wave & dynamic curve amplification.
 * 8. Safe curved-bezel header layout with Vector Battery & Vector BLE icons.
 * 9. Zero-flicker Home screen (no periodic full-screen wipe).
 * 10. Auto-Sleep timer selector (1 to 5 min / Never).
 * 11. RTC RAM Sequence Counter (#1, #2, #3...) immune to Flash wear & sleep reset.
 */

#include "Arduino_DriveBus_Library.h"
#include "Arduino_GFX_Library.h"
#include "SensorQMI8658.hpp"
#include "XPowersLib.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "pin_config.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <Preferences.h>
#include <Wire.h>

// ==========================================
// 1. CONFIGURATION & CONSTANTS
// ==========================================
#define DEFAULT_BOTTLE_ID 1
#define BOOT_BTN_PIN 0
#define IMU_INT2_PIN 21

// Audio Amplifier Shutdown Pin (Held LOW for 0 mA silent operation)
#define AUDIO_PA_PIN 46

// Display Dimensions
#define SCR_W 240
#define SCR_H 284
#define MARGIN_X 16
#define HEADER_SAFE_X 22 // Offset to safely clear curved glass corners

// Modern AMOLED Colors (16-bit RGB565)
#define COLOR_BG 0x0000       // Deep AMOLED Black
#define COLOR_COKE_RED 0xE904 // Official Coca-Cola Crimson Red
#define COLOR_CARD_BG 0x18C3  // Dark Charcoal Slate
#define COLOR_CARD_BRD 0x39E7 // Border Grey
#define COLOR_WHITE 0xFFFF    // Pure White
#define COLOR_TEXT_DIM 0x9CD3 // Subdued Grey
#define COLOR_GREEN 0x07E0    // Vivid Emerald Green
#define COLOR_CYAN 0x07FF     // Neon BLE Cyan
#define COLOR_YELLOW 0xFDE0   // Warning Yellow

// BLE UUIDs
#define GAME_SERVICE_UUID "19b10000-e8f2-537e-4f6c-d104768a1214"
#define GAME_CHAR_UUID "19b10001-e8f2-537e-4f6c-d104768a1214"

// Touch Button Structure
struct TouchButton {
  int16_t x, y, w, h;
  const char *label;
  uint16_t bgColor;
  uint16_t textColor;

  bool contains(int32_t tx, int32_t ty) const {
    return (tx >= x && tx <= (x + w) && ty >= y && ty <= (y + h));
  }
};

// Peripherals & Display
Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI);
Arduino_GFX *gfx =
    new Arduino_ST7789(bus, LCD_RST, 0, true, SCR_W, SCR_H, 0, 0, 0, 0);

std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void IIC_Touch_Interrupt_Handler(void);
std::unique_ptr<Arduino_IIC>
    CST816T(new Arduino_CST816x(IIC_Bus, CST816T_DEVICE_ADDRESS, TP_RST, TP_INT,
                                IIC_Touch_Interrupt_Handler));

void IIC_Touch_Interrupt_Handler(void) { CST816T->IIC_Interrupt_Flag = true; }

// Hardware Objects
SensorQMI8658 qmi;
XPowersAXP2101 pmu;
Preferences prefs;

// BLE Server & Characteristic
NimBLEServer *bleServer = nullptr;
NimBLECharacteristic *shakeChar = nullptr;
bool isBleConnected = false;

// ==========================================
// APPLICATION STATE MACHINE
// ==========================================
enum ScreenState { STATE_HOME, STATE_SETTINGS, STATE_SET_COUNTER };

ScreenState currentState = STATE_HOME;
int currentSettingsPage = 0; // 0: Device ID, 1: Counter ID, 2: Brightness, 3:
                             // Shake Diag, 4: Auto Sleep, 5: Power Control, 6: Touch Canvas
const int TOTAL_SETTINGS_PAGES = 7;

// Settings (Saved in NVS)
int bottleNumber = DEFAULT_BOTTLE_ID; // 1 or 2
float shakeThreshold = 0.35f;         // Range: 0.10g to 2.00g (Default 0.35g)
int shakeCooldownMs =
    30; // Range: 5ms (Ultra Fast) to 100ms (Normal) (Default 30ms)
int brightnessPWM = 160;    // Range: 5 (Super dim) to 255 (Full bright)
int sleepTimerSeconds = 60; // Default: 60s (1 min). Options: 10s, 60s, 120s,
                            // 180s, 300s, 0 (Always ON)
int sleepProfile = 0;       // 0: FAST STANDBY (~14mA, Radio ON), 1: ULTRA SLEEP
                            // (~70uA, True Deep Sleep)

// Power & Screen Sleep States (Screen stays OFF inside bottle to save battery)
bool isScreenOn = true;
bool isCountingDownSleep = false;
bool countdownFrameDrawn = false;
unsigned long sleepCountdownStartTime = 0;
unsigned long lastActivityTime = 0;
int lastCountdownSecondDrawn = -1;
int lastCountdownBarWidth = -1;

// Wakeup Splash & Bottle Active Tracking
bool isBottleActive = false;
bool isWakeupSplashActive = false;
unsigned long wakeupSplashEndTime = 0;

// RTC RAM Shake Counter (Preserves across sleep without Flash wear)
RTC_DATA_ATTR uint32_t shakeCounter = 1;

// ==========================================
// OMNI-DIRECTIONAL PEAK-TO-PEAK SHAKE ENGINE
// ==========================================
enum ShakeP2PState { P2P_IDLE, P2P_WAIT_REVERSE, P2P_COOLDOWN };

ShakeP2PState p2pState = P2P_IDLE;
float p2pFirstPeakX = 0.0f;
float p2pFirstPeakY = 0.0f;
float p2pFirstPeakZ = 0.0f;
float p2pFirstPeakMag = 0.0f;
unsigned long p2pFirstPeakTime = 0;
unsigned long lastValidShakeTime = 0;

// Diagnostics Waveform Buffer (for Mini Oscilloscope)
const int WAVE_BUFFER_SIZE = 40;
float waveBuffer[WAVE_BUFFER_SIZE];
int waveBufIndex = 0;
unsigned long lastWaveSampleTime = 0;

// Diagnostics Display Status
bool diagTriggered = false;
unsigned long diagTriggerTime = 0;
int diagLastIntensity = 0;

// Touch Drag State Tracking
bool isTouching = false;
int32_t touchDownX = -1;
int32_t touchDownY = -1;
int32_t currentTouchX = -1;
int32_t currentTouchY = -1;
unsigned long lastTouchSeenTime = 0;
bool isBrightnessDragging = false;
bool isForceDragging = false;
bool isSpeedDragging = false;

// Touch Diagnostic Canvas Tracking (Page 7/7)
int32_t lastTouchPaintX = -1;
int32_t lastTouchPaintY = -1;
uint32_t touchPaintCount = 0;

// Temporary buffer for Numpad
String tempInputNumber = "";

// Forward Declarations
void drawHeader(const char *title);
void drawButton(const TouchButton &btn, bool active = false);
void drawSettingsNavButtons();
void renderHomeScreen();
void renderSettingsScreen();
void updateSettingsPageOnly();
void renderSettingsPageContent();
void renderNumpadScreen();
void updateNumpadDisplay();
void drawNumpadButton(int idx, bool active = false);
void drawNumpadCancelButton(bool active = false);
void drawOscilloscopeWaveform();
void updateDiagStatusBadge(bool triggered, int intensity);
void updateBrightnessSliderUI();
void updateDualSlidersUI();
void handleForceDrag(int32_t curX);
void handleSpeedDrag(int32_t curX);
void renderTouchCanvasPage();
void handleTouchPaintPress(int32_t tx, int32_t ty);
void handleTouchPaintDrag(int32_t tx, int32_t ty);
void handleTouchPaintRelease();
void applyBrightnessPWM(int pwm);
int getBatteryLevelPercentage();
void drawVectorBattery(int x, int y, bool forceOutline = true);
void updateBatteryIndicatorRealtime(bool force = false);
void sendShakeData(int intensity);
void executeDeepSleep();
void turnScreenOff();
void turnScreenOn();
void executeSleepNow();
void executePowerOff();
void disconnectBleClients();
void resetInactivityTimer();
void renderSleepCountdownScreen(int remSec, int progressPct);
void renderWakeupSplash();
void checkPhysicalButtons();
String getBottleFullName();
void initBle();
void loadSettings();
void saveSettings();

String getBottleFullName() {
  return (bottleNumber == 2) ? "COCA-COLA-BOTTLE-02" : "COCA-COLA-BOTTLE-01";
}

// Text Centering Utility
void drawCenteredText(const char *text, int y, uint16_t color,
                      uint8_t size = 1) {
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  int cardW = SCR_W - (MARGIN_X * 2);
  int textW = strlen(text) * (6 * size);
  int tx = MARGIN_X + (cardW - textW) / 2;
  gfx->setCursor(tx, y);
  gfx->print(text);
}

// ==========================================
// BLE CALLBACKS & SETUP
// ==========================================
int getBatteryLevelPercentage() {
  int batt = pmu.getBatteryPercent();
  if (batt <= 0 || batt > 100) {
    uint16_t vbat = pmu.getBattVoltage();
    if (vbat >= 3300) {
      batt = constrain((int)((vbat - 3300) * 100 / (4180 - 3300)), 1, 100);
    } else if (vbat > 2000) {
      batt = 1;
    } else {
      batt = 100;
    }
  }
  return constrain(batt, 1, 100);
}

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
    isBleConnected = true;
    lastValidShakeTime = millis(); // Refresh timer agar game mendapat batas 3
                                   // menit penuh sejak connect
    Serial.println("[BLE] >>> GAME CONNECTED! <<<");
    // NOTE: Tidak kirim paket dummy saat koneksi.
    // Data baterai akan ikut terkirim otomatis pada shake berikutnya
    // (sendShakeData).
  }

  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo,
                    int reason) override {
    isBleConnected = false;
    lastActivityTime = millis();
    lastValidShakeTime = millis();
    Serial.printf("[BLE] --- DISCONNECTED (Reason: 0x%02X) ---\n", reason);

    // Selalu nyalakan kembali advertising agar auto-reconnect bekerja seketika
    // saat botol kembali dalam jangkauan sinyal!
    NimBLEDevice::startAdvertising();
    Serial.println("[BLE] Advertising Restarted for Auto-Reconnect");
  }
};

void disconnectBleClients() {
  if (bleServer && bleServer->getConnectedCount() > 0) {
    std::vector<uint16_t> peers = bleServer->getPeerDevices();
    for (uint16_t handle : peers) {
      bleServer->disconnect(handle);
    }
    delay(120); // Allow BLE radio to emit LL_TERMINATE_IND frame cleanly
  }
}

void initBle() {
  String fullName = getBottleFullName();
  Serial.printf("[BLE] Starting BLE: %s\n", fullName.c_str());

  NimBLEDevice::init(fullName.c_str());
  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new ServerCallbacks());

  NimBLEService *pService = bleServer->createService(GAME_SERVICE_UUID);
  shakeChar = pService->createCharacteristic(
      GAME_CHAR_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  char initVal[32];
  snprintf(initVal, sizeof(initVal), "SHAKE,0,0,%d",
           getBatteryLevelPercentage());
  shakeChar->setValue(initVal);
  pService->start();
  bleServer->start();

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->setName(fullName.c_str());
  advertising->addServiceUUID(GAME_SERVICE_UUID);
  // Ultra-Fast BLE advertising: 30ms min, 60ms max for instant discovery &
  // quick rebutan!
  advertising->setMinInterval(48);
  advertising->setMaxInterval(96);
  advertising->start();
  Serial.printf("[BLE] Advertising as %s\n", fullName.c_str());
}

void sendShakeData(int intensity) {
  uint32_t currentId = shakeCounter++;
  diagLastIntensity = intensity;
  int battPct = getBatteryLevelPercentage();

  char payload[36];
  snprintf(payload, sizeof(payload), "SHAKE,%u,%d,%d", currentId, intensity,
           battPct);
  Serial.printf("[BLE-TX] >>> %s\n", payload);

  if (isBleConnected && shakeChar) {
    shakeChar->setValue((uint8_t *)payload, strlen(payload));
    shakeChar->notify();
  }
}

int calculateIntensity(float combinedForce) {
  float maxScale = max(shakeThreshold + 0.8f, 2.8f);
  float norm = (combinedForce - shakeThreshold) / (maxScale - shakeThreshold);
  norm = constrain(norm, 0.0f, 1.0f);
  int intensity = 15 + (int)(norm * 85.0f);
  return constrain(intensity, 15, 100);
}

// ==========================================
// BACKLIGHT PWM & SCREEN POWER CONTROL
// ==========================================
void applyBrightnessPWM(int pwm) {
  if (pwm <= 0) {
    ledcWrite(LCD_BL, 0);
  } else {
    brightnessPWM = constrain(pwm, 5, 255);
    ledcWrite(LCD_BL, brightnessPWM);
  }
}

// ============================================================
// TRANSISI HALAMAN INSTAN & PARTIAL REDRAW
// Transisi PWM backlight dihapus agar responsivitas UI instan (<10ms),
// tidak ada jeda/delay, dan konsisten di seluruh level kecerahan.
// ============================================================

void resetInactivityTimer() { lastActivityTime = millis(); }

void executeDeepSleep() {
  Serial.println("[PWR] >>> Entering TRUE DEEP SLEEP (~70 uA) <<<");

  // 1. Mute backlight LED immediately (0 mA power)
  applyBrightnessPWM(0);

  // 2. Put ST7789 display controller into deep sleep
  gfx->displayOff();

  // 3. Cleanly disconnect BLE clients and stop advertising
  disconnectBleClients();
  if (NimBLEDevice::getAdvertising()->isAdvertising()) {
    NimBLEDevice::getAdvertising()->stop();
  }

  // 4. Configure QMI8658 to Wake-on-Motion (WoM) on INT2 (GPIO 21)
  // Low-Power 128Hz, Threshold 180 (responsive to gentle bottle pickup/shake),
  // defaultPinValue = 1 (idle HIGH, falling edge on motion)
  qmi.configWakeOnMotion(180, SensorQMI8658::ACC_ODR_LOWPOWER_128Hz,
                         SensorQMI8658::IntPin2, 1);

  // 5. Setup Wakeup Pins (GPIO 21 = IMU INT2, GPIO 0 = BOOT button)
  pinMode(IMU_INT2_PIN, INPUT_PULLUP);
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
  gpio_pullup_en((gpio_num_t)IMU_INT2_PIN);
  gpio_pullup_en((gpio_num_t)BOOT_BTN_PIN);
  gpio_pulldown_dis((gpio_num_t)IMU_INT2_PIN);
  gpio_pulldown_dis((gpio_num_t)BOOT_BTN_PIN);

  uint64_t wakeMask = (1ULL << IMU_INT2_PIN) | (1ULL << BOOT_BTN_PIN);

#if SOC_PM_SUPPORT_EXT1_WAKEUP
  // ESP32-S3 / S2 / Classic: EXT1 RTC Wakeup
  esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);
#elif SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  // ESP32-C3 / C-series: GPIO Deep Sleep Wakeup
  esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);
#endif

  Serial.println("[PWR] Deep sleep configured. Goodnight!");
  Serial.flush();
  delay(30);

  // 6. Enter Deep Sleep!
  esp_deep_sleep_start();
}

void turnScreenOff() {
  if (!isScreenOn && !isCountingDownSleep && !isWakeupSplashActive)
    return;
  isScreenOn = false;
  isCountingDownSleep = false;
  isWakeupSplashActive = false;
  countdownFrameDrawn = false;
  lastCountdownSecondDrawn = -1;
  lastCountdownBarWidth = -1;

  // 1. Mute backlight LED immediately (0 mA power)
  applyBrightnessPWM(0);

  // 2. Put ST7789 display controller into deep sleep
  gfx->displayOff();

  Serial.println("[PWR] >>> Screen OFF: Display & Backlight 0 mA (BLE remains "
                 "active) <<<");

  resetInactivityTimer();
}

void turnScreenOn() {
  isScreenOn = true;
  isCountingDownSleep = false;
  isWakeupSplashActive = false;
  countdownFrameDrawn = false;
  lastCountdownSecondDrawn = -1;
  lastCountdownBarWidth = -1;
  // CATATAN: isBottleActive TIDAK di-set true di sini.
  // isBottleActive hanya di-set oleh goncangan yang valid (Case B/C di shake
  // engine). Ini mencegah inactivity timer tidak ter-reset setiap kali BOOT
  // ditekan.

  // 1. Wake ST7789 display controller from sleep
  gfx->displayOn();

  // 2. If Ultra Sleep mode was active, restart BLE radio now!
  if (sleepProfile == 1) {
    if (!NimBLEDevice::getAdvertising()->isAdvertising() && !isBleConnected) {
      NimBLEDevice::startAdvertising();
      Serial.println(
          "[PWR] >>> Wake from Ultra Sleep: BLE Radio Restarted! <<<");
    }
  }

  // 3. Redraw active UI screen cleanly
  if (currentState == STATE_HOME) {
    renderHomeScreen();
  } else if (currentState == STATE_SETTINGS) {
    renderSettingsScreen();
  } else if (currentState == STATE_SET_COUNTER) {
    renderNumpadScreen();
  }

  // 4. Restore Backlight PWM to saved level
  applyBrightnessPWM(brightnessPWM);

  resetInactivityTimer();
  Serial.println("[PWR] >>> Screen ON: Display & Backlight Restored <<<");
}

void executeSleepNow() {
  Serial.println("[PWR] >>> Executing User SLEEP NOW <<<");
  if (isScreenOn) {
    gfx->fillScreen(COLOR_BG);
    drawCenteredText("GOING TO SLEEP...", 120, COLOR_WHITE, 2);
    const char *profTxt =
        (sleepProfile == 1) ? "Deep Sleep (~70 uA)" : "Fast Standby (~14 mA)";
    drawCenteredText(profTxt, 150,
                     (sleepProfile == 1) ? COLOR_GREEN : COLOR_CYAN, 1);
    drawCenteredText("Wake: Shake or Button", 175, COLOR_YELLOW, 1);
    delay(400);
  }

  if (sleepProfile == 1) {
    executeDeepSleep();
  } else {
    disconnectBleClients();
    turnScreenOff();
  }
}

void executePowerOff() {
  Serial.println("[PWR] >>> Executing User POWER OFF (0 mA) <<<");
  gfx->fillScreen(COLOR_BG);
  drawCenteredText("POWERING OFF...", 110, COLOR_COKE_RED, 2);
  drawCenteredText("Total Shutdown (0 mA)", 140, 0xFBA0, 1);
  drawCenteredText("IMU & Radio are completely OFF", 162, COLOR_WHITE, 1);
  drawCenteredText("Press Power Button to turn ON", 185, COLOR_YELLOW, 1);
  delay(600);

  // 1. Cleanly disconnect BLE clients
  disconnectBleClients();

  // 2. Shut off LCD backlight & display controller
  applyBrightnessPWM(0);
  gfx->displayOff();
  delay(100);

  // 3. Hardware shutdown via AXP2101 PMIC
  pmu.shutdown();
}

void renderSleepCountdownScreen(int remSec, int progressPct) {
  int cardW = SCR_W - (MARGIN_X * 2);
  int cardY = 48;
  int cardH = 172;
  int trackX = MARGIN_X + 16;
  int trackW = cardW - 32;
  int trackY = 150;
  int trackH = 16;

  // 1. Redraw static frame ONCE when entering countdown (Zero screen wipe
  // flicker!)
  if (!countdownFrameDrawn) {
    countdownFrameDrawn = true;
    gfx->fillScreen(COLOR_BG);
    drawHeader("AUTO SLEEP");

    gfx->fillRoundRect(MARGIN_X, cardY, cardW, cardH, 10, COLOR_CARD_BG);
    gfx->drawRoundRect(MARGIN_X, cardY, cardW, cardH, 10, COLOR_CARD_BRD);

    drawCenteredText("GOING TO SLEEP IN", cardY + 68, COLOR_WHITE, 1);
    drawCenteredText("Tap anywhere to cancel", cardY + 144, COLOR_TEXT_DIM, 1);

    // Track frame background
    gfx->fillRoundRect(trackX, trackY, trackW, trackH, 6, 0x0841);
    gfx->drawRoundRect(trackX, trackY, trackW, trackH, 6, COLOR_CARD_BRD);

    TouchButton btnCancel = {
        MARGIN_X,       234,        (int16_t)cardW, 38, "[ TAP TO CANCEL ]",
        COLOR_COKE_RED, COLOR_WHITE};
    drawButton(btnCancel);
    lastCountdownSecondDrawn = -1;
    lastCountdownBarWidth = -1;
  }

  // 2. Only redraw number digit & seconds text when remSec changes (Zero
  // full-screen blink!)
  if (lastCountdownSecondDrawn != remSec) {
    lastCountdownSecondDrawn = remSec;

    // Clear and redraw big number
    gfx->fillRect(MARGIN_X + 10, cardY + 14, cardW - 20, 42, COLOR_CARD_BG);
    char numBuf[16];
    snprintf(numBuf, sizeof(numBuf), "%d", remSec);
    drawCenteredText(numBuf, cardY + 18, COLOR_YELLOW, 4);

    // Clear and redraw seconds text
    gfx->fillRect(MARGIN_X + 10, cardY + 84, cardW - 20, 16, COLOR_CARD_BG);
    char secBuf[32];
    snprintf(secBuf, sizeof(secBuf), "%d SECONDS...", remSec);
    drawCenteredText(secBuf, cardY + 86, COLOR_YELLOW, 1);
  }

  // 3. Smoothly update receding progress bar only when pixel width changes
  int fillW = constrain((progressPct * (trackW - 4)) / 100, 0, trackW - 4);
  if (fillW != lastCountdownBarWidth) {
    lastCountdownBarWidth = fillW;
    if (fillW > 0) {
      gfx->fillRoundRect(trackX + 2, trackY + 2, fillW, trackH - 4, 4,
                         COLOR_YELLOW);
    }
    if (fillW < trackW - 4) {
      gfx->fillRect(trackX + 2 + fillW, trackY + 2, (trackW - 4) - fillW,
                    trackH - 4, 0x0841);
    }
  }
}

void renderWakeupSplash() {
  gfx->fillScreen(COLOR_BG);
  drawHeader("COCA-COLA SHAKE");

  int cardW = SCR_W - (MARGIN_X * 2);
  int cardY = 52;
  int cardH = 168;

  gfx->fillRoundRect(MARGIN_X, cardY, cardW, cardH, 12, COLOR_CARD_BG);
  gfx->drawRoundRect(MARGIN_X, cardY, cardW, cardH, 12, COLOR_GREEN);

  // Large Status Title
  drawCenteredText("BOTTLE READY!", cardY + 24, COLOR_GREEN, 2);
  drawCenteredText("SYSTEM ACTIVE", cardY + 54, COLOR_WHITE, 1);

  // Connection Pill
  int pillY = cardY + 76;
  uint16_t pillBg = isBleConnected ? 0x03E0 : 0x2124;
  uint16_t pillText = isBleConnected ? COLOR_GREEN : COLOR_TEXT_DIM;
  const char *statusStr = isBleConnected ? "BLE CONNECTED" : "BLE ADVERTISING";
  gfx->fillRoundRect(MARGIN_X + 20, pillY, cardW - 40, 22, 11, pillBg);
  gfx->setTextSize(1);
  gfx->setTextColor(pillText);
  gfx->setCursor(MARGIN_X + (cardW - strlen(statusStr) * 6) / 2, pillY + 7);
  gfx->print(statusStr);

  drawCenteredText("Shake to Play!", cardY + 116, COLOR_YELLOW, 1);
  drawCenteredText("Screen dims to save battery", cardY + 138, COLOR_TEXT_DIM,
                   1);

  // Bottom Status
  TouchButton btnActive = {
      MARGIN_X,      234,        (int16_t)cardW, 38, "[ BOTTLE ACTIVE ]",
      COLOR_CARD_BG, COLOR_GREEN};
  drawButton(btnActive);
}

// ==========================================
// NVS SETTINGS PERSISTENCE
// ==========================================
void loadSettings() {
  prefs.begin("cokeshake", false);
  bottleNumber = prefs.getInt("bot_num", DEFAULT_BOTTLE_ID);
  shakeThreshold = prefs.getFloat("thresh", 0.35f);
  shakeThreshold = constrain(shakeThreshold, 0.10f, 2.00f);
  shakeCooldownMs = prefs.getInt("cooldown", 30);
  shakeCooldownMs = constrain(shakeCooldownMs, 5, 100);
  brightnessPWM = prefs.getInt("bright_pwm", 160);
  int savedSec = prefs.getInt("sleep_sec", -1);
  if (savedSec != -1) {
    sleepTimerSeconds = savedSec;
  } else {
    int oldMin = prefs.getInt("sleep_min", 1);
    sleepTimerSeconds = (oldMin == 0) ? 0 : (oldMin * 60);
  }
  sleepProfile = prefs.getInt("sleep_prof", 0);
  prefs.end();
}

void saveSettings() {
  prefs.begin("cokeshake", false);
  prefs.putInt("bot_num", bottleNumber);
  prefs.putFloat("thresh", shakeThreshold);
  prefs.putInt("cooldown", shakeCooldownMs);
  prefs.putInt("bright_pwm", brightnessPWM);
  prefs.putInt("sleep_sec", sleepTimerSeconds);
  prefs.putInt("sleep_prof", sleepProfile);
  prefs.end();
}

// ==========================================
// VECTOR GRAPHICS & HEADER HELPERS
// ==========================================
void drawVectorBattery(int x, int y, bool forceOutline) {
  int percent = getBatteryLevelPercentage();
  bool chg = pmu.isCharging();
  bool vbus = pmu.isVbusIn();
  bool isFull =
      (vbus && (!chg || percent >= 100 ||
                pmu.getChargerStatus() == XPOWERS_AXP2101_CHG_DONE_STATE));

  if (forceOutline) {
    // Battery Body Outline (W: 24, H: 12)
    gfx->drawRoundRect(x, y, 24, 12, 2, COLOR_CARD_BRD);
    // Terminal Nub
    gfx->fillRect(x + 24, y + 3, 2, 6, COLOR_CARD_BRD);
  }

  // Fill Bar
  int fillW = constrain((percent * 20) / 100, 1, 20);
  uint16_t barColor;
  char battStr[16];

  if (vbus) {
    if (isFull) {
      barColor = COLOR_GREEN;
      snprintf(battStr, sizeof(battStr), "FULL %d%%", percent);
    } else {
      barColor = COLOR_CYAN;
      snprintf(battStr, sizeof(battStr), "CHG %d%%", percent);
    }
  } else {
    barColor = (percent < 20) ? COLOR_COKE_RED : COLOR_GREEN;
    snprintf(battStr, sizeof(battStr), "%d%%", percent);
  }

  // Draw inner fill bar (clears unfilled space inside battery icon)
  gfx->fillRect(x + 2, y + 2, fillW, 8, barColor);
  if (fillW < 20) {
    gfx->fillRect(x + 2 + fillW, y + 2, 20 - fillW, 8, COLOR_BG);
  }

  // Clear text label background first to avoid ghost characters (e.g. switching
  // from "FULL 100%" to "85%")
  gfx->fillRect(x + 28, y, 70, 13, COLOR_BG);

  // Text label
  gfx->setTextSize(1);
  gfx->setTextColor(barColor);
  gfx->setCursor(x + 30, y + 3);
  gfx->print(battStr);
}

void updateBatteryIndicatorRealtime(bool force) {
  if (!isScreenOn || isCountingDownSleep || isWakeupSplashActive) {
    return;
  }

  static int lastPercent = -1;
  static bool lastChg = false;
  static bool lastVbus = false;
  static unsigned long lastBatteryCheck = 0;

  unsigned long now = millis();
  if (!force && (now - lastBatteryCheck < 500)) {
    return; // Cek status baterai setiap 500ms
  }
  lastBatteryCheck = now;

  int percent = getBatteryLevelPercentage();
  bool chg = pmu.isCharging();
  bool vbus = pmu.isVbusIn();

  if (force || percent != lastPercent || chg != lastChg || vbus != lastVbus) {
    lastPercent = percent;
    lastChg = chg;
    lastVbus = vbus;
    // Partial update: hanya redraw bar dan teks tanpa menghapus seluruh header
    drawVectorBattery(HEADER_SAFE_X, 7, force);
  }
}

void drawVectorBle(int x, int y) {
  // Clear BLE area first for clean partial redraw
  gfx->fillRect(x - 46, y, 58, 14, COLOR_BG);

  uint16_t col = isBleConnected ? COLOR_CYAN : COLOR_CARD_BRD;
  // Center stem
  gfx->drawFastVLine(x + 4, y, 13, col);
  // Arrows
  gfx->drawLine(x + 4, y, x + 8, y + 4, col);
  gfx->drawLine(x + 8, y + 4, x, y + 9, col);
  gfx->drawLine(x + 4, y + 12, x + 8, y + 8, col);
  gfx->drawLine(x + 8, y + 8, x, y + 3, col);

  // Status text with comfortable gap before logo
  gfx->setTextSize(1);
  gfx->setTextColor(isBleConnected ? COLOR_CYAN : COLOR_TEXT_DIM);
  gfx->setCursor(x - 38, y + 3);
  gfx->print(isBleConnected ? "READY" : "ADVRT");
}

void drawHeader(const char *title) {
  // Clear header bar
  gfx->fillRect(0, 0, SCR_W, 44, COLOR_BG);

  // Vector Battery on Left (Safe X >= 22)
  drawVectorBattery(HEADER_SAFE_X, 7, true);

  // Vector BLE on Right
  drawVectorBle(SCR_W - HEADER_SAFE_X - 10, 7);

  // Subtle separator line
  gfx->drawFastHLine(HEADER_SAFE_X, 21, SCR_W - (HEADER_SAFE_X * 2),
                     COLOR_CARD_BRD);

  // Screen Title (Center, lowered away from top separator line for clean
  // breathing room)
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(1);
  int textW = strlen(title) * 6;
  gfx->setCursor((SCR_W - textW) / 2, 30);
  gfx->print(title);
}

void drawButton(const TouchButton &btn, bool active) {
  uint16_t bg = active ? COLOR_COKE_RED : btn.bgColor;
  uint16_t brd = active ? COLOR_WHITE : COLOR_CARD_BRD;
  gfx->fillRoundRect(btn.x, btn.y, btn.w, btn.h, 8, bg);
  gfx->drawRoundRect(btn.x, btn.y, btn.w, btn.h, 8, brd);

  gfx->setTextColor(active ? COLOR_WHITE : btn.textColor);
  gfx->setTextSize(1);
  int len = strlen(btn.label);
  int textW = len * 6;
  int tx = btn.x + (btn.w - textW) / 2;
  int ty = btn.y + (btn.h - 8) / 2;
  gfx->setCursor(tx, ty);
  gfx->print(btn.label);
}

// ==========================================
// HOME SCREEN
// ==========================================
void renderHomeScreen() {
  gfx->fillScreen(COLOR_BG);
  drawHeader("COCA-COLA SHAKE");

  int cardW = SCR_W - (MARGIN_X * 2);

  // Main Identity Card (Matches Settings Menu Card: rounded 10, COLOR_CARD_BG,
  // COLOR_CARD_BRD)
  int cardY = 44;
  int cardH = 180;
  gfx->fillRoundRect(MARGIN_X, cardY, cardW, cardH, 10, COLOR_CARD_BG);
  gfx->drawRoundRect(MARGIN_X, cardY, cardW, cardH, 10, COLOR_CARD_BRD);

  // Bottle Badge
  int bY = cardY + 14;
  gfx->fillRoundRect(MARGIN_X + 16, bY, cardW - 32, 36, 8, COLOR_COKE_RED);
  gfx->drawRoundRect(MARGIN_X + 16, bY, cardW - 32, 36, 8, COLOR_WHITE);
  const char *bName = (bottleNumber == 2) ? "BOTTLE-02" : "BOTTLE-01";
  drawCenteredText(bName, bY + 11, COLOR_WHITE, 2);

  // BLE Connection Pill Status
  int pillY = cardY + 58;
  uint16_t pillBg = isBleConnected ? 0x03E0 : 0x2124;
  uint16_t pillText = isBleConnected ? COLOR_GREEN : COLOR_TEXT_DIM;
  const char *statusStr =
      isBleConnected ? "BLE CONNECTED" : "SEARCHING GAME...";
  gfx->fillRoundRect(MARGIN_X + 24, pillY, cardW - 48, 20, 10, pillBg);
  gfx->setTextSize(1);
  gfx->setTextColor(pillText);
  gfx->setCursor(MARGIN_X + (cardW - strlen(statusStr) * 6) / 2, pillY + 6);
  gfx->print(statusStr);

  // Information Lines inside the card
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(MARGIN_X + 22, cardY + 88);
  gfx->printf("SHAKE COUNTER : #%u", shakeCounter);

  gfx->setTextColor(COLOR_TEXT_DIM);
  gfx->setCursor(MARGIN_X + 22, cardY + 106);
  gfx->printf("Sensitivity   : %.2fg (%dms)", shakeThreshold, shakeCooldownMs);

  gfx->setCursor(MARGIN_X + 22, cardY + 124);
  gfx->printf("Brightness    : %d%%", (brightnessPWM * 100) / 255);

  // Ready prompt inside card (clean & spacious text, no standalone box)
  drawCenteredText("READY! SHAKE BOTTLE", cardY + 154, COLOR_GREEN, 1);

  // Settings Button at bottom (matching Settings nav bar)
  TouchButton btnSettings = {
      MARGIN_X,      234,        (int16_t)cardW, 38, "[ SETTINGS MENU ]",
      COLOR_CARD_BG, COLOR_WHITE};
  drawButton(btnSettings);
}

// ==========================================
// SETTINGS PAGER (5-PAGE NOKIA / SMARTWATCH CAROUSEL)
// ==========================================
// Bottom Navigation Bar Helper
void drawSettingsNavButtons() {
  TouchButton btnPrev = {MARGIN_X, 234,           66,         38,
                         "< PREV", COLOR_CARD_BG, COLOR_WHITE};
  TouchButton btnHome = {MARGIN_X + 72,  234,        66, 38, "HOME",
                         COLOR_COKE_RED, COLOR_WHITE};
  TouchButton btnNext = {MARGIN_X + 144, 234,           66,         38,
                         "NEXT >",       COLOR_CARD_BG, COLOR_WHITE};

  drawButton(btnPrev, false);
  drawButton(btnHome, false);
  drawButton(btnNext, false);
}

void renderSettingsScreen() {
  gfx->fillScreen(COLOR_BG);

  char titleBuf[32];
  snprintf(titleBuf, sizeof(titleBuf), "SETTINGS (%d/%d)",
           currentSettingsPage + 1, TOTAL_SETTINGS_PAGES);
  drawHeader(titleBuf);

  renderSettingsPageContent();
  drawSettingsNavButtons();
}

// Partial Redraw untuk Navigasi Halaman Settings (PREV / NEXT / Swipes):
// Hanya update teks judul halaman di header dan isi kartu tengah.
// Tombol Bawah (PREV, HOME, NEXT) dan Ikon Baterai/BLE TETAP DIAM tanpa kedip!
void updateSettingsPageOnly() {
  char titleBuf[32];
  snprintf(titleBuf, sizeof(titleBuf), "SETTINGS (%d/%d)",
           currentSettingsPage + 1, TOTAL_SETTINGS_PAGES);

  // Clear area teks judul di header saja (y: 23 s/d 42), di bawah garis pemisah
  // & di atas kartu
  gfx->fillRect(HEADER_SAFE_X, 23, SCR_W - (HEADER_SAFE_X * 2), 20, COLOR_BG);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(1);
  int textW = strlen(titleBuf) * 6;
  gfx->setCursor((SCR_W - textW) / 2, 30);
  gfx->print(titleBuf);

  // Render konten kartu halaman baru secara instan (< 8 ms)
  renderSettingsPageContent();

  // Kembalikan visual tombol navigasi ke state normal (agar warna merah pressed
  // tidak ngebekas!)
  drawSettingsNavButtons();

  resetInactivityTimer();
}

void renderSettingsPageContent() {
  int cardW = SCR_W - (MARGIN_X * 2);
  int contentY = 44;
  int contentH = 180;

  // Clear page area
  gfx->fillRect(MARGIN_X, contentY, cardW, contentH, COLOR_BG);

  // Page Card Background
  gfx->fillRoundRect(MARGIN_X, contentY, cardW, contentH, 10, COLOR_CARD_BG);
  gfx->drawRoundRect(MARGIN_X, contentY, cardW, contentH, 10, COLOR_CARD_BRD);

  switch (currentSettingsPage) {
  case 0: { // 1. DEVICE IDENTITY
    drawCenteredText("SELECT DEVICE", contentY + 12, COLOR_TEXT_DIM, 1);

    // Big Select Button
    int bY = contentY + 32;
    gfx->fillRoundRect(MARGIN_X + 16, bY, cardW - 32, 46, 8, COLOR_COKE_RED);
    gfx->drawRoundRect(MARGIN_X + 16, bY, cardW - 32, 46, 8, COLOR_WHITE);
    const char *curName = (bottleNumber == 2) ? "BOTTLE-02" : "BOTTLE-01";
    drawCenteredText(curName, bY + 15, COLOR_WHITE, 2);

    drawCenteredText("Tap Button Bottle 01 <-> 02", contentY + 86, COLOR_YELLOW,
                     1);

    // Developer Test Trigger Button (replaces "BLE will re-advertise
    // instantly")
    TouchButton btnSendTest = {(int16_t)(MARGIN_X + 16),
                               (int16_t)(contentY + 116),
                               (int16_t)(cardW - 32),
                               38,
                               "[ SEND TEST SHAKE ]",
                               0x1A25,
                               COLOR_YELLOW};
    drawButton(btnSendTest);
    break;
  }

  case 1: { // 2. SHAKE COUNTER ID
    drawCenteredText("SEQUENCE COUNTER ID", contentY + 16, COLOR_TEXT_DIM, 1);

    // ID Display Box
    int bY = contentY + 40;
    gfx->fillRoundRect(MARGIN_X + 16, bY, cardW - 32, 44, 6, 0x0841);
    gfx->drawRoundRect(MARGIN_X + 16, bY, cardW - 32, 44, 6, COLOR_CARD_BRD);
    char idStr[16];
    snprintf(idStr, sizeof(idStr), "#%u", shakeCounter);
    drawCenteredText(idStr, bY + 14, COLOR_CYAN, 2);

    drawCenteredText("Stored safely in RTC RAM", contentY + 98, COLOR_TEXT_DIM,
                     1);

    // Edit Button
    TouchButton btnEdit = {(int16_t)(MARGIN_X + 16),
                           (int16_t)(contentY + 124),
                           (int16_t)(cardW - 32),
                           38,
                           "[ EDIT NUMBER KEYPAD ]",
                           0x2945,
                           COLOR_WHITE};
    drawButton(btnEdit);
    break;
  }

  case 2: { // 3. BRIGHTNESS SLIDER
    drawCenteredText("DISPLAY BRIGHTNESS", contentY + 16, COLOR_TEXT_DIM, 1);
    updateBrightnessSliderUI();
    break;
  }

  case 3: { // 4. SHAKE CALIBRATION (DUAL SLIDERS: FORCE & SPEED)
    // Top title: SENSITIVITY SET (centered, no values)
    drawCenteredText("SENSITIVITY SET", contentY + 8, COLOR_WHITE, 1);
    // Plain text subtitle below title (no border)
    drawCenteredText("Shake bottle back & forth", contentY + 22, COLOR_TEXT_DIM,
                     1);

    // Mini Oscilloscope Frame (X: 24, Y: 80, W: 192, H: 34)
    int oscX = MARGIN_X + 8;
    int oscY = contentY + 36;
    int oscW = cardW - 16;
    int oscH = 34;
    gfx->fillRect(oscX, oscY, oscW, oscH, 0x0000);
    gfx->drawRect(oscX, oscY, oscW, oscH, COLOR_CARD_BRD);

    drawOscilloscopeWaveform();
    updateDualSlidersUI();
    updateDiagStatusBadge(diagTriggered, diagLastIntensity);
    break;
  }

  case 4: { // 5. AUTO SLEEP & POWER PROFILE
    drawCenteredText("POWER & SLEEP SET", contentY + 12, COLOR_TEXT_DIM, 1);

    // Card 1: Sleep Timer Selector
    int box1Y = contentY + 28;
    int box1H = 48;
    gfx->fillRoundRect(MARGIN_X + 10, box1Y, cardW - 20, box1H, 8, 0x0841);
    gfx->drawRoundRect(MARGIN_X + 10, box1Y, cardW - 20, box1H, 8, COLOR_CYAN);

    drawCenteredText("SLEEP TIMEOUT", box1Y + 7, COLOR_TEXT_DIM, 1);
    const char *optName = "ALWAYS ON";
    if (sleepTimerSeconds == 10)
      optName = "10 SECONDS";
    else if (sleepTimerSeconds == 60)
      optName = "1 MINUTE";
    else if (sleepTimerSeconds == 120)
      optName = "2 MINUTES";
    else if (sleepTimerSeconds == 180)
      optName = "3 MINUTES";
    else if (sleepTimerSeconds == 300)
      optName = "5 MINUTES";

    drawCenteredText(optName, box1Y + 24, COLOR_CYAN, 2);

    // Card 2: Sleep Profile Selector (Fast Standby vs Ultra Sleep)
    int box2Y = contentY + 84;
    int box2H = 58;
    uint16_t profBorder = (sleepProfile == 1) ? COLOR_GREEN : COLOR_CYAN;
    uint16_t profBg = (sleepProfile == 1) ? 0x0260 : 0x0841;

    gfx->fillRoundRect(MARGIN_X + 10, box2Y, cardW - 20, box2H, 8, profBg);
    gfx->drawRoundRect(MARGIN_X + 10, box2Y, cardW - 20, box2H, 8, profBorder);

    if (sleepProfile == 1) {
      drawCenteredText("MODE : ULTRA SLEEP", box2Y + 8, COLOR_GREEN, 1);
      drawCenteredText("DEEP SLEEP ~70uA / Weeks+", box2Y + 25, COLOR_WHITE, 1);
      drawCenteredText("Wake: Shake IMU / Button", box2Y + 41, COLOR_YELLOW, 1);
    } else {
      drawCenteredText("MODE : FAST STANDBY", box2Y + 8, COLOR_CYAN, 1);
      drawCenteredText("Radio ON / ~14mA / ~30h", box2Y + 25, COLOR_WHITE, 1);
      drawCenteredText("Wake: Instant 0ms Reconnect", box2Y + 41, COLOR_CYAN,
                       1);
    }

    drawCenteredText("Tap card to change", contentY + 150, COLOR_TEXT_DIM, 1);
    break;
  }

  case 5: { // 6. POWER & SLEEP MANAGEMENT
    drawCenteredText("POWER CONTROL", contentY + 10, COLOR_TEXT_DIM, 1);

    // Button 1: SLEEP NOW (Follows sleep profile from Page 5)
    int box1Y = contentY + 26;
    int box1H = 58;
    uint16_t sleepBorder = (sleepProfile == 1) ? COLOR_GREEN : COLOR_CYAN;
    uint16_t sleepBg = (sleepProfile == 1) ? 0x0260 : 0x0841;

    gfx->fillRoundRect(MARGIN_X + 10, box1Y, cardW - 20, box1H, 8, sleepBg);
    gfx->drawRoundRect(MARGIN_X + 10, box1Y, cardW - 20, box1H, 8, sleepBorder);

    drawCenteredText("[ SLEEP NOW ]", box1Y + 8, sleepBorder, 2);
    if (sleepProfile == 1) {
      drawCenteredText("DEEP SLEEP (~70 uA)", box1Y + 28, COLOR_WHITE, 1);
      drawCenteredText("Wake: Shake Bottle / Button", box1Y + 42, COLOR_YELLOW,
                       1);
    } else {
      drawCenteredText("FAST STANDBY (Radio ON)", box1Y + 28, COLOR_WHITE, 1);
      drawCenteredText("Wake: Shake Bottle / Button", box1Y + 42, COLOR_CYAN,
                       1);
    }

    // Button 2: POWER OFF (Hardware Shutdown 0 mA)
    int box2Y = contentY + 92;
    int box2H = 58;
    gfx->fillRoundRect(MARGIN_X + 10, box2Y, cardW - 20, box2H, 8, 0x4000);
    gfx->drawRoundRect(MARGIN_X + 10, box2Y, cardW - 20, box2H, 8,
                       COLOR_COKE_RED);

    drawCenteredText("[ POWER OFF ]", box2Y + 8, COLOR_WHITE, 2);
    drawCenteredText("TOTAL SHUTDOWN (0 mA)", box2Y + 28, 0xFBA0, 1);
    drawCenteredText("Wake: Power Button Only", box2Y + 42, COLOR_TEXT_DIM, 1);

    drawCenteredText("Tap button to execute", contentY + 158, COLOR_TEXT_DIM,
                     1);
    break;
  }

  case 6: { // 7. TOUCH DIAGNOSTIC CANVAS
    renderTouchCanvasPage();
    break;
  }
  }
}

// ==========================================
// BRIGHTNESS SLIDER UI (Sleek Clean Pill Bar, No White Artifacts)
// ==========================================
void updateBrightnessSliderUI() {
  if (currentState != STATE_SETTINGS || currentSettingsPage != 2)
    return;
  int cardW = SCR_W - (MARGIN_X * 2);
  int contentY = 44;
  int trackX = MARGIN_X + 16;
  int trackY = contentY + 84;
  int trackW = cardW - 32;
  int trackH = 26;

  // Percentage Text
  int pct = (brightnessPWM * 100) / 255;
  gfx->fillRect(MARGIN_X + 16, contentY + 36, cardW - 32, 42, COLOR_CARD_BG);
  char pctBuf[32];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
  drawCenteredText(pctBuf, contentY + 40, COLOR_WHITE, 2);

  const char *subLabel =
      (brightnessPWM <= 15)
          ? "(Ultra-Low / Night Saver)"
          : ((brightnessPWM >= 230) ? "(Max Daylight)" : "(Standard)");
  drawCenteredText(subLabel, contentY + 62, COLOR_YELLOW, 1);

  // Clean Pill Bar (No floating circle thumb to avoid remnants)
  gfx->fillRoundRect(trackX, trackY, trackW, trackH, 6, 0x0841);
  gfx->drawRoundRect(trackX, trackY, trackW, trackH, 6, COLOR_CARD_BRD);

  int fillW =
      constrain(map(brightnessPWM, 5, 255, 4, trackW - 4), 4, trackW - 4);
  gfx->fillRoundRect(trackX + 2, trackY + 2, fillW, trackH - 4, 4,
                     COLOR_COKE_RED);

  drawCenteredText("Slide to Setting", contentY + 128, COLOR_TEXT_DIM, 1);
}

// ==========================================
// DUAL SLIDERS & DIAGNOSTICS UI (FORCE & SPEED)
// ==========================================
void drawBarWithInvertedText(int trackX, int trackY, int trackW, int trackH,
                             int fillW, uint16_t fillColor, const char *label,
                             uint16_t filledTextColor,
                             uint16_t unfilledTextColor) {
  // Clear and draw track background
  gfx->fillRoundRect(trackX, trackY, trackW, trackH, 6, 0x0841);
  gfx->drawRoundRect(trackX, trackY, trackW, trackH, 6, COLOR_CARD_BRD);

  // Draw Fill Progress Bar
  if (fillW > 0) {
    gfx->fillRoundRect(trackX + 2, trackY + 2, fillW, trackH - 4, 4, fillColor);
  }

  // Draw Centered Text with Per-Character Inverted Contrast
  int textW = strlen(label) * 6;
  int textX = trackX + (trackW - textW) / 2;
  int textY = trackY + (trackH - 8) / 2;
  int fillEndX = trackX + 2 + fillW;

  gfx->setTextSize(1);
  for (int i = 0; label[i] != '\0'; i++) {
    int cx = textX + i * 6;
    if (cx + 3 < fillEndX) {
      gfx->setTextColor(filledTextColor);
    } else {
      gfx->setTextColor(unfilledTextColor);
    }
    gfx->setCursor(cx, textY);
    gfx->print(label[i]);
  }
}

void updateDualSlidersUI() {
  if (currentState != STATE_SETTINGS || currentSettingsPage != 3)
    return;
  int cardW = SCR_W - (MARGIN_X * 2);
  int contentY = 44;
  int trackX = MARGIN_X + 16;
  int trackW = cardW - 32;
  int trackH = 22;

  // 1. Slider 1: FORCE (0.10g - 2.00g)
  int forceTrackY = contentY + 74;
  int fillWForce = constrain(
      map((int)(shakeThreshold * 100), 10, 200, 4, trackW - 4), 4, trackW - 4);
  char forceBuf[32];
  snprintf(forceBuf, sizeof(forceBuf), "FORCE : %.2fg", shakeThreshold);
  drawBarWithInvertedText(trackX, forceTrackY, trackW, trackH, fillWForce,
                          COLOR_COKE_RED, forceBuf, COLOR_WHITE,
                          COLOR_TEXT_DIM);

  // 2. Slider 2: SPEED (Ultra Fast 5ms - Normal 100ms)
  int speedTrackY = contentY + 100;
  int fillWSpeed =
      constrain(map(shakeCooldownMs, 5, 100, 4, trackW - 4), 4, trackW - 4);
  char speedBuf[32];
  snprintf(speedBuf, sizeof(speedBuf), "SPEED : %dms", shakeCooldownMs);
  drawBarWithInvertedText(trackX, speedTrackY, trackW, trackH, fillWSpeed,
                          COLOR_CYAN, speedBuf, 0x0000, COLOR_CYAN);
}

void drawOscilloscopeWaveform() {
  if (currentState != STATE_SETTINGS || currentSettingsPage != 3)
    return;
  int cardW = SCR_W - (MARGIN_X * 2);
  int contentY = 44;
  int oscX = MARGIN_X + 8;
  int oscY = contentY + 36;
  int oscW = cardW - 16;
  int oscH = 34;

  // Clear inner canvas
  gfx->fillRect(oscX + 1, oscY + 1, oscW - 2, oscH - 2, 0x0000);

  // Redraw threshold line (dotted red, scaled to 2.4g max with comfortable
  // headroom)
  int threshY =
      oscY + oscH - 1 -
      constrain((int)((shakeThreshold / 2.4f) * (oscH - 2)), 1, oscH - 2);
  for (int tx = oscX + 2; tx < oscX + oscW - 2; tx += 6) {
    gfx->drawFastHLine(tx, threshY, 3, COLOR_COKE_RED);
  }

  // Draw Waveform samples (Line spans 100% full width to the right border)
  for (int i = 0; i < WAVE_BUFFER_SIZE - 1; i++) {
    int idx1 = (waveBufIndex + i) % WAVE_BUFFER_SIZE;
    int idx2 = (waveBufIndex + i + 1) % WAVE_BUFFER_SIZE;

    int x1 = (oscX + 1) + (i * (oscW - 3)) / (WAVE_BUFFER_SIZE - 1);
    int x2 = (oscX + 1) + ((i + 1) * (oscW - 3)) / (WAVE_BUFFER_SIZE - 1);

    // Dynamic curve amplification (2.4g max scale gives clear view of
    // high-speed human shaking)
    int y1 =
        oscY + oscH - 1 -
        constrain((int)((waveBuffer[idx1] / 2.4f) * (oscH - 2)), 1, oscH - 2);
    int y2 =
        oscY + oscH - 1 -
        constrain((int)((waveBuffer[idx2] / 2.4f) * (oscH - 2)), 1, oscH - 2);

    gfx->drawLine(x1, y1, x2, y2, COLOR_GREEN);
  }
}

void updateDiagStatusBadge(bool triggered, int intensity) {
  if (currentState != STATE_SETTINGS || currentSettingsPage != 3)
    return;
  int cardW = SCR_W - (MARGIN_X * 2);
  int contentY = 44;
  int statusY = contentY + 128;

  gfx->fillRect(MARGIN_X + 16, statusY, cardW - 32, 12, COLOR_CARD_BG);
  if (triggered) {
    char badgeBuf[32];
    snprintf(badgeBuf, sizeof(badgeBuf), "VALID SHAKE! INT: %d%%", intensity);
    drawCenteredText(badgeBuf, statusY, COLOR_GREEN, 1);
  } else {
    drawCenteredText("Slide bars to calibrate", statusY, COLOR_TEXT_DIM, 1);
  }
}

// ==========================================
// TOUCH DIAGNOSTIC CANVAS (PAGE 7/7)
// ==========================================
void renderTouchCanvasPage() {
  int contentY = 44;

  // Top toolbar: [ CLEAR ] button & Live Coordinate/Count Badge
  TouchButton btnClear = {10, (int16_t)(contentY + 2), 56, 20, "CLEAR", COLOR_COKE_RED, COLOR_WHITE};
  drawButton(btnClear);

  gfx->fillRoundRect(70, contentY + 2, SCR_W - 70 - 10, 20, 4, COLOR_CARD_BG);
  gfx->drawRoundRect(70, contentY + 2, SCR_W - 70 - 10, 20, 4, COLOR_CARD_BRD);
  gfx->setTextColor(COLOR_YELLOW);
  gfx->setTextSize(1);
  gfx->setCursor(76, contentY + 8);
  if (touchPaintCount == 0) {
    gfx->print("TOUCH & DRAG TEST");
  } else {
    gfx->printf("X:%-3d Y:%-3d #%-4u", (lastTouchPaintX >= 0) ? lastTouchPaintX : 0,
               (lastTouchPaintY >= 0) ? lastTouchPaintY : 0, touchPaintCount);
  }

  // Wide Canvas Area: X = 6..234 (W = 228), Y = 68..220 (H = 152)
  int cvX = 6;
  int cvY = 68;
  int cvW = SCR_W - 12;
  int cvH = 152;

  gfx->fillRect(cvX, cvY, cvW, cvH, 0x0000);
  gfx->drawRect(cvX, cvY, cvW, cvH, COLOR_CARD_BRD);

  // Subtle grid lines (28px vertical, 25px horizontal)
  for (int gx = cvX + 28; gx < cvX + cvW; gx += 28) {
    gfx->drawFastVLine(gx, cvY + 1, cvH - 2, 0x18C3);
  }
  for (int gy = cvY + 25; gy < cvY + cvH; gy += 25) {
    gfx->drawFastHLine(cvX + 1, gy, cvW - 2, 0x18C3);
  }

  // Instructions if empty
  if (touchPaintCount == 0) {
    drawCenteredText("SCRATCH / DRAW HERE", cvY + 56, 0x39E7, 1);
    drawCenteredText("Visual Dead Zone Check", cvY + 74, 0x2124, 1);
  }
}

void handleTouchPaintPress(int32_t tx, int32_t ty) {
  int contentY = 44;

  // Clear button check
  if (tx >= 10 && tx <= 66 && ty >= (contentY + 2) && ty <= (contentY + 22)) {
    touchPaintCount = 0;
    lastTouchPaintX = -1;
    lastTouchPaintY = -1;
    renderTouchCanvasPage();
    return;
  }

  // Update coordinate badge for ANY touch on page 7 (helps detect edge/bezel response!)
  gfx->fillRect(72, contentY + 4, SCR_W - 72 - 12, 16, COLOR_CARD_BG);
  gfx->setTextColor(COLOR_GREEN);
  gfx->setTextSize(1);
  gfx->setCursor(76, contentY + 8);
  gfx->printf("X:%-3d Y:%-3d #%-4u", tx, ty, touchPaintCount + 1);

  // Drawing inside canvas
  if (ty >= 68 && ty < 220) {
    if (touchPaintCount == 0) {
      // Clear hint text on first touch
      int cvX = 6;
      int cvY = 68;
      int cvW = SCR_W - 12;
      int cvH = 152;
      gfx->fillRect(cvX + 1, cvY + 1, cvW - 2, cvH - 2, 0x0000);
      for (int gx = cvX + 28; gx < cvX + cvW; gx += 28) {
        gfx->drawFastVLine(gx, cvY + 1, cvH - 2, 0x18C3);
      }
      for (int gy = cvY + 25; gy < cvY + cvH; gy += 25) {
        gfx->drawFastHLine(cvX + 1, gy, cvW - 2, 0x18C3);
      }
    }

    touchPaintCount++;
    lastTouchPaintX = tx;
    lastTouchPaintY = ty;
    gfx->fillCircle(tx, ty, 3, COLOR_GREEN);
    gfx->drawPixel(tx, ty, COLOR_WHITE);
  }
}

void handleTouchPaintDrag(int32_t tx, int32_t ty) {
  int contentY = 44;

  // Update live coordinate badge on every drag move
  static unsigned long lastBadgeUpdate = 0;
  if (millis() - lastBadgeUpdate > 50) {
    lastBadgeUpdate = millis();
    gfx->fillRect(72, contentY + 4, SCR_W - 72 - 12, 16, COLOR_CARD_BG);
    gfx->setTextColor(COLOR_GREEN);
    gfx->setTextSize(1);
    gfx->setCursor(76, contentY + 8);
    gfx->printf("X:%-3d Y:%-3d #%-4u", tx, ty, touchPaintCount);
  }

  // Draw in canvas
  if (ty >= 68 && ty < 220) {
    touchPaintCount++;
    if (lastTouchPaintX >= 0 && lastTouchPaintY >= 0) {
      gfx->drawLine(lastTouchPaintX, lastTouchPaintY, tx, ty, COLOR_GREEN);
      gfx->fillCircle(tx, ty, 3, COLOR_GREEN);
      gfx->drawPixel(tx, ty, COLOR_WHITE);
    } else {
      gfx->fillCircle(tx, ty, 3, COLOR_GREEN);
      gfx->drawPixel(tx, ty, COLOR_WHITE);
    }
    lastTouchPaintX = tx;
    lastTouchPaintY = ty;
  }
}

void handleTouchPaintRelease() {
  lastTouchPaintX = -1;
  lastTouchPaintY = -1;

  int contentY = 44;
  gfx->fillRect(72, contentY + 4, SCR_W - 72 - 12, 16, COLOR_CARD_BG);
  gfx->setTextColor(COLOR_CYAN);
  gfx->setTextSize(1);
  gfx->setCursor(76, contentY + 8);
  gfx->printf("UP X:%-3d Y:%-3d #%-4u", (currentTouchX >= 0) ? currentTouchX : 0,
             (currentTouchY >= 0) ? currentTouchY : 0, touchPaintCount);
}

// ==========================================
// NUMPAD SCREEN (MANUAL COUNTER EDIT)
// ==========================================
void updateNumpadDisplay() {
  int boxW = SCR_W - (MARGIN_X * 2);
  gfx->fillRoundRect(MARGIN_X, 42, boxW, 34, 6, COLOR_CARD_BG);
  gfx->drawRoundRect(MARGIN_X, 42, boxW, 34, 6, COLOR_CARD_BRD);
  gfx->setTextColor(COLOR_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(MARGIN_X + 16, 51);
  gfx->printf("ID: %-8s", tempInputNumber.c_str());
}

void drawNumpadButton(int idx, bool active) {
  int padStartX = 10;
  int padStartY = 82;
  int btnW = 68;
  int btnH = 34;
  int stepX = 74;
  int stepY = 39;

  const char *keys[12] = {"1", "2", "3", "4",   "5", "6",
                          "7", "8", "9", "CLR", "0", "OK"};

  int r = idx / 3;
  int c = idx % 3;
  uint16_t bg = COLOR_CARD_BG;
  uint16_t tc = COLOR_WHITE;
  if (idx == 9) {
    bg = 0x5000;
  } // Dark red for CLR
  if (idx == 11) {
    bg = COLOR_COKE_RED;
  } // Coke red for OK

  TouchButton b = {(int16_t)(padStartX + c * stepX),
                   (int16_t)(padStartY + r * stepY),
                   (int16_t)btnW,
                   (int16_t)btnH,
                   keys[idx],
                   bg,
                   tc};
  drawButton(b, active);
}

void drawNumpadCancelButton(bool active) {
  TouchButton bCancel = {10,           242,           220,           34,
                         "[ CANCEL ]", COLOR_CARD_BG, COLOR_TEXT_DIM};
  drawButton(bCancel, active);
}

void renderNumpadScreen() {
  gfx->fillScreen(COLOR_BG);
  drawHeader("SET COUNTER ID");

  updateNumpadDisplay();

  for (int idx = 0; idx < 12; idx++) {
    drawNumpadButton(idx, false);
  }

  drawNumpadCancelButton(false);
}

// ==========================================
// PHYSICAL BUTTONS (BOOT GPIO 0 & AXP2101 PEK)
// ==========================================
void checkPhysicalButtons() {
  static int lastPinState = HIGH;
  static int debouncedState = HIGH;
  static unsigned long lastDebounceTime = 0;
  static unsigned long lastActionTime = 0;
  static unsigned long lastPmuCheckTime = 0;

  unsigned long now = millis();

  // ============================================================
  // 1. BOOT Button (GPIO 0) — Fungsi: Toggle Layar ON / OFF
  //    Saat layar OFF: hidupkan layar (kembali ke UI).
  //    Saat layar ON : matikan layar (screen off, hemat baterai).
  //    Ini adalah SATU-SATUNYA tombol untuk kontrol layar.
  // ============================================================
  int reading = digitalRead(BOOT_BTN_PIN);
  if (reading != lastPinState) {
    lastDebounceTime = now;
    lastPinState = reading;
  }

  if ((now - lastDebounceTime) > 40) {
    if (reading != debouncedState) {
      debouncedState = reading;
      // Aksi saat tombol dilepas (click & release)
      if (debouncedState == HIGH) {
        if (now - lastActionTime > 300) {
          lastActionTime = now;
          if (isScreenOn && !isWakeupSplashActive) {
            Serial.println("[BTN] BOOT -> Layar OFF (manual oleh user)");
            turnScreenOff();
          } else {
            Serial.println("[BTN] BOOT -> Layar ON (kembali ke UI)");
            turnScreenOn();
          }
        }
      }
    }
  }

  // ============================================================
  // 2. AXP2101 PEKey (Tombol Power Samping)
  //    Short Press: TIDAK melakukan apa-apa.
  //      (Tombol BOOT sudah handle toggle layar. PWR tidak redundan.)
  //    Long Press (hold ~2-3s via firmware, 6s via hardware AXP2101):
  //      -> Total Hardware Shutdown (0 mA, semua rail mati)
  // ============================================================
  if (now - lastPmuCheckTime >= 50) {
    lastPmuCheckTime = now;
    pmu.getIrqStatus();

    // PEKey Short Press -> Diabaikan (tidak ada aksi)
    if (pmu.isPekeyShortPressIrq()) {
      pmu.clearIrqStatus();
      Serial.println("[PMU] PEK Short Press -> Diabaikan (gunakan BOOT untuk "
                     "toggle layar)");
    }

    // PEKey Long Press -> Total Hardware Shutdown (0 mA)
    if (pmu.isPekeyLongPressIrq()) {
      pmu.clearIrqStatus();
      Serial.println("[PMU] PEK Long Press -> HARDWARE SHUTDOWN (0 mA)");
      executePowerOff();
    }
  }
}

// ==========================================
// TOUCH HANDLING & DRAGGING
// ==========================================
bool readTouchPoint(int32_t *x, int32_t *y) {
  if (!CST816T->IIC_Interrupt_Flag) {
    if (digitalRead(TP_INT) == HIGH)
      return false;
  }
  CST816T->IIC_Interrupt_Flag = false;

  int32_t touchX = CST816T->IIC_Read_Device_Value(
      CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
  int32_t touchY = CST816T->IIC_Read_Device_Value(
      CST816T->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);

  // Jangan drop sentuhan di tepi kaca lengkung! Clamp ke batas layar.
  if (touchX < -25 || touchX > (SCR_W + 25) || touchY < -25 ||
      touchY > (SCR_H + 25))
    return false;

  *x = constrain(touchX, 0, (int32_t)(SCR_W - 1));
  *y = constrain(touchY, 0, (int32_t)(SCR_H - 1));
  return true;
}

void onTouchPressed(int32_t tx, int32_t ty) {
  int cardW = SCR_W - (MARGIN_X * 2);

  if (currentState == STATE_HOME) {
    TouchButton btnSettings = {MARGIN_X, 234, (int16_t)cardW, 38, "", 0, 0};
    if (ty >= 220)
      drawButton(btnSettings, true);
  } else if (currentState == STATE_SETTINGS) {
    // Navigation bar buttons highlight dengan Zona Lebar Tanpa Dead Zone
    TouchButton btnPrev = {MARGIN_X, 234,           66,         38,
                           "< PREV", COLOR_CARD_BG, COLOR_WHITE};
    TouchButton btnHome = {MARGIN_X + 72,  234,        66, 38, "HOME",
                           COLOR_COKE_RED, COLOR_WHITE};
    TouchButton btnNext = {MARGIN_X + 144, 234,           66,         38,
                           "NEXT >",       COLOR_CARD_BG, COLOR_WHITE};

    if (ty >= 220) {
      if (tx <= 80) {
        drawButton(btnPrev, true);
      } else if (tx >= 160) {
        drawButton(btnNext, true);
      } else {
        drawButton(btnHome, true);
      }
    }

    // Page 0 (Device ID & Send Test Shake)
    if (currentSettingsPage == 0) {
      int contentY = 44;
      TouchButton btnSendTest = {(int16_t)(MARGIN_X + 16),
                                 (int16_t)(contentY + 116),
                                 (int16_t)(cardW - 32),
                                 38,
                                 "",
                                 0,
                                 0};
      if (btnSendTest.contains(tx, ty))
        drawButton(btnSendTest, true);
    }
    // Page 1 (Counter ID Edit Button Highlight)
    else if (currentSettingsPage == 1) {
      int contentY = 44;
      if (ty >= (contentY + 110) && ty <= (contentY + 180)) {
        TouchButton btnEdit = {(int16_t)(MARGIN_X + 16),
                               (int16_t)(contentY + 124),
                               (int16_t)(cardW - 32),
                               38,
                               "[ EDIT NUMBER KEYPAD ]",
                               COLOR_CARD_BG,
                               COLOR_WHITE};
        drawButton(btnEdit, true);
      }
    }
    // Brightness Page Drag Check
    else if (currentSettingsPage == 2) {
      int trackY = 44 + 75;
      if (ty >= trackY && ty <= trackY + 50 && tx >= MARGIN_X &&
          tx <= MARGIN_X + cardW) {
        isBrightnessDragging = true;
      }
    }
    // Dual Sliders Drag Check (FORCE & SPEED)
    else if (currentSettingsPage == 3) {
      int contentY = 44;
      int forceTrackY = contentY + 74;  // 118
      int speedTrackY = contentY + 100; // 144
      if (ty >= (forceTrackY - 8) && ty <= (forceTrackY + 24) &&
          tx >= MARGIN_X && tx <= MARGIN_X + cardW) {
        isForceDragging = true;
        handleForceDrag(tx);
      } else if (ty >= (speedTrackY - 6) && ty <= (speedTrackY + 24) &&
                 tx >= MARGIN_X && tx <= MARGIN_X + cardW) {
        isSpeedDragging = true;
        handleSpeedDrag(tx);
      }
    }
    // Page 6 (Touch Diagnostic Canvas Press)
    else if (currentSettingsPage == 6) {
      handleTouchPaintPress(tx, ty);
    }
  } else if (currentState == STATE_SET_COUNTER) {
    if (ty >= 238) {
      drawNumpadCancelButton(true);
    } else if (ty >= 78) {
      int c = (tx < 81) ? 0 : ((tx < 155) ? 1 : 2);
      int r;
      if (ty < 118)
        r = 0;
      else if (ty < 157)
        r = 1;
      else if (ty < 196)
        r = 2;
      else
        r = 3;
      int idx = r * 3 + c;
      drawNumpadButton(idx, true);
    }
  }
}

void onTouchReleased(int32_t upX, int32_t upY) {
  int cardW = SCR_W - (MARGIN_X * 2);

  if (currentState == STATE_HOME) {
    TouchButton btnSettings = {
        MARGIN_X,      234,        (int16_t)cardW, 38, "[ SETTINGS MENU ]",
        COLOR_CARD_BG, COLOR_WHITE};
    if (touchDownY >= 220 && upY >= 210) {
      currentState = STATE_SETTINGS;
      renderSettingsScreen();
    } else {
      drawButton(btnSettings, false);
    }
  } else if (currentState == STATE_SETTINGS) {
    // Reset visual highlight tombol navigasi (agar warna merah saat ditekan
    // tidak ngebekas!)
    drawSettingsNavButtons();

    // Zona Navigasi Bawah Lebar (Full Width Bottom Strip: y >= 220)
    // PREV : x = 0..80 (Zona Kiri Penuh sampai tepi kaca)
    // HOME : x = 81..159 (Zona Tengah Penuh)
    // NEXT : x = 160..239 (Zona Kanan Penuh sampai tepi kaca)
    if (touchDownY >= 220 && upY >= 210) {
      if (touchDownX <= 80) {
        currentSettingsPage--;
        if (currentSettingsPage < 0)
          currentSettingsPage = TOTAL_SETTINGS_PAGES - 1;
        updateSettingsPageOnly();
        return;
      } else if (touchDownX >= 160) {
        currentSettingsPage++;
        if (currentSettingsPage >= TOTAL_SETTINGS_PAGES)
          currentSettingsPage = 0;
        updateSettingsPageOnly();
        return;
      } else {
        saveSettings();
        currentState = STATE_HOME;
        renderHomeScreen();
        return;
      }
    }

    // Gesture Usap Layar (Swipe Left / Swipe Right) untuk Navigasi Halaman
    // Bekerja di halaman 0, 1, 4, 5 (halaman 2 & 3 menggunakan dragging slider, halaman 6 untuk drawing)
    if (currentSettingsPage != 2 && currentSettingsPage != 3 && currentSettingsPage != 6) {
      int32_t deltaX = upX - touchDownX;
      int32_t deltaY = upY - touchDownY;
      if (abs(deltaX) > 40 && abs(deltaX) > abs(deltaY) * 2) {
        if (deltaX < 0) {
          // Usap ke Kiri (Swipe Left) -> Halaman Berikutnya (NEXT)
          currentSettingsPage++;
          if (currentSettingsPage >= TOTAL_SETTINGS_PAGES)
            currentSettingsPage = 0;
          updateSettingsPageOnly();
          return;
        } else {
          // Usap ke Kanan (Swipe Right) -> Halaman Sebelumnya (PREV)
          currentSettingsPage--;
          if (currentSettingsPage < 0)
            currentSettingsPage = TOTAL_SETTINGS_PAGES - 1;
          updateSettingsPageOnly();
          return;
        }
      }
    }

    // Page Specific Content Taps
    int contentY = 44;
    switch (currentSettingsPage) {
    case 0: { // Device ID Toggle & Send Test Shake
      TouchButton btnToggle = {(int16_t)(MARGIN_X + 16),
                               (int16_t)(contentY + 32),
                               (int16_t)(cardW - 32),
                               46,
                               "",
                               0,
                               0};
      TouchButton btnSendTest = {(int16_t)(MARGIN_X + 16),
                                 (int16_t)(contentY + 116),
                                 (int16_t)(cardW - 32),
                                 38,
                                 "[ SEND TEST SHAKE ]",
                                 0x1A25,
                                 COLOR_YELLOW};

      if (btnToggle.contains(touchDownX, touchDownY) &&
          btnToggle.contains(upX, upY)) {
        bottleNumber = (bottleNumber == 1) ? 2 : 1;
        saveSettings();
        NimBLEDevice::deinit(true);
        initBle();
        renderSettingsPageContent();
      } else if (btnSendTest.contains(touchDownX, touchDownY) &&
                 btnSendTest.contains(upX, upY)) {
        sendShakeData(80);
        char sentBuf[32];
        snprintf(sentBuf, sizeof(sentBuf), "[ SENT: SHAKE #%u ]",
                 shakeCounter - 1);
        TouchButton btnFeedback = {(int16_t)(MARGIN_X + 16),
                                   (int16_t)(contentY + 116),
                                   (int16_t)(cardW - 32),
                                   38,
                                   sentBuf,
                                   COLOR_GREEN,
                                   COLOR_BG};
        drawButton(btnFeedback);
        delay(180);
        drawButton(btnSendTest, false);
      } else {
        drawButton(btnSendTest, false);
      }
      break;
    }
    case 1: { // Counter Edit
      int contentY = 44;
      TouchButton btnEdit = {(int16_t)(MARGIN_X + 16),
                             (int16_t)(contentY + 124),
                             (int16_t)(cardW - 32),
                             38,
                             "[ EDIT NUMBER KEYPAD ]",
                             0x2945,
                             COLOR_WHITE};
      drawButton(btnEdit, false);
      // Hit area luas: y: 155..220 di seluruh lebar kartu
      if (touchDownY >= (contentY + 110) && touchDownY <= (contentY + 180) &&
          upY >= (contentY + 100) && upY <= (contentY + 190)) {
        tempInputNumber = String(shakeCounter);
        currentState = STATE_SET_COUNTER;
        renderNumpadScreen();
      }
      break;
    }
    case 2: { // Brightness Tap / End Drag
      isBrightnessDragging = false;
      saveSettings();
      break;
    }
    case 3: { // Dual Sliders End Drag
      isForceDragging = false;
      isSpeedDragging = false;
      saveSettings();
      break;
    }
    case 4: { // Page 5: Sleep Timer & Sleep Profile
      TouchButton btnTimer = {(int16_t)(MARGIN_X + 10),
                              (int16_t)(contentY + 28),
                              (int16_t)(cardW - 20),
                              48,
                              "",
                              0,
                              0};
      TouchButton btnProfile = {(int16_t)(MARGIN_X + 10),
                                (int16_t)(contentY + 84),
                                (int16_t)(cardW - 20),
                                58,
                                "",
                                0,
                                0};

      if (btnTimer.contains(touchDownX, touchDownY) &&
          btnTimer.contains(upX, upY)) {
        // Cycle: 10s -> 60s (1m) -> 120s (2m) -> 180s (3m) -> 300s (5m) -> 0s
        // (ALWAYS ON) -> 10s
        if (sleepTimerSeconds == 10)
          sleepTimerSeconds = 60;
        else if (sleepTimerSeconds == 60)
          sleepTimerSeconds = 120;
        else if (sleepTimerSeconds == 120)
          sleepTimerSeconds = 180;
        else if (sleepTimerSeconds == 180)
          sleepTimerSeconds = 300;
        else if (sleepTimerSeconds == 300)
          sleepTimerSeconds = 0;
        else
          sleepTimerSeconds = 10;
        saveSettings();
        renderSettingsPageContent();
      } else if (btnProfile.contains(touchDownX, touchDownY) &&
                 btnProfile.contains(upX, upY)) {
        // Toggle: 0 (Fast Standby) <-> 1 (Ultra Sleep)
        sleepProfile = (sleepProfile == 0) ? 1 : 0;
        saveSettings();
        renderSettingsPageContent();
      }
      break;
    }
    case 5: { // Page 6: Sleep Now & Power Off
      TouchButton btnSleepNow = {(int16_t)(MARGIN_X + 10),
                                 (int16_t)(contentY + 26),
                                 (int16_t)(cardW - 20),
                                 58,
                                 "",
                                 0,
                                 0};
      TouchButton btnPowerOff = {(int16_t)(MARGIN_X + 10),
                                 (int16_t)(contentY + 92),
                                 (int16_t)(cardW - 20),
                                 58,
                                 "",
                                 0,
                                 0};

      if (btnSleepNow.contains(touchDownX, touchDownY) &&
          btnSleepNow.contains(upX, upY)) {
        executeSleepNow();
      } else if (btnPowerOff.contains(touchDownX, touchDownY) &&
                 btnPowerOff.contains(upX, upY)) {
        executePowerOff();
      }
      break;
    }
    case 6: { // Page 7: Touch Diagnostic Canvas
      int contentY = 44;
      if (touchDownX >= 10 && touchDownX <= 66 && touchDownY >= (contentY + 2) && touchDownY <= (contentY + 24)) {
        touchPaintCount = 0;
        lastTouchPaintX = -1;
        lastTouchPaintY = -1;
        renderTouchCanvasPage();
      }
      break;
    }
    }
  } else if (currentState == STATE_SET_COUNTER) {
    int32_t deltaX = abs(upX - touchDownX);
    int32_t deltaY = abs(upY - touchDownY);

    if (touchDownY >= 238) {
      drawNumpadCancelButton(false);
      if (deltaY < 35 && deltaX < 60) {
        currentState = STATE_SETTINGS;
        renderSettingsScreen();
      }
      return;
    } else if (touchDownY >= 78) {
      int c = (touchDownX < 81) ? 0 : ((touchDownX < 155) ? 1 : 2);
      int r;
      if (touchDownY < 118)
        r = 0;
      else if (touchDownY < 157)
        r = 1;
      else if (touchDownY < 196)
        r = 2;
      else
        r = 3;
      int idx = r * 3 + c;

      drawNumpadButton(idx, false);

      if (deltaY < 35 && deltaX < 45) {
        if (idx == 9) { // CLR
          tempInputNumber = "";
          updateNumpadDisplay();
        } else if (idx == 11) { // OK
          if (tempInputNumber.length() > 0) {
            shakeCounter = (uint32_t)tempInputNumber.toInt();
          } else {
            shakeCounter = 1;
          }
          currentState = STATE_SETTINGS;
          renderSettingsScreen();
        } else { // Digit 0-9
          int digit = (idx == 10) ? 0 : (idx + 1);
          if (tempInputNumber.length() < 7) {
            tempInputNumber += String(digit);
            updateNumpadDisplay();
          }
        }
      }
      return;
    }
  }
}

// Continuous Drag Handler for Brightness Slider
void handleBrightnessDrag(int32_t curX) {
  int cardW = SCR_W - (MARGIN_X * 2);
  int trackX = MARGIN_X + 16;
  int trackW = cardW - 32;

  int rawVal = map(curX, trackX, trackX + trackW, 5, 255);
  int newPWM = constrain(rawVal, 5, 255);

  if (abs(newPWM - brightnessPWM) >= 3) {
    applyBrightnessPWM(newPWM);
    updateBrightnessSliderUI();
  }
}

// Continuous Drag Handler for FORCE Slider (0.10g to 2.00g)
void handleForceDrag(int32_t curX) {
  int cardW = SCR_W - (MARGIN_X * 2);
  int trackX = MARGIN_X + 16;
  int trackW = cardW - 32;

  int rawVal = map(curX, trackX, trackX + trackW, 10, 200);
  float newThresh = constrain(rawVal, 10, 200) / 100.0f;

  if (fabs(newThresh - shakeThreshold) >= 0.02f) {
    shakeThreshold = newThresh;
    updateDualSlidersUI();
    drawOscilloscopeWaveform();
  }
}

// Continuous Drag Handler for SPEED Slider (5ms to 100ms)
void handleSpeedDrag(int32_t curX) {
  int cardW = SCR_W - (MARGIN_X * 2);
  int trackX = MARGIN_X + 16;
  int trackW = cardW - 32;

  int rawVal = map(curX, trackX, trackX + trackW, 5, 100);
  int newSpeed = constrain(rawVal, 5, 100);

  if (abs(newSpeed - shakeCooldownMs) >= 1) {
    shakeCooldownMs = newSpeed;
    updateDualSlidersUI();
  }
}

// ==========================================
// ARDUINO SETUP & INITIALIZATION
// ==========================================
void setup() {
  // Set CPU frequency to 80 MHz for power savings & cool operation
  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  delay(50);
  Serial.println("\n[BOOT] Starting COKE_SHAKE Controller...");

  // Check Wakeup Cause from Deep Sleep
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  bool isWokenByMotion = false;

#if SOC_PM_SUPPORT_EXT1_WAKEUP
  if (wakeupReason == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t ext1Status = esp_sleep_get_ext1_wakeup_status();
    if (ext1Status & (1ULL << IMU_INT2_PIN)) {
      isWokenByMotion = true;
    }
  }
#elif SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
  if (wakeupReason == ESP_SLEEP_WAKEUP_GPIO) {
    if (digitalRead(BOOT_BTN_PIN) != LOW) {
      isWokenByMotion = true;
    }
  }
#endif

  // BOOT Button Pin
  pinMode(BOOT_BTN_PIN, INPUT_PULLUP);

  // Initialize I2C Bus for PMIC, IMU, Touch, Audio
  Wire.begin(IIC_SDA, IIC_SCL);
  Wire.setClock(400000);

  // Initialize Power Management (AXP2101)
  if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    Serial.println("[PMU] AXP2101 detected. Enabling essential power rails...");
    pmu.enableBattDetection();
    pmu.enableVbusVoltageMeasure();
    pmu.enableBattVoltageMeasure();
    pmu.enableSystemVoltageMeasure();

    pmu.setDC1Voltage(3300);
    pmu.enableDC1();

    // CRITICAL: ALDO1 supplies CST816T Touch Controller
    pmu.setALDO1Voltage(3300);
    pmu.enableALDO1();

    pmu.setALDO2Voltage(3300);
    pmu.enableALDO2();
    pmu.setALDO3Voltage(3300);
    pmu.enableALDO3();

    // ALDO4 supplies 3.3V to ST7789P LCD VCC
    pmu.setALDO4Voltage(3300);
    pmu.enableALDO4();

    pmu.setBLDO1Voltage(3300);
    pmu.enableBLDO1();
    pmu.setBLDO2Voltage(3300);
    pmu.enableBLDO2();

    // Enable Power Key interrupts (Short Press = Screen Toggle, Long Press =
    // Power Off 0 mA)
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ |
                  XPOWERS_AXP2101_PKEY_LONG_IRQ);
  }

  delay(50);

  // Ensure Audio PA is permanently muted (0 mA power consumption)
  pinMode(AUDIO_PA_PIN, OUTPUT);
  digitalWrite(AUDIO_PA_PIN, LOW);

  // Load Settings from NVS
  loadSettings();

  // Initialize Backlight PWM via LEDC
  ledcAttach(LCD_BL, 5000, 8);
  applyBrightnessPWM(brightnessPWM);

  // Hardware Reset & Display Setup
  pinMode(LCD_RST, OUTPUT);
  digitalWrite(LCD_RST, HIGH);
  gfx->begin();
  Serial.println("[DISPLAY] ST7789 ready");

  // Hardware Reset Touch (CST816T)
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(30);
  digitalWrite(TP_RST, HIGH);
  delay(50);

  if (CST816T->begin()) {
    CST816T->IIC_Write_Device_State(
        CST816T->Arduino_IIC_Touch::Device::TOUCH_DEVICE_INTERRUPT_MODE,
        CST816T
            ->Arduino_IIC_Touch::Device_Mode::TOUCH_DEVICE_INTERRUPT_PERIODIC);
    Serial.println("[TOUCH] CST816T active");
  }

  // Initialize IMU (QMI8658) at 250Hz (4ms per sample for instantaneous human
  // hand response) Reconfigures QMI8658 from Low-Power WoM mode into full
  // active gaming mode!
  qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  qmi.enableINT(SensorQMI8658::IntPin2, false);
  qmi.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                          SensorQMI8658::ACC_ODR_250Hz,
                          SensorQMI8658::LPF_MODE_0);
  qmi.enableAccelerometer();

  // Clear Waveform buffer
  for (int i = 0; i < WAVE_BUFFER_SIZE; i++)
    waveBuffer[i] = 0.0f;

  // Initialize Game BLE
  initBle();

  // Branch based on Wakeup Cause:
  if (isWokenByMotion) {
    // Woken up by IMU Shake from inside bottle!
    Serial.println("[WAKE] Woken from DEEP SLEEP by IMU SHAKE! Bottle armed, "
                   "showing splash 2s.");
    isBottleActive = true;
    lastValidShakeTime = millis();
    isWakeupSplashActive = true;
    wakeupSplashEndTime = millis() + 2000UL;
    isScreenOn =
        false; // Screen is in temporary splash mode, dims back after 2s
    renderWakeupSplash();
    applyBrightnessPWM(brightnessPWM);
    resetInactivityTimer();
  } else {
    // Normal Boot, Reset, or Woken up by BOOT Button (GPIO 0)
    Serial.println(
        "[WAKE] Normal Boot or Woken by BOOT Button. Showing Home Screen.");
    isScreenOn = true;
    isBottleActive = false;
    renderHomeScreen();
    applyBrightnessPWM(brightnessPWM);
    resetInactivityTimer();
  }

  Serial.println("[READY] System initialized successfully!");
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  unsigned long now = millis();

  // 1. Check Physical Buttons (Click & Release Toggle Screen ON/OFF)
  checkPhysicalButtons();

  // Refresh now after button check so now >= lastActivityTime always!
  now = millis();

  // ============================================================
  // AUTO-SLEEP & INACTIVITY MANAGEMENT
  // ============================================================
  // Handle Temporary Wakeup Splash Timeout (Screen dims back to save battery
  // during gameplay)
  if (isWakeupSplashActive && (now >= wakeupSplashEndTime)) {
    Serial.println(
        "[WAKE] Splash 2s selesai -> Layar mati (tetap armed untuk gameplay)");
    isScreenOn = false;
    isWakeupSplashActive = false;
    countdownFrameDrawn = false;
    lastCountdownSecondDrawn = -1;
    lastCountdownBarWidth = -1;
    applyBrightnessPWM(0);
    gfx->displayOff();
    isBottleActive = true;
    lastValidShakeTime = millis();
    lastActivityTime = millis();
    now = millis();
  }

  // PENTING: Jika pengguna memilih ALWAYS ON (sleepTimerSeconds == 0),
  // JANGAN PERNAH tidur otomatis! Patuhi pilihan pengguna 100%.
  if (sleepTimerSeconds > 0) {
    unsigned long timeoutMs = (unsigned long)sleepTimerSeconds * 1000UL;
    unsigned long idleRef = max(lastValidShakeTime, lastActivityTime);
    unsigned long idleDuration = (now >= idleRef) ? (now - idleRef) : 0;

    // KASUS 1: SEDANG TERHUBUNG KE GAME (isBleConnected == true)
    // Jika botol terhubung tapi ditinggal idle tanpa goncangan melebihi
    // sleepTimerSeconds: Lakukan pemutusan koneksi secara bersih (Graceful
    // Disconnect) lalu masuk mode sleep
    if (isBleConnected) {
      if (idleDuration >= timeoutMs) {
        Serial.println("[PWR] Idle melebihi timer saat terkoneksi -> Graceful "
                       "Disconnect & Sleep");
        executeSleepNow();
      }
    }
    // KASUS 2: TIDAK TERHUBUNG KE GAME (isBleConnected == false)
    // Mengikuti waktu sleepTimerSeconds yang dipilih di menu (10s, 60s, 120s,
    // 180s, 300s):
    else {
      // A. Jika Layar sedang MATI (Screen OFF):
      if (!isScreenOn) {
        if (idleDuration >= timeoutMs) {
          isBottleActive = false;
          if (sleepProfile == 1) {
            Serial.println(
                "[PWR] Disconnected & Idle Timeout -> Masuk TRUE DEEP SLEEP");
            executeDeepSleep();
          }
          // Jika Fast Standby (sleepProfile == 0), radio tetap aktif stand-by
          // mencari game (~14mA)
        }
      }
      // B. Jika Layar sedang NYALA (Screen ON):
      // Munculkan countdown peringatan 5 detik sebelum layar mati / tidur
      else if (!isWakeupSplashActive) {
        if (idleDuration >= (timeoutMs - 5000UL)) {
          if (!isCountingDownSleep) {
            isCountingDownSleep = true;
            countdownFrameDrawn = false;
            sleepCountdownStartTime = now;
            lastCountdownSecondDrawn = -1;
            lastCountdownBarWidth = -1;
          }

          unsigned long countdownElapsed = now - sleepCountdownStartTime;
          if (countdownElapsed >= 5000UL) {
            // 5 detik countdown selesai tanpa disentuh -> Masuk sleep sesuai
            // profile
            if (sleepProfile == 1) {
              executeDeepSleep();
            } else {
              turnScreenOff();
            }
          } else {
            int remSec = 5 - (int)(countdownElapsed / 1000UL);
            remSec = constrain(remSec, 1, 5);
            int pctRemaining =
                (int)(((5000UL - countdownElapsed) * 100UL) / 5000UL);
            renderSleepCountdownScreen(remSec, pctRemaining);
          }
        }
      }
    }
  }

  // BLE Auto-Advertising Watchdog:
  // Jika tidak terkoneksi, pastikan radio BLE tetap memancarkan iklan
  // (advertising) agar laptop bisa mendeteksi & menyambung otomatis seketika
  // saat botol mendekat. Pengecekan hanya setiap 1000ms (1 detik) sehingga 0%
  // beban CPU / hemat daya.
  static unsigned long lastAdvWatchdogTime = 0;
  if (!isBleConnected && (now - lastAdvWatchdogTime >= 1000UL)) {
    lastAdvWatchdogTime = now;
    if (!NimBLEDevice::getAdvertising()->isAdvertising()) {
      NimBLEDevice::startAdvertising();
      Serial.println("[BLE] Watchdog: Advertising dipastikan AKTIF");
    }
  }

  // 3. Read IMU Accelerometer & Run Omni-directional Peak-to-Peak Filter
  float ax = 0, ay = 0, az = 0;
  if (qmi.getDataReady()) {
    qmi.getAccelerometer(ax, ay, az);

    // Fast Low-Pass Filter: Smooths chassis micro-jitter while preserving fast
    // human peaks
    static float lpfAx = 0, lpfAy = 0, lpfAz = 1.0f;
    static bool lpfInit = false;
    if (!lpfInit) {
      lpfAx = ax;
      lpfAy = ay;
      lpfAz = az;
      lpfInit = true;
    } else {
      lpfAx = 0.50f * lpfAx + 0.50f * ax;
      lpfAy = 0.50f * lpfAy + 0.50f * ay;
      lpfAz = 0.50f * lpfAz + 0.50f * az;
    }

    // Dynamic Gravity Baseline Subtraction (Slow ~1.5s tracking at 250Hz: does
    // NOT absorb hand shakes!)
    static float gX = 0.0f, gY = 0.0f, gZ = 1.0f;
    static bool gInit = false;
    if (!gInit) {
      gX = lpfAx;
      gY = lpfAy;
      gZ = lpfAz;
      gInit = true;
    } else {
      gX = 0.998f * gX + 0.002f * lpfAx;
      gY = 0.998f * gY + 0.002f * lpfAy;
      gZ = 0.998f * gZ + 0.002f * lpfAz;
    }

    float dynX = lpfAx - gX;
    float dynY = lpfAy - gY;
    float dynZ = lpfAz - gZ;
    float dynMag = sqrt(dynX * dynX + dynY * dynY + dynZ * dynZ);

    // Sample into Waveform Oscilloscope buffer (every 35ms, only when screen is
    // ON and on Page 4)
    if (isScreenOn && !isCountingDownSleep && !isWakeupSplashActive &&
        (now - lastWaveSampleTime > 35)) {
      lastWaveSampleTime = now;
      waveBuffer[waveBufIndex] = dynMag;
      waveBufIndex = (waveBufIndex + 1) % WAVE_BUFFER_SIZE;
      if (currentState == STATE_SETTINGS && currentSettingsPage == 3) {
        drawOscilloscopeWaveform();
      }
    }

    // ========================================================
    // OMNI-DIRECTIONAL PEAK-TO-PEAK ZERO-CROSSING STATE MACHINE
    // ========================================================
    switch (p2pState) {
    case P2P_IDLE: {
      if (dynMag >= shakeThreshold) {
        // First peak detected in any 3D direction
        p2pFirstPeakX = dynX;
        p2pFirstPeakY = dynY;
        p2pFirstPeakZ = dynZ;
        p2pFirstPeakMag = dynMag;
        p2pFirstPeakTime = now;
        p2pState = P2P_WAIT_REVERSE;
      }
      break;
    }

    case P2P_WAIT_REVERSE: {
      unsigned long dt = now - p2pFirstPeakTime;
      unsigned long maxTurnaround =
          max(240UL, (unsigned long)(shakeCooldownMs * 4));
      if (dt > maxTurnaround) {
        // Timeout: no reverse stroke occurred (was just a bump/tap)
        p2pState = P2P_IDLE;
      } else if (dt >=
                 4) { // Ultra-fast human turnaround cadence (down to 4ms!)
        // 3D Dot Product between current direction and first stroke
        float dot = (dynX * p2pFirstPeakX) + (dynY * p2pFirstPeakY) +
                    (dynZ * p2pFirstPeakZ);
        float oppThresh = -0.15f * (p2pFirstPeakMag * dynMag);

        // Check if direction inverted (bipolar swing) with sufficient magnitude
        if (dot < oppThresh && dynMag >= (shakeThreshold * 0.60f)) {
          // CONFIRMED VALID SHAKE!
          p2pState = P2P_COOLDOWN;
          lastValidShakeTime = now;

          // Case A: Screen was in countdown warning -> Cancel sleep & restore
          // screen
          if (isCountingDownSleep) {
            isCountingDownSleep = false;
            countdownFrameDrawn = false;
            resetInactivityTimer();
            turnScreenOn();
          }
          // Case B: Screen is OFF and Bottle was in Standby -> Shake #1 is
          // Wakeup Shake!
          else if (!isScreenOn && !isBottleActive) {
            isBottleActive = true;
            isWakeupSplashActive = true;
            wakeupSplashEndTime = now + 2000UL;

            // If Ultra Sleep mode was active, restart BLE advertising now!
            if (sleepProfile == 1) {
              if (!NimBLEDevice::getAdvertising()->isAdvertising() &&
                  !isBleConnected) {
                NimBLEDevice::startAdvertising();
                Serial.println("[WAKE] BLE Advertising Restarted!");
              }
            }

            gfx->displayOn();
            renderWakeupSplash();
            applyBrightnessPWM(brightnessPWM);
            Serial.println(
                "[WAKE] Shake #1 (Screen was OFF) -> Woken Up! Splash 2s");
            return; // Consume Shake #1 as wakeup! Shake #2 onwards will
                    // transmit!
          }
          // Case C: Bottle is already active (Screen ON, or active game session
          // with screen off)
          else {
            isBottleActive = true;
            resetInactivityTimer();
          }

          float combinedForce = (p2pFirstPeakMag + dynMag) * 0.5f;
          int intensity = calculateIntensity(combinedForce);

          // Send packet to Game BLE immediately
          sendShakeData(intensity);

          // ONLY update screen if screen is ON!
          // When screen is OFF (inside bottle), DO NOT turn on screen (0 mA
          // battery saving)
          if (isScreenOn && !isCountingDownSleep && !isWakeupSplashActive) {
            if (currentState == STATE_HOME) {
              gfx->fillRect(MARGIN_X + 22, 44 + 88, 175, 12, COLOR_CARD_BG);
              gfx->setTextColor(COLOR_WHITE);
              gfx->setTextSize(1);
              gfx->setCursor(MARGIN_X + 22, 44 + 88);
              gfx->printf("SHAKE COUNTER : #%u", shakeCounter);
            } else if (currentState == STATE_SETTINGS &&
                       currentSettingsPage == 3) {
              diagTriggered = true;
              diagTriggerTime = now;
              diagLastIntensity = intensity;
              updateDiagStatusBadge(true, intensity);
            }
          }
        }
      }
      break;
    }

    case P2P_COOLDOWN: {
      if (now - lastValidShakeTime >= (unsigned long)shakeCooldownMs) {
        p2pState = P2P_IDLE;
      }
      break;
    }
    }

    // Reset Diag trigger highlight after brief pulse (only if screen is ON)
    if (isScreenOn && !isCountingDownSleep && !isWakeupSplashActive &&
        diagTriggered && (now - diagTriggerTime > 400)) {
      diagTriggered = false;
      if (currentState == STATE_SETTINGS && currentSettingsPage == 3) {
        updateDiagStatusBadge(false, 0);
      }
    }
  }

  // 4. Real-time Battery Indicator Update (500ms check, partial redraw,
  // zero-flicker)
  updateBatteryIndicatorRealtime(false);

  // Periodic BLE Status Check (Instant partial update when status changes)
  static bool lastBleState = false;
  if (isScreenOn && !isCountingDownSleep && !isWakeupSplashActive &&
      (isBleConnected != lastBleState)) {
    lastBleState = isBleConnected;

    // Partial update: Redraw BLE vector icon cleanly
    drawVectorBle(SCR_W - HEADER_SAFE_X - 10, 7);

    // If on HOME screen, cleanly update connection pill
    if (currentState == STATE_HOME) {
      int cardW = SCR_W - (MARGIN_X * 2);
      int cardY = 44;
      int pillY = cardY + 58;
      uint16_t pillBg = isBleConnected ? 0x03E0 : 0x2124;
      uint16_t pillText = isBleConnected ? COLOR_GREEN : COLOR_TEXT_DIM;
      const char *statusStr =
          isBleConnected ? "BLE CONNECTED" : "SEARCHING GAME...";
      gfx->fillRoundRect(MARGIN_X + 24, pillY, cardW - 48, 20, 10, pillBg);
      gfx->setTextSize(1);
      gfx->setTextColor(pillText);
      gfx->setCursor(MARGIN_X + (cardW - strlen(statusStr) * 6) / 2, pillY + 6);
      gfx->print(statusStr);
    }
  }

  // 5. Touch Input & Dragging Management
  int32_t tx = 0, ty = 0;
  if (readTouchPoint(&tx, &ty)) {
    lastTouchSeenTime = now;
    currentTouchX = tx;
    currentTouchY = ty;

    // When screen is OFF, ignore all touch inputs completely (zero ghost touch
    // inside bottle)
    if (!isScreenOn) {
      return;
    }

    // When wakeup splash is active, any tap cancels splash and goes to Home
    if (isWakeupSplashActive) {
      isWakeupSplashActive = false;
      currentState = STATE_HOME;
      renderHomeScreen();
      resetInactivityTimer();
      delay(150);
      return;
    }

    // When countdown warning is active, any tap cancels sleep and restores
    // previous screen!
    if (isCountingDownSleep) {
      isCountingDownSleep = false;
      countdownFrameDrawn = false;
      resetInactivityTimer();
      turnScreenOn();
      delay(150);
      return;
    }

    resetInactivityTimer();

    if (!isTouching) {
      isTouching = true;
      touchDownX = tx;
      touchDownY = ty;
      onTouchPressed(tx, ty);
    } else {
      // Continuous dragging while finger is held down
      if (currentState == STATE_SETTINGS) {
        if (currentSettingsPage == 2 && isBrightnessDragging) {
          handleBrightnessDrag(tx);
        } else if (currentSettingsPage == 3) {
          if (isForceDragging) {
            handleForceDrag(tx);
          } else if (isSpeedDragging) {
            handleSpeedDrag(tx);
          }
        } else if (currentSettingsPage == 6) {
          handleTouchPaintDrag(tx, ty);
        }
      }
    }
  } else {
    if (isTouching && (now - lastTouchSeenTime > 75)) {
      int32_t releaseX = touchDownX;
      int32_t releaseY = touchDownY;
      isTouching = false;
      isBrightnessDragging = false;
      isForceDragging = false;
      isSpeedDragging = false;
      if (currentSettingsPage == 6) {
        handleTouchPaintRelease();
      }
      onTouchReleased(releaseX, releaseY);
    }
  }

  // 6. FreeRTOS Micro-Sleep Power Saver:
  // When screen is OFF:
  // - Ultra Sleep (sleepProfile == 1, during active gameplay with screen off):
  // delay(2)
  // - Fast Standby (sleepProfile == 0): delay(3) yields CPU while keeping BLE
  // responsiveness (~14 mA)
  if (!isScreenOn) {
    if (sleepProfile == 1) {
      delay(2);
    } else {
      delay(3);
    }
  } else {
    delay(1);
  }
}
