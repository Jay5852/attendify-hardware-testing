/**
 * ESP32 SMART ENROLL SYSTEM with WiFi Scanning - FIXED VERSION
 * IoT Biometric Attendance System with Smart Enroll Feature
 * Version: 2.2 Production - Clean UI & Fixed Logic
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
#define FINGERPRINT_TX 4
#define FINGERPRINT_RX 2

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

// =================== FINGERPRINT CAPTURE STATES ===================
enum FingerprintState {
  FP_IDLE,
  FP_WAIT_FOR_FIRST,
  FP_CAPTURE_FIRST,
  FP_WAIT_FOR_SECOND,
  FP_CAPTURE_SECOND,
  FP_PROCESSING,
  FP_COMPLETE,
  FP_FAILED
};

// =================== GLOBAL VARIABLES ===================
ScreenState currentScreen = SCREEN_BOOT;
EnrollState enrollState = ENROLL_IDLE;
FingerprintState fpState = FP_IDLE;
int menuIndex = 0;
bool needRefresh = true;
bool displayInitialized = false;
bool fingerprintInitialized = false;

// Button tracking
unsigned long buttonPressTime[4] = {0, 0, 0, 0};
bool buttonLongPressed[4] = {false, false, false, false};

// WiFi Variables
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
char passwordChars[63] = {0};
int charSetIndex = 0;

// Character set
const char* charSet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+[]{}|;:,.<>?";
int charSetLength = 84;

// Backend Configuration
String BACKEND_URL = "http://192.168.0.119:5000";

// Enrollment Data
String pendingStudentName = "";
int pendingStudentRoll = -1;
bool studentProcessed = false; // To prevent re-processing same student

// Server Polling
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 2000;

// Refresh & Notifications
unsigned long lastAutoRefresh = 0;
const unsigned long REFRESH_INTERVAL = 30000;
bool autoRefreshEnabled = false; // DISABLED to prevent background animations
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
void handleFingerprintEnrollment();
void startFingerprintCapture();
void sendEnrollmentConfirmation(int rollNo, int fingerprintId);
void resetEnrollmentState();
void resetFingerprintState();

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

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== SMART ENROLL SYSTEM ===");
  
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
  
  // Initialize Fingerprint Sensor
  fingerSerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
  delay(1000); // Give sensor time to initialize
  
  // Check fingerprint sensor
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor OK");
    fingerprintInitialized = true;
  } else {
    Serial.println("Fingerprint sensor NOT FOUND!");
    fingerprintInitialized = false;
  }
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.println("Hardware initialized");
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  
  // Poll server if in enrollment mode and idle
  if (currentScreen == SCREEN_ENROLL_MODE && enrollState == ENROLL_IDLE && !studentProcessed) {
    if (millis() - lastPollTime > POLL_INTERVAL) {
      pollServerForEnrollment();
      lastPollTime = millis();
    }
  }
  
  // Handle fingerprint enrollment process
  if (enrollState == ENROLL_CAPTURING) {
    handleFingerprintEnrollment();
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

// =================== WIFI FUNCTIONS ===================
void checkWifiStatusChange() {
  wl_status_t currentStatus = WiFi.status();
  
  if (currentStatus != lastWifiStatus) {
    needRefresh = true;
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
}

void connectToWiFi(String ssid, String password) {
  connectedSSID = "";
  wifiConnecting = true;
  needRefresh = true;
  
  int bracketPos = ssid.indexOf(" [");
  if (bracketPos != -1) {
    ssid = ssid.substring(0, bracketPos);
  }
  
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (attempts < 20) {
    if (WiFi.status() == WL_CONNECTED) {
      connectedSSID = ssid;
      wifiConnecting = false;
      needRefresh = true;
      break;
    }
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnecting = false;
    needRefresh = true;
  }
}

// =================== ENROLLMENT FUNCTIONS ===================
void pollServerForEnrollment() {
  if (WiFi.status() != WL_CONNECTED) {
    enrollState = ENROLL_ERROR;
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
      enrollState = ENROLL_ERROR;
      return;
    }
    
    bool pending = doc["pending"];
    
    if (pending && !studentProcessed) {
      pendingStudentName = doc["name"].as<String>();
      pendingStudentRoll = doc["rollNo"];
      enrollState = ENROLL_PENDING;
      needRefresh = true;
      studentProcessed = false; // Reset for new student
    } else {
      if (enrollState != ENROLL_IDLE) {
        enrollState = ENROLL_IDLE;
        needRefresh = true;
      }
    }
  } else {
    enrollState = ENROLL_ERROR;
  }
  
  http.end();
}

void handleFingerprintEnrollment() {
  static unsigned long lastFingerCheck = 0;
  
  switch(fpState) {
    case FP_IDLE:
      fpState = FP_WAIT_FOR_FIRST;
      showNotificationMsg("Place finger");
      break;
      
    case FP_WAIT_FOR_FIRST:
      if (millis() - lastFingerCheck > 500) {
        int p = finger.getImage();
        if (p == FINGERPRINT_OK) {
          fpState = FP_CAPTURE_FIRST;
        }
        lastFingerCheck = millis();
      }
      break;
      
    case FP_CAPTURE_FIRST:
      if (finger.image2Tz(1) == FINGERPRINT_OK) {
        showNotificationMsg("Remove finger");
        fpState = FP_WAIT_FOR_SECOND;
      } else {
        fpState = FP_FAILED;
        showNotificationMsg("Capture failed");
      }
      break;
      
    case FP_WAIT_FOR_SECOND:
      showNotificationMsg("Place same finger");
      if (millis() - lastFingerCheck > 500) {
        int p = finger.getImage();
        if (p == FINGERPRINT_OK) {
          fpState = FP_CAPTURE_SECOND;
        }
        lastFingerCheck = millis();
      }
      break;
      
    case FP_CAPTURE_SECOND:
      if (finger.image2Tz(2) == FINGERPRINT_OK) {
        fpState = FP_PROCESSING;
        showNotificationMsg("Processing...");
      } else {
        fpState = FP_FAILED;
        showNotificationMsg("Capture failed");
      }
      break;
      
    case FP_PROCESSING:
      if (finger.createModel() == FINGERPRINT_OK) {
        if (finger.storeModel(pendingStudentRoll) == FINGERPRINT_OK) {
          fpState = FP_COMPLETE;
          enrollState = ENROLL_UPLOADING;
          sendEnrollmentConfirmation(pendingStudentRoll, pendingStudentRoll);
        } else {
          fpState = FP_FAILED;
          enrollState = ENROLL_ERROR;
          showNotificationMsg("Store failed");
        }
      } else {
        fpState = FP_FAILED;
        enrollState = ENROLL_ERROR;
        showNotificationMsg("Finger mismatch");
      }
      break;
      
    case FP_COMPLETE:
      // Nothing to do here, waiting for upload
      break;
      
    case FP_FAILED:
      // Error state, will be reset
      break;
  }
}

void sendEnrollmentConfirmation(int rollNo, int fingerprintId) {
  if (WiFi.status() != WL_CONNECTED) {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("No WiFi");
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
    showNotificationMsg("Enrolled!");
    
    // Reset after success
    delay(2000);
    resetEnrollmentState();
  } else {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("Upload failed");
  }
  
  http.end();
}

void resetEnrollmentState() {
  enrollState = ENROLL_IDLE;
  fpState = FP_IDLE;
  pendingStudentName = "";
  pendingStudentRoll = -1;
  studentProcessed = true; // Mark as processed to prevent re-polling
  needRefresh = true;
}

void resetFingerprintState() {
  fpState = FP_IDLE;
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
  
  // Show notification at bottom
  if (notificationActive) {
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setCursor(5, 56);
    
    if (notificationMessage.length() > 21) {
      display.print(notificationMessage.substring(0, 18) + "...");
    } else {
      display.print(notificationMessage);
    }
  }
  
  display.display();
}

void drawFooter() {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  switch(currentScreen) {
    case SCREEN_HOME:
      display.setCursor(50, 56);
      display.print("SEL=M");
      break;
      
    case SCREEN_MAIN_MENU:
      display.setCursor(0, 56);
      display.print("U/D");
      display.setCursor(40, 56);
      display.print("SEL");
      display.setCursor(80, 56);
      display.print("B=M");
      break;
      
    case SCREEN_ENROLL_MODE:
      if (enrollState == ENROLL_PENDING) {
        display.setCursor(40, 56);
        display.print("S=Start");
      } else if (enrollState == ENROLL_CAPTURING) {
        display.setCursor(50, 56);
        display.print("...");
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        display.setCursor(0, 56);
        display.print("S=Ref");
        display.setCursor(50, 56);
        display.print("L=C");
        display.setCursor(90, 56);
        display.print("B=M");
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      display.setCursor(0, 56);
      display.print("U/D");
      display.setCursor(40, 56);
      display.print("S=+");
      display.setCursor(80, 56);
      display.print("B=-");
      break;
      
    default:
      if (currentScreen != SCREEN_BOOT && currentScreen != SCREEN_HOME) {
        display.setCursor(50, 56);
        display.print("B=M");
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
  display.print("v2.2");
}

void drawHomeScreen() {
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Date - smaller font
    display.setCursor(5, 0);
    display.printf("%s %02d %s", 
                  dayNames[now.dayOfTheWeek()], 
                  now.day(),
                  monthNames[now.month()-1]);
    
    // Time - medium size
    display.setCursor(25, 15);
    display.setTextSize(2);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    // Year
    display.setCursor(55, 35);
    display.printf("%04d", now.year());
    
    // Status
    display.setCursor(5, 45);
    display.print("Status: ");
    if (WiFi.status() == WL_CONNECTED) {
      display.print("ONLINE");
    } else {
      display.print("OFFLINE");
    }
    
    // Enrollment ready
    display.setCursor(5, 55);
    if (fingerprintInitialized) {
      display.print("FP: Ready");
    } else {
      display.print("FP: Error");
    }
  } else {
    display.setCursor(30, 25);
    display.println("RTC NOT");
    display.setCursor(35, 40);
    display.println("FOUND");
  }
}

void drawMainMenu() {
  display.setCursor(45, 2);
  display.println("MENU");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  
  String menuItems[4] = {
    "1. ENROLL",
    "2. WIFI",
    "3. NETWORK",
    "4. ABOUT"
  };
  
  // Show only 3 items at a time
  int startIndex = 0;
  if (menuIndex > 2) {
    startIndex = menuIndex - 2;
  }
  
  for (int i = 0; i < 3 && (startIndex + i) < 4; i++) {
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
}

void drawEnrollmentScreen() {
  display.setCursor(40, 2);
  display.println("ENROLL");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  switch(enrollState) {
    case ENROLL_IDLE:
      display.setCursor(25, 25);
      display.println("Waiting...");
      display.setCursor(15, 40);
      display.println("No student");
      break;
      
    case ENROLL_PENDING:
      display.setCursor(35, 20);
      display.println("STUDENT");
      display.drawLine(30, 28, 98, 28, SH110X_WHITE);
      
      display.setCursor(5, 35);
      display.print("Name: ");
      if (pendingStudentName.length() > 12) {
        display.print(pendingStudentName.substring(0, 10));
      } else {
        display.print(pendingStudentName);
      }
      
      display.setCursor(5, 45);
      display.print("Roll: ");
      display.print(pendingStudentRoll);
      break;
      
    case ENROLL_CAPTURING:
      switch(fpState) {
        case FP_WAIT_FOR_FIRST:
          display.setCursor(20, 25);
          display.println("Place finger");
          break;
        case FP_WAIT_FOR_SECOND:
          display.setCursor(15, 25);
          display.println("Place again");
          break;
        case FP_PROCESSING:
          display.setCursor(30, 25);
          display.println("Processing");
          break;
        default:
          display.setCursor(30, 25);
          display.println("Capturing...");
          break;
      }
      break;
      
    case ENROLL_UPLOADING:
      display.setCursor(30, 25);
      display.println("Uploading...");
      break;
      
    case ENROLL_SUCCESS:
      display.setCursor(40, 25);
      display.println("SUCCESS");
      display.setCursor(20, 40);
      display.println("Enrollment done");
      break;
      
    case ENROLL_ERROR:
      display.setCursor(45, 25);
      display.println("ERROR");
      display.setCursor(20, 40);
      display.println("Try again");
      break;
  }
}

void drawWifiScanScreen() {
  display.setCursor(40, 0);
  display.println("WIFI");
  
  if (wifiScanning) {
    display.setCursor(40, 30);
    display.print("SCANNING");
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
  
  int startY = 12;
  
  // Refresh option
  if (wifiSelectedIndex == 0) {
    display.fillRect(0, startY - 1, 128, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  
  display.setCursor(2, startY);
  display.print("REFRESH");
  
  if (wifiSelectedIndex == 0) {
    display.setTextColor(SH110X_WHITE);
  }
  
  // Networks
  for (int i = 0; i < 3 && i < wifiNetworkCount; i++) {
    int yPos = startY + 12 + (i * 12);
    int displayIndex = i + 1;
    
    if (wifiSelectedIndex == displayIndex) {
      display.fillRect(0, yPos - 1, 128, 12, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(2, yPos);
    
    String displayText = String(displayIndex) + ". " + wifiNetworks[i];
    if (displayText.length() > 20) {
      displayText = displayText.substring(0, 17) + "...";
    }
    display.print(displayText);
    
    if (wifiSelectedIndex == displayIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawNetworkStatusScreen() {
  display.setCursor(20, 2);
  display.println("NETWORK");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(10, 20);
  display.print("Status: ");
  
  wl_status_t wifiStatus = WiFi.status();
  
  if (wifiConnecting) {
    display.print("Connecting");
  } else {
    switch(wifiStatus) {
      case WL_CONNECTED:
        display.print("Connected");
        break;
      case WL_DISCONNECTED:
        display.print("Disconnected");
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
    if (ssid.length() > 10) {
      display.print(ssid.substring(0, 7) + "...");
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
    display.print("dBm");
  }
}

void drawWifiConnectScreen() {
  display.setCursor(30, 2);
  display.println("CONNECTING");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(40, 25);
  
  if (wifiConnecting) {
    display.print("CONNECTING");
  } else {
    if (WiFi.status() == WL_CONNECTED) {
      display.print("CONNECTED");
    } else {
      display.print("FAILED");
    }
  }
  
  if (wifiSelectedIndex > 0 && wifiSelectedIndex <= wifiNetworkCount) {
    display.setCursor(10, 40);
    display.print("To: ");
    String ssid = wifiNetworks[wifiSelectedIndex - 1];
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
  display.println("WIFI");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(20, 25);
    display.print("Connected");
    
    display.setCursor(5, 35);
    display.print("SSID: ");
    String ssid = connectedSSID;
    if (ssid.length() > 12) {
      ssid = ssid.substring(0, 9) + "...";
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
    display.print("No Connection");
  }
}

void drawPasswordEntryScreen() {
  display.setCursor(30, 2);
  display.println("PASSWORD");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  String displaySSID = wifiNetworks[wifiSelectedIndex - 1];
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
  display.println("Smart Enroll");
  display.setCursor(5, 30);
  display.println("Version 2.2");
  display.setCursor(5, 40);
  display.println("With WiFi Scan");
  display.setCursor(5, 50);
  display.println("R307 Sensor");
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
  else if (currentScreen == SCREEN_WIFI_SCAN && button == 2 && !wifiScanning && wifiSelectedIndex > 0) {
    String selectedNetwork = wifiNetworks[wifiSelectedIndex - 1];
    
    if (selectedNetwork.indexOf("[OPEN]") != -1) {
      currentScreen = SCREEN_WIFI_CONNECT;
      connectToWiFi(selectedNetwork, "");
    } else {
      currentScreen = SCREEN_WIFI_CONNECT;
      attemptEmptyPasswordConnection(selectedNetwork);
      
      delay(2000);
      
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
  else if (currentScreen == SCREEN_ENROLL_MODE && button == 2 && enrollState == ENROLL_PENDING) {
    resetEnrollmentState();
    showNotificationMsg("Cancelled");
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
            studentProcessed = false; // Reset for new enrollment session
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
          fpState = FP_IDLE;
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
              
              delay(2000);
              
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