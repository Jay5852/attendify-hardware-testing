/**
 * ESP32 OLED SYSTEM - COMPLETE FIXED VERSION
 * SH1106 OLED (128x64) + DS3231 RTC + 4 Buttons
 * FIXED: WiFi scanning and connection for both open and encrypted networks
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

// =================== DISPLAY CONSTANTS ===================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDR 0x3C

// =================== OLED SETUP ===================
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =================== RTC SETUP ===================
RTC_DS3231 rtc;

// =================== PREFERENCES ===================
Preferences preferences;

// =================== SCREEN STATES ===================
enum ScreenState {
  SCREEN_BOOT,
  SCREEN_HOME,
  SCREEN_MAIN_MENU,
  SCREEN_BUTTON_TEST,
  SCREEN_SYSTEM_INFO,
  SCREEN_SET_TIME,
  SCREEN_RESET_MEM,
  SCREEN_DEMO_MODE,
  SCREEN_OLED_TEST,
  SCREEN_RTC_TEST,
  SCREEN_VOLT_TEST,
  SCREEN_WIFI_SCAN,
  SCREEN_WIFI_CONNECT,
  SCREEN_WIFI_STATUS
};

// =================== WIFI NETWORK STRUCTURE ===================
struct WiFiNetwork {
  String ssid;
  int32_t rssi;
  uint8_t encryptionType;
  bool isOpen;
};

// =================== GLOBAL VARIABLES ===================
ScreenState currentScreen = SCREEN_BOOT;
int menuIndex = 0;
unsigned long lastButtonPress = 0;
unsigned long lastUpdate = 0;
bool needRefresh = true;

// Button tracking
int buttonPressCount[4] = {0, 0, 0, 0};
bool buttonStates[4] = {false, false, false, false};

// Demo mode
bool demoActive = false;
unsigned long demoStartTime = 0;
int demoCounter = 0;

// OLED test
int oledTestPattern = 0;

// WiFi variables - FIXED STRUCTURE
WiFiNetwork wifiNetworks[20];
int wifiNetworkCount = 0;
int wifiSelectedIndex = 0;
bool wifiScanning = false;
bool wifiRefreshRequested = false;
String connectedSSID = "";
bool wifiConnecting = false;
unsigned long wifiScanStart = 0;
const int WIFI_SCAN_INTERVAL = 15000; // 15 seconds

// *** SET YOUR HOTSPOT PASSWORD HERE ***
String savedPassword ="";  // Leave empty for open networks, or set "YourPassword123"

// Date/time
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
String getEncryptionType(uint8_t encType);
void scanWiFiNetworks();
void connectToWiFi(String ssid);

// Screen functions
void drawBootScreen();
void drawHomeScreen();
void drawMainMenu();
void drawButtonTest();
void drawSystemInfo();
void drawSetTime();
void drawResetMem();
void drawDemoMode();
void drawOledTest();
void drawRtcTest();
void drawVoltTest();
void drawWifiScanScreen();
void drawWifiConnectScreen();
void drawWifiStatusScreen();

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESP32 OLED System ===");
  Serial.println("Version: 6.0 - WiFi Fixed");
  
  setCpuFrequencyMhz(80);
  Serial.printf("CPU: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  
  initializeHardware();
  initializePreferences();
  loadButtonCounts();
  
  currentScreen = SCREEN_BOOT;
  showScreen();
  delay(1500);
  
  currentScreen = SCREEN_HOME;
  needRefresh = true;
  
  Serial.println("System ready");
  
  // Test WiFi
  WiFi.mode(WIFI_STA);
  delay(100);
  Serial.printf("WiFi MAC: %s\n", WiFi.macAddress().c_str());
  WiFi.mode(WIFI_OFF);
}

// =================== PREFERENCES ===================
void initializePreferences() {
  preferences.begin("oled_system", false);
  Serial.println("Preferences initialized");
}

void saveButtonCounts() {
  preferences.putInt("btn_up", buttonPressCount[0]);
  preferences.putInt("btn_down", buttonPressCount[1]);
  preferences.putInt("btn_sel", buttonPressCount[2]);
  preferences.putInt("btn_back", buttonPressCount[3]);
  preferences.putInt("demo_counter", demoCounter);
  Serial.println("Data saved");
}

void loadButtonCounts() {
  buttonPressCount[0] = preferences.getInt("btn_up", 0);
  buttonPressCount[1] = preferences.getInt("btn_down", 0);
  buttonPressCount[2] = preferences.getInt("btn_sel", 0);
  buttonPressCount[3] = preferences.getInt("btn_back", 0);
  demoCounter = preferences.getInt("demo_counter", 0);
  
  Serial.printf("Loaded: U:%d D:%d S:%d B:%d\n",
                buttonPressCount[0], buttonPressCount[1], 
                buttonPressCount[2], buttonPressCount[3]);
}

// =================== HARDWARE INIT ===================
void initializeHardware() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);
  
  Serial.println("Initializing OLED...");
  bool oledFound = false;
  if (display.begin(OLED_ADDR, true)) {
    oledFound = true;
    Serial.println("OLED found at 0x3C");
  } else if (display.begin(0x3D, true)) {
    oledFound = true;
    Serial.println("OLED found at 0x3D");
  }
  
  if (!oledFound) {
    Serial.println("OLED not found!");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setRotation(0);
    display.setContrast(255);
    Serial.println("OLED ready");
  }
  
  if (rtc.begin()) {
    if (rtc.lostPower()) {
      Serial.println("RTC: Setting time");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println("RTC ready");
  } else {
    Serial.println("RTC not found");
  }
  
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  
  Serial.println("Hardware ready");
}

// =================== WIFI HELPER ===================
String getEncryptionType(uint8_t encType) {
  switch(encType) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default: return "UNK";
  }
}

// =================== WIFI SCAN - FIXED ===================
void scanWiFiNetworks() {
  wifiScanning = true;
  wifiNetworkCount = 0;
  wifiScanStart = millis();
  
  // Clear list
  for (int i = 0; i < 20; i++) {
    wifiNetworks[i].ssid = "";
    wifiNetworks[i].rssi = 0;
    wifiNetworks[i].encryptionType = 0;
    wifiNetworks[i].isOpen = false;
  }
  
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
  
  Serial.println("\n=== WIFI SCAN ===");
  
  // Scan with show_hidden=true
  int n = WiFi.scanNetworks(false, true, false, 300);
  
  Serial.printf("Found %d networks\n", n);
  
  if (n > 0) {
    for (int i = 0; i < n && wifiNetworkCount < 20; i++) {
      String ssid = WiFi.SSID(i);
      
      if (ssid.length() == 0) {
        Serial.printf("  [%d] Hidden - skipped\n", i);
        continue;
      }
      
      wifiNetworks[wifiNetworkCount].ssid = ssid;
      wifiNetworks[wifiNetworkCount].rssi = WiFi.RSSI(i);
      wifiNetworks[wifiNetworkCount].encryptionType = WiFi.encryptionType(i);
      wifiNetworks[wifiNetworkCount].isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      
      Serial.printf("  [%d] %-20s %4d dBm %s %s\n", 
                    wifiNetworkCount,
                    ssid.c_str(),
                    wifiNetworks[wifiNetworkCount].rssi,
                    getEncryptionType(wifiNetworks[wifiNetworkCount].encryptionType).c_str(),
                    wifiNetworks[wifiNetworkCount].isOpen ? "[OPEN]" : "");
      
      wifiNetworkCount++;
    }
  }
  
  WiFi.scanDelete();
  wifiScanning = false;
  Serial.printf("=== %d networks found ===\n\n", wifiNetworkCount);
  
  if (wifiNetworkCount == 0) {
    Serial.println("TIPS:");
    Serial.println("1. Enable 2.4GHz hotspot (not 5GHz)");
    Serial.println("2. Make hotspot visible");
    Serial.println("3. Keep phone close to ESP32");
  }
}

// =================== WIFI CONNECT - FIXED ===================
void connectToWiFi(String ssid) {
  // Find network in scan results
  int networkIndex = -1;
  for (int i = 0; i < wifiNetworkCount; i++) {
    if (wifiNetworks[i].ssid == ssid) {
      networkIndex = i;
      break;
    }
  }
  
  if (networkIndex == -1) {
    Serial.println("ERROR: Network not found!");
    wifiConnecting = false;
    return;
  }
  
  wifiConnecting = true;
  connectedSSID = "";
  
  WiFiNetwork &network = wifiNetworks[networkIndex];
  
  Serial.println("\n=== CONNECTING ===");
  Serial.printf("SSID: %s\n", network.ssid.c_str());
  Serial.printf("Type: %s\n", getEncryptionType(network.encryptionType).c_str());
  Serial.printf("Signal: %d dBm\n", network.rssi);
  Serial.printf("Open: %s\n", network.isOpen ? "YES" : "NO");
  
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(100);
  
  if (network.isOpen) {
    // OPEN NETWORK - no password
    Serial.println("Connecting to OPEN network (no password)...");
    WiFi.begin(network.ssid.c_str());
  } else {
    // ENCRYPTED NETWORK - need password
    Serial.println("Network is ENCRYPTED");
    
    if (savedPassword.length() > 0) {
      Serial.printf("Using saved password (%d chars)\n", savedPassword.length());
      WiFi.begin(network.ssid.c_str(), savedPassword.c_str());
    } else {
      Serial.println("\nERROR: No password set!");
      Serial.println("Set password in code:");
      Serial.println("  String savedPassword = \"YourPassword\";");
      wifiConnecting = false;
      return;
    }
  }
  
  // Wait for connection (15 seconds)
  int attempts = 0;
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  // Check result
  if (WiFi.status() == WL_CONNECTED) {
    connectedSSID = network.ssid;
    Serial.println("\n*** SUCCESS! ***");
    Serial.printf("SSID: %s\n", connectedSSID.c_str());
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    currentScreen = SCREEN_WIFI_STATUS;
  } else {
    Serial.println("\n*** FAILED! ***");
    if (!network.isOpen) {
      Serial.println("Reasons:");
      Serial.println("- Wrong password");
      Serial.println("- WPA3 not supported");
      Serial.println("- Too many attempts");
    } else {
      Serial.println("Reasons:");
      Serial.println("- Signal too weak");
      Serial.println("- Hotspot turned off");
      Serial.println("- Too many devices");
    }
    WiFi.disconnect();
  }
  
  wifiConnecting = false;
  needRefresh = true;
  Serial.println("==================\n");
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  
  if (millis() - lastUpdate > 200 || needRefresh) {
    showScreen();
    lastUpdate = millis();
    needRefresh = false;
  }
  
  if (wifiRefreshRequested) {
    scanWiFiNetworks();
    wifiRefreshRequested = false;
    needRefresh = true;
  }
  
  if (currentScreen == SCREEN_WIFI_SCAN && !wifiScanning) {
    if (millis() - wifiScanStart > WIFI_SCAN_INTERVAL) {
      wifiRefreshRequested = true;
    }
  }
  
  if (currentScreen == SCREEN_DEMO_MODE && demoActive) {
    if (millis() - demoStartTime > 500) {
      demoCounter++;
      if (demoCounter > 999) demoCounter = 0;
      demoStartTime = millis();
      needRefresh = true;
    }
  }
  
  delay(10);
}

// =================== DISPLAY FUNCTIONS ===================
void showScreen() {
  display.clearDisplay();
  
  switch(currentScreen) {
    case SCREEN_BOOT: drawBootScreen(); break;
    case SCREEN_HOME: drawHomeScreen(); break;
    case SCREEN_MAIN_MENU: drawMainMenu(); break;
    case SCREEN_BUTTON_TEST: drawButtonTest(); break;
    case SCREEN_SYSTEM_INFO: drawSystemInfo(); break;
    case SCREEN_SET_TIME: drawSetTime(); break;
    case SCREEN_RESET_MEM: drawResetMem(); break;
    case SCREEN_DEMO_MODE: drawDemoMode(); break;
    case SCREEN_OLED_TEST: drawOledTest(); break;
    case SCREEN_RTC_TEST: drawRtcTest(); break;
    case SCREEN_VOLT_TEST: drawVoltTest(); break;
    case SCREEN_WIFI_SCAN: drawWifiScanScreen(); break;
    case SCREEN_WIFI_CONNECT: drawWifiConnectScreen(); break;
    case SCREEN_WIFI_STATUS: drawWifiStatusScreen(); break;
  }
  
  if (currentScreen != SCREEN_HOME && currentScreen != SCREEN_BOOT) {
    drawFooter();
  }
  
  display.display();
}

void drawFooter() {
  display.setTextSize(1);
  
  switch(currentScreen) {
    case SCREEN_MAIN_MENU:
      display.setCursor(0, 57);
      display.print("U/D:Nav  S:OK  B:Home");
      break;
    case SCREEN_BUTTON_TEST:
      display.setCursor(0, 57);
      display.print("B:Menu  Auto-save");
      break;
    case SCREEN_RESET_MEM:
      display.setCursor(5, 57);
      display.print("S:Confirm  B:Cancel");
      break;
    case SCREEN_DEMO_MODE:
      display.setCursor(20, 57);
      display.print(demoActive ? "S:Stop  B:Menu" : "S:Start  B:Menu");
      break;
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        display.setCursor(5, 57);
        display.print(wifiSelectedIndex == 0 ? "S:Refresh  B:Menu" : "S:Connect  B:Menu");
      } else {
        display.setCursor(40, 57);
        display.print("Scanning...");
      }
      break;
    case SCREEN_WIFI_CONNECT:
      display.setCursor(10, 57);
      display.print("Connecting...  B:Cancel");
      break;
    case SCREEN_WIFI_STATUS:
      display.setCursor(50, 57);
      display.print("S/B:Menu");
      break;
    case SCREEN_OLED_TEST:
      display.setCursor(5, 57);
      display.print("U/D:Change  B:Menu");
      break;
    default:
      display.setCursor(50, 57);
      display.print("B:Menu");
      break;
  }
}

void drawBootScreen() {
  display.setCursor(35, 20);
  display.setTextSize(2);
  display.println("SYSTEM");
  display.setCursor(40, 40);
  display.println("TEST");
  display.setTextSize(1);
  display.setCursor(30, 56);
  display.print("v6.0 Fixed");
}

void drawHomeScreen() {
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    display.setCursor(25, 10);
    display.setTextSize(3);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    display.setCursor(20, 40);
    display.print(dayNames[now.dayOfTheWeek()]);
    display.print(" ");
    display.print(now.day());
    display.print(" ");
    display.print(monthNames[now.month()-1]);
    
    display.setCursor(20, 50);
    display.print("Temp: ");
    display.print((int)rtc.getTemperature());
    display.print("C");
  } else {
    display.setCursor(30, 25);
    display.println("RTC ERROR");
  }
  
  display.setCursor(35, 57);
  display.print("Press SELECT");
}

void drawMainMenu() {
  display.setCursor(40, 2);
  display.println("MENU");
  
  String menuItems[9] = {
    "BUTTON TEST", "SYSTEM INFO", "SET TIME", "RESET DATA",
    "DEMO MODE", "DISPLAY TEST", "RTC TEST", "VOLTAGE TEST", "WIFI SCAN"
  };
  
  int startIdx = (menuIndex > 3) ? menuIndex - 3 : 0;
  
  for (int i = 0; i < 4 && (startIdx + i) < 9; i++) {
    int idx = startIdx + i;
    int yPos = 15 + (i * 10);
    
    if (idx == menuIndex) {
      display.fillRect(0, yPos - 1, 128, 9, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    display.print(idx + 1);
    display.print(". ");
    display.print(menuItems[idx]);
    
    if (idx == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
  
  if (startIdx > 0) {
    display.setCursor(120, 15);
    display.print("^");
  }
  if (startIdx + 4 < 9) {
    display.setCursor(120, 50);
    display.print("v");
  }
}

void drawButtonTest() {
  display.setCursor(40, 2);
  display.println("BUTTONS");
  
  display.setCursor(10, 20);
  display.print("UP:   ");
  display.print(buttonPressCount[0]);
  
  display.setCursor(70, 20);
  display.print("DOWN: ");
  display.print(buttonPressCount[1]);
  
  display.setCursor(10, 35);
  display.print("SEL:  ");
  display.print(buttonPressCount[2]);
  
  display.setCursor(70, 35);
  display.print("BACK: ");
  display.print(buttonPressCount[3]);
  
  display.setCursor(10, 50);
  display.print("DEMO: ");
  display.print(demoCounter);
}

void drawSystemInfo() {
  display.setCursor(40, 2);
  display.println("SYSTEM");
  
  display.setCursor(5, 20);
  display.print("CPU: ");
  display.print(getCpuFrequencyMhz());
  display.print(" MHz");
  
  display.setCursor(5, 30);
  display.print("RAM: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.print(" KB");
  
  display.setCursor(5, 40);
  display.print("CHIP: ESP32");
  
  display.setCursor(5, 50);
  display.print("DISPLAY: OK");
}

void drawSetTime() {
  display.setCursor(40, 2);
  display.println("SET TIME");
  
  display.setCursor(25, 25);
  display.println("Feature");
  display.setCursor(20, 40);
  display.println("Coming Soon");
}

void drawResetMem() {
  display.setCursor(40, 2);
  display.println("RESET");
  
  display.setCursor(5, 20);
  display.println("This will reset all");
  display.setCursor(5, 30);
  display.println("counters to zero.");
  
  display.setCursor(10, 45);
  display.println("Press SELECT to reset");
}

void drawDemoMode() {
  display.setCursor(40, 2);
  display.println("DEMO");
  
  display.setCursor(50, 25);
  display.setTextSize(2);
  display.print(demoCounter);
  display.setTextSize(1);
  
  display.setCursor(demoActive ? 45 : 40, 45);
  display.print(demoActive ? "RUNNING" : "PRESS START");
}

void drawOledTest() {
  display.setCursor(40, 2);
  display.println("DISPLAY");
  
  display.setCursor(40, 20);
  display.print("Pattern ");
  display.print(oledTestPattern + 1);
  
  switch(oledTestPattern % 4) {
    case 0:
      display.fillRect(10, 30, 108, 24, SH110X_WHITE);
      break;
    case 1:
      display.fillCircle(64, 42, 20, SH110X_WHITE);
      break;
    case 2:
      display.fillTriangle(30, 50, 64, 30, 98, 50, SH110X_WHITE);
      break;
    case 3:
      for (int i = 0; i < 8; i++) {
        display.fillRect(10 + (i * 15), 30, 10, 24, SH110X_WHITE);
      }
      break;
  }
}

void drawRtcTest() {
  display.setCursor(40, 2);
  display.println("RTC");
  
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    display.setCursor(10, 20);
    display.printf("Date: %04d-%02d-%02d", now.year(), now.month(), now.day());
    
    display.setCursor(10, 30);
    display.printf("Time: %02d:%02d:%02d", now.hour(), now.minute(), now.second());
    
    display.setCursor(10, 40);
    display.print("Day: ");
    display.print(dayNames[now.dayOfTheWeek()]);
    
    display.setCursor(10, 50);
    display.print("Temp: ");
    display.print(rtc.getTemperature(), 1);
    display.print("C");
  } else {
    display.setCursor(30, 30);
    display.println("RTC ERROR");
  }
}

void drawVoltTest() {
  display.setCursor(40, 2);
  display.println("VOLTAGE");
  
  int adcValue = analogRead(34);
  float voltage = adcValue * (3.3 / 4095.0);
  
  display.setCursor(10, 20);
  display.print("ADC: ");
  display.print(adcValue);
  
  display.setCursor(10, 35);
  display.print("Voltage: ");
  display.print(voltage, 2);
  display.print("V");
  
  display.setCursor(10, 50);
  display.print("BATTERY: ");
  display.print(voltage > 3.0 ? "OK" : (voltage > 2.5 ? "LOW" : "BAD"));
}

void drawWifiScanScreen() {
  display.setCursor(40, 2);
  display.println("WIFI");
  
  if (wifiScanning) {
    display.setCursor(30, 30);
    display.print("SCANNING...");
    display.setCursor(25, 45);
    display.print("Please wait");
    return;
  }
  
  if (wifiNetworkCount == 0) {
    display.setCursor(20, 20);
    display.println("No networks");
    display.setCursor(30, 35);
    display.println("found");
    display.setCursor(5, 50);
    display.print("Check 2.4GHz hotspot");
  } else {
    int displayCount = 0;
    int startY = 15;
    
    // Refresh option
    int yPos = startY;
    if (wifiSelectedIndex == 0) {
      display.fillRect(0, yPos - 1, 128, 9, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    display.setCursor(5, yPos);
    display.print("[Refresh]");
    if (wifiSelectedIndex == 0) display.setTextColor(SH110X_WHITE);
    displayCount++;
    
    // Networks (up to 4)
    for (int i = 0; i < min(wifiNetworkCount, 4); i++) {
      yPos = startY + (displayCount * 10);
      
      if (wifiSelectedIndex == i + 1) {
        display.fillRect(0, yPos - 1, 128, 9, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      
      String displayText = wifiNetworks[i].ssid;
      if (displayText.length() > 13) {
        displayText = displayText.substring(0, 10) + "...";
      }
      
      display.setCursor(5, yPos);
      display.print(displayText);
      
      // Show OPEN indicator
      if (wifiNetworks[i].isOpen) {
        display.setCursor(100, yPos);
        display.print("OPN");
      }
      
      if (wifiSelectedIndex == i + 1) display.setTextColor(SH110X_WHITE);
      displayCount++;
    }
    
    // Network count
    display.setCursor(100, 2);
    display.print(wifiNetworkCount);
  }
}

void drawWifiConnectScreen() {
  display.setCursor(30, 2);
  display.println("CONNECTING");
  
  if (wifiSelectedIndex > 0 && wifiSelectedIndex <= wifiNetworkCount) {
    display.setCursor(5, 20);
    display.print("Network:");
    
    display.setCursor(5, 30);
    String ssid = wifiNetworks[wifiSelectedIndex - 1].ssid;
    if (ssid.length() > 18) {
      ssid = ssid.substring(0, 15) + "...";
    }
    display.print(ssid);
    
    display.setCursor(5, 40);
    if (wifiNetworks[wifiSelectedIndex - 1].isOpen) {
      display.print("Type: OPEN");
    } else {
      display.print("Type: ENCRYPTED");
    }
  }
  
  if (wifiConnecting) {
    display.setCursor(30, 50);
    display.print("Please wait...");
  }
}

void drawWifiStatusScreen() {
  display.setCursor(40, 2);
  display.println("STATUS");
  
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(30, 20);
    display.println("CONNECTED!");
    
    display.setCursor(5, 32);
    display.print("SSID:");
    display.setCursor(5, 40);
    if (connectedSSID.length() > 18) {
      display.print(connectedSSID.substring(0, 15) + "...");
    } else {
      display.print(connectedSSID);
    }
    
    display.setCursor(5, 50);
    display.print("IP:");
    display.print(WiFi.localIP().toString());
  } else {
    display.setCursor(20, 25);
    display.println("NOT CONNECTED");
    display.setCursor(10, 40);
    display.print("Check password or");
    display.setCursor(20, 50);
    display.print("signal strength");
  }
}

// =================== BUTTON HANDLING ===================
void checkButtons() {
  bool upNow = (digitalRead(BUTTON_UP) == LOW);
  bool downNow = (digitalRead(BUTTON_DOWN) == LOW);
  bool selectNow = (digitalRead(BUTTON_SELECT) == LOW);
  bool backNow = (digitalRead(BUTTON_BACK) == LOW);
  
  if (millis() - lastButtonPress < 200) return;
  
  if (upNow && !buttonStates[0]) {
    buttonStates[0] = true;
    buttonPressCount[0]++;
    handleButtonPress(0);
  } else if (!upNow) buttonStates[0] = false;
  
  if (downNow && !buttonStates[1]) {
    buttonStates[1] = true;
    buttonPressCount[1]++;
    handleButtonPress(1);
  } else if (!downNow) buttonStates[1] = false;
  
  if (selectNow && !buttonStates[2]) {
    buttonStates[2] = true;
    buttonPressCount[2]++;
    handleButtonPress(2);
  } else if (!selectNow) buttonStates[2] = false;
  
  if (backNow && !buttonStates[3]) {
    buttonStates[3] = true;
    buttonPressCount[3]++;
    handleButtonPress(3);
  } else if (!backNow) buttonStates[3] = false;
}

void handleButtonPress(int button) {
  lastButtonPress = millis();
  needRefresh = true;
  
  static int saveCounter = 0;
  saveCounter++;
  if (saveCounter >= 5) {
    saveButtonCounts();
    saveCounter = 0;
  }
  
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
          case 2: currentScreen = SCREEN_SET_TIME; break;
          case 3: currentScreen = SCREEN_RESET_MEM; break;
          case 4: 
            currentScreen = SCREEN_DEMO_MODE;
            demoActive = true;
            demoStartTime = millis();
            break;
          case 5: 
            currentScreen = SCREEN_OLED_TEST;
            oledTestPattern = (oledTestPattern + 1) % 4;
            break;
          case 6: currentScreen = SCREEN_RTC_TEST; break;
          case 7: currentScreen = SCREEN_VOLT_TEST; break;
          case 8:
            currentScreen = SCREEN_WIFI_SCAN;
            wifiSelectedIndex = 0;
            wifiRefreshRequested = true;
            break;
        }
      } else if (button == 3) {
        currentScreen = SCREEN_HOME;
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        if (button == 0) {
          wifiSelectedIndex--;
          if (wifiSelectedIndex < 0) wifiSelectedIndex = wifiNetworkCount;
        } else if (button == 1) {
          wifiSelectedIndex++;
          if (wifiSelectedIndex > wifiNetworkCount) wifiSelectedIndex = 0;
        } else if (button == 2) {
          if (wifiSelectedIndex == 0) {
            wifiRefreshRequested = true;
          } else if (wifiSelectedIndex <= wifiNetworkCount) {
            currentScreen = SCREEN_WIFI_CONNECT;
            connectToWiFi(wifiNetworks[wifiSelectedIndex - 1].ssid);
          }
        } else if (button == 3) {
          currentScreen = SCREEN_MAIN_MENU;
          WiFi.disconnect();
          WiFi.mode(WIFI_OFF);
        }
      }
      break;
      
    case SCREEN_WIFI_CONNECT:
      if (button == 3) {
        wifiConnecting = false;
        WiFi.disconnect();
        currentScreen = SCREEN_WIFI_SCAN;
      }
      break;
      
    case SCREEN_WIFI_STATUS:
      if (button == 2 || button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
      }
      break;
      
    case SCREEN_RESET_MEM:
      if (button == 2) {
        for (int i = 0; i < 4; i++) buttonPressCount[i] = 0;
        demoCounter = 0;
        demoActive = false;
        saveButtonCounts();
        
        display.clearDisplay();
        display.setCursor(40, 30);
        display.println("RESET DONE");
        display.display();
        delay(1000);
        
        currentScreen = SCREEN_MAIN_MENU;
        needRefresh = true;
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_DEMO_MODE:
      if (button == 2) {
        demoActive = !demoActive;
        if (demoActive) demoStartTime = millis();
      } else if (button == 3) {
        demoActive = false;
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_OLED_TEST:
      if (button == 0 || button == 1) {
        oledTestPattern = (oledTestPattern + 1) % 4;
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    default:
      if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
  }
}