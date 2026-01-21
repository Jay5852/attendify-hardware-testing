/**
 * ESP32 SMART ENROLL SYSTEM with WiFi Scanning
 * IoT Biometric Attendance System with Smart Enroll Feature
 * Hardware: ESP32 + R307 Fingerprint Sensor + SH1106 OLED + 4 Buttons
 * Fingerprint Pins: TX->GPIO4, RX->GPIO2
 * Version: 2.0 Production with WiFi Scan
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "RTClib.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_Fingerprint.h>
#include <Preferences.h>

// =================== PIN CONFIGURATION ===================
#define OLED_SDA 21
#define OLED_SCL 22
#define BUTTON_UP 32
#define BUTTON_DOWN 33
#define BUTTON_SELECT 25
#define BUTTON_BACK 26

// FINGERPRINT SENSOR PINS (Updated as per your requirement)
#define FINGERPRINT_TX 4  // Fingerprint TX -> ESP32 GPIO4
#define FINGERPRINT_RX 2  // Fingerprint RX -> ESP32 GPIO2

// =================== OLED SETUP ===================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// =================== RTC SETUP ===================
RTC_DS3231 rtc;

// =================== FINGERPRINT SENSOR ===================
HardwareSerial fingerSerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

// =================== PREFERENCES ===================
Preferences preferences;

// =================== SCREEN STATE ENUM ===================
enum ScreenState {
  SCREEN_BOOT,
  SCREEN_HOME,
  SCREEN_MAIN_MENU,
  SCREEN_ENROLL_MODE,
  SCREEN_WIFI_SCAN,
  SCREEN_NETWORK_STATUS,
  SCREEN_WIFI_CONNECT,
  SCREEN_WIFI_STATUS,
  SCREEN_PASSWORD_ENTRY,
  SCREEN_ABOUT
};

// =================== ENROLLMENT STATES ===================
enum EnrollState {
  ENROLL_IDLE,
  ENROLL_PENDING,
  ENROLL_CAPTURING,
  ENROLL_UPLOADING,
  ENROLL_SUCCESS,
  ENROLL_ERROR
};

// =================== GLOBAL VARIABLES ===================
ScreenState currentScreen = SCREEN_BOOT;
EnrollState enrollState = ENROLL_IDLE;
int menuIndex = 0;
bool needRefresh = true;
bool displayInitialized = false;

// Button tracking
unsigned long buttonPressTime[4] = {0, 0, 0, 0};
bool buttonLongPressed[4] = {false, false, false, false};

// WiFi Variables (from your previous code)
String wifiNetworks[20];
int wifiNetworkCount = 0;
int wifiSelectedIndex = 0;
bool wifiScanning = false;
bool wifiConnecting = false;
String connectedSSID = "";

// Password entry (from your previous code)
String wifiPassword = "";
bool passwordEntryMode = false;
int passwordCursorPos = 0;
char passwordChars[63] = {0};
int charSetIndex = 0;

// Character set
const char* charSet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+[]{}|;:,.<>?";
int charSetLength = 84;

// Backend Configuration
String BACKEND_URL = "http://192.168.0.119:5000"; // Your IP address

// Enrollment Data
String pendingStudentName = "";
int pendingStudentRoll = -1;

// Server Polling
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 2000; // Poll every 2 seconds

// Refresh & Notifications
unsigned long lastAutoRefresh = 0;
const unsigned long REFRESH_INTERVAL = 30000;
bool autoRefreshEnabled = true;
bool notificationActive = false;
String notificationMessage = "";
unsigned long notificationStartTime = 0;
const unsigned long NOTIFICATION_DURATION = 3000;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;

// Day and month names
const char* dayNames[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* monthNames[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", 
                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// =================== FUNCTION DECLARATIONS ===================
void initializeHardware();
void initializePreferences();
void showScreen();
void drawFooter();
void checkButtons();
void handleButtonPress(int button);
void handleLongPress(int button);
void scanWiFiNetworks();
void connectToWiFi(String ssid, String password);
void checkWifiStatusChange();
void showNotificationMsg(String message);
void clearNotification();
void attemptEmptyPasswordConnection(String ssid);
void manualRefreshWiFi();
void pollServerForEnrollment();
void startFingerprintEnrollment();
void sendEnrollmentConfirmation(int rollNo, int fingerprintId);
void resetEnrollmentState();

// Screen drawing functions
void drawBootScreen();
void drawHomeScreen();
void drawMainMenu();
void drawEnrollmentScreen();
void drawWifiScanScreen();
void drawNetworkStatusScreen();
void drawWifiConnectScreen();
void drawWifiStatusScreen();
void drawPasswordEntryScreen();
void drawAboutScreen();

// WiFi functions from your previous code
void checkWifiStatusChange();
void showNotificationMsg(String message);
void clearNotification();
void manualRefreshWiFi();
void attemptEmptyPasswordConnection(String ssid);
void scanWiFiNetworks();
void connectToWiFi(String ssid, String password);

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SMART ENROLL SYSTEM ===");
  Serial.println("Fingerprint Pins: TX->GPIO4, RX->GPIO2");
  
  initializeHardware();
  initializePreferences();
  
  currentScreen = SCREEN_BOOT;
  showScreen();
  delay(1500);
  
  currentScreen = SCREEN_HOME;
  needRefresh = true;
  
  lastWifiStatus = WiFi.status();
}

// =================== INITIALIZE PREFERENCES ===================
void initializePreferences() {
  preferences.begin("enroll_system", false);
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
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println("RTC initialized");
  }
  
  // Initialize Buttons
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  
  // Initialize Fingerprint Sensor with new pins
  fingerSerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
  Serial.print("Fingerprint sensor on pins RX:");
  Serial.print(FINGERPRINT_RX);
  Serial.print(" TX:");
  Serial.println(FINGERPRINT_TX);
  
  delay(100); // Give sensor time to initialize
  
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor OK");
  } else {
    Serial.println("Fingerprint sensor NOT FOUND!");
    Serial.println("Check wiring: TX->GPIO4, RX->GPIO2");
  }
  
  // Initialize WiFi (don't auto-connect, let user choose network)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.println("Hardware initialized");
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  
  // Poll server if in enrollment mode and idle
  if (currentScreen == SCREEN_ENROLL_MODE && enrollState == ENROLL_IDLE) {
    if (millis() - lastPollTime > POLL_INTERVAL) {
      pollServerForEnrollment();
      lastPollTime = millis();
    }
  }
  
  // Auto refresh WiFi scan
  if (currentScreen == SCREEN_WIFI_SCAN && autoRefreshEnabled && !wifiScanning) {
    if (millis() - lastAutoRefresh > REFRESH_INTERVAL) {
      scanWiFiNetworks();
      lastAutoRefresh = millis();
    }
  }
  
  // Handle fingerprint enrollment process
  if (enrollState == ENROLL_CAPTURING) {
    startFingerprintEnrollment();
  }
  
  checkWifiStatusChange();
  
  if (notificationActive && millis() - notificationStartTime > NOTIFICATION_DURATION) {
    clearNotification();
  }
  
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 200 || needRefresh) {
    showScreen();
    lastUpdate = millis();
    needRefresh = false;
  }
  
  delay(50);
}

// =================== WIFI FUNCTIONS (FROM YOUR PREVIOUS CODE) ===================
void checkWifiStatusChange() {
  wl_status_t currentStatus = WiFi.status();
  
  if (currentStatus != lastWifiStatus) {
    needRefresh = true;
    
    switch(currentStatus) {
      case WL_CONNECTED:
        if (lastWifiStatus != WL_CONNECTED) {
          showNotificationMsg("WiFi Connected!");
          connectedSSID = WiFi.SSID();
        }
        break;
      case WL_DISCONNECTED:
        if (lastWifiStatus == WL_CONNECTED) {
          showNotificationMsg("WiFi Disconnected!");
          connectedSSID = "";
        }
        break;
      case WL_CONNECTION_LOST:
        showNotificationMsg("Connection Lost!");
        break;
      case WL_CONNECT_FAILED:
        showNotificationMsg("Connection Failed!");
        break;
      case WL_NO_SSID_AVAIL:
        showNotificationMsg("Network Not Found!");
        break;
    }
    
    lastWifiStatus = currentStatus;
  }
}

void showNotificationMsg(String message) {
  notificationMessage = message;
  notificationActive = true;
  notificationStartTime = millis();
  needRefresh = true;
}

void clearNotification() {
  notificationActive = false;
  notificationMessage = "";
  needRefresh = true;
}

void manualRefreshWiFi() {
  if (!wifiScanning) {
    scanWiFiNetworks();
    showNotificationMsg("Refreshing...");
  }
}

void attemptEmptyPasswordConnection(String ssid) {
  connectToWiFi(ssid, "");
}

void scanWiFiNetworks() {
  if (wifiScanning) return;
  
  wifiScanning = true;
  wifiNetworkCount = 0;
  needRefresh = true;
  
  for (int i = 0; i < 20; i++) {
    wifiNetworks[i] = "";
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  showNotificationMsg("Scanning...");
  
  int16_t n = WiFi.scanNetworks();
  
  for (int i = 0; i < n && wifiNetworkCount < 20; i++) {
    String ssid = WiFi.SSID(i);
    int32_t rssi = WiFi.RSSI(i);
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    
    if (ssid.length() == 0 || rssi < -95) continue;
    
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
    
    String displayName = ssid;
    if (ssid.length() > 12) {
      displayName = ssid.substring(0, 9) + "...";
    }
    
    wifiNetworks[wifiNetworkCount] = displayName + security;
    wifiNetworkCount++;
  }
  
  WiFi.scanDelete();
  wifiScanning = false;
  lastAutoRefresh = millis();
  
  if (wifiNetworkCount == 0) {
    showNotificationMsg("No networks found");
  } else {
    showNotificationMsg(String(wifiNetworkCount) + " networks found");
  }
}

void connectToWiFi(String ssid, String password) {
  connectedSSID = "";
  wifiConnecting = true;
  needRefresh = true;
  
  int bracketPos = ssid.indexOf(" [");
  if (bracketPos != -1) {
    ssid = ssid.substring(0, bracketPos);
  }
  
  showNotificationMsg("Connecting...");
  
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (attempts < 20) {
    if (WiFi.status() == WL_CONNECTED) {
      connectedSSID = ssid;
      wifiConnecting = false;
      needRefresh = true;
      showNotificationMsg("Connected!");
      lastAutoRefresh = millis();
      break;
    }
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnecting = false;
    needRefresh = true;
    showNotificationMsg("Failed! Try again");
  }
}

// =================== ENROLLMENT FUNCTIONS ===================
void pollServerForEnrollment() {
  if (WiFi.status() != WL_CONNECTED) {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("No WiFi Connection");
    return;
  }
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/next-enrollment");
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.print("JSON parse failed: ");
      Serial.println(error.c_str());
      enrollState = ENROLL_ERROR;
      return;
    }
    
    bool pending = doc["pending"];
    
    if (pending) {
      pendingStudentName = doc["name"].as<String>();
      pendingStudentRoll = doc["rollNo"];
      enrollState = ENROLL_PENDING;
      needRefresh = true;
      showNotificationMsg("Student in queue!");
    } else {
      // No pending enrollments
      if (enrollState != ENROLL_IDLE) {
        enrollState = ENROLL_IDLE;
        needRefresh = true;
      }
    }
  } else {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("Server Error!");
  }
  
  http.end();
}

void startFingerprintEnrollment() {
  // Wait for finger to be placed
  int p = finger.getImage();
  
  if (p == FINGERPRINT_OK) {
    showNotificationMsg("Finger detected");
    delay(500);
    
    // Convert image
    p = finger.image2Tz(1);
    if (p == FINGERPRINT_OK) {
      showNotificationMsg("Remove finger");
      delay(2000);
      
      // Wait for finger removal
      while (finger.getImage() != FINGERPRINT_NOFINGER);
      
      showNotificationMsg("Place same finger again");
      delay(2000);
      
      // Get second image
      p = finger.getImage();
      if (p == FINGERPRINT_OK) {
        p = finger.image2Tz(2);
        if (p == FINGERPRINT_OK) {
          // Create model
          p = finger.createModel();
          if (p == FINGERPRINT_OK) {
            // Store model using roll number as ID
            p = finger.storeModel(pendingStudentRoll);
            if (p == FINGERPRINT_OK) {
              enrollState = ENROLL_UPLOADING;
              needRefresh = true;
              sendEnrollmentConfirmation(pendingStudentRoll, pendingStudentRoll);
            } else {
              enrollState = ENROLL_ERROR;
              showNotificationMsg("Store failed!");
            }
          } else {
            enrollState = ENROLL_ERROR;
            showNotificationMsg("Fingerprints mismatch!");
          }
        } else {
          enrollState = ENROLL_ERROR;
          showNotificationMsg("Image2Tz failed!");
        }
      } else {
        enrollState = ENROLL_ERROR;
        showNotificationMsg("Second image fail!");
      }
    } else {
      enrollState = ENROLL_ERROR;
      showNotificationMsg("Image2Tz failed!");
    }
  } else if (p == FINGERPRINT_NOFINGER) {
    // No finger yet, continue waiting
    return;
  } else {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("Sensor error!");
  }
}

void sendEnrollmentConfirmation(int rollNo, int fingerprintId) {
  if (WiFi.status() != WL_CONNECTED) {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("No WiFi for upload");
    return;
  }
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/enroll-confirm");
  http.addHeader("Content-Type", "application/json");
  
  JsonDocument doc;
  doc["rollNo"] = rollNo;
  doc["fingerprintId"] = fingerprintId;
  
  String json;
  serializeJson(doc, json);
  
  int httpCode = http.POST(json);
  
  if (httpCode == 200) {
    enrollState = ENROLL_SUCCESS;
    showNotificationMsg("Enrollment Complete!");
    
    // Reset after success
    delay(2000);
    resetEnrollmentState();
  } else {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("Upload failed!");
  }
  
  http.end();
}

void resetEnrollmentState() {
  enrollState = ENROLL_IDLE;
  pendingStudentName = "";
  pendingStudentRoll = -1;
  needRefresh = true;
}

// =================== DISPLAY FUNCTIONS ===================
void showScreen() {
  if (!displayInitialized) return;
  
  display.clearDisplay();
  
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
    case SCREEN_ENROLL_MODE:
      drawEnrollmentScreen();
      break;
    case SCREEN_WIFI_SCAN:
      drawWifiScanScreen();
      break;
    case SCREEN_NETWORK_STATUS:
      drawNetworkStatusScreen();
      break;
    case SCREEN_WIFI_CONNECT:
      drawWifiConnectScreen();
      break;
    case SCREEN_WIFI_STATUS:
      drawWifiStatusScreen();
      break;
    case SCREEN_PASSWORD_ENTRY:
      drawPasswordEntryScreen();
      break;
    case SCREEN_ABOUT:
      drawAboutScreen();
      break;
  }
  
  drawFooter();
  
  // Show notification overlay if active
  if (notificationActive && currentScreen != SCREEN_HOME) {
    display.fillRect(0, 50, 128, 14, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(10, 52);
    
    if (notificationMessage.length() > 18) {
      display.print(notificationMessage.substring(0, 15) + "...");
    } else {
      display.print(notificationMessage);
    }
    
    display.setTextColor(SH110X_WHITE);
  }
  
  display.display();
}

void drawFooter() {
  display.setTextSize(1);
  
  switch(currentScreen) {
    case SCREEN_HOME:
      display.setCursor(50, 56);
      display.print("SEL=Menu");
      break;
      
    case SCREEN_MAIN_MENU:
      display.setCursor(0, 56);
      display.print("U/D");
      display.setCursor(30, 56);
      display.print("S");
      display.setCursor(60, 56);
      display.print("B");
      break;
      
    case SCREEN_ENROLL_MODE:
      switch(enrollState) {
        case ENROLL_PENDING:
          display.setCursor(40, 56);
          display.print("SEL=Start");
          break;
        case ENROLL_CAPTURING:
          display.setCursor(50, 56);
          display.print("Capturing...");
          break;
        default:
          display.setCursor(40, 56);
          display.print("Polling...");
          break;
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        display.setCursor(0, 56);
        display.print("SEL=Refresh");
        display.setCursor(70, 56);
        display.print("L=C");
        display.setCursor(100, 56);
        display.print("B=M");
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      display.setCursor(0, 56);
      display.print("U/D");
      display.setCursor(30, 56);
      display.print("S=+");
      display.setCursor(60, 56);
      display.print("B=-");
      display.setCursor(90, 56);
      display.print("L=C");
      break;
      
    case SCREEN_WIFI_STATUS:
      display.setCursor(50, 56);
      display.print("B=M");
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
  display.setCursor(20, 20);
  display.setTextSize(2);
  display.println("SMART");
  display.setCursor(25, 40);
  display.println("ENROLL");
  display.setTextSize(1);
  display.setCursor(50, 56);
  display.print("v2.0");
}

void drawHomeScreen() {
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Date at top
    display.setCursor(10, 0);
    display.printf("%s %02d %s %04d", 
                  dayNames[now.dayOfTheWeek()], 
                  now.day(),
                  monthNames[now.month()-1],
                  now.year());
    
    // Large time
    display.setCursor(20, 15);
    display.setTextSize(3);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    // System status
    display.setCursor(10, 45);
    display.print("Status: ");
    
    wl_status_t wifiStatus = WiFi.status();
    if (wifiStatus == WL_CONNECTED) {
      display.print("ONLINE");
      display.setCursor(90, 0);
      display.print("WiFi");
    } else {
      display.print("OFFLINE");
      display.setCursor(90, 0);
      display.print("No WiFi");
    }
    
    display.setCursor(10, 55);
    display.print("Ready for Enrollment");
  } else {
    display.setCursor(30, 25);
    display.println("RTC NOT");
    display.setCursor(35, 40);
    display.println("FOUND");
  }
  
  if (notificationActive) {
    display.fillRect(0, 50, 128, 14, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(10, 52);
    
    if (notificationMessage.length() > 18) {
      display.print(notificationMessage.substring(0, 15) + "...");
    } else {
      display.print(notificationMessage);
    }
    
    display.setTextColor(SH110X_WHITE);
  }
}

void drawMainMenu() {
  display.setCursor(40, 2);
  display.println("MAIN MENU");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  
  // Production menu items
  String menuItems[5] = {
    "1. ENROLL MODE",
    "2. WIFI SCAN",
    "3. NETWORK STATUS",
    "4. ABOUT SYSTEM"
  };
  
  // Show only 3 items at a time
  int startIndex = 0;
  if (menuIndex > 2) {
    startIndex = menuIndex - 2;
  }
  
  for (int i = 0; i < 3 && (startIndex + i) < 5; i++) {
    int yPos = 15 + (i * 15);
    int idx = startIndex + i;
    
    if (idx == menuIndex) {
      display.fillRect(0, yPos - 2, 128, 15, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    display.print(menuItems[idx]);
    
    if (idx == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
  
  // Scroll indicators
  if (menuIndex > 2) {
    display.setCursor(122, 15);
    display.print("^");
  }
  if (menuIndex < 2) {
    display.setCursor(122, 55);
    display.print("v");
  }
}

void drawEnrollmentScreen() {
  display.setCursor(30, 2);
  display.println("ENROLL MODE");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  switch(enrollState) {
    case ENROLL_IDLE:
      display.setCursor(20, 25);
      display.println("Polling Server...");
      display.setCursor(15, 40);
      display.println("Waiting for students");
      break;
      
    case ENROLL_PENDING:
      display.setCursor(30, 20);
      display.println("STUDENT");
      display.drawLine(30, 28, 98, 28, SH110X_WHITE);
      
      display.setCursor(10, 35);
      display.print("Name: ");
      if (pendingStudentName.length() > 12) {
        display.print(pendingStudentName.substring(0, 9) + "...");
      } else {
        display.print(pendingStudentName);
      }
      
      display.setCursor(10, 45);
      display.print("Roll No: ");
      display.print(pendingStudentRoll);
      
      display.setCursor(30, 55);
      display.print("Press SELECT");
      break;
      
    case ENROLL_CAPTURING:
      display.setCursor(25, 20);
      display.println("PLACE FINGER");
      display.setCursor(30, 35);
      display.println("on sensor");
      display.setCursor(20, 45);
      display.println("Keep it steady...");
      break;
      
    case ENROLL_UPLOADING:
      display.setCursor(30, 25);
      display.println("UPLOADING");
      display.setCursor(20, 40);
      display.println("to server...");
      break;
      
    case ENROLL_SUCCESS:
      display.setCursor(40, 25);
      display.println("SUCCESS!");
      display.setCursor(20, 40);
      display.println("Enrollment complete");
      break;
      
    case ENROLL_ERROR:
      display.setCursor(45, 25);
      display.println("ERROR!");
      display.setCursor(15, 40);
      display.println("Please try again");
      break;
  }
  
  // Show WiFi status in corner
  display.setCursor(90, 0);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("ON");
  } else {
    display.print("OFF");
  }
}

// =================== WIFI SCREEN FUNCTIONS (FROM YOUR PREVIOUS CODE) ===================
void drawWifiScanScreen() {
  // Top header
  display.setCursor(40, 0);
  display.println("WIFI SCAN");
  
  // Show network count as 1/4, 2/4 etc
  if (wifiNetworkCount > 0) {
    display.setCursor(100, 0);
    display.printf("%d/%d", wifiSelectedIndex + 1, wifiNetworkCount);
  }
  
  if (wifiScanning) {
    display.setCursor(40, 30);
    display.print("SCANNING...");
    return;
  }
  
  if (wifiNetworkCount == 0) {
    display.setCursor(15, 20);
    display.println("No networks");
    display.setCursor(5, 35);
    display.println("Press SELECT");
    display.setCursor(5, 45);
    display.println("to refresh");
    return;
  }
  
  // Display networks with refresh option at the top
  int startY = 12;
  
  // Always show refresh option as first item
  if (wifiSelectedIndex == 0) {
    display.fillRect(0, startY - 1, 128, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  
  display.setCursor(2, startY);
  display.print(">>> REFRESH <<<");
  
  if (wifiSelectedIndex == 0) {
    display.setTextColor(SH110X_WHITE);
  }
  
  // Now show actual networks (starting from index 1 for user)
  for (int i = 0; i < 3 && i < wifiNetworkCount; i++) {
    int yPos = startY + 12 + (i * 12);
    int displayIndex = i + 1; // Start from 1 because 0 is refresh
    
    if (wifiSelectedIndex == displayIndex) {
      display.fillRect(0, yPos - 1, 128, 12, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(2, yPos);
    
    // Format: "1. TP-Link F3208 [WPA2]"
    String displayText = String(displayIndex) + ". " + wifiNetworks[i];
    if (displayText.length() > 20) {
      displayText = displayText.substring(0, 17) + "...";
    }
    display.print(displayText);
    
    if (wifiSelectedIndex == displayIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
  
  // Scroll indicators
  if (wifiSelectedIndex > 3 && wifiNetworkCount > 3) {
    display.setCursor(122, 12);
    display.print("^");
  }
  if (wifiSelectedIndex < (wifiNetworkCount - 1) && wifiNetworkCount > 3) {
    display.setCursor(122, 56);
    display.print("v");
  }
}

void drawNetworkStatusScreen() {
  display.setCursor(20, 2);
  display.println("NETWORK STATUS");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(10, 20);
  display.print("Status: ");
  
  wl_status_t wifiStatus = WiFi.status();
  
  if (wifiConnecting) {
    display.print("Connecting...");
  } else {
    switch(wifiStatus) {
      case WL_CONNECTED:
        display.print("Connected");
        break;
      case WL_DISCONNECTED:
        display.print("Disconnected");
        break;
      case WL_CONNECTION_LOST:
        display.print("Connection Lost");
        break;
      case WL_CONNECT_FAILED:
        display.print("Connection Failed");
        break;
      case WL_NO_SSID_AVAIL:
        display.print("Network Not Found");
        break;
      default:
        display.print("Unknown");
        break;
    }
  }
  
  if (wifiStatus == WL_CONNECTED) {
    display.setCursor(10, 30);
    display.print("SSID: ");
    String ssid = WiFi.SSID();
    if (ssid.length() > 12) {
      display.print(ssid.substring(0, 9) + "...");
    } else {
      display.print(ssid);
    }
    
    display.setCursor(10, 40);
    display.print("IP: ");
    String ip = WiFi.localIP().toString();
    if (ip.length() > 15) {
      ip = ip.substring(0, 12) + "...";
    }
    display.print(ip);
    
    display.setCursor(10, 50);
    display.print("Signal: ");
    display.print(WiFi.RSSI());
    display.print(" dBm");
  } else {
    display.setCursor(20, 30);
    display.print("No active");
    display.setCursor(20, 40);
    display.print("connection");
  }
}

void drawWifiConnectScreen() {
  display.setCursor(30, 2);
  display.println("CONNECTING");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(40, 25);
  
  if (wifiConnecting) {
    static int dots = 0;
    display.print("CONNECTING");
    for (int i = 0; i < dots; i++) display.print(".");
    dots = (dots + 1) % 4;
    
    display.setCursor(10, 40);
    display.print("[");
    for (int i = 0; i < 20; i++) {
      if (i < (millis() / 500) % 20) {
        display.print("=");
      } else {
        display.print(" ");
      }
    }
    display.print("]");
  } else {
    display.print("COMPLETE!");
  }
  
  if (wifiSelectedIndex > 0 && wifiSelectedIndex <= wifiNetworkCount) {
    display.setCursor(10, 55);
    display.print("To: ");
    String ssid = wifiNetworks[wifiSelectedIndex - 1]; // Adjust for refresh option
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
  
  wl_status_t wifiStatus = WiFi.status();
  
  if (wifiStatus == WL_CONNECTED) {
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

void drawPasswordEntryScreen() {
  display.setCursor(30, 2);
  display.println("ENTER PASSWORD");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  String displaySSID = wifiNetworks[wifiSelectedIndex - 1]; // Adjust for refresh option
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
  
  display.setCursor(5, 30);
  display.print("Pass: ");
  for (int i = 0; i < passwordCursorPos; i++) {
    if (passwordChars[i] != 0) {
      display.print("*");
    }
  }
  
  display.setCursor(5 + (passwordCursorPos * 6), 30);
  display.print("_");
  
  display.setCursor(5, 45);
  display.print("Char: ");
  if (passwordCursorPos < sizeof(passwordChars) && passwordChars[passwordCursorPos] != 0) {
    display.print(passwordChars[passwordCursorPos]);
  } else {
    display.print("a");
  }
}

void drawAboutScreen() {
  display.setCursor(45, 2);
  display.println("ABOUT");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(5, 20);
  display.println("Smart Enroll System");
  display.setCursor(5, 30);
  display.println("Version 2.0");
  display.setCursor(5, 40);
  display.println("With WiFi Scan");
  display.setCursor(5, 50);
  display.println("& R307 Sensor");
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
      } else if (now - buttonPressTime[i] > 1000 && !buttonLongPressed[i]) {
        buttonLongPressed[i] = true;
        handleLongPress(i);
      }
    } else {
      if (buttonPressTime[i] > 0) {
        if (!buttonLongPressed[i] && now - lastDebounce > 200) {
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
  // Long press SELECT on password entry screen
  if (currentScreen == SCREEN_PASSWORD_ENTRY && button == 2) {
    String password = "";
    for (int i = 0; i < passwordCursorPos; i++) {
      password += passwordChars[i];
    }
    
    currentScreen = SCREEN_WIFI_CONNECT;
    connectToWiFi(wifiNetworks[wifiSelectedIndex - 1], password);
    
    memset(passwordChars, 0, sizeof(passwordChars));
    passwordCursorPos = 0;
    needRefresh = true;
  } 
  // Long press SELECT on a WiFi network to connect
  else if (currentScreen == SCREEN_WIFI_SCAN && button == 2 && !wifiScanning && wifiSelectedIndex > 0) {
    String selectedNetwork = wifiNetworks[wifiSelectedIndex - 1];
    
    if (selectedNetwork.indexOf("[OPEN]") != -1) {
      currentScreen = SCREEN_WIFI_CONNECT;
      connectToWiFi(selectedNetwork, "");
    } else {
      currentScreen = SCREEN_WIFI_CONNECT;
      attemptEmptyPasswordConnection(selectedNetwork);
      
      unsigned long startTime = millis();
      while (millis() - startTime < 2000 && WiFi.status() != WL_CONNECTED) {
        delay(100);
      }
      
      if (WiFi.status() != WL_CONNECTED) {
        currentScreen = SCREEN_PASSWORD_ENTRY;
        passwordEntryMode = true;
        passwordCursorPos = 0;
        memset(passwordChars, 0, sizeof(passwordChars));
        passwordChars[0] = 'a';
      } else {
        currentScreen = SCREEN_WIFI_STATUS;
      }
    }
  }
  // Long press SELECT in enrollment pending state to skip/cancel
  else if (currentScreen == SCREEN_ENROLL_MODE && button == 2 && enrollState == ENROLL_PENDING) {
    resetEnrollmentState();
    showNotificationMsg("Enrollment cancelled");
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
        menuIndex = (menuIndex > 0) ? menuIndex - 1 : 3;
      } else if (button == 1) {
        menuIndex = (menuIndex < 3) ? menuIndex + 1 : 0;
      } else if (button == 2) {
        switch(menuIndex) {
          case 0: 
            currentScreen = SCREEN_ENROLL_MODE;
            resetEnrollmentState();
            break;
          case 1: 
            currentScreen = SCREEN_WIFI_SCAN;
            wifiSelectedIndex = 0;
            scanWiFiNetworks();
            break;
          case 2: 
            currentScreen = SCREEN_NETWORK_STATUS;
            break;
          case 3: 
            currentScreen = SCREEN_ABOUT;
            break;
        }
      } else if (button == 3) {
        currentScreen = SCREEN_HOME;
      }
      break;
      
    case SCREEN_ENROLL_MODE:
      if (button == 2) {
        if (enrollState == ENROLL_PENDING) {
          enrollState = ENROLL_CAPTURING;
          showNotificationMsg("Start fingerprint");
        }
      } else if (button == 3) {
        resetEnrollmentState();
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        if (button == 0) {
          wifiSelectedIndex = (wifiSelectedIndex > 0) ? wifiSelectedIndex - 1 : wifiNetworkCount;
        } else if (button == 1) {
          wifiSelectedIndex = (wifiSelectedIndex < wifiNetworkCount) ? wifiSelectedIndex + 1 : 0;
        } else if (button == 2) {
          if (wifiSelectedIndex == 0) {
            manualRefreshWiFi();
          } else if (wifiSelectedIndex > 0 && wifiSelectedIndex <= wifiNetworkCount) {
            String selectedNetwork = wifiNetworks[wifiSelectedIndex - 1];
            
            if (selectedNetwork.indexOf("[OPEN]") != -1) {
              currentScreen = SCREEN_WIFI_CONNECT;
              connectToWiFi(selectedNetwork, "");
            } else {
              currentScreen = SCREEN_WIFI_CONNECT;
              attemptEmptyPasswordConnection(selectedNetwork);
              
              unsigned long startTime = millis();
              while (millis() - startTime < 2000 && WiFi.status() != WL_CONNECTED) {
                delay(100);
              }
              
              if (WiFi.status() != WL_CONNECTED) {
                currentScreen = SCREEN_PASSWORD_ENTRY;
                passwordEntryMode = true;
                passwordCursorPos = 0;
                memset(passwordChars, 0, sizeof(passwordChars));
                passwordChars[0] = 'a';
              } else {
                currentScreen = SCREEN_WIFI_STATUS;
              }
            }
          }
        } else if (button == 3) {
          currentScreen = SCREEN_MAIN_MENU;
        }
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      if (button == 0) {
        if (passwordCursorPos < sizeof(passwordChars)) {
          char currentChar = passwordChars[passwordCursorPos];
          int charIndex = 0;
          
          for (int i = 0; i < charSetLength; i++) {
            if (charSet[i] == currentChar) {
              charIndex = i;
              break;
            }
          }
          
          charIndex = (charIndex + 1) % charSetLength;
          passwordChars[passwordCursorPos] = charSet[charIndex];
        }
      } else if (button == 1) {
        if (passwordCursorPos < sizeof(passwordChars)) {
          char currentChar = passwordChars[passwordCursorPos];
          int charIndex = 0;
          
          for (int i = 0; i < charSetLength; i++) {
            if (charSet[i] == currentChar) {
              charIndex = i;
              break;
            }
          }
          
          charIndex = (charIndex - 1 + charSetLength) % charSetLength;
          passwordChars[passwordCursorPos] = charSet[charIndex];
        }
      } else if (button == 2) {
        if (passwordCursorPos < sizeof(passwordChars) - 1) {
          passwordCursorPos++;
          if (passwordChars[passwordCursorPos] == 0) {
            passwordChars[passwordCursorPos] = 'a';
          }
        }
      } else if (button == 3) {
        if (passwordCursorPos > 0) {
          passwordCursorPos--;
          passwordChars[passwordCursorPos + 1] = 0;
        } else {
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
}