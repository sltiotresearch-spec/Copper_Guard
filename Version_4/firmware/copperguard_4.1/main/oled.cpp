#include "oled.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold18pt7b.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C
#define BANNER_HEIGHT 16

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool flash_ST = false;

// ================= INIT =================
void init_oled() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    while (1);
  }
  display.clearDisplay();

  // Dim (lower contrast) to reduce glare + power a bit.
  // NOTE: This does NOT turn the panel off; it reduces contrast.
  display.dim(true);

  display.display();
}

// ================= BANNER =================
void draw_banner() {
  display.fillRect(0, 0, SCREEN_WIDTH, BANNER_HEIGHT, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(2, 5);
  display.print("COPPER GUARD");
  display.setTextColor(WHITE);
  display.display();
}

// ================= BOOT SPLASH =================
static void drawTopBarText(const char* header) {
  display.fillRect(0, 0, SCREEN_WIDTH, BANNER_HEIGHT, WHITE);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setFont(NULL);
  display.setCursor(2, 5);

  // Keep it simple (no scrolling): truncate at ~20 chars
  char tmp[22];
  strncpy(tmp, header ? header : "COPPER GUARD", sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  display.print(tmp);
}

void draw_boot_screen(const char* header, bool at_ok, bool net_ok, int cooldownSec) {
  display.clearDisplay();

  drawTopBarText(header);

  // Outer frame
  display.drawRect(0, BANNER_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - BANNER_HEIGHT, WHITE);

  // Left block (logo)
  display.fillRect(4, BANNER_HEIGHT + 4, (SCREEN_WIDTH / 2) - 8, (SCREEN_HEIGHT - BANNER_HEIGHT) - 8, WHITE);
  display.setFont(&FreeSansBold18pt7b);
  display.setTextColor(BLACK);
  display.setCursor(6, 52);
  display.print(F("CG"));

  // Right block (tests)
  display.setFont(NULL);
  display.setTextColor(WHITE);
  int x = (SCREEN_WIDTH / 2) + 6;
  int y = BANNER_HEIGHT + 8;

  display.setCursor(x, y);
  display.print(F("AT  "));
  display.print(at_ok ? F("OK") : F("--"));

  display.setCursor(x, y + 12);
  display.print(F("NET "));
  display.print(net_ok ? F("OK") : F("--"));

  if (cooldownSec >= 0) {
    display.setCursor(x, y + 28);
    display.print(F("CD "));
    display.print(cooldownSec);
    display.print(F("s"));
  }

  display.display();
}

// ================= MAIN FRAME =================
void draw_frame(bool line_EN[4], bool line_ST[4], bool at_ok, bool net_ok, bool armed) {
  display.fillRect(
    0,
    BANNER_HEIGHT,
    SCREEN_WIDTH,
    SCREEN_HEIGHT - BANNER_HEIGHT,
    BLACK
  );

  // ---- LINE STATUS ----
  int y_pos = BANNER_HEIGHT + 4;
  for (int i = 0; i < 4; i++) {
    display.setCursor(0, y_pos);
    display.print("L");
    display.print(i + 1);

    display.print(line_EN[i] ? " EN" : " --  --");
    display.print(line_EN[i] && line_ST[i] ? "  OK" : "");
    display.print(line_EN[i] && !line_ST[i] && flash_ST ? " CUT" : "");

    y_pos += 12;
  }

  // ---- DIVIDER ----
  display.drawLine(64, BANNER_HEIGHT, 64, SCREEN_HEIGHT, SSD1306_WHITE);

  // ---- MINIMAL GSM STATUS ----
  display.setCursor(70, BANNER_HEIGHT + 4);
  display.print("AT   ");
  display.print(at_ok ? "OK" : (flash_ST ? "ER" : "--"));

  display.setCursor(70, BANNER_HEIGHT + 16);
  display.print("NET  ");
  display.print(net_ok ? "OK" : "--");

  display.setCursor(70, BANNER_HEIGHT + 28);
  display.print("ARM  ");
  display.print(armed ? "ON" : "OFF");

  // ---- RUN ANIMATION ----
  display.setCursor(96, BANNER_HEIGHT + 44);
  display.print(flash_ST ? "--" : "|");

  flash_ST = !flash_ST;
  display.display();
}

// ================= CONFIG SCREEN =================
void config_oled() {
  display.fillRect(
    0,
    BANNER_HEIGHT,
    SCREEN_WIDTH,
    SCREEN_HEIGHT - BANNER_HEIGHT,
    BLACK
  );
  display.setCursor(20, 24);
  display.print("Config Mode");
  display.setCursor(20, 36);
  display.print("192.168.1.1");
  display.display();
}

// ================= LEGACY BOOT STEP =================
void draw_boot_step(const char* currentTask, const char* history) {
  display.clearDisplay();

  // Header (Current Task)
  display.fillRect(0, 0, SCREEN_WIDTH, BANNER_HEIGHT, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(2, 5);
  display.print("> ");
  display.print(currentTask);

  // History
  display.setTextColor(WHITE);
  display.setCursor(0, BANNER_HEIGHT + 10);
  display.print(history);

  display.display();
}
