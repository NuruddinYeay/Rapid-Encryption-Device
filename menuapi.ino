#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#define TFT_CS    10
#define TFT_DC     9
#define TFT_RST    8
#define TFT_BLK    6   
#define BTN_UP     3 
#define BTN_DOWN   2   
#define BTN_SELECT 4
#define BTN_BACK   5


#define CMD_SIGN        0xAA
#define CMD_MAXLEN      128
#define CMD_GET_INFO    0x01
#define CMD_GET_STAT    0x02
#define CMD_CLEAR       0x03
#define CMD_PING        0xFF
#define CMD_ERR_CRC     0xFD 

typedef struct { uint8_t sign; uint8_t cd; uint8_t len; } t_cmd_hdr;
typedef struct { uint8_t sign; uint8_t cd; uint8_t len; uint8_t data[CMD_MAXLEN - 4]; uint8_t crc; } t_cmd;
typedef struct { int len; uint8_t buf[256]; } t_UartBuf;
#define CMD_SIZE(cmd)   (((t_cmd*)cmd)->len + sizeof(t_cmd_hdr) + 1)

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
t_UartBuf Buf = {0, {0}};

enum State { LOGO, MENU, DISPLAY_IP, SLEEP };
State currentState = LOGO;
State previousState = LOGO; 

int menuChoice = 1; 
unsigned long lastActivity = 0;
const unsigned long SLEEP_TIMEOUT = 60000; 

unsigned long usbMissingTime = 0;
unsigned long usbConnectedTime = 0;
String ip1 = "Loading...", ip2 = "Loading...", usbStatus = "0";
String lastUsbStatus = "0";

String targetIp = "WAITING...";
int targetStatus = 0;
bool isPingChecked = false;

uint8_t get_cmd_crc(t_cmd* cmd, int len) {
    uint8_t crc = 0; uint8_t* ptr = (uint8_t*)cmd;
    for(int c=0; c < len; c++, ptr++) crc ^= *ptr;
    return crc;
}

//  0x01 service state (2 bytes payload)
int cmd_GetInfo(uint8_t* data, int len, uint8_t* ans) {
    if (len >= 2) {
        String newUsbStatus = String(data[0]);
        if (newUsbStatus == "0" && lastUsbStatus == "1") usbMissingTime = millis();
        if (newUsbStatus == "1" && lastUsbStatus == "0") usbConnectedTime = millis();
        usbStatus = newUsbStatus;
        lastUsbStatus = newUsbStatus;
        if (currentState == DISPLAY_IP && menuChoice != 3) drawIP();
    }
    memcpy(ans, data, len); return len;
}

// API STRICT: 0x02 is Platform State (32 bytes name + 1 byte flag = 33 bytes)
int cmd_GetStat(uint8_t* data, int len, uint8_t* ans) {
    if (len >= 33) {
        char name[33]; memcpy(name, data, 32); name[32] = '\0';
        String payloadStr = String(name);
        
        if (payloadStr.startsWith("L:")) {
            String full = payloadStr.substring(2); 
            int comma = full.indexOf(',');
            if(comma != -1) { ip1 = full.substring(0, comma); ip2 = full.substring(comma+1); ip2.trim(); }
            if (currentState == DISPLAY_IP && menuChoice != 3) drawIP();
        } 
        else if (payloadStr.startsWith("R:")) {
            targetIp = payloadStr.substring(2); targetIp.trim();
            targetStatus = data[32];
            if (currentState == DISPLAY_IP && menuChoice == 3) drawIP();
        }
    }
    memcpy(ans, data, len); return len;
}

bool parse_cmd(t_cmd* cmd, int len) {
    t_cmd ans; int payload_len = len - 4; int ret = 0;
    ans.cd = cmd->cd; 

    if (cmd->cd == CMD_GET_INFO) ret = cmd_GetInfo(&cmd->data[0], payload_len, &ans.data[0]);
    else if (cmd->cd == CMD_GET_STAT) ret = cmd_GetStat(&cmd->data[0], payload_len, &ans.data[0]);
    else if (cmd->cd == CMD_PING) { 
        ans.cd = 0x00; // API STRICT: Answer Ping with code 0x00
        memcpy(&ans.data[0], &cmd->data[0], payload_len); ret = payload_len; 
    }
    else if (cmd->cd == CMD_CLEAR) {
        targetIp = "WAITING..."; targetStatus = 0; isPingChecked = false;
        if (currentState == DISPLAY_IP && menuChoice == 3) drawIP();
        memcpy(&ans.data[0], &cmd->data[0], payload_len); ret = payload_len;
    }

    if (ret > 0) {
        ans.sign = CMD_SIGN; ans.len = ret;
        ans.data[ret] = get_cmd_crc(&ans, ans.len + 3);
        Serial.write((const uint8_t *)&ans, CMD_SIZE((&ans)));
    }
    return true;
}

void setup() {
    Serial.begin(9600);
    pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP); pinMode(BTN_BACK, INPUT_PULLUP);
    pinMode(TFT_BLK, OUTPUT); digitalWrite(TFT_BLK, HIGH);
    tft.init(135, 240); tft.setRotation(3); tft.fillScreen(ST77XX_BLACK);
    drawLogo(); lastActivity = millis();
}

void loop() {
    checkButtons();
    if (currentState != SLEEP && (millis() - lastActivity > SLEEP_TIMEOUT)) goToSleep();
    
    if (currentState == DISPLAY_IP) {
        static unsigned long lastRefresh = 0;
        if (millis() - lastRefresh > 1000) { drawIP(); lastRefresh = millis(); }
    }

    // --- API STRICT UART READER WITH CRC CHECK ---
    while (Serial.available() > 0 && Buf.len < 256) Buf.buf[Buf.len++] = Serial.read();
    int ofs = 0; int cur_len = Buf.len;
    
    while (cur_len > 0 && ofs < cur_len) {
        t_cmd* cmd = (t_cmd*)&Buf.buf[ofs];
        
        if (CMD_SIGN != cmd->sign || CMD_SIZE(cmd) > (cur_len - ofs)) {
            ofs++; // Not a valid start or packet incomplete
        } else {
            uint8_t expected_crc = get_cmd_crc(cmd, CMD_SIZE(cmd) - 1);
            uint8_t actual_crc = ((uint8_t*)cmd)[CMD_SIZE(cmd) - 1]; 

            if (expected_crc != actual_crc) {
                uint8_t err_packet[5] = {0xAA, CMD_ERR_CRC, 0x00, 0x00, 0x00};
                err_packet[4] = err_packet[0] ^ err_packet[1] ^ err_packet[2] ^ err_packet[3];
                Serial.write(err_packet, 5);
            } else {
                parse_cmd(cmd, cur_len - ofs);
            }
            ofs += CMD_SIZE(cmd);
        }
    }
    if (ofs > 0) { if (ofs < cur_len) memmove(&Buf.buf[0], &Buf.buf[ofs], cur_len - ofs); Buf.len = cur_len - ofs; }
}

void checkButtons() {
    if (digitalRead(BTN_UP) == LOW || digitalRead(BTN_DOWN) == LOW || digitalRead(BTN_SELECT) == LOW || digitalRead(BTN_BACK) == LOW) {
        if (currentState == SLEEP) { wakeUp(); lastActivity = millis(); delay(250); return; }
        lastActivity = millis();
        
        if (digitalRead(BTN_UP) == LOW) {
            if (currentState == MENU) { menuChoice--; if(menuChoice < 1) menuChoice = 3; drawMenu(); }
            delay(150);
        }
        if (digitalRead(BTN_DOWN) == LOW) {
            if (currentState == MENU) { menuChoice++; if(menuChoice > 3) menuChoice = 1; drawMenu(); }
            delay(150);
        }
        if (digitalRead(BTN_SELECT) == LOW) { 
            if (currentState == LOGO) { currentState = MENU; drawMenu(); } 
            else if (currentState == MENU) { currentState = DISPLAY_IP; isPingChecked = false; drawIP(); } 
            else if (currentState == DISPLAY_IP && menuChoice == 3 && !isPingChecked && targetIp != "WAITING...") {
                isPingChecked = true; drawIP(); 
            }
            delay(200); 
        }
        if (digitalRead(BTN_BACK) == LOW) { 
            if (currentState == DISPLAY_IP) { 
                if (menuChoice == 3 && isPingChecked) { isPingChecked = false; drawIP(); } 
                else { currentState = MENU; drawMenu(); }
            } 
            else if (currentState == MENU) { currentState = LOGO; drawLogo(); } 
            delay(200); 
        }
    }
}

void drawLogo() {
    tft.fillScreen(ST77XX_BLACK); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3);
    tft.setCursor(75, 25); tft.print("RAPID"); tft.setCursor(30, 55); tft.print("ENCRYPTION"); tft.setCursor(65, 85); tft.print("DEVICE");
}

void drawMenu() {
    tft.fillScreen(ST77XX_BLACK); tft.setTextSize(2); tft.setCursor(45, 15); tft.setTextColor(ST77XX_WHITE); tft.print("SELECT PORT:");
    tft.setCursor(35, 45); tft.setTextColor(menuChoice == 1 ? ST77XX_YELLOW : ST77XX_WHITE); tft.print(menuChoice == 1 ? "> PORT 1" : "  PORT 1");
    tft.setCursor(35, 70); tft.setTextColor(menuChoice == 2 ? ST77XX_YELLOW : ST77XX_WHITE); tft.print(menuChoice == 2 ? "> PORT 2" : "  PORT 2");
    tft.setCursor(35, 95); tft.setTextColor(menuChoice == 3 ? ST77XX_ORANGE : ST77XX_WHITE); tft.print(menuChoice == 3 ? "> PEER STATUS" : "  PEER STATUS");
}

void drawIP() {
    tft.fillScreen(ST77XX_BLACK);
    if (menuChoice == 1 || menuChoice == 2) {
        if (usbStatus == "1") {
            if (millis() - usbConnectedTime < 5000) {
                tft.setTextSize(2); tft.setCursor(45, 95); tft.setTextColor(ST77XX_YELLOW); tft.print("USB TOKEN: OK");
            } else { tft.fillCircle(220, 15, 6, ST77XX_YELLOW); }
        } else {
            if (millis() - usbMissingTime < 5000) {
                tft.setTextSize(2); tft.setCursor(15, 95); tft.setTextColor(ST77XX_RED); tft.print("USB TOKEN: MISSING");
            }
        }
        tft.setTextSize(2); tft.setCursor(30, 25);
        if(menuChoice == 1) { tft.setTextColor(ST77XX_GREEN); tft.print("PORT 1 ADDRESS:"); } else { tft.setTextColor(ST77XX_CYAN); tft.print("PORT 2 ADDRESS:"); }
        tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE); tft.setCursor(15, 55); 
        tft.print(menuChoice == 1 ? ip1 : ip2);
    }
    else if (menuChoice == 3) {
        tft.setTextSize(2); tft.setCursor(50, 25); tft.setTextColor(ST77XX_ORANGE);
        tft.print("PEER TARGET");
        
        tft.setCursor(15, 60); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3);
        tft.print(targetIp);
        
        if (targetIp != "WAITING...") {
            if (!isPingChecked) {
                tft.setTextSize(1); tft.setCursor(55, 105); tft.setTextColor(ST77XX_CYAN); tft.print("PRESS SELECT TO PING");
            } else {
                tft.setTextSize(2); tft.setCursor(45, 95);
                if (targetStatus == 1) { tft.setTextColor(ST77XX_YELLOW); tft.print("STATUS: ONLINE"); }
                else { tft.setTextColor(ST77XX_RED); tft.print("STATUS: OFFLINE"); }
            }
        }
    }
}

void wakeUp() { currentState = previousState; digitalWrite(TFT_BLK, HIGH); tft.enableDisplay(true); if(currentState==LOGO) drawMenu(); else drawIP(); }
void goToSleep() { previousState = currentState; tft.fillScreen(ST77XX_BLACK); digitalWrite(TFT_BLK, LOW); tft.enableDisplay(false); currentState = SLEEP; }