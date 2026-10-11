#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <LittleFS.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "config.h"

Adafruit_ILI9341 tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);

enum AppMode : uint8_t {
  MODE_MENU = 0,
  MODE_QUICK_REF,
  MODE_FLASHCARDS,
  MODE_BREATHING,
  MODE_MONITOR,
  MODE_MESSAGES
};

enum BreathPhase : uint8_t {
  PHASE_INHALE = 0,
  PHASE_HOLD,
  PHASE_EXHALE,
  PHASE_PAUSE
};

struct Flashcard {
  const char* q;
  const char* a;
};

static const Flashcard FLASHCARDS[] = {
  {"Q: Normal adult HR?", "A: 60-100 bpm"},
  {"Q: CPR ratio (single rescuer)?", "A: 30 compressions : 2 breaths"},
  {"Q: Adult RR normal?", "A: 12-20 breaths/min"},
  {"Q: Hypoxia concern SpO2?", "A: Usually < 92%"},
  {"Q: Compression depth adult?", "A: ~5-6 cm (2-2.4 in)"}
};

static const char* QUICK_REF[] = {
  "ABCs: Airway-Breathing-Circulation",
  "Primary Survey: DRSABCD",
  "Shock signs: pale, cool, clammy skin",
  "Stroke FAST: Face-Arm-Speech-Time",
  "Sepsis warning: fever + tachy + hypotension"
};

FS* activeFS = nullptr;
String activeStorageName = "none";

AppMode mode = MODE_MENU;
AppMode prevMode = MODE_MENU;

bool redrawFull = true;
bool touchAvailable = false;
bool autoDemoEnabled = false;
uint32_t lastRenderMs = 0;
uint32_t lastInputMs = 0;
uint32_t lastAutoStepMs = 0;
uint8_t autoStep = 0;

// Flashcards state
int flashIndex = 0;
bool flashAnswerShown = false;
int flashCorrect = 0;
int flashWrong = 0;

// Breathing state
bool breathingRunning = true;
BreathPhase breathPhase = PHASE_INHALE;
uint32_t breathPhaseStartMs = 0;
const uint16_t breathDurSec[] = {4, 4, 6, 2};

// Monitor state
uint32_t lastMonitorMs = 0;
uint16_t heartRate = 78;
uint16_t spo2 = 98;
uint16_t rr = 16;
uint16_t sysBP = 118;
uint16_t diaBP = 76;
bool monitorAlarm = false;
int wave[SCREEN_W];
float wavePhase = 0.0f;

// Messages state
String messages[8];
uint8_t messageCount = 0;
uint8_t messageIndex = 0;
uint32_t lastMessageRotateMs = 0;

// Touch state
bool touchPressed = false;
bool touchPressedPrev = false;
int16_t touchX = -1;
int16_t touchY = -1;

// Serial command
String serialLine;

static const char* DATA_FILE = "/rtbuddy_data.txt";

void logMode(AppMode m) {
  const char* n = "MENU";
  if (m == MODE_QUICK_REF) n = "QUICK_REF";
  else if (m == MODE_FLASHCARDS) n = "FLASHCARDS";
  else if (m == MODE_BREATHING) n = "BREATHING";
  else if (m == MODE_MONITOR) n = "MONITOR";
  else if (m == MODE_MESSAGES) n = "MESSAGES";
  Serial.printf("[UI] Mode => %s\n", n);
}

void setMode(AppMode newMode) {
  if (mode != newMode) {
    prevMode = mode;
    mode = newMode;
    redrawFull = true;
    logMode(mode);
  }
}

uint16_t readTouch12(uint8_t cmd) {
  uint16_t data = 0;
  for (int8_t i = 7; i >= 0; --i) {
    digitalWrite(TOUCH_DIN_PIN, (cmd >> i) & 0x01);
    digitalWrite(TOUCH_CLK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(TOUCH_CLK_PIN, LOW);
    delayMicroseconds(1);
  }
  for (uint8_t i = 0; i < 12; i++) {
    digitalWrite(TOUCH_CLK_PIN, HIGH);
    delayMicroseconds(1);
    data = (data << 1) | (digitalRead(TOUCH_DO_PIN) ? 1 : 0);
    digitalWrite(TOUCH_CLK_PIN, LOW);
    delayMicroseconds(1);
  }
  return data;
}

bool sampleTouchRaw(uint16_t& rawX, uint16_t& rawY, uint16_t& pressure) {
  digitalWrite(TFT_CS_PIN, HIGH);
  digitalWrite(TOUCH_CS_PIN, LOW);
  delayMicroseconds(2);

  uint16_t x = readTouch12(0xD0);
  uint16_t y = readTouch12(0x90);
  uint16_t z1 = readTouch12(0xB0);
  uint16_t z2 = readTouch12(0xC0);

  digitalWrite(TOUCH_CS_PIN, HIGH);

  if (z1 == 0) {
    pressure = 0;
  } else {
    int32_t p = (int32_t)z1 + (4095 - (int32_t)z2);
    if (p < 0) p = 0;
    pressure = (uint16_t)p;
  }

  rawX = x;
  rawY = y;
  return pressure > 80;
}

bool touchReadScreen(int16_t& sx, int16_t& sy) {
  uint16_t rx, ry, p;
  bool pressed = sampleTouchRaw(rx, ry, p);
  if (!pressed) return false;

  // Basic calibration/map for 2.8" XPT2046 style with rotation(1)
  int16_t tx = map((int32_t)ry, 220, 3900, 0, SCREEN_W - 1);
  int16_t ty = map((int32_t)rx, 220, 3900, 0, SCREEN_H - 1);

  if (tx < 0) tx = 0;
  if (tx >= SCREEN_W) tx = SCREEN_W - 1;
  if (ty < 0) ty = 0;
  if (ty >= SCREEN_H) ty = SCREEN_H - 1;

  sx = tx;
  sy = ty;
  return true;
}

bool initStorage() {
  bool littlefsOk = LittleFS.begin(true);
  if (!littlefsOk) {
    Serial.println("[STORAGE] LittleFS mount failed.");
  } else {
    Serial.println("[STORAGE] LittleFS mounted.");
  }

  // SD architecture is supported, but SD CS is unwired in the provided pin-map.
  if (SD_CS_PIN >= 0) {
    if (SD.begin(SD_CS_PIN)) {
      activeFS = &SD;
      activeStorageName = "SD";
      Serial.println("[STORAGE] SD mounted and selected (preferred).");
      return true;
    } else {
      Serial.println("[STORAGE] SD mount failed; using LittleFS fallback.");
    }
  } else {
    Serial.println("[STORAGE] SD CS not wired in pin-map; using LittleFS fallback.");
  }

  if (littlefsOk) {
    activeFS = &LittleFS;
    activeStorageName = "LittleFS";
    return true;
  }

  activeFS = nullptr;
  activeStorageName = "none";
  return false;
}

void saveData() {
  if (!activeFS) return;

  File f = activeFS->open(DATA_FILE, FILE_WRITE);
  if (!f) {
    Serial.println("[STORAGE] Save failed: cannot open file.");
    return;
  }

  f.printf("SCORE,%d,%d\n", flashCorrect, flashWrong);
  f.printf("MSGCOUNT,%u\n", messageCount);
  for (uint8_t i = 0; i < messageCount; i++) {
    String line = messages[i];
    line.replace("\n", " ");
    f.printf("MSG,%s\n", line.c_str());
  }
  f.close();
  Serial.printf("[STORAGE] Data saved to %s on %s.\n", DATA_FILE, activeStorageName.c_str());
}

void loadData() {
  if (!activeFS || !activeFS->exists(DATA_FILE)) {
    Serial.println("[STORAGE] No saved data found; using defaults.");
    return;
  }

  File f = activeFS->open(DATA_FILE, FILE_READ);
  if (!f) return;

  messageCount = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    if (line.startsWith("SCORE,")) {
      int c1 = line.indexOf(',');
      int c2 = line.indexOf(',', c1 + 1);
      if (c2 > 0) {
        flashCorrect = line.substring(c1 + 1, c2).toInt();
        flashWrong = line.substring(c2 + 1).toInt();
      }
    } else if (line.startsWith("MSGCOUNT,")) {
      // count line is optional in parser
    } else if (line.startsWith("MSG,")) {
      if (messageCount < 8) {
        messages[messageCount++] = line.substring(4);
      }
    }
  }
  f.close();

  if (messageCount == 0) {
    messages[messageCount++] = "You are doing amazing.";
    messages[messageCount++] = "Stay calm, assess, act.";
    messages[messageCount++] = "Small steps save lives.";
  }

  Serial.printf("[STORAGE] Loaded from %s (%u messages, score %d/%d).\n",
                activeStorageName.c_str(), messageCount, flashCorrect, flashWrong);
}

void ensureDefaultMessages() {
  if (messageCount == 0) {
    messages[messageCount++] = "You are doing amazing.";
    messages[messageCount++] = "Stay calm, assess, act.";
    messages[messageCount++] = "Small steps save lives.";
  }
}

void drawPixelArtHeader(const char* title) {
  tft.fillRect(0, 0, SCREEN_W, 28, COL_BG_LIGHT);
  tft.fillRect(0, 28, SCREEN_W, 2, COL_ACCENT_2);

  for (int x = 6; x < 30; x += 4) {
    tft.drawFastVLine(x, 6, 14, COL_ACCENT_1);
  }
  for (int x = SCREEN_W - 28; x < SCREEN_W - 4; x += 4) {
    tft.drawFastVLine(x, 6, 14, COL_ACCENT_2);
  }

  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(36, 7);
  tft.print("RT Buddy");
  tft.setTextSize(1);
  tft.setCursor(220, 10);
  tft.print(title);
}

void drawHomeButton() {
  tft.fillRoundRect(4, 206, 52, 28, 6, COL_ACCENT_1);
  tft.drawRoundRect(4, 206, 52, 28, 6, COL_TEXT);
  tft.setTextColor(COL_BG_DARK);
  tft.setTextSize(1);
  tft.setCursor(16, 216);
  tft.print("HOME");
}

bool inRect(int16_t x, int16_t y, int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
  return (x >= rx && x < (rx + rw) && y >= ry && y < (ry + rh));
}

void renderMenu() {
  tft.fillScreen(COL_BG_DARK);
  drawPixelArtHeader("MENU");

  const char* items[5] = {
    "1 Quick Reference",
    "2 Flashcards",
    "3 Breathing Trainer",
    "4 Patient Monitor",
    "5 Messages"
  };

  for (uint8_t i = 0; i < 5; i++) {
    int y = 40 + i * 36;
    uint16_t c = (i % 2 == 0) ? COL_ACCENT_1 : COL_ACCENT_2;
    tft.fillRoundRect(18, y, 284, 28, 6, c);
    tft.drawRoundRect(18, y, 284, 28, 6, COL_TEXT);
    tft.setTextColor(COL_BG_DARK);
    tft.setTextSize(1);
    tft.setCursor(28, y + 10);
    tft.print(items[i]);
  }

  tft.setTextColor(COL_MUTED);
  tft.setCursor(10, 226);
  tft.print("Touch menu or Serial: 1..5, m, help");
}

void renderQuickRef() {
  tft.fillScreen(COL_BG_DARK);
  drawPixelArtHeader("QUICK REF");
  drawHomeButton();

  tft.setTextColor(COL_TEXT);
  tft.setTextSize(1);
  int y = 42;
  for (uint8_t i = 0; i < 5; i++) {
    tft.fillRoundRect(10, y - 4, 300, 30, 5, (i % 2 == 0) ? 0x1293 : 0x1A96);
    tft.drawRoundRect(10, y - 4, 300, 30, 5, COL_ACCENT_1);
    tft.setCursor(16, y + 6);
    tft.print(QUICK_REF[i]);
    y += 36;
  }
}

void renderFlashcards() {
  tft.fillScreen(COL_BG_DARK);
  drawPixelArtHeader("FLASHCARDS");
  drawHomeButton();

  const Flashcard& fc = FLASHCARDS[flashIndex];
  tft.fillRoundRect(16, 44, 288, 92, 8, 0x1274);
  tft.drawRoundRect(16, 44, 288, 92, 8, COL_ACCENT_2);

  tft.setTextColor(COL_TEXT);
  tft.setTextSize(1);
  tft.setCursor(26, 58);
  tft.print(fc.q);

  tft.fillRoundRect(16, 144, 288, 46, 8, 0x01EA);
  tft.drawRoundRect(16, 144, 288, 46, 8, COL_ACCENT_1);
  tft.setCursor(26, 162);
  if (flashAnswerShown) tft.print(fc.a);
  else tft.print("Tap center / Serial 'a' to reveal answer");

  tft.fillRoundRect(70, 206, 70, 28, 6, 0x07E0);
  tft.fillRoundRect(150, 206, 70, 28, 6, 0xFBE0);
  tft.fillRoundRect(230, 206, 70, 28, 6, 0xF81F);
  tft.drawRoundRect(70, 206, 70, 28, 6, COL_TEXT);
  tft.drawRoundRect(150, 206, 70, 28, 6, COL_TEXT);
  tft.drawRoundRect(230, 206, 70, 28, 6, COL_TEXT);

  tft.setTextColor(COL_BG_DARK);
  tft.setCursor(88, 216);  tft.print("NEXT");
  tft.setCursor(167, 216); tft.print("GOOD");
  tft.setCursor(249, 216); tft.print("MISS");

  tft.setTextColor(COL_MUTED);
  tft.setCursor(18, 196);
  tft.printf("Card %d/%d  Score G:%d M:%d",
             flashIndex + 1, (int)(sizeof(FLASHCARDS) / sizeof(FLASHCARDS[0])),
             flashCorrect, flashWrong);
}

const char* breathLabel(BreathPhase p) {
  if (p == PHASE_INHALE) return "INHALE";
  if (p == PHASE_HOLD) return "HOLD";
  if (p == PHASE_EXHALE) return "EXHALE";
  return "PAUSE";
}

void renderBreathing() {
  tft.fillScreen(COL_BG_DARK);
  drawPixelArtHeader("BREATHING");
  drawHomeButton();

  uint32_t elapsed = (millis() - breathPhaseStartMs) / 1000;
  uint32_t dur = breathDurSec[breathPhase];
  uint32_t remain = (elapsed >= dur) ? 0 : (dur - elapsed);

  int cx = 160;
  int cy = 118;
  float pct = dur ? (float)elapsed / (float)dur : 1.0f;
  if (pct > 1.0f) pct = 1.0f;

  float radiusF = 24.0f;
  if (breathPhase == PHASE_INHALE) radiusF = 24.0f + 42.0f * pct;
  else if (breathPhase == PHASE_HOLD) radiusF = 66.0f;
  else if (breathPhase == PHASE_EXHALE) radiusF = 66.0f - 42.0f * pct;
  else radiusF = 24.0f;

  int r = (int)radiusF;
  tft.fillCircle(cx, cy, r, COL_ACCENT_1);
  tft.drawCircle(cx, cy, r + 2, COL_ACCENT_2);

  tft.setTextColor(COL_BG_DARK);
  tft.setTextSize(2);
  tft.setCursor(cx - 34, cy - 8);
  tft.print(breathLabel(breathPhase));

  tft.setTextSize(2);
  tft.setTextColor(COL_TEXT);
  tft.setCursor(140, 190);
  tft.print(remain);

  tft.setTextSize(1);
  tft.setCursor(88, 220);
  tft.print("Tap bubble to start/stop");
}

void updateWave() {
  wavePhase += 0.28f;
  if (wavePhase > 1000.0f) wavePhase = 0.0f;

  float base = 0.5f + 0.25f * sinf(wavePhase);
  float qrs = 0.0f;
  float x = fmodf(wavePhase * 3.5f, 6.28318f);
  if (x > 2.9f && x < 3.2f) qrs = 1.2f - fabsf(3.05f - x) * 4.0f;
  if (qrs < 0) qrs = 0;

  float noise = ((float)random(-10, 10)) / 100.0f;
  float signal = base + qrs + noise;

  int y = (int)(signal * 34.0f);
  if (y < 0) y = 0;
  if (y > 70) y = 70;

  for (int i = 0; i < SCREEN_W - 1; i++) wave[i] = wave[i + 1];
  wave[SCREEN_W - 1] = y;

  heartRate = 76 + (uint16_t)(8 * sinf(wavePhase * 0.23f)) + random(-2, 3);
  spo2 = 97 + (uint16_t)(1 * sinf(wavePhase * 0.11f));
  rr = 15 + (uint16_t)(2 * sinf(wavePhase * 0.09f));
  sysBP = 116 + (uint16_t)(6 * sinf(wavePhase * 0.12f));
  diaBP = 74 + (uint16_t)(4 * sinf(wavePhase * 0.08f));

  monitorAlarm = (heartRate < 50 || heartRate > 120 || spo2 < 92 || rr < 8 || rr > 28);
}

void renderMonitor() {
  tft.fillScreen(COL_BG_DARK);
  drawPixelArtHeader("PATIENT MON");
  drawHomeButton();

  tft.fillRect(8, 44, 304, 84, 0x0000);
  for (int x = 0; x < SCREEN_W - 1; x++) {
    int y1 = 120 - wave[x];
    int y2 = 120 - wave[x + 1];
    tft.drawLine(x, y1, x + 1, y2, COL_ACCENT_1);
  }

  tft.setTextColor(COL_TEXT);
  tft.setTextSize(1);
  tft.setCursor(14, 138);
  tft.printf("HR: %ubpm   SpO2: %u%%   RR: %u/min", heartRate, spo2, rr);
  tft.setCursor(14, 154);
  tft.printf("BP: %u/%u mmHg", sysBP, diaBP);

  tft.fillRoundRect(14, 172, 292, 28, 6, monitorAlarm ? COL_WARN : 0x07E0);
  tft.drawRoundRect(14, 172, 292, 28, 6, COL_TEXT);
  tft.setTextColor(COL_BG_DARK);
  tft.setCursor(24, 182);
  if (monitorAlarm) tft.print("ALARM: Check thresholds / patient status");
  else tft.print("Stable simulation waveform");
}

void renderMessages() {
  tft.fillScreen(COL_BG_DARK);
  drawPixelArtHeader("MESSAGES");
  drawHomeButton();
  ensureDefaultMessages();

  tft.fillRoundRect(14, 52, 292, 104, 8, 0x11D2);
  tft.drawRoundRect(14, 52, 292, 104, 8, COL_ACCENT_2);

  tft.setTextColor(COL_TEXT);
  tft.setTextSize(2);
  tft.setCursor(28, 76);
  tft.print(messages[messageIndex]);

  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED);
  tft.setCursor(16, 170);
  tft.printf("Msg %u/%u  Serial: msg <text>, nextmsg, save", messageIndex + 1, messageCount);
}

void renderCurrent() {
  if (mode == MODE_MENU) renderMenu();
  else if (mode == MODE_QUICK_REF) renderQuickRef();
  else if (mode == MODE_FLASHCARDS) renderFlashcards();
  else if (mode == MODE_BREATHING) renderBreathing();
  else if (mode == MODE_MONITOR) renderMonitor();
  else if (mode == MODE_MESSAGES) renderMessages();
}

void nextFlashcard() {
  flashIndex++;
  if (flashIndex >= (int)(sizeof(FLASHCARDS) / sizeof(FLASHCARDS[0]))) flashIndex = 0;
  flashAnswerShown = false;
  redrawFull = true;
}

void handleTouchTap(int16_t x, int16_t y) {
  lastInputMs = millis();

  if (mode != MODE_MENU && inRect(x, y, 4, 206, 52, 28)) {
    setMode(MODE_MENU);
    return;
  }

  if (mode == MODE_MENU) {
    if (inRect(x, y, 18, 40, 284, 28)) setMode(MODE_QUICK_REF);
    else if (inRect(x, y, 18, 76, 284, 28)) setMode(MODE_FLASHCARDS);
    else if (inRect(x, y, 18, 112, 284, 28)) setMode(MODE_BREATHING);
    else if (inRect(x, y, 18, 148, 284, 28)) setMode(MODE_MONITOR);
    else if (inRect(x, y, 18, 184, 284, 28)) setMode(MODE_MESSAGES);
  } else if (mode == MODE_FLASHCARDS) {
    if (inRect(x, y, 16, 144, 288, 46)) {
      flashAnswerShown = true;
      redrawFull = true;
    } else if (inRect(x, y, 70, 206, 70, 28)) {
      nextFlashcard();
    } else if (inRect(x, y, 150, 206, 70, 28)) {
      flashCorrect++;
      nextFlashcard();
      saveData();
    } else if (inRect(x, y, 230, 206, 70, 28)) {
      flashWrong++;
      nextFlashcard();
      saveData();
    }
  } else if (mode == MODE_BREATHING) {
    if (inRect(x, y, 80, 52, 160, 140)) {
      breathingRunning = !breathingRunning;
      if (breathingRunning) breathPhaseStartMs = millis();
      redrawFull = true;
    }
  } else if (mode == MODE_MESSAGES) {
    if (inRect(x, y, 14, 52, 292, 104)) {
      messageIndex = (messageIndex + 1) % messageCount;
      redrawFull = true;
    }
  }
}

void processTouch() {
  int16_t x, y;
  bool nowPressed = touchReadScreen(x, y);
  touchPressed = nowPressed;
  if (nowPressed) {
    touchX = x;
    touchY = y;
  }

  if (touchPressedPrev && !touchPressed) {
    handleTouchTap(touchX, touchY);
  }
  touchPressedPrev = touchPressed;
}

void processBreathingState() {
  if (!breathingRunning) return;
  uint32_t now = millis();
  uint32_t elapsed = (now - breathPhaseStartMs) / 1000;
  uint16_t dur = breathDurSec[breathPhase];
  if (elapsed >= dur) {
    breathPhase = (BreathPhase)((breathPhase + 1) % 4);
    breathPhaseStartMs = now;
    redrawFull = true;
    Serial.printf("[BREATH] Phase -> %s\n", breathLabel(breathPhase));
  }
}

void processMonitorState() {
  uint32_t now = millis();
  if (now - lastMonitorMs >= 40) {
    lastMonitorMs = now;
    updateWave();
    if (mode == MODE_MONITOR) redrawFull = true;
  }
}

void processMessagesState() {
  if (mode != MODE_MESSAGES || messageCount == 0) return;
  uint32_t now = millis();
  if (now - lastMessageRotateMs > 6000) {
    lastMessageRotateMs = now;
    messageIndex = (messageIndex + 1) % messageCount;
    redrawFull = true;
  }
}

void processAutoDemo() {
  if (!autoDemoEnabled) return;
  uint32_t now = millis();
  if (now - lastAutoStepMs < 12000) return;
  lastAutoStepMs = now;

  autoStep = (autoStep + 1) % 6;
  setMode((AppMode)autoStep);
  if (mode == MODE_FLASHCARDS) {
    flashAnswerShown = (random(0, 2) == 1);
  }
  if (mode == MODE_MESSAGES && messageCount > 0) {
    messageIndex = (messageIndex + 1) % messageCount;
  }
}

void printHelp() {
  Serial.println("\n=== RT Buddy Serial Commands ===");
  Serial.println("help        - this help");
  Serial.println("m           - menu");
  Serial.println("1..5        - open feature");
  Serial.println("a           - flashcard show answer");
  Serial.println("n           - flashcard next");
  Serial.println("c           - mark flashcard correct");
  Serial.println("w           - mark flashcard wrong");
  Serial.println("demo on/off - auto demo mode");
  Serial.println("nextmsg     - next personalized message");
  Serial.println("msg <text>  - add message (max 8)");
  Serial.println("save        - save persistent data");
  Serial.println("load        - reload persistent data");
  Serial.println("home        - same as menu");
  Serial.println("================================\n");
}

void handleSerialCommand(const String& cmdRaw) {
  String cmd = cmdRaw;
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.printf("[SERIAL] cmd: %s\n", cmd.c_str());
  lastInputMs = millis();

  if (cmd == "help") printHelp();
  else if (cmd == "m" || cmd == "home") setMode(MODE_MENU);
  else if (cmd == "1") setMode(MODE_QUICK_REF);
  else if (cmd == "2") setMode(MODE_FLASHCARDS);
  else if (cmd == "3") setMode(MODE_BREATHING);
  else if (cmd == "4") setMode(MODE_MONITOR);
  else if (cmd == "5") setMode(MODE_MESSAGES);
  else if (cmd == "a") { flashAnswerShown = true; redrawFull = true; }
  else if (cmd == "n") nextFlashcard();
  else if (cmd == "c") { flashCorrect++; nextFlashcard(); saveData(); }
  else if (cmd == "w") { flashWrong++; nextFlashcard(); saveData(); }
  else if (cmd == "nextmsg") {
    if (messageCount) {
      messageIndex = (messageIndex + 1) % messageCount;
      redrawFull = true;
    }
  }
  else if (cmd == "save") saveData();
  else if (cmd == "load") { loadData(); redrawFull = true; }
  else if (cmd.startsWith("demo ")) {
    String v = cmd.substring(5);
    v.trim();
    autoDemoEnabled = (v == "on");
    Serial.printf("[DEMO] %s\n", autoDemoEnabled ? "enabled" : "disabled");
  }
  else if (cmd.startsWith("msg ")) {
    String txt = cmd.substring(4);
    txt.trim();
    if (txt.length() > 0) {
      if (messageCount < 8) {
        messages[messageCount++] = txt;
      } else {
        for (uint8_t i = 0; i < 7; i++) messages[i] = messages[i + 1];
        messages[7] = txt;
      }
      messageIndex = messageCount ? (messageCount - 1) : 0;
      saveData();
      redrawFull = true;
    }
  }
  else {
    Serial.println("[SERIAL] Unknown command. Type 'help'.");
  }
}

void processSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      handleSerialCommand(serialLine);
      serialLine = "";
    } else {
      serialLine += c;
      if (serialLine.length() > 140) serialLine = serialLine.substring(0, 140);
    }
  }
}

void detectTouchAvailability() {
  uint8_t hits = 0;
  for (uint8_t i = 0; i < 10; i++) {
    uint16_t x, y, p;
    if (sampleTouchRaw(x, y, p)) hits++;
    delay(5);
  }
  touchAvailable = (hits > 0);
  if (!touchAvailable) {
    autoDemoEnabled = true;
    Serial.println("[TOUCH] No touch detected; enabling auto-demo + serial navigation.");
  } else {
    Serial.println("[TOUCH] Touch controller appears responsive.");
  }
}

void setupPins() {
  pinMode(TFT_CS_PIN, OUTPUT);
  digitalWrite(TFT_CS_PIN, HIGH);

  pinMode(TOUCH_CS_PIN, OUTPUT);
  digitalWrite(TOUCH_CS_PIN, HIGH);

  pinMode(TOUCH_CLK_PIN, OUTPUT);
  digitalWrite(TOUCH_CLK_PIN, LOW);

  pinMode(TOUCH_DIN_PIN, OUTPUT);
  digitalWrite(TOUCH_DIN_PIN, LOW);

  pinMode(TOUCH_DO_PIN, INPUT);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] RT Buddy starting...");

  setupPins();
  SPI.begin(); // default SPI bus for TFT (IO18/IO23/IO19)

  tft.begin(27000000);
  tft.setRotation(1);
  tft.fillScreen(COL_BG_DARK);

  randomSeed(esp_random());

  if (!initStorage()) {
    Serial.println("[STORAGE] No storage backend available.");
  }
  loadData();
  ensureDefaultMessages();

  for (int i = 0; i < SCREEN_W; i++) wave[i] = 20;
  breathPhaseStartMs = millis();
  lastMonitorMs = millis();
  lastAutoStepMs = millis();
  lastMessageRotateMs = millis();

  detectTouchAvailability();
  printHelp();
  logMode(mode);
  redrawFull = true;
}

void loop() {
  processSerial();
  processBreathingState();
  processMonitorState();
  processMessagesState();

  if (touchAvailable) processTouch();
  processAutoDemo();

  uint32_t now = millis();
  if (redrawFull || (now - lastRenderMs >= 66)) {
    lastRenderMs = now;
    renderCurrent();
    redrawFull = false;
  }
}
