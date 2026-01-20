/**
 * ESP32 OLED SYSTEM - CLEAN UI WITH WIFI REFRESH
 * SH1106 OLED (128x64) + DS3231 RTC + 4 Buttons
 * FIXED: UI layout, WiFi refresh, small footer text
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

// WiFi variables - IMPROVED
String wifiNetworks[15];  // Increased capacity
int wifiNetworkCount = 0;
int wifiSelectedIndex = 0;
bool wifiScanning = false;
bool wifiRefreshRequested = false;
bool showRefreshOption = true;
String connectedSSID = "";
bool wifiConnecting = false;

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

// WiFi functions
void enableWiFiForScan();
void scanWiFiNetworks();
void connectToWiFi(String ssid);

// Screen drawing functions
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
  Serial.println("Version: 4.0 - Clean UI");
  
  // Set CPU frequency for stability
  setCpuFrequencyMhz(80);
  Serial.printf("CPU: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  
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
  
  Serial.println("System ready");
}

// =================== INITIALIZE PREFERENCES ===================
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
  
  Serial.println("Data saved to flash");
}

void loadButtonCounts() {
  buttonPressCount[0] = preferences.getInt("btn_up", 0);
  buttonPressCount[1] = preferences.getInt("btn_down", 0);
  buttonPressCount[2] = preferences.getInt("btn_sel", 0);
  buttonPressCount[3] = preferences.getInt("btn_back", 0);
  demoCounter = preferences.getInt("demo_counter", 0);
  
  Serial.printf("Buttons: U:%d D:%d S:%d B:%d\n",
                buttonPressCount[0], buttonPressCount[1], 
                buttonPressCount[2], buttonPressCount[3]);
}

// =================== INITIALIZE HARDWARE ===================
void initializeHardware() {
  // Initialize I2C
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);
  
  // Initialize OLED
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
    display.setContrast(255); // Max contrast for better visibility
    Serial.println("OLED ready");
  }
  
  // Initialize RTC
  if (rtc.begin()) {
    if (rtc.lostPower()) {
      Serial.println("RTC: Setting default time");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println("RTC ready");
  } else {
    Serial.println("RTC not found");
  }
  
  // Initialize buttons
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  
  // Disable WiFi initially
  WiFi.mode(WIFI_OFF);
  Serial.println("WiFi disabled (initial)");
}

// =================== WIFI FUNCTIONS ===================
void enableWiFiForScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.println("WiFi enabled for scan");
}

void scanWiFiNetworks() {
  wifiScanning = true;
  wifiNetworkCount = 0;
  showRefreshOption = true;
  
  // Clear network list
  for (int i = 0; i < 15; i++) {
    wifiNetworks[i] = "";
  }
  
  enableWiFiForScan();
  
  Serial.println("Scanning for WiFi...");
  
  // Start scan
  int n = WiFi.scanNetworks(false, true); // async=false, show_hidden=true
  
  Serial.printf("Found %d networks\n", n);
  
  // Store networks
  for (int i = 0; i < n && wifiNetworkCount < 15; i++) {
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    
    // Skip empty SSIDs
    if (ssid.length() == 0) continue;
    
    // Create formatted string: SSID (RSSI)
    String networkInfo = ssid;
    
    wifiNetworks[wifiNetworkCount] = networkInfo;
    wifiNetworkCount++;
    
    Serial.printf("%d: %s (%d dBm)\n", wifiNetworkCount, ssid.c_str(), rssi);
  }
  
  WiFi.scanDelete();
  wifiScanning = false;
  
  if (wifiNetworkCount == 0) {
    Serial.println("No networks found");
  }
}

void connectToWiFi(String ssid) {
  wifiConnecting = true;
  connectedSSID = "";
  
  enableWiFiForScan();
  
  Serial.printf("Connecting to: %s\n", ssid.c_str());
  
  // Try without password first (for open networks)
  WiFi.begin(ssid.c_str());
  
  int attempts = 0;
  while (attempts < 20) {
    if (WiFi.status() == WL_CONNECTED) {
      connectedSSID = ssid;
      Serial.println("Connected!");
      Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
      break;
    }
    delay(500);
    attempts++;
    Serial.print(".");
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFailed to connect");
    WiFi.disconnect();
  }
  
  wifiConnecting = false;
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  
  // Update screen if needed
  if (millis() - lastUpdate > 200 || needRefresh) {
    showScreen();
    lastUpdate = millis();
    needRefresh = false;
  }
  
  // Handle WiFi refresh request
  if (wifiRefreshRequested) {
    scanWiFiNetworks();
    wifiRefreshRequested = false;
    needRefresh = true;
  }
  
  // Demo mode updates
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
  
  // Draw main content based on current screen
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
    case SCREEN_SET_TIME:
      drawSetTime();
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
    case SCREEN_WIFI_CONNECT:
      drawWifiConnectScreen();
      break;
    case SCREEN_WIFI_STATUS:
      drawWifiStatusScreen();
      break;
  }
  
  // Draw footer for non-home screens
  if (currentScreen != SCREEN_HOME && currentScreen != SCREEN_BOOT) {
    drawFooter();
  }
  
  display.display();
}

void drawFooter() {
  // Small text at bottom - using smallest possible
  display.setTextSize(1);
  
  switch(currentScreen) {
    case SCREEN_MAIN_MENU:
      display.setCursor(0, 57);
      display.print("U/D:Nav  S:OK  B:Home");
      break;
      
    case SCREEN_BUTTON_TEST:
      display.setCursor(0, 57);
      display.print("B:Menu  Data saved");
      break;
      
    case SCREEN_RESET_MEM:
      display.setCursor(0, 57);
      display.print("S:Confirm  B:Cancel");
      break;
      
    case SCREEN_DEMO_MODE:
      if (demoActive) {
        display.setCursor(0, 57);
        display.print("S:Stop  B:Menu");
      } else {
        display.setCursor(0, 57);
        display.print("S:Start  B:Menu");
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        if (wifiSelectedIndex == 0 && showRefreshOption) {
          display.setCursor(0, 57);
          display.print("S:Refresh  B:Menu");
        } else {
          display.setCursor(0, 57);
          display.print("S:Connect  B:Menu");
        }
      }
      break;
      
    case SCREEN_WIFI_CONNECT:
      display.setCursor(0, 57);
      display.print("Connecting...  B:Cancel");
      break;
      
    case SCREEN_WIFI_STATUS:
      display.setCursor(0, 57);
      display.print("S/B:Menu");
      break;
      
    default:
      // Default footer for other screens
      display.setCursor(0, 57);
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
  
  // Version info
  display.setCursor(40, 56);
  display.print("v4.0");
}

void drawHomeScreen() {
  // No header line at top - cleaner look
  
  // Large time display
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Time (large)
    display.setCursor(25, 10);
    display.setTextSize(3);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    // Date below
    display.setCursor(20, 40);
    display.print(dayNames[now.dayOfTheWeek()]);
    display.print(" ");
    display.print(now.day());
    display.print(" ");
    display.print(monthNames[now.month()-1]);
    
    // Temperature if available
    display.setCursor(20, 50);
    display.print("Temp: ");
    display.print((int)rtc.getTemperature());
    display.print("C");
  } else {
    display.setCursor(30, 25);
    display.println("RTC ERROR");
  }
  
  // Bottom instruction - small and centered
  display.setCursor(35, 57);
  display.print("Press SELECT");
}

void drawMainMenu() {
  display.setCursor(40, 2);
  display.println("MENU");
  
  String menuItems[9] = {
    "BUTTON TEST",
    "SYSTEM INFO",
    "SET TIME",
    "RESET DATA",
    "DEMO MODE",
    "OLED TEST",
    "RTC TEST",
    "VOLTAGE TEST",
    "WIFI SCAN"
  };
  
  // Show 4 menu items at a time
  int startIdx = 0;
  if (menuIndex > 3) {
    startIdx = menuIndex - 3;
  }
  
  for (int i = 0; i < 4 && (startIdx + i) < 9; i++) {
    int idx = startIdx + i;
    int yPos = 15 + (i * 10);
    
    // Highlight selected item
    if (idx == menuIndex) {
      display.fillRect(0, yPos - 1, 128, 9, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    // Menu number + name
    display.setCursor(5, yPos);
    display.print(idx + 1);
    display.print(". ");
    display.print(menuItems[idx]);
    
    if (idx == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
  
  // Scroll indicator
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
  
  // Display counts in a clean grid
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
  
  // Demo counter
  display.setCursor(10, 50);
  display.print("DEMO: ");
  display.print(demoCounter);
}

void drawSystemInfo() {
  display.setCursor(40, 2);
  display.println("SYSTEM");
  
  // System info in clean layout
  display.setCursor(5, 20);
  display.print("CPU: ");
  display.print(getCpuFrequencyMhz());
  display.print(" MHz");
  
  display.setCursor(5, 30);
  display.print("MEM: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.print(" KB");
  
  display.setCursor(5, 40);
  display.print("CHIP: ESP32");
  
  // RTC status
  display.setCursor(5, 50);
  if (rtc.begin()) {
    display.print("RTC: OK");
  } else {
    display.print("RTC: ERROR");
  }
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
  
  // Large counter in center
  display.setCursor(50, 25);
  display.setTextSize(2);
  display.print(demoCounter);
  display.setTextSize(1);
  
  // Status
  if (demoActive) {
    display.setCursor(45, 45);
    display.print("RUNNING");
  } else {
    display.setCursor(40, 45);
    display.print("PRESS START");
  }
}

void drawOledTest() {
  display.setCursor(40, 2);
  display.println("DISPLAY");
  
  // Pattern info
  display.setCursor(40, 20);
  display.print("Pattern ");
  display.print(oledTestPattern + 1);
  
  // Draw test pattern
  switch(oledTestPattern % 4) {
    case 0:
      // Horizontal bars
      for (int i = 0; i < 64; i += 8) {
        display.fillRect(10, 30 + i, 108, 4, SH110X_WHITE);
      }
      break;
    case 1:
      // Circle
      display.fillCircle(64, 40, 20, SH110X_WHITE);
      break;
    case 2:
      // Triangle
      display.fillTriangle(30, 50, 64, 30, 98, 50, SH110X_WHITE);
      break;
    case 3:
      // Grid
      for (int x = 10; x < 118; x += 12) {
        for (int y = 30; y < 54; y += 12) {
          display.fillRect(x, y, 8, 8, SH110X_WHITE);
        }
      }
      break;
  }
}

void drawRtcTest() {
  display.setCursor(40, 2);
  display.println("RTC");
  
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Date
    display.setCursor(10, 20);
    display.printf("Date: %04d-%02d-%02d", 
                   now.year(), now.month(), now.day());
    
    // Time
    display.setCursor(10, 30);
    display.printf("Time: %02d:%02d:%02d", 
                   now.hour(), now.minute(), now.second());
    
    // Day of week
    display.setCursor(10, 40);
    display.print("Day: ");
    display.print(dayNames[now.dayOfTheWeek()]);
    
    // Temperature
    display.setCursor(10, 50);
    display.print("Temp: ");
    display.print(rtc.getTemperature(), 1);
    display.print("C");
  } else {
    display.setCursor(30, 30);
    display.println("RTC NOT FOUND");
  }
}

void drawVoltTest() {
  display.setCursor(40, 2);
  display.println("VOLTAGE");
  
  // Read ADC for voltage (GPIO34)
  int adcValue = analogRead(34);
  float voltage = adcValue * (3.3 / 4095.0);
  
  display.setCursor(10, 20);
  display.print("ADC: ");
  display.print(adcValue);
  
  display.setCursor(10, 35);
  display.print("Voltage: ");
  display.print(voltage, 2);
  display.print("V");
  
  // Battery status
  display.setCursor(10, 50);
  if (voltage > 3.0) {
    display.print("BATTERY: OK");
  } else if (voltage > 2.5) {
    display.print("BATTERY: LOW");
  } else {
    display.print("BATTERY: CRITICAL");
  }
}

void drawWifiScanScreen() {
  display.setCursor(40, 2);
  display.println("WIFI");
  
  if (wifiScanning) {
    display.setCursor(40, 30);
    display.print("SCANNING...");
    return;
  }
  
  // Show networks or "Refresh" option
  if (wifiNetworkCount == 0 && !showRefreshOption) {
    display.setCursor(20, 25);
    display.println("No networks");
    display.setCursor(15, 40);
    display.println("found");
  } else {
    // Always show "Refresh" as first option
    int displayCount = 0;
    int startY = 15;
    
    // Option 1: Refresh (always shown)
    int yPos = startY + (displayCount * 10);
    
    if (wifiSelectedIndex == 0) {
      display.fillRect(0, yPos - 1, 128, 9, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    display.print("[Refresh Networks]");
    
    if (wifiSelectedIndex == 0) {
      display.setTextColor(SH110X_WHITE);
    }
    
    displayCount++;
    
    // Show actual networks
    for (int i = 0; i < wifiNetworkCount && displayCount < 4; i++) {
      yPos = startY + (displayCount * 10);
      int idx = i;
      
      if (wifiSelectedIndex == idx + 1) {
        display.fillRect(0, yPos - 1, 128, 9, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      
      // Truncate long SSIDs
      String displayText = wifiNetworks[idx];
      if (displayText.length() > 16) {
        displayText = displayText.substring(0, 13) + "...";
      }
      
      display.setCursor(5, yPos);
      display.print(displayText);
      
      if (wifiSelectedIndex == idx + 1) {
        display.setTextColor(SH110X_WHITE);
      }
      
      displayCount++;
    }
    
    // Scroll indicators if needed
    if (wifiSelectedIndex > 0 && wifiNetworkCount > 3) {
      display.setCursor(120, 15);
      display.print("^");
      display.setCursor(120, 45);
      display.print("v");
    }
  }
}

void drawWifiConnectScreen() {
  display.setCursor(30, 2);
  display.println("CONNECTING");
  
  if (wifiSelectedIndex > 0 && wifiSelectedIndex <= wifiNetworkCount) {
    display.setCursor(10, 25);
    display.print("To: ");
    display.print(wifiNetworks[wifiSelectedIndex - 1]);
  }
  
  if (wifiConnecting) {
    display.setCursor(40, 40);
    display.print("Please wait...");
  }
}

void drawWifiStatusScreen() {
  display.setCursor(40, 2);
  display.println("STATUS");
  
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(20, 25);
    display.println("CONNECTED");
    
    display.setCursor(5, 40);
    display.print("SSID: ");
    if (connectedSSID.length() > 12) {
      display.print(connectedSSID.substring(0, 12));
    } else {
      display.print(connectedSSID);
    }
    
    display.setCursor(5, 50);
    display.print("IP: ");
    display.print(WiFi.localIP().toString());
  } else {
    display.setCursor(25, 30);
    display.println("NOT CONNECTED");
  }
}

// =================== BUTTON HANDLING ===================
void checkButtons() {
  bool upNow = (digitalRead(BUTTON_UP) == LOW);
  bool downNow = (digitalRead(BUTTON_DOWN) == LOW);
  bool selectNow = (digitalRead(BUTTON_SELECT) == LOW);
  bool backNow = (digitalRead(BUTTON_BACK) == LOW);
  
  // Debounce
  if (millis() - lastButtonPress < 200) return;
  
  // Check button presses
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
  
  // Auto-save every 10 button presses
  static int saveCounter = 0;
  saveCounter++;
  if (saveCounter >= 10) {
    saveButtonCounts();
    saveCounter = 0;
  }
  
  switch(currentScreen) {
    case SCREEN_HOME:
      if (button == 2) {  // SELECT
        currentScreen = SCREEN_MAIN_MENU;
        menuIndex = 0;
      }
      break;
      
    case SCREEN_MAIN_MENU:
      if (button == 0) {  // UP
        menuIndex = (menuIndex > 0) ? menuIndex - 1 : 8;
      } else if (button == 1) {  // DOWN
        menuIndex = (menuIndex < 8) ? menuIndex + 1 : 0;
      } else if (button == 2) {  // SELECT
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
            showRefreshOption = true;
            wifiRefreshRequested = true;
            break;
        }
      } else if (button == 3) {  // BACK
        currentScreen = SCREEN_HOME;
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        if (button == 0) {  // UP
          wifiSelectedIndex--;
          if (wifiSelectedIndex < 0) {
            wifiSelectedIndex = wifiNetworkCount; // Wrap to bottom
          }
        } else if (button == 1) {  // DOWN
          wifiSelectedIndex++;
          if (wifiSelectedIndex > wifiNetworkCount) {
            wifiSelectedIndex = 0; // Wrap to top (Refresh)
          }
        } else if (button == 2) {  // SELECT
          if (wifiSelectedIndex == 0) {
            // Refresh networks
            wifiRefreshRequested = true;
          } else if (wifiSelectedIndex <= wifiNetworkCount) {
            // Connect to selected network
            currentScreen = SCREEN_WIFI_CONNECT;
            connectToWiFi(wifiNetworks[wifiSelectedIndex - 1]);
          }
        } else if (button == 3) {  // BACK
          currentScreen = SCREEN_MAIN_MENU;
          WiFi.disconnect();
          WiFi.mode(WIFI_OFF);
        }
      }
      break;
      
    case SCREEN_WIFI_CONNECT:
      if (button == 3) {  // BACK - Cancel
        wifiConnecting = false;
        currentScreen = SCREEN_WIFI_SCAN;
      }
      break;
      
    case SCREEN_WIFI_STATUS:
      if (button == 2 || button == 3) {  // SELECT or BACK
        currentScreen = SCREEN_MAIN_MENU;
        WiFi.disconnect();
        WiFi.mode(WIFI_OFF);
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
        if (demoActive) demoStartTime = millis();
      } else if (button == 3) {  // BACK
        demoActive = false;
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_OLED_TEST:
      if (button == 0 || button == 1) {  // UP/DOWN - Change pattern
        oledTestPattern = (oledTestPattern + 1) % 4;
      } else if (button == 3) {  // BACK
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    default:
      // Default back button for most screens
      if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
  }
}