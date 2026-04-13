#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// =========================
// HARDWARE PINS (NANO + ST7789)
// =========================
#define TFT_CS    10
#define TFT_DC     9
#define TFT_RST    8
#define TFT_BLK    6   

// Swapped to match your physical soldering orientation
const int BTN_UP     = 3;   
const int BTN_DOWN   = 2;   
const int BTN_SELECT = 4;
const int BTN_BACK   = 5;

// =========================
// CONFIG
// =========================
#define CMD_SIGN        0xAA
#define CMD_SERVICE     0x01
#define CMD_PLATFORM    0x02
#define CMD_PING        0xFF

#define CMD_MAXLEN      128
#define UART_BAUD       115200

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// =========================
// DATA STRUCTURES
// =========================
struct Packet {
  uint8_t sign;
  uint8_t cd;
  uint8_t len;
  uint8_t data[CMD_MAXLEN - 4];
  uint8_t crc;
};

struct UiState {
  bool linkOk = false;
  unsigned long lastPacketMs = 0;
  unsigned long lastPollMs = 0;
  unsigned long lastPingMs = 0;

  uint16_t serviceState = 0;
  bool serviceValid = false;

  char platformName[33] = {0};   
  uint8_t platformState = 0;
  bool platformValid = false;
} ui;

// =========================
// MENU
// =========================
enum Screen {
  SCREEN_DASHBOARD = 0,
  SCREEN_SERVICE,
  SCREEN_PLATFORM,
  SCREEN_LINK,
  SCREEN_COUNT
};

Screen currentScreen = SCREEN_DASHBOARD;

// =========================
// BUTTON HANDLING
// =========================
struct ButtonState {
  bool lastStable = HIGH;
  bool lastRead = HIGH;
  unsigned long lastDebounceMs = 0;
};

ButtonState btnSelectState, btnBackState, btnUpState, btnDownState;
const unsigned long DEBOUNCE_MS = 30;

bool readButtonPressed(int pin, ButtonState &st) {
  bool reading = digitalRead(pin);

  if (reading != st.lastRead) {
    st.lastDebounceMs = millis();
    st.lastRead = reading;
  }

  if ((millis() - st.lastDebounceMs) > DEBOUNCE_MS) {
    if (reading != st.lastStable) {
      st.lastStable = reading;
      if (st.lastStable == LOW) {
        return true; 
      }
    }
  }
  return false;
}

// =========================
// CRC / PACKET
// =========================
uint8_t calcCrc(const uint8_t *buf, int len) {
  uint8_t crc = 0;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
  }
  return crc;
}

void sendPacket(uint8_t cd, const uint8_t *data, uint8_t len) {
  uint8_t buf[3 + CMD_MAXLEN];
  buf[0] = CMD_SIGN;
  buf[1] = cd;
  buf[2] = len;

  for (uint8_t i = 0; i < len; i++) {
    buf[3 + i] = data[i];
  }

  uint8_t crc = calcCrc(buf, 3 + len);
  buf[3 + len] = crc;

  Serial.write(buf, 4 + len);
}

void requestServiceState() { sendPacket(CMD_SERVICE, nullptr, 0); }
void requestPlatformState() { sendPacket(CMD_PLATFORM, nullptr, 0); }

void sendPing() {
  static uint32_t pingCounter = 1;
  uint8_t payload[4];
  payload[0] = (uint8_t)(pingCounter & 0xFF);
  payload[1] = (uint8_t)((pingCounter >> 8) & 0xFF);
  payload[2] = (uint8_t)((pingCounter >> 16) & 0xFF);
  payload[3] = (uint8_t)((pingCounter >> 24) & 0xFF);
  sendPacket(CMD_PING, payload, 4);
  pingCounter++;
}

// =========================
// RECEIVE BUFFER / PARSER
// =========================
uint8_t rxBuf[256];
int rxLen = 0;

void processPacket(uint8_t cd, const uint8_t *data, uint8_t len) {
  ui.lastPacketMs = millis();
  ui.linkOk = true;

  if (cd == CMD_SERVICE) {
    if (len >= 2) {
      ui.serviceState = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
      ui.serviceValid = true;
    }
  }
  else if (cd == CMD_PLATFORM) {
    if (len >= 33) {
      memcpy(ui.platformName, data, 32);
      ui.platformName[32] = '\0';

      for (int i = 0; i < 32; i++) {
        if (ui.platformName[i] == '\0') break;
        if ((uint8_t)ui.platformName[i] < 32 || (uint8_t)ui.platformName[i] > 126) {
          ui.platformName[i] = ' ';
        }
      }

      ui.platformState = data[32];
      ui.platformValid = true;
    }
  }
}

void parseIncoming() {
  while (Serial.available()) {
    if (rxLen < (int)sizeof(rxBuf)) {
      rxBuf[rxLen++] = Serial.read();
    } else {
      rxLen = 0;
    }
  }

  int ofs = 0;

  while (rxLen - ofs >= 4) {
    if (rxBuf[ofs] != CMD_SIGN) {
      ofs++;
      continue;
    }

    uint8_t cd  = rxBuf[ofs + 1];
    uint8_t len = rxBuf[ofs + 2];

    if (len > (CMD_MAXLEN - 4)) {
      ofs++;
      continue;
    }

    int totalSize = 4 + len; 
    if ((rxLen - ofs) < totalSize) break; 

    uint8_t expectedCrc = calcCrc(&rxBuf[ofs], 3 + len);
    uint8_t actualCrc   = rxBuf[ofs + 3 + len];

    if (expectedCrc != actualCrc) {
      ofs++;
      continue;
    }

    processPacket(cd, &rxBuf[ofs + 3], len);
    ofs += totalSize;
  }

  if (ofs > 0) {
    memmove(rxBuf, rxBuf + ofs, rxLen - ofs);
    rxLen -= ofs;
  }
}

// =========================
// STATE DECODERS
// =========================
String serviceStateToText(uint16_t s) {
  if (s == 0x0000) return "0x0000 OK/IDLE";
  return "0x" + String(s, HEX);
}

String platformStateToText(uint8_t s) {
  switch (s) {
    case 0x00: return "0x00 IDLE";
    case 0x01: return "0x01 ACTIVE";
    default:   return "0x" + String(s, HEX);
  }
}

// =========================
// TFT DRAW HELPERS
// =========================
void tftPrintLine(int row, const String &text) {
  // Simulates a 4-row LCD layout on the TFT
  int yPos = row * 30 + 10;
  tft.setCursor(5, yPos);
  
  if(row == 0) tft.setTextColor(ST77XX_YELLOW); // Title in yellow
  else tft.setTextColor(ST77XX_WHITE);          // Data in white
  
  tft.print(text);
}

void drawDashboard() {
  tft.fillScreen(ST77XX_BLACK);
  tftPrintLine(0, "RED FRONT PANEL");
  tftPrintLine(1, String("Link: ") + (ui.linkOk ? "OK" : "DOWN"));
  tftPrintLine(2, String("Svc : ") + (ui.serviceValid ? serviceStateToText(ui.serviceState) : "N/A"));
  tftPrintLine(3, String("Plat: ") + (ui.platformValid ? platformStateToText(ui.platformState) : "N/A"));
}

void drawService() {
  tft.fillScreen(ST77XX_BLACK);
  tftPrintLine(0, "SERVICE STATE");
  if (ui.serviceValid) {
    tftPrintLine(1, "Raw:");
    tftPrintLine(2, "0x" + String(ui.serviceState, HEX));
    tftPrintLine(3, serviceStateToText(ui.serviceState));
  } else {
    tftPrintLine(1, "No data");
  }
}

void drawPlatform() {
  tft.fillScreen(ST77XX_BLACK);
  tftPrintLine(0, "PLATFORM STATE");
  if (ui.platformValid) {
    tftPrintLine(1, String("Name: ") + String(ui.platformName));
    tftPrintLine(2, String("State: 0x") + String(ui.platformState, HEX));
    tftPrintLine(3, platformStateToText(ui.platformState));
  } else {
    tftPrintLine(1, "No data");
  }
}

void drawLink() {
  tft.fillScreen(ST77XX_BLACK);
  tftPrintLine(0, "LINK STATUS");
  tftPrintLine(1, String("UART: ") + (ui.linkOk ? "CONNECTED" : "TIMEOUT"));
  tftPrintLine(2, String("Last rx: ") + String((millis() - ui.lastPacketMs) / 1000) + "s");
  tft.setTextSize(1);
  tft.setCursor(5, 115);
  tft.setTextColor(ST77XX_CYAN);
  tft.print("Sel=req  Back=ping");
  tft.setTextSize(2);
}

void drawScreen() {
  switch (currentScreen) {
    case SCREEN_DASHBOARD: drawDashboard(); break;
    case SCREEN_SERVICE:   drawService();   break;
    case SCREEN_PLATFORM:  drawPlatform();  break;
    case SCREEN_LINK:      drawLink();      break;
    default:               drawDashboard(); break;
  }
}

// =========================
// BUTTON ACTIONS
// =========================
void handleButtons() {
  if (readButtonPressed(BTN_UP, btnUpState)) {
    if (currentScreen == 0) currentScreen = (Screen)(SCREEN_COUNT - 1);
    else currentScreen = (Screen)((int)currentScreen - 1);
    drawScreen();
  }

  if (readButtonPressed(BTN_DOWN, btnDownState)) {
    currentScreen = (Screen)(((int)currentScreen + 1) % SCREEN_COUNT);
    drawScreen();
  }

  if (readButtonPressed(BTN_SELECT, btnSelectState)) {
    switch (currentScreen) {
      case SCREEN_DASHBOARD:
        requestServiceState();
        delay(50);
        requestPlatformState();
        break;
      case SCREEN_SERVICE:
        requestServiceState();
        break;
      case SCREEN_PLATFORM:
        requestPlatformState();
        break;
      case SCREEN_LINK:
        requestServiceState();
        requestPlatformState();
        break;
      default:
        break;
    }
  }

  if (readButtonPressed(BTN_BACK, btnBackState)) {
    if (currentScreen == SCREEN_LINK) {
      sendPing();
    } else {
      currentScreen = SCREEN_DASHBOARD;
      drawScreen();
    }
  }
}

// =========================
// SETUP / LOOP
// =========================
void setup() {
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_BACK,   INPUT_PULLUP);
  pinMode(BTN_UP,     INPUT_PULLUP);
  pinMode(BTN_DOWN,   INPUT_PULLUP);
  
  pinMode(TFT_BLK, OUTPUT); 
  digitalWrite(TFT_BLK, HIGH);

  // Replaced dual serials with single native Serial for Nano
  Serial.begin(115200);

  // Initialize your specific TFT screen
  tft.init(135, 240); 
  tft.setRotation(1); 
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextWrap(false);

  tftPrintLine(0, "Starting RED UI...");
  tftPrintLine(1, "UART 115200");
  tftPrintLine(2, "ST7789 TFT Ready");
  tftPrintLine(3, "Buttons Ready");

  delay(1000);

  requestServiceState();
  delay(100);
  requestPlatformState();

  drawScreen();
}

void loop() {
  parseIncoming();
  handleButtons();

  if ((millis() - ui.lastPacketMs) > 5000) {
    ui.linkOk = false;
  }

  if ((millis() - ui.lastPollMs) > 2000) {
    ui.lastPollMs = millis();
    requestServiceState();
    delay(30);
    requestPlatformState();
  }

  if ((millis() - ui.lastPingMs) > 10000) {
    ui.lastPingMs = millis();
    sendPing();
  }

  static unsigned long lastRedraw = 0;
  if ((millis() - lastRedraw) > 1000) {
    lastRedraw = millis();
    if (currentScreen == SCREEN_DASHBOARD || currentScreen == SCREEN_LINK) {
      drawScreen();
    }
  }
}