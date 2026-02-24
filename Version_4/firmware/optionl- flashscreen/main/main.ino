#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSansBold18pt7b.h>

static constexpr uint8_t  SCREEN_WIDTH   = 128;
static constexpr uint8_t  SCREEN_HEIGHT  = 64;
static constexpr uint8_t  SCREEN_ADDRESS = 0x3C;
static constexpr uint8_t  BANNER_HEIGHT  = 16;

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

int BANNER_POS;

int stringLength(const char* text){
  int x;
  while(text[x]){x++;}
  return x;
}

char* clippedText(const char* OG_text){
  char clippedTxT[21];

  
  return clippedTxT;
} 

static void drawTopBar(const char* text) {
  display.fillRect(0, 0, SCREEN_WIDTH, BANNER_HEIGHT, WHITE);
  display.setFont(NULL);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(2, 4);

  
  
}

static void drawBootScreen(int checks){
  display.drawRect(0, BANNER_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT-BANNER_HEIGHT, WHITE);
  display.fillRect(4, BANNER_HEIGHT+4, (SCREEN_WIDTH/2)-4 , (SCREEN_HEIGHT-BANNER_HEIGHT)-8, WHITE);
  
  display.setFont(&FreeSansBold18pt7b);
  display.setTextColor(BLACK);
  display.setTextSize(1);
  display.setCursor(6, 50);
  display.print(F("CG"));

  display.setFont(NULL); 
  display.setTextColor(WHITE);
  display.setCursor((SCREEN_WIDTH/2)+8, BANNER_HEIGHT+6);
  display.print(F("AT  -"));
  display.setCursor((SCREEN_WIDTH/2)+8, BANNER_HEIGHT+6+10);
  display.print(F("SIM -"));
  display.setCursor((SCREEN_WIDTH/2)+8, BANNER_HEIGHT+6+20);
  display.print(F("SIG -"));
  display.setCursor((SCREEN_WIDTH/2)+8, BANNER_HEIGHT+6+30);
  display.print(F("NET -"));

  if(checks>=1){
    display.setCursor((SCREEN_WIDTH/2)+45, BANNER_HEIGHT+6);
    display.print(F("OK"));
  }
  if(checks>=2){
    display.setCursor((SCREEN_WIDTH/2)+45, BANNER_HEIGHT+6+10);
    display.print(F("OK"));
  }
  if(checks>=3){
    display.setCursor((SCREEN_WIDTH/2)+45, BANNER_HEIGHT+6+20);
    display.print(F("OK"));
  }
  if(checks>=4){
    display.setCursor((SCREEN_WIDTH/2)+45, BANNER_HEIGHT+6+30);
    display.print(F("OK"));
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {delay(100);}
  display.clearDisplay();
}

void loop() {
  drawTopBar("COPPER GUARD - SLT R&D - DIGITAL LAB");
  drawBootScreen(2);
  display.display();
  delay(500);
}
