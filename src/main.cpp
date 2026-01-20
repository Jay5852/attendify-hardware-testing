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
  SCREEN_PASSWORD_ENTRY,  // New screen
  SCREEN_WIFI_CONNECT,
  SCREEN_WIFI_STATUS
};

// =================== GLOBAL VARIABLES ===================
ScreenState currentScreen = SCREEN_BOOT;
int menuIndex = 0;
bool needRefresh = true;
bool displayInitialized = false;

// Button tracking
int buttonPressCount[4] = {0, 0, 0, 0};
unsigned long buttonPressTime[4] = {0, 0, 0, 0};
bool buttonLongPressed[4] = {false, false, false, false};

// Demo mode
bool demoActive = false;
int demoCounter = 0;

// WiFi
String wifiNetworks[20];
int wifiNetworkCount = 0;
int wifiSelectedIndex = 0;
bool wifiScanning = false;
bool wifiConnecting = false;
String connectedSSID = "";

// Password entry
String wifiPassword = "";
bool passwordEntryMode = false;
int passwordCursorPos = 0;
char passwordChars[63] = {0};  // Max WPA2 password length
int charSetIndex = 0;

// Character set for password entry
const char* charSet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+[]{}|;:,.<>?";
int charSetLength = 84;  // Length of charSet string

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
void drawFooter();
void checkButtons();
void handleButtonPress(int button);
void handleLongPress(int button);
void scanWiFiNetworks();
void connectToWiFi(String ssid, String password);

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

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 System ===");
  
  // Initialize hardware
  initializeHardware();
  
  // Initialize preferences
  initializePreferences();
  loadButtonCounts();
  
  // Show boot screen
  currentScreen = SCREEN_BOOT;
  showScreen();
  delay(1500);
  
  // Go to home screen
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
  
  // Update screen
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 200 || needRefresh) {
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
  
  // Clear network list
  for (int i = 0; i < 20; i++) {
    wifiNetworks[i] = "";
  }
  
  // Enable WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.println("Scanning for WiFi...");
  
  // Scan networks
  int16_t n = WiFi.scanNetworks();
  
  Serial.printf("Found %d networks\n", n);
  
  for (int i = 0; i < n && wifiNetworkCount < 20; i++) {
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    
    // Skip empty or very weak networks
    if (ssid.length() == 0 || rssi < -95) continue;
    
    // Add security indicator to SSID
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
    } else {
      security = " [SECURE]";
    }
    
    // Truncate long SSIDs
    String displayName = ssid;
    if (ssid.length() > 12) {
      displayName = ssid.substring(0, 9) + "...";
    }
    
    wifiNetworks[wifiNetworkCount] = displayName + security;
    wifiNetworkCount++;
    
    Serial.printf("%d: %s (%d dBm) Auth: %d\n", wifiNetworkCount, ssid.c_str(), rssi, auth);
  }
  
  WiFi.scanDelete();
  wifiScanning = false;
  
  if (wifiNetworkCount == 0) {
    Serial.println("No networks found. Ensure hotspot is ON and visible.");
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
  Serial.printf("Password: %s\n", password.c_str());
  
  // Connect with password
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (attempts < 20) {
    if (WiFi.status() == WL_CONNECTED) {
      connectedSSID = ssid;
      Serial.println("Connected!");
      wifiConnecting = false;
      needRefresh = true;
      break;
    }
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connection failed!");
    wifiConnecting = false;
    needRefresh = true;
  }
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
  }
  
  // Draw footer (button hints)
  drawFooter();
  display.display();
}

void drawFooter() {
  // Small font for button hints (at bottom)
  display.setTextSize(1);
  
  switch(currentScreen) {
    case SCREEN_HOME:
      display.setCursor(50, 56);
      display.print("SEL=Menu");
      break;
      
    case SCREEN_MAIN_MENU:
      display.setCursor(0, 56);
      display.print("U/D=Nav");
      display.setCursor(50, 56);
      display.print("S=Select");
      display.setCursor(90, 56);
      display.print("B=Back");
      break;
      
    case SCREEN_RESET_MEM:
      display.setCursor(10, 56);
      display.print("S=Confirm");
      display.setCursor(80, 56);
      display.print("B=Cancel");
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        display.setCursor(0, 56);
        display.print("S=Connect");
        display.setCursor(80, 56);
        display.print("B=Menu");
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      display.setCursor(0, 56);
      display.print("U/D:Char");
      display.setCursor(50, 56);
      display.print("S=Add");
      display.setCursor(80, 56);
      display.print("B=Del");
      display.setCursor(110, 56);
      display.print("L=C");
      break;
      
    case SCREEN_DEMO_MODE:
      display.setCursor(40, 56);
      display.print("S=Toggle");
      break;
      
    default:
      if (currentScreen != SCREEN_BOOT && currentScreen != SCREEN_HOME) {
        display.setCursor(50, 56);
        display.print("B=Menu");
      }
      break;
  }
}

void drawBootScreen() {
  display.setCursor(30, 20);
  display.setTextSize(2);
  display.println("SYSTEM");
  display.setCursor(45, 40);
  display.println("TEST");
  display.setTextSize(1);
  
  display.setCursor(40, 56);
  display.print("v1.2");
}

void drawHomeScreen() {
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Top: Day and date (small font)
    display.setCursor(10, 0);
    display.printf("%s %02d %s", 
                  dayNames[now.dayOfTheWeek()], 
                  now.day(),
                  monthNames[now.month()-1]);
    
    // Center: Large time
    display.setCursor(20, 15);
    display.setTextSize(3);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    // Bottom: Year
    display.setCursor(55, 45);
    display.printf("20%02d", now.year() % 100);
  } else {
    display.setCursor(30, 25);
    display.println("RTC NOT");
    display.setCursor(35, 40);
    display.println("FOUND");
  }
  
  // WiFi indicator if connected
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(100, 0);
    display.print("WiFi");
  }
}

void drawMainMenu() {
  display.setCursor(40, 2);
  display.println("MAIN MENU");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  
  String menuItems[10] = {
    "1. BUTTON TEST",
    "2. SYSTEM INFO",
    "3. RESET MEMORY",
    "4. DEMO MODE",
    "5. OLED TEST",
    "6. RTC TEST",
    "7. VOLTAGE TEST",
    "8. WIFI SCAN",
    "9. SET TIME",
    "10. ABOUT"
  };
  
  // Show 4 items at a time
  int startIndex = 0;
  if (menuIndex > 3) {
    startIndex = menuIndex - 3;
  }
  
  for (int i = 0; i < 4 && (startIndex + i) < 10; i++) {
    int yPos = 15 + (i * 12);
    int idx = startIndex + i;
    
    if (idx == menuIndex) {
      display.fillRect(0, yPos - 2, 128, 12, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    display.print(menuItems[idx]);
    
    if (idx == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
  
  // Scroll indicators
  if (menuIndex > 3) {
    display.setCursor(120, 15);
    display.print("^");
  }
  if (menuIndex < 6) {
    display.setCursor(120, 55);
    display.print("v");
  }
}

void drawButtonTest() {
  display.setCursor(35, 2);
  display.println("BUTTON TEST");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(10, 20);
  display.print("UP:    ");
  display.print(buttonPressCount[0]);
  
  display.setCursor(10, 30);
  display.print("DOWN:  ");
  display.print(buttonPressCount[1]);
  
  display.setCursor(10, 40);
  display.print("SELECT:");
  display.print(buttonPressCount[2]);
  
  display.setCursor(10, 50);
  display.print("BACK:  ");
  display.print(buttonPressCount[3]);
}

void drawSystemInfo() {
  display.setCursor(35, 2);
  display.println("SYSTEM INFO");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(10, 20);
  display.print("ESP32");
  
  display.setCursor(10, 30);
  display.print("Freq: ");
  display.print(getCpuFrequencyMhz());
  display.print(" MHz");
  
  display.setCursor(10, 40);
  display.print("Heap: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.print(" KB");
  
  display.setCursor(10, 50);
  display.print("Flash: ");
  display.print(ESP.getFlashChipSize() / (1024 * 1024));
  display.print(" MB");
}

void drawResetMem() {
  display.setCursor(40, 2);
  display.println("RESET DATA");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(5, 20);
  display.print("This will reset");
  display.setCursor(5, 30);
  display.print("all counters to");
  display.setCursor(5, 40);
  display.print("zero.");
  
  display.setCursor(20, 50);
  display.print("Press SEL");
}

void drawDemoMode() {
  display.setCursor(40, 2);
  display.println("DEMO MODE");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(50, 25);
  display.setTextSize(2);
  display.printf("%04d", demoCounter);
  display.setTextSize(1);
  
  if (demoActive) {
    display.setCursor(45, 45);
    display.print("RUNNING");
  } else {
    display.setCursor(45, 45);
    display.print("PAUSED");
  }
}

void drawOledTest() {
  display.setCursor(40, 2);
  display.println("OLED TEST");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  static int pattern = 0;
  
  display.setCursor(45, 25);
  display.print("Pattern ");
  display.print(pattern + 1);
  
  switch(pattern % 4) {
    case 0:
      display.fillRect(30, 35, 68, 10, SH110X_WHITE);
      break;
    case 1:
      display.fillCircle(64, 40, 12, SH110X_WHITE);
      break;
    case 2:
      display.fillTriangle(30, 45, 64, 30, 98, 45, SH110X_WHITE);
      break;
    case 3:
      for (int i = 0; i < 128; i += 6) {
        display.drawLine(i, 35, i, 50, SH110X_WHITE);
      }
      break;
  }
}

void drawRtcTest() {
  display.setCursor(45, 2);
  display.println("RTC TEST");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    display.setCursor(10, 20);
    display.print("Date: ");
    display.print(now.year());
    display.print("-");
    display.print(now.month());
    display.print("-");
    display.print(now.day());
    
    display.setCursor(10, 30);
    display.print("Time: ");
    display.print(now.hour());
    display.print(":");
    display.print(now.minute());
    display.print(":");
    display.print(now.second());
    
    display.setCursor(10, 40);
    display.print("Temp: ");
    display.print(rtc.getTemperature(), 1);
    display.print(" C");
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
  
  int adcValue = analogRead(34);
  float voltage = adcValue * (3.3 / 4095.0);
  
  display.setCursor(20, 25);
  display.print("ADC: ");
  display.print(adcValue);
  
  display.setCursor(20, 35);
  display.print("Voltage: ");
  display.print(voltage, 2);
  display.print("V");
  
  display.setCursor(20, 45);
  if (voltage > 3.0) {
    display.print("BATTERY OK");
  } else if (voltage > 2.5) {
    display.print("LOW BATTERY");
  } else {
    display.print("CRITICAL!");
  }
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
    display.setCursor(15, 25);
    display.println("No networks");
    display.setCursor(5, 40);
    display.println("Turn ON hotspot");
    display.setCursor(5, 50);
    display.println("and try again");
  } else {
    // Show 3 networks at a time (smaller font)
    int startIndex = 0;
    if (wifiSelectedIndex > 2) {
      startIndex = wifiSelectedIndex - 2;
    }
    
    for (int i = 0; i < 3 && (startIndex + i) < wifiNetworkCount; i++) {
      int yPos = 12 + (i * 16);  // More spacing
      int idx = startIndex + i;
      
      if (idx == wifiSelectedIndex) {
        display.fillRect(0, yPos - 1, 128, 15, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      
      display.setCursor(2, yPos);
      
      // Show network with security indicator
      String displayText = String(idx + 1) + ". " + wifiNetworks[idx];
      if (displayText.length() > 20) {
        displayText = displayText.substring(0, 17) + "...";
      }
      display.print(displayText);
      
      if (idx == wifiSelectedIndex) {
        display.setTextColor(SH110X_WHITE);
      }
    }
    
    // Scroll indicators (small)
    if (wifiSelectedIndex > 2) {
      display.setCursor(122, 12);
      display.print("^");
    }
    if (wifiSelectedIndex < wifiNetworkCount - 1) {
      display.setCursor(122, 56);
      display.print("v");
    }
    
    // Show count
    display.setCursor(100, 2);
    display.printf("%d/%d", wifiSelectedIndex + 1, wifiNetworkCount);
  }
}

void drawPasswordEntryScreen() {
  display.setCursor(30, 2);
  display.println("ENTER PASSWORD");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Show selected network (truncated)
  String displaySSID = wifiNetworks[wifiSelectedIndex];
  // Remove security info for display
  int bracketPos = displaySSID.indexOf(" [");
  if (bracketPos != -1) {
    displaySSID = displaySSID.substring(0, bracketPos);
  }
  
  display.setCursor(5, 18);
  display.print("For: ");
  if (displaySSID.length() > 16) {
    display.print(displaySSID.substring(0, 13) + "...");
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
  
  // Show current character
  display.setCursor(5, 45);
  display.print("Char: ");
  if (passwordCursorPos < sizeof(passwordChars) && passwordChars[passwordCursorPos] != 0) {
    display.print(passwordChars[passwordCursorPos]);
  } else {
    display.print("a");
  }
  
  // Show password length
  display.setCursor(70, 45);
  display.print("Len: ");
  display.print(passwordCursorPos);
}

void drawWifiConnectScreen() {
  display.setCursor(30, 2);
  display.println("CONNECTING");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(40, 30);
  
  if (wifiConnecting) {
    static int dots = 0;
    display.print("PLEASE WAIT");
    for (int i = 0; i < dots; i++) display.print(".");
    dots = (dots + 1) % 4;
  } else {
    display.print("COMPLETE!");
  }
  
  if (wifiSelectedIndex < wifiNetworkCount) {
    display.setCursor(10, 45);
    display.print("To: ");
    String ssid = wifiNetworks[wifiSelectedIndex];
    int bracketPos = ssid.indexOf(" [");
    if (bracketPos != -1) {
      ssid = ssid.substring(0, bracketPos);
    }
    if (ssid.length() > 15) {
      display.print(ssid.substring(0, 12) + "...");
    } else {
      display.print(ssid);
    }
  }
}

void drawWifiStatusScreen() {
  display.setCursor(40, 2);
  display.println("WIFI STATUS");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(20, 25);
    display.print("Connected!");
    
    display.setCursor(5, 35);
    display.print("SSID: ");
    String ssid = connectedSSID;
    if (ssid.length() > 15) {
      ssid = ssid.substring(0, 12) + "...";
    }
    display.print(ssid);
    
    display.setCursor(5, 45);
    display.print("IP: ");
    String ip = WiFi.localIP().toString();
    if (ip.length() > 15) {
      ip = ip.substring(0, 12) + "...";
    }
    display.print(ip);
  } else {
    display.setCursor(25, 25);
    display.print("Not Connected");
    
    display.setCursor(30, 40);
    display.print("Try Again");
  }
}

// =================== BUTTON HANDLING ===================
void checkButtons() {
  static bool lastUp = HIGH, lastDown = HIGH, lastSel = HIGH, lastBack = HIGH;
  static unsigned long lastDebounce = 0;
  
  bool upNow = digitalRead(BUTTON_UP);
  bool downNow = digitalRead(BUTTON_DOWN);
  bool selNow = digitalRead(BUTTON_SELECT);
  bool backNow = digitalRead(BUTTON_BACK);
  
  // Check for button presses with debouncing
  bool buttonStates[4] = {upNow == LOW, downNow == LOW, selNow == LOW, backNow == LOW};
  unsigned long now = millis();
  
  for (int i = 0; i < 4; i++) {
    if (buttonStates[i]) {
      if (buttonPressTime[i] == 0) {
        // First press detection
        buttonPressTime[i] = now;
      } else if (now - buttonPressTime[i] > 1000 && !buttonLongPressed[i]) {
        // Long press detected (1 second)
        buttonLongPressed[i] = true;
        handleLongPress(i);
      }
    } else {
      if (buttonPressTime[i] > 0) {
        if (!buttonLongPressed[i] && now - lastDebounce > 200) {
          // Short press detected
          buttonPressCount[i]++;
          handleButtonPress(i);
          lastDebounce = now;
        }
        // Reset button tracking
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
    
    // Clear password
    memset(passwordChars, 0, sizeof(passwordChars));
    passwordCursorPos = 0;
    needRefresh = true;
  }
}

void handleButtonPress(int button) {
  needRefresh = true;
  
  switch(currentScreen) {
    case SCREEN_HOME:
      if (button == 2) {  // SELECT -> Menu
        currentScreen = SCREEN_MAIN_MENU;
        menuIndex = 0;
      }
      break;
      
    case SCREEN_MAIN_MENU:
      if (button == 0) {  // UP
        menuIndex = (menuIndex > 0) ? menuIndex - 1 : 9;
      } else if (button == 1) {  // DOWN
        menuIndex = (menuIndex < 9) ? menuIndex + 1 : 0;
      } else if (button == 2) {  // SELECT
        switch(menuIndex) {
          case 0: currentScreen = SCREEN_BUTTON_TEST; break;
          case 1: currentScreen = SCREEN_SYSTEM_INFO; break;
          case 2: currentScreen = SCREEN_RESET_MEM; break;
          case 3: 
            currentScreen = SCREEN_DEMO_MODE;
            demoActive = !demoActive;
            break;
          case 4: currentScreen = SCREEN_OLED_TEST; break;
          case 5: currentScreen = SCREEN_RTC_TEST; break;
          case 6: currentScreen = SCREEN_VOLT_TEST; break;
          case 7:
            currentScreen = SCREEN_WIFI_SCAN;
            wifiSelectedIndex = 0;
            scanWiFiNetworks();
            break;
          case 8:  // SET TIME - Placeholder
            currentScreen = SCREEN_HOME;
            break;
          case 9:  // ABOUT - Placeholder
            currentScreen = SCREEN_HOME;
            break;
        }
      } else if (button == 3) {  // BACK -> Home
        currentScreen = SCREEN_HOME;
      }
      break;
      
    case SCREEN_RESET_MEM:
      if (button == 2) {  // SELECT - Reset
        for (int i = 0; i < 4; i++) buttonPressCount[i] = 0;
        demoCounter = 0;
        demoActive = false;
        saveButtonCounts();
        
        // Show confirmation
        display.clearDisplay();
        display.setCursor(40, 30);
        display.println("RESET DONE");
        display.display();
        delay(1000);
        
        currentScreen = SCREEN_MAIN_MENU;
        needRefresh = true;
      } else if (button == 3) {  // BACK
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_DEMO_MODE:
      if (button == 2) {  // SELECT - Toggle
        demoActive = !demoActive;
      } else if (button == 3) {  // BACK
        demoActive = false;
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_OLED_TEST:
      if (button == 0 || button == 1) {  // UP/DOWN - Change pattern
        // Pattern changes handled in draw function
      } else if (button == 3) {  // BACK
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        if (button == 0) {  // UP
          wifiSelectedIndex = (wifiSelectedIndex > 0) ? wifiSelectedIndex - 1 : wifiNetworkCount - 1;
        } else if (button == 1) {  // DOWN
          wifiSelectedIndex = (wifiSelectedIndex < wifiNetworkCount - 1) ? wifiSelectedIndex + 1 : 0;
        } else if (button == 2 && wifiNetworkCount > 0) {  // SELECT - Connect
          // Check if network is open (no password needed)
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
            passwordEntryMode = true;
            passwordCursorPos = 0;
            memset(passwordChars, 0, sizeof(passwordChars));
            passwordChars[0] = 'a';  // Start with 'a'
          }
        } else if (button == 3) {  // BACK
          currentScreen = SCREEN_MAIN_MENU;
        }
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      if (button == 0) {  // UP - Next character
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
      } else if (button == 1) {  // DOWN - Previous character
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
      } else if (button == 2) {  // SELECT - Add character/move cursor
        if (passwordCursorPos < sizeof(passwordChars) - 1) {
          passwordCursorPos++;
          // Initialize new position with 'a'
          if (passwordChars[passwordCursorPos] == 0) {
            passwordChars[passwordCursorPos] = 'a';
          }
        }
      } else if (button == 3) {  // BACK - Delete character
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
      if (button == 2 || button == 3) {  // SELECT or BACK
        currentScreen = SCREEN_WIFI_SCAN;
      }
      break;
      
    default:
      if (button == 3) {  // BACK -> Menu (default)
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