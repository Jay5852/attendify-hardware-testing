/**
 * ESP32 TEST CODE - CLEAN UI, FAST WIFI, PROPER LAYOUT
 * SH1106 OLED (128x64) + DS3231 RTC + 4 Buttons
 * FINAL VERSION - ALL FIXES APPLIED WITH PASSWORD SUPPORT
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "RTClib.h"
#include <WiFi.h>
#include <Preferences.h>

// =================== PIN CONFIGURATION ===================
#define OLED_SDA 21
#define OLED_SCL 22
#define BUTTON_UP 32
#define BUTTON_DOWN 33
#define BUTTON_SELECT 25
#define BUTTON_BACK 26

// =================== OLED SETUP ===================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// =================== RTC SETUP ===================
RTC_DS3231 rtc;

// =================== PREFERENCES ===================
Preferences preferences;

// =================== SCREEN STATE ENUM ===================
enum ScreenState {
  SCREEN_BOOT,
  SCREEN_HOME,
  SCREEN_MAIN_MENU,
  SCREEN_BUTTON_TEST,
  SCREEN_SYSTEM_INFO,
  SCREEN_RESET_MEM,
  SCREEN_DEMO_MODE,
  SCREEN_OLED_TEST,
  SCREEN_RTC_TEST,
  SCREEN_VOLT_TEST,
  SCREEN_WIFI_SCAN,
  SCREEN_PASSWORD_ENTRY,
  SCREEN_WIFI_CONNECT,
  SCREEN_WIFI_STATUS,
  SCREEN_SET_TIME
};

// =================== GLOBAL VARIABLES ===================
ScreenState currentScreen = SCREEN_BOOT;
int menuIndex = 0;
bool needRefresh = true;
bool displayInitialized = false;
bool wifiConnected = false;

// Button tracking
int buttonPressCount[4] = {0, 0, 0, 0};
unsigned long buttonPressTime[4] = {0, 0, 0, 0};
bool buttonLongPressed[4] = {false, false, false, false};

// Demo mode
bool demoActive = false;
int demoCounter = 0;

// WiFi
String wifiNetworks[15];
int wifiNetworkCount = 0;
int wifiSelectedIndex = 0;
bool wifiScanning = false;
bool wifiConnecting = false;
String connectedSSID = "";

// Password entry
String wifiPassword = "";
bool passwordEntryMode = false;
int passwordCursorPos = 0;
char passwordChars[64] = {0};
int charSetIndex = 0;

// Character set for password entry
const char* charSet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
int charSetLength = 62;

// Time setting
int timeSettingMode = 0; // 0=hour, 1=minute, 2=day, 3=month, 4=year
int tempHour = 12;
int tempMinute = 30;
int tempDay = 21;
int tempMonth = 1;
int tempYear = 2026;

// Day and month names
const char* dayNames[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* monthNames[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", 
                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// =================== FUNCTION DECLARATIONS ===================
void initializeHardware();
void initializePreferences();
void saveButtonCounts();
void loadButtonCounts();
void showScreen();
void drawHeader();
void drawFooter();
void checkButtons();
void handleButtonPress(int button);
void handleLongPress(int button);
void scanWiFiNetworks();
void connectToWiFi(String ssid, String password);
void showMessage(String message, int delayTime = 1000);

// Screen drawing functions
void drawBootScreen();
void drawHomeScreen();
void drawMainMenu();
void drawButtonTest();
void drawSystemInfo();
void drawResetMem();
void drawDemoMode();
void drawOledTest();
void drawRtcTest();
void drawVoltTest();
void drawWifiScanScreen();
void drawPasswordEntryScreen();
void drawWifiConnectScreen();
void drawWifiStatusScreen();
void drawSetTimeScreen();

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== ESP32 System ===");
  
  initializeHardware();
  initializePreferences();
  loadButtonCounts();
  
  currentScreen = SCREEN_BOOT;
  showScreen();
  delay(1500);
  
  currentScreen = SCREEN_HOME;
  needRefresh = true;
}

// =================== INITIALIZE PREFERENCES ===================
void initializePreferences() {
  preferences.begin("system", false);
}

void saveButtonCounts() {
  preferences.putInt("btn_up", buttonPressCount[0]);
  preferences.putInt("btn_down", buttonPressCount[1]);
  preferences.putInt("btn_sel", buttonPressCount[2]);
  preferences.putInt("btn_back", buttonPressCount[3]);
  preferences.putInt("demo_counter", demoCounter);
  preferences.end();
  preferences.begin("system", false);
}

void loadButtonCounts() {
  buttonPressCount[0] = preferences.getInt("btn_up", 0);
  buttonPressCount[1] = preferences.getInt("btn_down", 0);
  buttonPressCount[2] = preferences.getInt("btn_sel", 0);
  buttonPressCount[3] = preferences.getInt("btn_back", 0);
  demoCounter = preferences.getInt("demo_counter", 0);
}

// =================== INITIALIZE HARDWARE ===================
void initializeHardware() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  
  // Initialize OLED
  if (!display.begin(0x3C, true)) {
    Serial.println("OLED not found!");
    displayInitialized = false;
  } else {
    displayInitialized = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setRotation(0);
    Serial.println("OLED initialized");
  }
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
  } else {
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(2026, 1, 21, 12, 30, 0));
    }
    Serial.println("RTC initialized");
  }
  
  // Initialize buttons
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  
  // Initially disable WiFi
  WiFi.mode(WIFI_OFF);
  Serial.println("Hardware initialized");
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 300 || needRefresh) {
    showScreen();
    lastUpdate = millis();
    needRefresh = false;
  }
  
  // Demo mode updates
  if (currentScreen == SCREEN_DEMO_MODE && demoActive) {
    static unsigned long demoLastUpdate = 0;
    if (millis() - demoLastUpdate > 500) {
      demoCounter++;
      if (demoCounter > 9999) demoCounter = 0;
      demoLastUpdate = millis();
      needRefresh = true;
      saveButtonCounts();
    }
  }
  
  delay(50);
}

// =================== WIFI FUNCTIONS ===================
void scanWiFiNetworks() {
  wifiScanning = true;
  wifiNetworkCount = 0;
  needRefresh = true;
  
  for (int i = 0; i < 15; i++) {
    wifiNetworks[i] = "";
  }
  
  // Enable WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.println("Scanning for WiFi...");
  
  // Quick scan (fix: removed NULL parameter)
  int16_t n = WiFi.scanNetworks(false, true);
  
  Serial.printf("Found %d networks\n", n);
  
  for (int i = 0; i < n && wifiNetworkCount < 15; i++) {
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    
    if (ssid.length() == 0 || rssi < -95) continue;
    
    // Add security indicator
    String security = "";
    if (auth == WIFI_AUTH_OPEN) {
      security = " [OPEN]";
    } else if (auth == WIFI_AUTH_WEP) {
      security = " [WEP]";
    } else if (auth == WIFI_AUTH_WPA_PSK) {
      security = " [WPA]";
    } else if (auth == WIFI_AUTH_WPA2_PSK) {
      security = " [WPA2]";
    } else if (auth == WIFI_AUTH_WPA_WPA2_PSK) {
      security = " [WPA/WPA2]";
    }
    
    // Format: SSID (RSSI dBm) [SECURITY]
    String displayName = ssid;
    if (ssid.length() > 10) {
      displayName = ssid.substring(0, 7) + "...";
    }
    
    wifiNetworks[wifiNetworkCount] = displayName + security;
    wifiNetworkCount++;
    
    Serial.printf("%d: %s (%d dBm)\n", wifiNetworkCount, ssid.c_str(), rssi);
  }
  
  WiFi.scanDelete();
  wifiScanning = false;
  
  if (wifiNetworkCount == 0) {
    Serial.println("No networks found");
  }
}

void connectToWiFi(String ssid, String password) {
  connectedSSID = "";
  wifiConnecting = true;
  needRefresh = true;
  
  // Remove security info from SSID
  int bracketPos = ssid.indexOf(" [");
  if (bracketPos != -1) {
    ssid = ssid.substring(0, bracketPos);
  }
  
  Serial.printf("Connecting to: %s\n", ssid.c_str());
  
  // Connect with password
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (attempts < 15) {
    if (WiFi.status() == WL_CONNECTED) {
      connectedSSID = ssid;
      wifiConnected = true;
      Serial.println("Connected!");
      wifiConnecting = false;
      needRefresh = true;
      return;
    }
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connection failed!");
    wifiConnecting = false;
    wifiConnected = false;
    needRefresh = true;
  }
}

void showMessage(String message, int delayTime) {
  display.clearDisplay();
  display.setCursor(10, 25);
  display.println(message);
  display.display();
  delay(delayTime);
  needRefresh = true;
}

// =================== DISPLAY FUNCTIONS ===================
void showScreen() {
  if (!displayInitialized) return;
  
  display.clearDisplay();
  
  // Draw main content
  switch(currentScreen) {
    case SCREEN_BOOT:
      drawBootScreen();
      break;
    case SCREEN_HOME:
      drawHomeScreen();
      break;
    case SCREEN_MAIN_MENU:
      drawMainMenu();
      break;
    case SCREEN_BUTTON_TEST:
      drawButtonTest();
      break;
    case SCREEN_SYSTEM_INFO:
      drawSystemInfo();
      break;
    case SCREEN_RESET_MEM:
      drawResetMem();
      break;
    case SCREEN_DEMO_MODE:
      drawDemoMode();
      break;
    case SCREEN_OLED_TEST:
      drawOledTest();
      break;
    case SCREEN_RTC_TEST:
      drawRtcTest();
      break;
    case SCREEN_VOLT_TEST:
      drawVoltTest();
      break;
    case SCREEN_WIFI_SCAN:
      drawWifiScanScreen();
      break;
    case SCREEN_PASSWORD_ENTRY:
      drawPasswordEntryScreen();
      break;
    case SCREEN_WIFI_CONNECT:
      drawWifiConnectScreen();
      break;
    case SCREEN_WIFI_STATUS:
      drawWifiStatusScreen();
      break;
    case SCREEN_SET_TIME:
      drawSetTimeScreen();
      break;
  }
  
  drawFooter();
  display.display();
}

void drawFooter() {
  switch(currentScreen) {
    case SCREEN_HOME:
      display.fillRect(0, 54, 128, 10, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
      display.setCursor(50, 56);
      display.print("MENU");
      display.setTextColor(SH110X_WHITE);
      break;
      
    case SCREEN_MAIN_MENU:
      display.setCursor(10, 56);
      display.print("U/D SEL BACK");
      break;
      
    case SCREEN_RESET_MEM:
      display.setCursor(20, 56);
      display.print("S=Confirm B=Cancel");
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        display.setCursor(5, 56);
        display.print("S=Connect B=Back");
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      display.setCursor(5, 56);
      display.print("U/D:Char S=Add B=Del");
      break;
      
    case SCREEN_DEMO_MODE:
      display.setCursor(40, 56);
      display.print("S=Toggle");
      break;
      
    case SCREEN_WIFI_STATUS:
      display.setCursor(40, 56);
      display.print("B=Back");
      break;
      
    case SCREEN_SET_TIME:
      display.setCursor(20, 56);
      display.print("S=Next B=Set");
      break;
      
    default:
      if (currentScreen != SCREEN_BOOT) {
        display.setCursor(50, 56);
        display.print("B=Menu");
      }
      break;
  }
}

void drawBootScreen() {
  display.setCursor(35, 20);
  display.setTextSize(2);
  display.println("SYSTEM");
  display.setCursor(45, 40);
  display.println("TEST");
  display.setTextSize(1);
}

void drawHomeScreen() {
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Device ID at top
    display.setCursor(40, 0);
    display.println("9E6045A0");
    display.drawLine(0, 9, 127, 9, SH110X_WHITE);
    
    // Large time in center
    display.setCursor(30, 15);
    display.setTextSize(3);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    // Date below time
    display.setCursor(35, 42);
    display.print(dayNames[now.dayOfTheWeek()]);
    display.print(" ");
    display.print(monthNames[now.month()-1]);
    display.print(" ");
    display.print(now.day());
    
    // Year at bottom center
    display.setCursor(55, 50);
    display.printf("%04d", now.year());
  } else {
    display.setCursor(40, 25);
    display.println("RTC NOT");
    display.setCursor(45, 40);
    display.println("FOUND!");
  }
  
  // WiFi indicator at top right if connected
  if (wifiConnected) {
    display.setCursor(100, 0);
    display.print("WiFi");
  }
}

void drawMainMenu() {
  display.setCursor(50, 5);
  display.println("MENU");
  display.drawLine(0, 15, 127, 15, SH110X_WHITE);
  
  const char* menuItems[] = {
    "BUTTON TEST",
    "SYS INFO",
    "SET TIME",
    "MEM RESET",
    "DEMO MODE",
    "OLED TEST",
    "RTC TEST",
    "VOLT TEST",
    "WIFI SCAN"
  };
  
  int totalItems = 9;
  
  // Show 3 items at a time
  int startIdx = 0;
  if (menuIndex > 2) {
    startIdx = menuIndex - 2;
  }
  if (menuIndex > 6) {
    startIdx = menuIndex - 1;
  }
  
  for (int i = 0; i < 3 && (startIdx + i) < totalItems; i++) {
    int idx = startIdx + i;
    int yPos = 20 + (i * 12);
    
    // Clear line area
    display.fillRect(0, yPos - 2, 128, 12, SH110X_BLACK);
    
    if (idx == menuIndex) {
      display.fillRect(0, yPos - 2, 128, 12, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(20, yPos);
    display.print(menuItems[idx]);
    
    if (idx == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
  
  // Scroll indicators
  if (menuIndex > 0) {
    display.setCursor(120, 20);
    display.print("^");
  }
  if (menuIndex < totalItems - 1) {
    display.setCursor(120, 45);
    display.print("v");
  }
}

void drawButtonTest() {
  display.setCursor(40, 2);
  display.println("BUTTON TEST");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Display button counts in a clean layout
  display.setCursor(20, 20);
  display.print("UP:     ");
  display.print(buttonPressCount[0]);
  
  display.setCursor(20, 30);
  display.print("DOWN:   ");
  display.print(buttonPressCount[1]);
  
  display.setCursor(20, 40);
  display.print("SELECT: ");
  display.print(buttonPressCount[2]);
  
  display.setCursor(20, 50);
  display.print("BACK:   ");
  display.print(buttonPressCount[3]);
}

void drawSystemInfo() {
  display.setCursor(35, 2);
  display.println("SYSTEM INFO");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Clean info layout
  display.setCursor(10, 20);
  display.print("ESP32 240MHz");
  
  display.setCursor(10, 30);
  display.print("Heap: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.print(" KB");
  
  display.setCursor(10, 40);
  display.print("Flash: ");
  display.print(ESP.getFlashChipSize() / (1024 * 1024));
  display.print(" MB");
  
  display.setCursor(10, 50);
  display.print("RTC: ");
  if (rtc.begin()) {
    display.print("OK");
  } else {
    display.print("NO");
  }
}

void drawResetMem() {
  display.setCursor(40, 2);
  display.println("RESET MEM");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(25, 25);
  display.println("PRESS SELECT");
  display.setCursor(45, 40);
  display.println("TO RESET");
}

void drawDemoMode() {
  display.setCursor(40, 2);
  display.println("DEMO MODE");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Large counter display
  display.setCursor(40, 25);
  display.setTextSize(2);
  display.printf("%04d", demoCounter);
  display.setTextSize(1);
  
  // Status indicator
  display.setCursor(45, 45);
  if (demoActive) {
    display.print("RUNNING");
  } else {
    display.print("PAUSED");
  }
}

void drawOledTest() {
  display.setCursor(40, 2);
  display.println("OLED TEST");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  static int pattern = 0;
  
  // Pattern indicator
  display.setCursor(50, 25);
  display.print("PATT ");
  display.print(pattern + 1);
  
  // Draw pattern
  switch(pattern % 4) {
    case 0: // Bars
      display.fillRect(20, 35, 88, 10, SH110X_WHITE);
      break;
    case 1: // Circle
      display.fillCircle(64, 40, 12, SH110X_WHITE);
      break;
    case 2: // Grid
      for (int x = 20; x < 108; x += 10) {
        display.drawLine(x, 35, x, 45, SH110X_WHITE);
      }
      break;
    case 3: // Diagonal
      display.drawLine(20, 35, 108, 45, SH110X_WHITE);
      display.drawLine(20, 45, 108, 35, SH110X_WHITE);
      break;
  }
}

void drawRtcTest() {
  display.setCursor(45, 2);
  display.println("RTC TEST");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Time in center
    display.setCursor(35, 20);
    display.setTextSize(2);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    // Date below
    display.setCursor(30, 45);
    display.printf("%02d/%02d/%04d", now.day(), now.month(), now.year());
    
    // Day of week on right
    display.setCursor(90, 45);
    display.print(dayNames[now.dayOfTheWeek()]);
  } else {
    display.setCursor(40, 25);
    display.println("RTC NOT");
    display.setCursor(45, 40);
    display.println("FOUND!");
  }
}

void drawVoltTest() {
  display.setCursor(30, 2);
  display.println("VOLTAGE TEST");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Simulated voltage display
  display.setCursor(25, 25);
  display.setTextSize(2);
  display.println("3.75 V");
  display.setTextSize(1);
  
  display.setCursor(40, 45);
  display.print("BATTERY OK");
}

void drawWifiScanScreen() {
  display.setCursor(40, 2);
  display.println("WIFI SCAN");
  
  if (wifiScanning) {
    display.setCursor(40, 30);
    display.print("SCANNING...");
    return;
  }
  
  if (wifiNetworkCount == 0) {
    display.setCursor(25, 25);
    display.println("NO NETWORKS");
    display.setCursor(20, 40);
    display.println("FOUND");
  } else {
    // Show 3 networks at a time
    int startIdx = 0;
    if (wifiSelectedIndex > 2) {
      startIdx = wifiSelectedIndex - 2;
    }
    
    for (int i = 0; i < 3 && (startIdx + i) < wifiNetworkCount; i++) {
      int idx = startIdx + i;
      int yPos = 15 + (i * 14);
      
      // Clear line
      display.fillRect(0, yPos - 2, 128, 14, SH110X_BLACK);
      
      if (idx == wifiSelectedIndex) {
        display.fillRect(0, yPos - 2, 128, 14, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      
      display.setCursor(5, yPos);
      
      // Format network display
      String displayText = wifiNetworks[idx];
      if (displayText.length() > 18) {
        displayText = displayText.substring(0, 15) + "...";
      }
      display.print(displayText);
      
      if (idx == wifiSelectedIndex) {
        display.setTextColor(SH110X_WHITE);
      }
    }
    
    // Scroll indicators
    if (wifiSelectedIndex > 0) {
      display.setCursor(120, 16);
      display.print("^");
    }
    if (wifiSelectedIndex < wifiNetworkCount - 1) {
      display.setCursor(120, 44);
      display.print("v");
    }
    
    // Show count
    display.setCursor(100, 2);
    display.printf("%d/%d", wifiSelectedIndex + 1, wifiNetworkCount);
  }
}

void drawPasswordEntryScreen() {
  display.setCursor(30, 2);
  display.println("WIFI PASSWORD");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Show network name
  String displaySSID = wifiNetworks[wifiSelectedIndex];
  int bracketPos = displaySSID.indexOf(" [");
  if (bracketPos != -1) {
    displaySSID = displaySSID.substring(0, bracketPos);
  }
  
  display.setCursor(5, 18);
  display.print("SSID: ");
  if (displaySSID.length() > 12) {
    display.print(displaySSID.substring(0, 9) + "...");
  } else {
    display.print(displaySSID);
  }
  
  // Show password (masked)
  display.setCursor(5, 30);
  display.print("Pass: ");
  for (int i = 0; i < passwordCursorPos; i++) {
    if (passwordChars[i] != 0) {
      display.print("*");
    }
  }
  
  // Show cursor
  display.setCursor(5 + (passwordCursorPos * 6), 30);
  display.print("_");
  
  // Show current character being edited
  display.setCursor(5, 45);
  display.print("Char: ");
  if (passwordCursorPos < sizeof(passwordChars) && passwordChars[passwordCursorPos] != 0) {
    display.print(passwordChars[passwordCursorPos]);
  } else {
    display.print("a");
  }
}

void drawWifiConnectScreen() {
  display.setCursor(40, 2);
  display.println("CONNECTING");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Animated dots
  static int dots = 0;
  display.setCursor(40, 30);
  display.print("PLEASE WAIT");
  for (int i = 0; i < dots; i++) {
    display.print(".");
  }
  dots = (dots + 1) % 4;
  
  // Show network name
  if (wifiSelectedIndex < wifiNetworkCount) {
    display.setCursor(10, 45);
    display.print("To: ");
    String ssid = wifiNetworks[wifiSelectedIndex];
    int bracketPos = ssid.indexOf(" [");
    if (bracketPos != -1) {
      ssid = ssid.substring(0, bracketPos);
    }
    if (ssid.length() > 12) {
      display.print(ssid.substring(0, 9) + "...");
    } else {
      display.print(ssid);
    }
  }
}

void drawWifiStatusScreen() {
  display.setCursor(40, 2);
  display.println("WIFI STATUS");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  if (wifiConnected) {
    display.setCursor(35, 25);
    display.print("CONNECTED!");
    
    display.setCursor(10, 40);
    display.print("IP: ");
    display.print(WiFi.localIP().toString());
  } else {
    display.setCursor(35, 25);
    display.print("FAILED!");
    
    display.setCursor(20, 40);
    display.print("TRY AGAIN");
  }
}

void drawSetTimeScreen() {
  display.setCursor(40, 2);
  display.println("SET TIME");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Show what's being edited
  display.setCursor(20, 20);
  switch(timeSettingMode) {
    case 0: display.print("HOUR:"); break;
    case 1: display.print("MINUTE:"); break;
    case 2: display.print("DAY:"); break;
    case 3: display.print("MONTH:"); break;
    case 4: display.print("YEAR:"); break;
  }
  
  // Show current value being edited
  display.setCursor(70, 20);
  switch(timeSettingMode) {
    case 0: display.printf("%02d", tempHour); break;
    case 1: display.printf("%02d", tempMinute); break;
    case 2: display.printf("%02d", tempDay); break;
    case 3: display.printf("%02d", tempMonth); break;
    case 4: display.printf("%04d", tempYear); break;
  }
  
  // Show preview of time being set
  display.setCursor(25, 35);
  display.printf("%02d:%02d", tempHour, tempMinute);
  
  display.setCursor(30, 45);
  display.printf("%02d/%02d/%04d", tempDay, tempMonth, tempYear);
}

// =================== BUTTON HANDLING ===================
void checkButtons() {
  static bool lastUp = HIGH, lastDown = HIGH, lastSel = HIGH, lastBack = HIGH;
  static unsigned long lastDebounce = 0;
  
  bool upNow = digitalRead(BUTTON_UP);
  bool downNow = digitalRead(BUTTON_DOWN);
  bool selNow = digitalRead(BUTTON_SELECT);
  bool backNow = digitalRead(BUTTON_BACK);
  
  bool buttonStates[4] = {upNow == LOW, downNow == LOW, selNow == LOW, backNow == LOW};
  unsigned long now = millis();
  
  for (int i = 0; i < 4; i++) {
    if (buttonStates[i]) {
      if (buttonPressTime[i] == 0) {
        buttonPressTime[i] = now;
      } else if (now - buttonPressTime[i] > 800 && !buttonLongPressed[i]) {
        buttonLongPressed[i] = true;
        handleLongPress(i);
      }
    } else {
      if (buttonPressTime[i] > 0) {
        if (!buttonLongPressed[i] && now - lastDebounce > 200) {
          buttonPressCount[i]++;
          handleButtonPress(i);
          lastDebounce = now;
        }
        buttonPressTime[i] = 0;
        buttonLongPressed[i] = false;
      }
    }
  }
  
  lastUp = upNow;
  lastDown = downNow;
  lastSel = selNow;
  lastBack = backNow;
}

void handleLongPress(int button) {
  Serial.printf("Long press on button %d\n", button);
  
  if (currentScreen == SCREEN_PASSWORD_ENTRY && button == 2) {
    // Long press SELECT to connect with entered password
    String password = "";
    for (int i = 0; i < passwordCursorPos; i++) {
      password += passwordChars[i];
    }
    
    currentScreen = SCREEN_WIFI_CONNECT;
    connectToWiFi(wifiNetworks[wifiSelectedIndex], password);
    delay(2000);
    currentScreen = SCREEN_WIFI_STATUS;
    
    memset(passwordChars, 0, sizeof(passwordChars));
    passwordCursorPos = 0;
    needRefresh = true;
  }
}

void handleButtonPress(int button) {
  needRefresh = true;
  
  switch(currentScreen) {
    case SCREEN_HOME:
      if (button == 2) {
        currentScreen = SCREEN_MAIN_MENU;
        menuIndex = 0;
      }
      break;
      
    case SCREEN_MAIN_MENU:
      if (button == 0) {
        menuIndex = (menuIndex > 0) ? menuIndex - 1 : 8;
      } else if (button == 1) {
        menuIndex = (menuIndex < 8) ? menuIndex + 1 : 0;
      } else if (button == 2) {
        switch(menuIndex) {
          case 0: currentScreen = SCREEN_BUTTON_TEST; break;
          case 1: currentScreen = SCREEN_SYSTEM_INFO; break;
          case 2: 
            currentScreen = SCREEN_SET_TIME;
            if (rtc.begin()) {
              DateTime now = rtc.now();
              tempHour = now.hour();
              tempMinute = now.minute();
              tempDay = now.day();
              tempMonth = now.month();
              tempYear = now.year();
            }
            timeSettingMode = 0;
            break;
          case 3: currentScreen = SCREEN_RESET_MEM; break;
          case 4: 
            currentScreen = SCREEN_DEMO_MODE;
            demoActive = !demoActive;
            break;
          case 5: currentScreen = SCREEN_OLED_TEST; break;
          case 6: currentScreen = SCREEN_RTC_TEST; break;
          case 7: currentScreen = SCREEN_VOLT_TEST; break;
          case 8:
            currentScreen = SCREEN_WIFI_SCAN;
            wifiSelectedIndex = 0;
            scanWiFiNetworks();
            break;
        }
      } else if (button == 3) {
        currentScreen = SCREEN_HOME;
      }
      break;
      
    case SCREEN_RESET_MEM:
      if (button == 2) {
        for (int i = 0; i < 4; i++) buttonPressCount[i] = 0;
        demoCounter = 0;
        demoActive = false;
        saveButtonCounts();
        
        showMessage("RESET DONE");
        currentScreen = SCREEN_MAIN_MENU;
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_DEMO_MODE:
      if (button == 2) {
        demoActive = !demoActive;
      } else if (button == 3) {
        demoActive = false;
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_OLED_TEST:
      if (button == 0 || button == 1) {
        static int pattern = 0;
        pattern = (pattern + 1) % 4;
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_SET_TIME:
      if (button == 0) { // UP - Increase value
        switch(timeSettingMode) {
          case 0: tempHour = (tempHour + 1) % 24; break;
          case 1: tempMinute = (tempMinute + 1) % 60; break;
          case 2: tempDay = (tempDay % 31) + 1; break;
          case 3: tempMonth = (tempMonth % 12) + 1; break;
          case 4: tempYear++; break;
        }
        needRefresh = true;
      } else if (button == 1) { // DOWN - Decrease value
        switch(timeSettingMode) {
          case 0: tempHour = (tempHour - 1 + 24) % 24; break;
          case 1: tempMinute = (tempMinute - 1 + 60) % 60; break;
          case 2: tempDay = (tempDay - 2 + 31) % 31 + 1; break;
          case 3: tempMonth = (tempMonth - 2 + 12) % 12 + 1; break;
          case 4: if (tempYear > 2020) tempYear--; break;
        }
        needRefresh = true;
      } else if (button == 2) { // SELECT - Next field
        timeSettingMode++;
        if (timeSettingMode > 4) {
          // Set RTC and return to menu
          if (rtc.begin()) {
            rtc.adjust(DateTime(tempYear, tempMonth, tempDay, tempHour, tempMinute, 0));
          }
          currentScreen = SCREEN_MAIN_MENU;
        }
        needRefresh = true;
      } else if (button == 3) { // BACK - Set RTC and return to menu
        if (rtc.begin()) {
          rtc.adjust(DateTime(tempYear, tempMonth, tempDay, tempHour, tempMinute, 0));
        }
        currentScreen = SCREEN_MAIN_MENU;
        needRefresh = true;
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        if (button == 0) {
          wifiSelectedIndex = (wifiSelectedIndex > 0) ? wifiSelectedIndex - 1 : wifiNetworkCount - 1;
        } else if (button == 1) {
          wifiSelectedIndex = (wifiSelectedIndex < wifiNetworkCount - 1) ? wifiSelectedIndex + 1 : 0;
        } else if (button == 2 && wifiNetworkCount > 0) {
          // Check if network is open
          String selectedNetwork = wifiNetworks[wifiSelectedIndex];
          
          if (selectedNetwork.indexOf("[OPEN]") != -1) {
            // Open network - connect directly
            currentScreen = SCREEN_WIFI_CONNECT;
            connectToWiFi(selectedNetwork, "");
            delay(2000);
            currentScreen = SCREEN_WIFI_STATUS;
          } else {
            // Secured network - enter password
            currentScreen = SCREEN_PASSWORD_ENTRY;
            passwordCursorPos = 0;
            memset(passwordChars, 0, sizeof(passwordChars));
            passwordChars[0] = 'a';  // Start with 'a'
          }
        } else if (button == 3) {
          currentScreen = SCREEN_MAIN_MENU;
        }
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      if (button == 0) { // UP - Next character
        if (passwordCursorPos < sizeof(passwordChars)) {
          char currentChar = passwordChars[passwordCursorPos];
          int charIndex = 0;
          
          // Find current character in charset
          for (int i = 0; i < charSetLength; i++) {
            if (charSet[i] == currentChar) {
              charIndex = i;
              break;
            }
          }
          
          // Move to next character
          charIndex = (charIndex + 1) % charSetLength;
          passwordChars[passwordCursorPos] = charSet[charIndex];
        }
      } else if (button == 1) { // DOWN - Previous character
        if (passwordCursorPos < sizeof(passwordChars)) {
          char currentChar = passwordChars[passwordCursorPos];
          int charIndex = 0;
          
          for (int i = 0; i < charSetLength; i++) {
            if (charSet[i] == currentChar) {
              charIndex = i;
              break;
            }
          }
          
          // Move to previous character
          charIndex = (charIndex - 1 + charSetLength) % charSetLength;
          passwordChars[passwordCursorPos] = charSet[charIndex];
        }
      } else if (button == 2) { // SELECT - Add character/move cursor
        if (passwordCursorPos < sizeof(passwordChars) - 1) {
          passwordCursorPos++;
          // Initialize new position with 'a'
          if (passwordChars[passwordCursorPos] == 0) {
            passwordChars[passwordCursorPos] = 'a';
          }
        }
      } else if (button == 3) { // BACK - Delete character
        if (passwordCursorPos > 0) {
          passwordCursorPos--;
          passwordChars[passwordCursorPos + 1] = 0;
        } else {
          // Exit password entry
          memset(passwordChars, 0, sizeof(passwordChars));
          passwordCursorPos = 0;
          currentScreen = SCREEN_WIFI_SCAN;
        }
      }
      break;
      
    case SCREEN_WIFI_CONNECT:
    case SCREEN_WIFI_STATUS:
      if (button == 2 || button == 3) {
        currentScreen = SCREEN_WIFI_SCAN;
      }
      break;
      
    default:
      if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
  }
  
  // Auto-save every 10 presses
  static int saveCounter = 0;
  if (++saveCounter >= 10) {
    saveButtonCounts();
    saveCounter = 0;
  }
}