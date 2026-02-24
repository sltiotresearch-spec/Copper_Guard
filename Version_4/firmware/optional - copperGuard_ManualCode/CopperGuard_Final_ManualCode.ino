#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTCMemory.h>

#define CONFIG_PIN  0
#define OPTO_PIN_1  12  
#define OPTO_PIN_2  13  

#define OPTO_PIN_3  14
#define OPTO_PIN_4  16

#define AP_SSID "CopperGuard"
#define AP_PASS "12345678"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C
#define BANNER_HEIGHT 16


static const int OPTO_PINS[]={OPTO_PIN_1,OPTO_PIN_2,OPTO_PIN_3,OPTO_PIN_4};
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

struct RTC_Data {uint32_t state;};
RTCMemory<RTC_Data> rtc;

int state;

void drawBanner(){
  display.fillRect(0, 0, SCREEN_WIDTH, BANNER_HEIGHT, WHITE);
  display.setTextColor(BLACK);
  display.setCursor(2, 5);
  display.print("PROJECT -COPPER GUARD");
}

void initDisplay(){
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);  display.clearDisplay();
  display.dim(true);
  display.clearDisplay();
  drawBanner();
  display.display();
}

void showTextOn(int x, int y, String text, bool color){
  display.setTextColor(color);
  display.setCursor(x, y);
  display.print(text);
  display.display();
}

void flushGSM(){
  while (Serial.available()) {char temp = Serial.read();} 
  delay(200);
}

void initGSM(){
  delay(2000);
  flushGSM();
}

bool SendCommandsToGSM(String command,String ExResponse, int timeout){
  uint32_t start = millis();
  String buf;
  Serial.println(command);
  while(millis() - start < timeout){
    while (Serial.available()) {
      char c = (char)Serial.read();
      buf += c;
      
      if (buf.indexOf("\r\n"+ExResponse+"\r\n") != -1 || buf.indexOf(ExResponse) != -1) return true;
      // prevent unbounded growth
      if (buf.length() > 200) buf.remove(0, 100);
    }
    delay(5);
  }
  return false;
}

void setup(){
  Serial.begin(9600);
  rtc.begin();  
  initDisplay();
  initGSM();
}

void loop(){
  //State Machine
  display.clearDisplay(); 
  drawBanner();
  
  switch (state) {
    case 0: // GSM Test
      flushGSM();
      if (SendCommandsToGSM("AT", "OK", 1000)) {state = 1;}
      else {display.clearDisplay(); showTextOn(20, 40, "GSM Fail", WHITE);}
    break;

    case 1: // GSM Pass
      {showTextOn(20, 40, "GSM PASS", WHITE);}
      delay(1000);
      state = 2;
    break;

    case 2: // Network OK ?
      flushGSM();
      if (SendCommandsToGSM("AT+CREG?", "CREG: 0,1", 1000)) {state = 3;}
      else {showTextOn(20, 40, "NET Fail", WHITE);}
    break;

    case 3: // Net Pass
      {showTextOn(20, 40, "Net PASS", WHITE);}
      delay(1000);
      state = 0;
    break;

    case -1: // 
    break;
  }
  display.display();
}