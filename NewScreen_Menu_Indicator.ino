#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// Pins for Hardware SPI (SDA=11, SCL=13)
#define TFT_CS    10
#define TFT_DC     9
#define TFT_RST    8
#define TFT_BLK    6   

// Button Pins
#define BTN_UP     2
#define BTN_DOWN   3
#define BTN_SELECT 4
#define BTN_BACK   5

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

enum State { LOGO, MENU, DISPLAY_IP, SLEEP };
State currentState = LOGO;
State previousState = LOGO; 

int menuChoice = 1; 
unsigned long lastActivity = 0;
const unsigned long SLEEP_TIMEOUT = 60000; // 1 minute
String ip1 = "Loading...", ip2 = "Loading...", usbStatus = "0";

// USB Indicator Variables
int usbDisplayState = 0; // 0 = Off, 1 = Text, 2 = Dot
unsigned long usbDetectTime = 0;
const unsigned long TEXT_DURATION = 5000; // 5 seconds

void setup() {
  Serial.begin(9600);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH); 

  tft.init(135, 240);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  drawLogo();
  lastActivity = millis();
}

void loop() {
  checkButtons();

  // 1. Handle Serial Data from Python
  if (Serial.available() > 0) {
    String data = Serial.readStringUntil('\n');
    data.trim(); 
    
    int firstComma = data.indexOf(',');
    int secondComma = data.indexOf(',', firstComma + 1);
    
    if (firstComma != -1 && secondComma != -1) {
      ip1 = data.substring(0, firstComma);
      ip2 = data.substring(firstComma + 1, secondComma);
      String newUsb = data.substring(secondComma + 1);
      
      // Trigger logic for USB/Token connection
      if (newUsb != usbStatus) {
        usbStatus = newUsb;
        if (usbStatus == "1") {
          usbDisplayState = 1; // Show "TOKEN DETECTED"
          usbDetectTime = millis();
        } else {
          usbDisplayState = 0; // Turn off everything
        }
        
        // Refresh indicator immediately if on a visible page
        if (currentState == MENU || currentState == DISPLAY_IP) {
          drawUSBIndicator();
        }
      }
      
      if (currentState == DISPLAY_IP) drawIP(); 
    }
  }

  // 2. Timer Logic: Switch from Text to Dot after 5 seconds
  if (usbDisplayState == 1 && (millis() - usbDetectTime >= TEXT_DURATION)) {
    usbDisplayState = 2; // Change state to Dot
    if (currentState == MENU || currentState == DISPLAY_IP) {
      drawUSBIndicator(); // Update the screen
    }
  }

  // 3. Sleep Timer
  if (currentState != SLEEP && (millis() - lastActivity > SLEEP_TIMEOUT)) {
    goToSleep();
  }
}

void checkButtons() {
  if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW || 
      digitalRead(BTN_SELECT) == LOW || digitalRead(BTN_BACK) == LOW) {
    
    if (currentState == SLEEP) {
      wakeUp();
      delay(200); 
      return;
    }
    
    lastActivity = millis();

    if (digitalRead(BTN_UP) == LOW) {
      if (currentState == MENU && menuChoice != 1) { menuChoice = 1; drawMenu(); }
      delay(100); 
    }
    if (digitalRead(BTN_DOWN) == LOW) {
      if (currentState == MENU && menuChoice != 2) { menuChoice = 2; drawMenu(); }
      delay(100); 
    }
    if (digitalRead(BTN_SELECT) == LOW) {
      if (currentState == LOGO) { currentState = MENU; tft.fillScreen(ST77XX_BLACK); drawMenu(); }
      else if (currentState == MENU) { currentState = DISPLAY_IP; tft.fillScreen(ST77XX_BLACK); drawIP(); }
      delay(200);
    }
    if (digitalRead(BTN_BACK) == LOW) {
      if (currentState == DISPLAY_IP) { currentState = MENU; tft.fillScreen(ST77XX_BLACK); drawMenu(); }
      else if (currentState == MENU) { currentState = LOGO; tft.fillScreen(ST77XX_BLACK); drawLogo(); }
      delay(200);
    }
  }
}

void wakeUp() {
  currentState = previousState; // Remember where we were
  digitalWrite(TFT_BLK, HIGH);
  tft.enableDisplay(true);
  
  if (currentState == LOGO) { tft.fillScreen(ST77XX_BLACK); drawLogo(); }
  else if (currentState == MENU) { tft.fillScreen(ST77XX_BLACK); drawMenu(); }
  else if (currentState == DISPLAY_IP) { tft.fillScreen(ST77XX_BLACK); drawIP(); }
  
  lastActivity = millis();
}

void goToSleep() {
  previousState = currentState;
  tft.fillScreen(ST77XX_BLACK);
  digitalWrite(TFT_BLK, LOW); 
  tft.enableDisplay(false);
  currentState = SLEEP;
}

void drawLogo() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(70, 20); tft.print("RAPID");
  tft.setCursor(30, 50); tft.print("ENCRYPTION");
  tft.setCursor(65, 80); tft.print("DEVICE");
}

void drawUSBIndicator() {
  // Clear only the specific areas where indicators appear
  tft.fillRect(0, 115, 240, 20, ST77XX_BLACK); // Clear Text Area
  tft.fillCircle(225, 15, 6, ST77XX_BLACK);    // Clear Dot Area

  if (usbDisplayState == 1) {
    // Show Text (Size 2 is approx 12x16px per char)
    tft.setTextSize(2);
    tft.setCursor(70, 120); // Centered for "TOKEN DETECTED"
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.print("TOKEN DETECTED");
  } 
  else if (usbDisplayState == 2) {
    // Show Dot
    tft.fillCircle(225, 15, 6, ST77XX_YELLOW);
  }
}

void drawMenu() {
  tft.setTextSize(2);
  tft.setCursor(10, 5);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.print("SELECT PORT:");
  
  drawUSBIndicator(); 

  tft.setTextSize(3);
  tft.setCursor(15, 45);
  tft.setTextColor(menuChoice == 1 ? ST77XX_YELLOW : ST77XX_WHITE, ST77XX_BLACK);
  tft.print(menuChoice == 1 ? "> PORT 1 " : "  PORT 1 ");

  tft.setCursor(15, 85);
  tft.setTextColor(menuChoice == 2 ? ST77XX_YELLOW : ST77XX_WHITE, ST77XX_BLACK);
  tft.print(menuChoice == 2 ? "> PORT 2 " : "  PORT 2 ");
}

void drawIP() {
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  uint16_t headerColor = (menuChoice == 1) ? ST77XX_GREEN : ST77XX_CYAN;
  tft.setTextColor(headerColor, ST77XX_BLACK);
  tft.print(menuChoice == 1 ? "PORT 1 ADDRESS: " : "PORT 2 ADDRESS: ");
  
  drawUSBIndicator(); 

  tft.setTextSize(2);
  tft.setCursor(5, 60);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  
  if (menuChoice == 1) tft.print(ip1 + "           "); 
  else tft.print(ip2 + "           ");
}