#pragma once

// TFT (ILI9341) wiring from pin-map
#define TFT_CS_PIN    27
#define TFT_DC_PIN    26
#define TFT_RST_PIN   25

// Resistive touch wiring from pin-map (XPT2046-style controller lines)
#define TOUCH_CS_PIN  33
#define TOUCH_DO_PIN  32
#define TOUCH_CLK_PIN 18
#define TOUCH_DIN_PIN 23

// Storage architecture:
// SD card CS is not wired in the provided pin-map, so SD is disabled by default.
// Set to >=0 only if a real SD CS line is physically wired.
#define SD_CS_PIN     -1

// Display layout
#define SCREEN_W 320
#define SCREEN_H 240

// Theme colors (blue-green-pink pixel-art palette)
#define COL_BG_DARK   0x084F  // deep blue
#define COL_BG_LIGHT  0x1D7A  // teal-blue
#define COL_ACCENT_1  0x07F0  // aqua green
#define COL_ACCENT_2  0xF81F  // pink
#define COL_ACCENT_3  0xFFE0  // yellow
#define COL_TEXT      0xFFFF  // white
#define COL_MUTED     0xBDF7  // pale cyan
#define COL_WARN      0xFD20  // orange-red
