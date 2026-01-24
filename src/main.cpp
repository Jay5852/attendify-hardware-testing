/**
 * ESP32 SMART ENROLL SYSTEM - OPTIMIZED ENROLLMENT
 * Fastest possible fingerprint enrollment for R307 sensor
 * Version: 3.3 Fast Enroll - WITH MICROSD AND RTC FIX
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
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// =================== PIN CONFIGURATION ===================
#define OLED_SDA 21
#define OLED_SCL 22
#define BUTTON_UP 32
#define BUTTON_DOWN 33
#define BUTTON_SELECT 25
#define BUTTON_BACK 26
#define FINGERPRINT_TX 4
#define FINGERPRINT_RX 2

// =================== MICROSD CARD PINS ===================
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18
#define SD_CS 5

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
  SCREEN_ABOUT,
  SCREEN_SD_MENU,
  SCREEN_SD_LOGS,
  SCREEN_RTC_SETUP
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
  FP_WAIT_FOR_REMOVAL,
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
int sdMenuIndex = 0;
int sdLogIndex = 0;
bool needRefresh = true;
bool displayInitialized = false;
bool fingerprintInitialized = false;
bool sdCardInitialized = false;
File logFile;

// RTC time adjustment (27 minutes fix)
const int RTC_TIME_ADJUST_MINUTES = 27;

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
unsigned long wifiConnectionStartTime = 0;
const unsigned long WIFI_CONNECTION_TIMEOUT = 20000; // 20 seconds timeout

// Password entry
String wifiPassword = "";
bool passwordEntryMode = false;
int passwordCursorPos = 0;
char passwordChars[63] = {0};

// Character set
const char* charSet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+[]{}|;:,.<>?";
int charSetLength = 84;

// Backend Configuration - CHANGE THIS TO YOUR IP
String BACKEND_URL = "http://192.168.0.119:5001";

// Enrollment Data
String pendingStudentName = "";
int pendingStudentRoll = -1;
bool studentInProgress = false;
unsigned long enrollmentStartTime = 0;
const unsigned long ENROLLMENT_TIMEOUT = 25000;
int enrollmentRetryCount = 0;
const int MAX_ENROLLMENT_RETRIES = 3;

// Fingerprint enrollment tracking
unsigned long fpStateStartTime = 0;
int fpRetryCount = 0;
const int MAX_FP_RETRIES = 2;
String lastFingerprintError = "";
bool firstCaptureDone = false;
bool secondCaptureDone = false;
unsigned long lastCaptureTime = 0;

// Server Polling
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 2000;

// Refresh & Notifications
bool notificationActive = false;
String notificationMessage = "";
unsigned long notificationStartTime = 0;
const unsigned long NOTIFICATION_DURATION = 2000;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;

// SD Card Logging
String sdLogs[20];
int sdLogCount = 0;
bool sdLoggingEnabled = true;

// Day and month names
const char* dayNames[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* monthNames[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", 
                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// =================== FUNCTION DECLARATIONS ===================
void initializeHardware();
void initializePreferences();
void initializeSDCard();
void logToSD(String message);
void readLogsFromSD();
void clearSDLogs();
void showScreen();
void drawFooter();
void checkButtons();
void handleButtonPress(int button);
void handleLongPress(int button);
void scanWiFiNetworks();
void connectToWiFi(String ssid, String password);
void checkWifiStatusChange();
void updateWiFiConnection();
void showNotificationMsg(String message);
void clearNotification();
void manualRefreshWiFi();
void attemptEmptyPasswordConnection(String ssid);
void pollServerForEnrollment();
void handleFingerprintEnrollment();
void handleFingerprintEnrollmentFast();
void sendEnrollmentConfirmation(int rollNo, int fingerprintId);
void resetEnrollmentState();
void resetFingerprintState();
void showDetailedError(int errorCode);
DateTime getAdjustedDateTime();
void adjustRTC();

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
void drawSDMenuScreen();
void drawSDLogsScreen();
void drawRTCSetupScreen();

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔══════════════════════════════════╗");
  Serial.println("║   SMART ENROLL SYSTEM v3.3      ║");
  Serial.println("║   WITH MICROSD & RTC FIX        ║");
  Serial.println("╚══════════════════════════════════╝");
  
  initializeHardware();
  initializePreferences();
  
  currentScreen = SCREEN_BOOT;
  showScreen();
  delay(1500);
  
  currentScreen = SCREEN_HOME;
  needRefresh = true;
  
  lastWifiStatus = WiFi.status();
  
  // Log initial boot
  logToSD("System booted - v3.3 with SD Card");
  
  Serial.println("System ready for fast enrollment!");
}

// =================== INITIALIZE PREFERENCES ===================
void initializePreferences() {
  preferences.begin("enroll_system", false);
  
  // Load saved WiFi credentials
  String savedSSID = preferences.getString("wifi_ssid", "");
  String savedPassword = preferences.getString("wifi_pass", "");
  
  if (savedSSID.length() > 0) {
    Serial.print("Found saved WiFi: ");
    Serial.println(savedSSID);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  }
}

// =================== INITIALIZE HARDWARE ===================
void initializeHardware() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  
  // Initialize OLED
  if (!display.begin(0x3C, true)) {
    Serial.println("❌ OLED NOT FOUND!");
    displayInitialized = false;
  } else {
    displayInitialized = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setRotation(0);
    Serial.println("✅ OLED initialized");
  }
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("⚠️ RTC not found");
  } else {
    if (rtc.lostPower()) {
      DateTime compileTime = DateTime(F(__DATE__), F(__TIME__));
      // Apply 27 minute adjustment
      compileTime = compileTime + TimeSpan(0, 0, RTC_TIME_ADJUST_MINUTES, 0);
      rtc.adjust(compileTime);
      Serial.println("RTC lost power - set to compile time + 27 min");
    }
    Serial.println("✅ RTC initialized");
    logToSD("RTC initialized");
  }
  
  // Initialize Buttons
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  Serial.println("✅ Buttons initialized");
  
  // Initialize Fingerprint Sensor
  Serial.println("Initializing fingerprint sensor...");
  fingerSerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
  delay(1000);
  
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.print("Fingerprint sensor attempt ");
    Serial.print(attempt);
    Serial.print("/3... ");
    
    if (finger.verifyPassword()) {
      fingerprintInitialized = true;
      Serial.println("✅ SUCCESS");
      
      // Disable sensor LED when idle to prevent blinking
      finger.LEDcontrol(false);
      
      int templateCount = finger.getTemplateCount();
      Serial.print("Templates: ");
      Serial.println(templateCount);
      
      logToSD("Fingerprint sensor OK - Templates: " + String(templateCount));
      
      break;
    } else {
      Serial.println("❌ FAILED");
      delay(500);
    }
  }
  
  if (!fingerprintInitialized) {
    Serial.println("❌ FINGERPRINT SENSOR NOT FOUND!");
    logToSD("Fingerprint sensor FAILED");
  }
  
  // Initialize SD Card
  initializeSDCard();
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  Serial.println("✅ WiFi initialized");
  
  Serial.println("✅ Hardware initialization complete");
}

// =================== INITIALIZE SD CARD ===================
void initializeSDCard() {
  Serial.println("Initializing SD card...");
  
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD Card initialization failed!");
    sdCardInitialized = false;
    return;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("❌ No SD card found");
    sdCardInitialized = false;
    return;
  }
  
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  
  sdCardInitialized = true;
  
  // Create logs directory if it doesn't exist
  if (!SD.exists("/logs")) {
    SD.mkdir("/logs");
  }
  
  // Create initial log entry
  logToSD("SD Card initialized successfully");
  logToSD("Card size: " + String(cardSize) + "MB");
  
  Serial.println("✅ SD Card initialized");
}

// =================== SD CARD LOGGING FUNCTIONS ===================
void logToSD(String message) {
  if (!sdCardInitialized) return;
  
  DateTime now = getAdjustedDateTime();
  
  char timestamp[20];
  sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d",
          now.year(), now.month(), now.day(),
          now.hour(), now.minute(), now.second());
  
  String logEntry = String(timestamp) + " - " + message;
  
  // Also print to serial
  Serial.println("[SD LOG] " + logEntry);
  
  // Open log file for appending
  File file = SD.open("/logs/enroll.log", FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open log file");
    return;
  }
  
  file.println(logEntry);
  file.close();
}

void readLogsFromSD() {
  if (!sdCardInitialized) {
    sdLogCount = 0;
    sdLogs[0] = "SD Card not available";
    sdLogCount = 1;
    return;
  }
  
  File file = SD.open("/logs/enroll.log");
  if (!file) {
    sdLogCount = 0;
    sdLogs[0] = "No log file found";
    sdLogCount = 1;
    return;
  }
  
  sdLogCount = 0;
  while (file.available() && sdLogCount < 20) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) {
      sdLogs[sdLogCount] = line;
      sdLogCount++;
    }
  }
  file.close();
  
  if (sdLogCount == 0) {
    sdLogs[0] = "Log file is empty";
    sdLogCount = 1;
  }
}

void clearSDLogs() {
  if (!sdCardInitialized) return;
  
  SD.remove("/logs/enroll.log");
  logToSD("Logs cleared manually");
  showNotificationMsg("Logs cleared");
}

// =================== RTC FUNCTIONS ===================
DateTime getAdjustedDateTime() {
  if (!rtc.begin()) {
    return DateTime(2000, 1, 1, 0, 0, 0);
  }
  
  DateTime now = rtc.now();
  // Apply the 27 minute adjustment
  now = now + TimeSpan(0, 0, RTC_TIME_ADJUST_MINUTES, 0);
  return now;
}

void adjustRTC() {
  if (!rtc.begin()) return;
  
  DateTime now = rtc.now();
  // Add 27 minutes to fix the timing issue
  DateTime adjustedTime = now + TimeSpan(0, 0, RTC_TIME_ADJUST_MINUTES, 0);
  rtc.adjust(adjustedTime);
  
  logToSD("RTC adjusted by +27 minutes");
  showNotificationMsg("RTC adjusted +27min");
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  
  // Update WiFi connection status if connecting
  if (wifiConnecting) {
    updateWiFiConnection();
  }
  
  if (currentScreen == SCREEN_ENROLL_MODE && 
      enrollState == ENROLL_IDLE && 
      !studentInProgress && 
      WiFi.status() == WL_CONNECTED) {
    
    if (millis() - lastPollTime > POLL_INTERVAL) {
      pollServerForEnrollment();
      lastPollTime = millis();
    }
  }
  
  if (enrollState == ENROLL_CAPTURING) {
    handleFingerprintEnrollmentFast();
  }
  
  if (studentInProgress && (millis() - enrollmentStartTime > ENROLLMENT_TIMEOUT)) {
    Serial.println("⏰ Enrollment timeout");
    logToSD("Enrollment timeout for Roll: " + String(pendingStudentRoll));
    showNotificationMsg("Timeout");
    resetEnrollmentState();
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
  
  delay(20);
}

// =================== WIFI FUNCTIONS ===================
void checkWifiStatusChange() {
  wl_status_t currentStatus = WiFi.status();
  
  if (currentStatus != lastWifiStatus) {
    needRefresh = true;
    lastWifiStatus = currentStatus;
    
    // Update connected SSID when status changes
    if (currentStatus == WL_CONNECTED && connectedSSID.length() == 0) {
      connectedSSID = WiFi.SSID();
      logToSD("WiFi connected to: " + connectedSSID);
    } else if (currentStatus != WL_CONNECTED && connectedSSID.length() > 0) {
      logToSD("WiFi disconnected from: " + connectedSSID);
      connectedSSID = "";
    }
  }
}

void updateWiFiConnection() {
  if (!wifiConnecting) return;
  
  wl_status_t status = WiFi.status();
  
  if (status == WL_CONNECTED) {
    wifiConnecting = false;
    connectedSSID = WiFi.SSID();
    needRefresh = true;
    showNotificationMsg("Connected!");
    
    // Save successful credentials
    preferences.putString("wifi_ssid", connectedSSID);
    preferences.putString("wifi_pass", wifiPassword);
    
    Serial.print("Connected to: ");
    Serial.println(connectedSSID);
    logToSD("WiFi connection successful: " + connectedSSID);
  }
  else if (millis() - wifiConnectionStartTime > WIFI_CONNECTION_TIMEOUT) {
    wifiConnecting = false;
    needRefresh = true;
    showNotificationMsg("Failed");
    
    Serial.println("WiFi connection timeout");
    logToSD("WiFi connection timeout for: " + wifiNetworks[wifiSelectedIndex]);
  }
}

void showNotificationMsg(String message) {
  notificationMessage = message;
  notificationActive = true;
  notificationStartTime = millis();
  needRefresh = true;
  
  Serial.println("📢 " + message);
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
  
  showNotificationMsg("Scanning...");
  logToSD("WiFi scan started");
  
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
  
  if (wifiNetworkCount == 0) {
    showNotificationMsg("No networks");
    logToSD("WiFi scan: No networks found");
  } else {
    showNotificationMsg(String(wifiNetworkCount) + " networks");
    logToSD("WiFi scan: " + String(wifiNetworkCount) + " networks found");
  }
}

void connectToWiFi(String ssid, String password) {
  // Extract SSID name from display string (remove security info)
  int bracketPos = ssid.indexOf(" [");
  String ssidOnly = ssid;
  if (bracketPos != -1) {
    ssidOnly = ssid.substring(0, bracketPos);
  }
  
  connectedSSID = "";
  wifiConnecting = true;
  wifiConnectionStartTime = millis();
  wifiPassword = password;
  needRefresh = true;
  
  showNotificationMsg("Connecting...");
  logToSD("Connecting to WiFi: " + ssidOnly);
  
  Serial.print("Connecting to: ");
  Serial.println(ssidOnly);
  
  // Stop any existing connection
  WiFi.disconnect();
  delay(100);
  
  // Start new connection
  WiFi.begin(ssidOnly.c_str(), password.c_str());
}

// =================== ENROLLMENT FUNCTIONS ===================
void pollServerForEnrollment() {
  if (WiFi.status() != WL_CONNECTED) {
    showNotificationMsg("No WiFi");
    return;
  }
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/next-enrollment");
  http.setTimeout(3000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      return;
    }
    
    bool pending = doc["pending"];
    
    if (pending) {
      String newName = doc["name"].as<String>();
      int newRoll = doc["rollNo"];
      
      if (pendingStudentRoll != newRoll) {
        pendingStudentName = newName;
        pendingStudentRoll = newRoll;
        enrollState = ENROLL_PENDING;
        studentInProgress = false;
        needRefresh = true;
        
        showNotificationMsg("Ready!");
        logToSD("New enrollment pending - Roll: " + String(newRoll) + ", Name: " + newName);
      }
    } else {
      if (enrollState != ENROLL_IDLE) {
        enrollState = ENROLL_IDLE;
        needRefresh = true;
      }
    }
  }
  
  http.end();
}

void handleFingerprintEnrollmentFast() {
  static unsigned long stateStartTime = 0;
  static int result;
  
  if (fpState == FP_IDLE) {
    stateStartTime = millis();
    fpStateStartTime = millis();
    firstCaptureDone = false;
    secondCaptureDone = false;
    Serial.println("Starting FAST enrollment...");
    logToSD("Enrollment started for Roll: " + String(pendingStudentRoll));
    
    // Turn on LED only when starting enrollment
    if (fingerprintInitialized) {
      finger.LEDcontrol(1); // Turn on LED for enrollment
    }
    
    fpState = FP_WAIT_FOR_FIRST;
    showNotificationMsg("Place finger");
  }
  
  // Check for overall timeout
  if (millis() - fpStateStartTime > 20000) {
    Serial.println("Enrollment timeout");
    logToSD("Enrollment timeout for Roll: " + String(pendingStudentRoll));
    showNotificationMsg("Too slow");
    
    // Turn off LED on timeout
    if (fingerprintInitialized) {
      finger.LEDcontrol(false);
    }
    
    resetFingerprintState();
    enrollState = ENROLL_ERROR;
    studentInProgress = false;
    needRefresh = true;
    return;
  }
  
  switch(fpState) {
    case FP_WAIT_FOR_FIRST:
      if (millis() - stateStartTime > 500) {
        result = finger.getImage();
        
        if (result == FINGERPRINT_OK) {
          Serial.println("First capture");
          fpState = FP_CAPTURE_FIRST;
          showNotificationMsg("Hold...");
        }
        // Check for timeout in waiting for finger
        else if (millis() - fpStateStartTime > 10000) {
          Serial.println("Timeout waiting for finger");
          logToSD("Timeout waiting for first finger - Roll: " + String(pendingStudentRoll));
          showNotificationMsg("No finger");
          fpState = FP_FAILED;
        }
        stateStartTime = millis();
      }
      break;
      
    case FP_CAPTURE_FIRST:
      delay(200);
      
      result = finger.image2Tz(1);
      if (result == FINGERPRINT_OK) {
        Serial.println("First image OK");
        logToSD("First fingerprint captured - Roll: " + String(pendingStudentRoll));
        firstCaptureDone = true;
        showNotificationMsg("Lift finger");
        fpState = FP_WAIT_FOR_REMOVAL;
        stateStartTime = millis();
      } else {
        Serial.println("First capture failed");
        logToSD("First fingerprint capture failed - Roll: " + String(pendingStudentRoll));
        showNotificationMsg("Failed - Retry");
        fpState = FP_FAILED;
      }
      break;
      
    case FP_WAIT_FOR_REMOVAL:
      if (millis() - stateStartTime > 500) {
        result = finger.getImage();
        
        if (result == FINGERPRINT_NOFINGER) {
          Serial.println("Finger removed");
          showNotificationMsg("Place again");
          delay(600);
          fpState = FP_WAIT_FOR_SECOND;
          stateStartTime = millis();
        } else if (result == FINGERPRINT_OK) {
          showNotificationMsg("Lift now!");
          // Check if finger has been there too long
          if (millis() - stateStartTime > 5000) {
            Serial.println("Finger not lifted");
            logToSD("Finger not lifted after first capture - Roll: " + String(pendingStudentRoll));
            showNotificationMsg("Remove finger");
            fpState = FP_FAILED;
          }
        }
        stateStartTime = millis();
      }
      break;
      
    case FP_WAIT_FOR_SECOND:
      if (millis() - stateStartTime > 500) {
        result = finger.getImage();
        
        if (result == FINGERPRINT_OK) {
          Serial.println("Second capture");
          fpState = FP_CAPTURE_SECOND;
          showNotificationMsg("Hold...");
        }
        // Check for timeout waiting for second finger
        else if (millis() - stateStartTime > 8000) {
          Serial.println("Timeout waiting for 2nd finger");
          logToSD("Timeout waiting for second finger - Roll: " + String(pendingStudentRoll));
          showNotificationMsg("Too slow");
          fpState = FP_FAILED;
        }
        stateStartTime = millis();
      }
      break;
      
    case FP_CAPTURE_SECOND:
      delay(200);
      
      result = finger.image2Tz(2);
      if (result == FINGERPRINT_OK) {
        Serial.println("Second image OK");
        logToSD("Second fingerprint captured - Roll: " + String(pendingStudentRoll));
        secondCaptureDone = true;
        showNotificationMsg("Processing...");
        fpState = FP_PROCESSING;
        stateStartTime = millis();
      } else {
        Serial.println("Second capture failed");
        logToSD("Second fingerprint capture failed - Roll: " + String(pendingStudentRoll));
        showNotificationMsg("Failed - Retry");
        fpState = FP_FAILED;
      }
      break;
      
    case FP_PROCESSING:
      result = finger.createModel();
      
      if (result == FINGERPRINT_OK) {
        Serial.println("Model created");
        showNotificationMsg("Storing...");
        
        result = finger.storeModel(pendingStudentRoll);
        
        if (result == FINGERPRINT_OK) {
          Serial.print("Stored ID: ");
          Serial.println(pendingStudentRoll);
          
          // Log successful storage
          logToSD("Fingerprint model created and stored - Roll: " + String(pendingStudentRoll));
          
          // Turn off LED after successful enrollment
          if (fingerprintInitialized) {
            finger.LEDcontrol(false);
          }
          
          fpState = FP_COMPLETE;
          enrollState = ENROLL_UPLOADING;
          sendEnrollmentConfirmation(pendingStudentRoll, pendingStudentRoll);
        } else {
          Serial.print("Store failed: ");
          Serial.println(result);
          logToSD("Fingerprint store failed - Error: " + String(result) + " - Roll: " + String(pendingStudentRoll));
          showDetailedError(result);
          fpState = FP_FAILED;
        }
      } else if (result == FINGERPRINT_ENROLLMISMATCH) {
        Serial.println("Fingers don't match");
        logToSD("Fingerprint mismatch - Roll: " + String(pendingStudentRoll));
        showNotificationMsg("Mismatch - Retry");
        fpState = FP_FAILED;
      } else {
        Serial.print("Model failed: ");
        Serial.println(result);
        logToSD("Fingerprint model creation failed - Error: " + String(result) + " - Roll: " + String(pendingStudentRoll));
        showNotificationMsg("Failed");
        fpState = FP_FAILED;
      }
      break;
      
    case FP_COMPLETE:
      break;
      
    case FP_FAILED:
      Serial.println("Fingerprint enrollment failed - resetting");
      logToSD("Fingerprint enrollment failed - Roll: " + String(pendingStudentRoll));
      
      // Turn off LED on failure
      if (fingerprintInitialized) {
        finger.LEDcontrol(false);
      }
      
      // Reset both states properly
      resetFingerprintState();
      
      // Clear any pending finger detection
      for (int i = 0; i < 3; i++) {
        finger.getImage(); // Clear any pending image
        delay(100);
      }
      
      // Set error state
      enrollState = ENROLL_ERROR;
      studentInProgress = false;
      enrollmentStartTime = 0;
      
      // Ensure we refresh the display
      needRefresh = true;
      
      // Show error notification
      showNotificationMsg("Failed - Press SEL");
      
      return;
      
    default:
      resetFingerprintState();
      enrollState = ENROLL_ERROR;
      studentInProgress = false;
      needRefresh = true;
      break;
  }
}

void showDetailedError(int errorCode) {
  switch(errorCode) {
    case FINGERPRINT_OK:
      lastFingerprintError = "Success";
      break;
    case FINGERPRINT_NOFINGER:
      lastFingerprintError = "No finger";
      break;
    case FINGERPRINT_IMAGEFAIL:
      lastFingerprintError = "Image failed";
      break;
    case FINGERPRINT_ENROLLMISMATCH:
      lastFingerprintError = "Mismatch";
      break;
    case FINGERPRINT_BADLOCATION:
      lastFingerprintError = "Bad location";
      break;
    case FINGERPRINT_FLASHERR:
      lastFingerprintError = "Flash error";
      break;
    default:
      lastFingerprintError = "Error " + String(errorCode);
      break;
  }
  
  showNotificationMsg(lastFingerprintError);
}

void sendEnrollmentConfirmation(int rollNo, int fingerprintId) {
  if (WiFi.status() != WL_CONNECTED) {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("No WiFi");
    logToSD("Upload failed - No WiFi - Roll: " + String(rollNo));
    return;
  }
  
  Serial.print("Sending confirmation for Roll ");
  Serial.println(rollNo);
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/enroll-confirm");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000);
  
  JsonDocument doc;
  doc["rollNo"] = rollNo;
  doc["fingerprintId"] = fingerprintId;
  
  String json;
  serializeJson(doc, json);
  
  int httpCode = http.POST(json);
  
  if (httpCode == 200) {
    enrollState = ENROLL_SUCCESS;
    showNotificationMsg("✅ Success!");
    logToSD("Enrollment completed successfully - Roll: " + String(rollNo) + ", FP ID: " + String(fingerprintId));
    
    delay(1000);
    resetEnrollmentState();
  } else {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("Upload failed");
    logToSD("Upload failed - HTTP: " + String(httpCode) + " - Roll: " + String(rollNo));
  }
  
  http.end();
}

void resetEnrollmentState() {
  enrollState = ENROLL_IDLE;
  fpState = FP_IDLE;
  studentInProgress = false;
  enrollmentStartTime = 0;
  enrollmentRetryCount = 0;
  fpRetryCount = 0;
  firstCaptureDone = false;
  secondCaptureDone = false;
  needRefresh = true;
  
  // Clear any pending sensor state
  if (fingerprintInitialized) {
    finger.getImage(); // Clear sensor buffer
    finger.LEDcontrol(false); // Ensure LED is off when idle
  }
  
  delay(500);
  pendingStudentName = "";
  pendingStudentRoll = -1;
}

void resetFingerprintState() {
  fpState = FP_IDLE;
  fpRetryCount = 0;
  firstCaptureDone = false;
  secondCaptureDone = false;
  
  // Ensure we're not stuck with any finger detection
  if (fingerprintInitialized) {
    finger.getImage(); // Clear sensor buffer
    finger.LEDcontrol(false); // Turn off LED
  }
}

// =================== DISPLAY FUNCTIONS ===================
void showScreen() {
  if (!displayInitialized) return;
  
  display.clearDisplay();
  
  // Draw header line for all screens except boot
  if (currentScreen != SCREEN_BOOT) {
    display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  }
  
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
    case SCREEN_SD_MENU:
      drawSDMenuScreen();
      break;
    case SCREEN_SD_LOGS:
      drawSDLogsScreen();
      break;
    case SCREEN_RTC_SETUP:
      drawRTCSetupScreen();
      break;
  }
  
  drawFooter();
  
  // Notification overlay (drawn last)
  if (notificationActive && notificationMessage.length() > 0) {
    display.fillRect(0, 50, 128, 14, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(4, 52);
    
    String msg = notificationMessage;
    if (msg.length() > 18) msg = msg.substring(0, 16) + "..";
    display.print(msg);
    
    display.setTextColor(SH110X_WHITE);
  }
  
  display.display();
}

void drawFooter() {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  // Clear footer area completely
  display.fillRect(0, 56, 128, 8, SH110X_BLACK);
  
  switch(currentScreen) {
    case SCREEN_HOME:
      display.setCursor(50, 56);
      display.print("SEL=MENU");
      break;
      
    case SCREEN_MAIN_MENU:
      display.setCursor(5, 56);
      display.print("U/D");
      display.setCursor(40, 56);
      display.print("SEL");
      display.setCursor(80, 56);
      display.print("B=MENU");
      break;
      
    case SCREEN_ENROLL_MODE:
      if (enrollState == ENROLL_PENDING || enrollState == ENROLL_ERROR) {
        display.setCursor(35, 56);
        display.print("SEL=START");
      } else if (enrollState == ENROLL_CAPTURING) {
        display.setCursor(45, 56);
        display.print("BUSY");
      } else {
        display.setCursor(45, 56);
        display.print("READY");
      }
      break;
      
    case SCREEN_SD_MENU:
    case SCREEN_SD_LOGS:
      display.setCursor(5, 56);
      display.print("U/D");
      display.setCursor(40, 56);
      display.print("SEL");
      display.setCursor(80, 56);
      display.print("B=BACK");
      break;
      
    default:
      if (currentScreen != SCREEN_BOOT && currentScreen != SCREEN_HOME) {
        display.setCursor(50, 56);
        display.print("B=MENU");
      }
      break;
  }
}

void drawBootScreen() {
  display.fillRect(0, 0, 128, 64, SH110X_BLACK);
  
  display.setCursor(25, 15);
  display.setTextSize(2);
  display.println("SMART");
  display.setCursor(30, 35);
  display.println("ENROLL");
  display.setTextSize(1);
  display.setCursor(40, 56);
  display.print("v3.3 SD+RTC");
}

void drawHomeScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  DateTime now = getAdjustedDateTime();
  
  // Date at top (centered)
  display.setCursor(15, 0);
  display.printf("%s %02d %s %04d", 
                dayNames[now.dayOfTheWeek()], 
                now.day(),
                monthNames[now.month()-1],
                now.year());
  
  // Time centered (larger font)
  display.setCursor(30, 20);
  display.setTextSize(2);
  display.printf("%02d:%02d", now.hour(), now.minute());
  display.setTextSize(1);
  
  // Status indicators
  display.setCursor(5, 40);
  display.print("WiFi: ");
  display.print(WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
  
  display.setCursor(70, 40);
  display.print("FP: ");
  display.print(fingerprintInitialized ? "OK" : "ERR");
  
  // SD Card status
  display.setCursor(5, 50);
  display.print("SD: ");
  display.print(sdCardInitialized ? "OK" : "NO");
}

void drawMainMenu() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(50, 0);
  display.println("MENU");
  
  String menuItems[6] = {
    "1. ENROLL MODE",
    "2. WIFI SCAN",
    "3. NETWORK STATUS",
    "4. SD CARD MENU",
    "5. RTC SETUP",
    "6. ABOUT"
  };
  
  // Calculate start index for scrolling
  int startIdx = 0;
  if (menuIndex > 2) {
    startIdx = menuIndex - 2;
  }
  
  // Display 3 menu items maximum
  for (int i = 0; i < 3; i++) {
    int itemIdx = startIdx + i;
    if (itemIdx >= 6) break;
    
    int yPos = 15 + (i * 12);
    
    display.fillRect(0, yPos - 1, 128, 12, SH110X_BLACK);
    
    if (itemIdx == menuIndex) {
      display.fillRect(0, yPos - 1, 128, 12, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    display.print(menuItems[itemIdx]);
    
    if (itemIdx == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawEnrollmentScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(45, 0);
  display.println("ENROLL");
  
  // Top indicators
  display.setCursor(100, 0);
  display.print(WiFi.status() == WL_CONNECTED ? "WIFI" : "NO NET");
  
  display.setCursor(0, 0);
  display.print(fingerprintInitialized ? "FP:OK" : "FP:ERR");
  
  switch(enrollState) {
    case ENROLL_IDLE:
      display.setCursor(25, 25);
      display.println("Waiting...");
      display.setCursor(20, 40);
      display.println("for student");
      break;
      
    case ENROLL_PENDING:
      display.setCursor(35, 15);
      display.println("STUDENT");
      
      display.setCursor(5, 30);
      display.print("Roll: ");
      display.print(pendingStudentRoll);
      
      display.setCursor(5, 40);
      display.print("Name: ");
      if (pendingStudentName.length() > 10) {
        display.print(pendingStudentName.substring(0, 8) + "..");
      } else {
        display.print(pendingStudentName);
      }
      break;
      
    case ENROLL_CAPTURING:
      switch(fpState) {
        case FP_WAIT_FOR_FIRST:
          display.setCursor(30, 20);
          display.println("STEP 1");
          display.setCursor(20, 35);
          display.println("Place finger");
          break;
        case FP_WAIT_FOR_REMOVAL:
          display.setCursor(30, 20);
          display.println("STEP 2");
          display.setCursor(15, 35);
          display.println("Lift finger");
          break;
        case FP_WAIT_FOR_SECOND:
          display.setCursor(30, 20);
          display.println("STEP 3");
          display.setCursor(15, 35);
          display.println("Place again");
          break;
        default:
          display.setCursor(30, 25);
          display.println("PROCESSING");
          break;
      }
      break;
      
    case ENROLL_SUCCESS:
      display.setCursor(40, 25);
      display.println("SUCCESS");
      display.setCursor(20, 40);
      display.println("Enrollment done!");
      break;
      
    case ENROLL_ERROR:
      display.setCursor(35, 15);
      display.println("STUDENT");
      
      display.setCursor(5, 30);
      display.print("Roll: ");
      display.print(pendingStudentRoll);
      
      display.setCursor(5, 40);
      display.print("Name: ");
      if (pendingStudentName.length() > 10) {
        display.print(pendingStudentName.substring(0, 8) + "..");
      } else {
        display.print(pendingStudentName);
      }
      
      display.setCursor(20, 50);
      display.print("Press SELECT");
      break;
      
    default:
      display.setCursor(30, 25);
      display.println("READY");
      break;
  }
}

void drawWifiScanScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(45, 0);
  display.println("WIFI SCAN");
  
  if (wifiScanning) {
    display.setCursor(40, 30);
    display.println("SCANNING...");
  } else if (wifiNetworkCount == 0) {
    display.setCursor(25, 30);
    display.println("NO NETWORKS");
  } else {
    int startIdx = (wifiSelectedIndex / 3) * 3;
    for (int i = 0; i < 3 && (startIdx + i) < wifiNetworkCount; i++) {
      int yPos = 15 + (i * 12);
      int idx = startIdx + i;
      
      display.fillRect(0, yPos - 1, 128, 12, SH110X_BLACK);
      
      if (idx == wifiSelectedIndex) {
        display.fillRect(0, yPos - 1, 128, 12, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      }
      
      display.setCursor(2, yPos);
      display.print(">");
      
      String network = wifiNetworks[idx];
      if (network.length() > 18) {
        network = network.substring(0, 16) + "..";
      }
      display.print(network);
      
      if (idx == wifiSelectedIndex) {
        display.setTextColor(SH110X_WHITE);
      }
    }
  }
}

void drawNetworkStatusScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(15, 0);
  display.println("NETWORK STATUS");
  
  wl_status_t status = WiFi.status();
  
  display.setCursor(10, 20);
  display.print("Status: ");
  
  if (status == WL_CONNECTED) {
    display.println("CONNECTED");
    display.setCursor(10, 32);
    display.print("SSID: ");
    String ssid = WiFi.SSID();
    if (ssid.length() > 15) {
      display.println(ssid.substring(0, 13) + "..");
    } else {
      display.println(ssid);
    }
    display.setCursor(10, 44);
    display.print("IP: ");
    display.println(WiFi.localIP());
  } else {
    switch(status) {
      case WL_NO_SHIELD:
        display.println("NO SHIELD");
        break;
      case WL_IDLE_STATUS:
        display.println("IDLE");
        break;
      case WL_NO_SSID_AVAIL:
        display.println("NO SSID");
        break;
      case WL_CONNECT_FAILED:
        display.println("FAILED");
        break;
      case WL_CONNECTION_LOST:
        display.println("LOST");
        break;
      case WL_DISCONNECTED:
        display.println("DISCONNECTED");
        break;
      default:
        display.println("UNKNOWN");
        break;
    }
  }
}

void drawWifiConnectScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(35, 0);
  display.println("CONNECT");
  
  if (wifiSelectedIndex >= wifiNetworkCount || wifiNetworkCount == 0) {
    display.setCursor(25, 30);
    display.println("NO NETWORK");
    return;
  }
  
  String selectedSSID = wifiNetworks[wifiSelectedIndex];
  int bracketPos = selectedSSID.indexOf(" [");
  String ssidOnly = selectedSSID.substring(0, bracketPos);
  
  display.setCursor(5, 20);
  display.print("SSID: ");
  if (ssidOnly.length() > 15) {
    display.println(ssidOnly.substring(0, 15));
  } else {
    display.println(ssidOnly);
  }
  
  if (selectedSSID.indexOf("[OPEN]") != -1) {
    display.setCursor(20, 35);
    display.println("OPEN NETWORK");
    display.setCursor(15, 45);
    display.println("PRESS SELECT");
  } else {
    display.setCursor(20, 35);
    display.println("PASSWORD:");
    display.setCursor(15, 45);
    display.println("ENTER PASSWORD");
  }
}

void drawWifiStatusScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(30, 0);
  display.println("WIFI INFO");
  
  wl_status_t status = WiFi.status();
  
  if (status == WL_CONNECTED) {
    display.setCursor(10, 20);
    display.print("Connected to:");
    
    String ssid = WiFi.SSID();
    display.setCursor(5, 30);
    if (ssid.length() > 16) {
      display.println(ssid.substring(0, 14) + "..");
    } else {
      display.println(ssid);
    }
    
    display.setCursor(5, 42);
    display.print("IP: ");
    display.println(WiFi.localIP());
    
    display.setCursor(5, 54);
    display.print("RSSI: ");
    display.print(WiFi.RSSI());
    display.print(" dBm");
  } else if (wifiConnecting) {
    display.setCursor(30, 30);
    display.println("CONNECTING...");
  } else {
    display.setCursor(30, 30);
    display.println("NOT CONNECTED");
  }
}

void drawPasswordEntryScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(30, 0);
  display.println("PASSWORD");
  
  if (wifiSelectedIndex < wifiNetworkCount) {
    String ssid = wifiNetworks[wifiSelectedIndex];
    int bracketPos = ssid.indexOf(" [");
    if (bracketPos != -1) {
      ssid = ssid.substring(0, bracketPos);
    }
    display.setCursor(5, 20);
    display.print("SSID: ");
    if (ssid.length() > 12) {
      display.println(ssid.substring(0, 12) + "..");
    } else {
      display.println(ssid);
    }
  }
  
  display.setCursor(5, 35);
  display.print("Pass: ");
  
  if (passwordCursorPos > 0) {
    for (int i = 0; i < passwordCursorPos; i++) {
      display.print("*");
    }
  } else {
    display.print("<enter>");
  }
  
  display.setCursor(5 + (passwordCursorPos * 6), 45);
  display.print("_");
  
  display.setCursor(5, 55);
  display.print("Char: ");
  if (passwordCursorPos < 63) {
    display.print(passwordChars[passwordCursorPos]);
  } else {
    display.print("MAX");
  }
}

void drawSDMenuScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(45, 0);
  display.println("SD CARD");
  
  String menuItems[3] = {
    "1. VIEW LOGS",
    "2. CLEAR LOGS",
    "3. SD CARD INFO"
  };
  
  for (int i = 0; i < 3; i++) {
    int yPos = 15 + (i * 12);
    
    display.fillRect(0, yPos - 1, 128, 12, SH110X_BLACK);
    
    if (i == sdMenuIndex) {
      display.fillRect(0, yPos - 1, 128, 12, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    display.print(menuItems[i]);
    
    if (i == sdMenuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawSDLogsScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(50, 0);
  display.println("LOGS");
  
  if (sdLogCount == 0) {
    readLogsFromSD();
  }
  
  if (sdLogCount == 0) {
    display.setCursor(30, 30);
    display.println("NO LOGS");
    return;
  }
  
  int startIdx = (sdLogIndex / 3) * 3;
  for (int i = 0; i < 3 && (startIdx + i) < sdLogCount; i++) {
    int yPos = 15 + (i * 12);
    int idx = startIdx + i;
    
    display.fillRect(0, yPos - 1, 128, 12, SH110X_BLACK);
    
    if (idx == sdLogIndex) {
      display.fillRect(0, yPos - 1, 128, 12, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(2, yPos);
    
    String logEntry = sdLogs[idx];
    if (logEntry.length() > 20) {
      logEntry = logEntry.substring(0, 18) + "..";
    }
    display.print(logEntry);
    
    if (idx == sdLogIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawRTCSetupScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(45, 0);
  display.println("RTC SETUP");
  
  DateTime now = rtc.now();
  DateTime adjusted = getAdjustedDateTime();
  
  display.setCursor(5, 20);
  display.print("RTC Time: ");
  display.printf("%02d:%02d", now.hour(), now.minute());
  
  display.setCursor(5, 32);
  display.print("Adj Time: ");
  display.printf("%02d:%02d", adjusted.hour(), adjusted.minute());
  
  display.setCursor(5, 44);
  display.print("Adjustment: +");
  display.print(RTC_TIME_ADJUST_MINUTES);
  display.print(" min");
  
  display.setCursor(20, 55);
  display.print("SEL=Adjust");
}

void drawAboutScreen() {
  display.fillRect(0, 0, 128, 64, SH110X_BLACK);
  
  display.setCursor(45, 0);
  display.println("ABOUT");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  
  display.setCursor(20, 25);
  display.println("SMART ENROLL");
  display.setCursor(35, 35);
  display.println("SYSTEM v3.3");
  
  display.setCursor(5, 50);
  display.print("Sensor: ");
  display.print(fingerprintInitialized ? "OK" : "ERR");
  
  display.setCursor(70, 50);
  display.print("SD: ");
  display.print(sdCardInitialized ? "OK" : "NO");
}

// =================== BUTTON HANDLING ===================
void checkButtons() {
  static bool lastUp = HIGH, lastDown = HIGH, lastSel = HIGH, lastBack = HIGH;
  
  bool upNow = digitalRead(BUTTON_UP);
  bool downNow = digitalRead(BUTTON_DOWN);
  bool selNow = digitalRead(BUTTON_SELECT);
  bool backNow = digitalRead(BUTTON_BACK);
  
  if (upNow == LOW && lastUp == HIGH) handleButtonPress(0);
  if (downNow == LOW && lastDown == HIGH) handleButtonPress(1);
  if (selNow == LOW && lastSel == HIGH) handleButtonPress(2);
  if (backNow == LOW && lastBack == HIGH) handleButtonPress(3);
  
  static unsigned long pressStart[4] = {0, 0, 0, 0};
  bool buttons[4] = {upNow == LOW, downNow == LOW, selNow == LOW, backNow == LOW};
  
  for (int i = 0; i < 4; i++) {
    if (buttons[i]) {
      if (pressStart[i] == 0) pressStart[i] = millis();
      else if (millis() - pressStart[i] > 800 && !buttonLongPressed[i]) {
        buttonLongPressed[i] = true;
        handleLongPress(i);
      }
    } else {
      pressStart[i] = 0;
      buttonLongPressed[i] = false;
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
    connectToWiFi(wifiNetworks[wifiSelectedIndex], password);
    
    memset(passwordChars, 0, sizeof(passwordChars));
    passwordCursorPos = 0;
    needRefresh = true;
  } 
  else if (currentScreen == SCREEN_ENROLL_MODE && button == 2 && enrollState == ENROLL_CAPTURING) {
    resetEnrollmentState();
    showNotificationMsg("Cancelled");
    logToSD("Enrollment cancelled by user");
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
      if (button == 0) menuIndex = (menuIndex > 0) ? menuIndex - 1 : 5;
      else if (button == 1) menuIndex = (menuIndex < 5) ? menuIndex + 1 : 0;
      else if (button == 2) {
        switch(menuIndex) {
          case 0: currentScreen = SCREEN_ENROLL_MODE; resetEnrollmentState(); break;
          case 1: currentScreen = SCREEN_WIFI_SCAN; wifiSelectedIndex = 0; scanWiFiNetworks(); break;
          case 2: currentScreen = SCREEN_NETWORK_STATUS; break;
          case 3: currentScreen = SCREEN_SD_MENU; sdMenuIndex = 0; break;
          case 4: currentScreen = SCREEN_RTC_SETUP; break;
          case 5: currentScreen = SCREEN_ABOUT; break;
        }
      } else if (button == 3) currentScreen = SCREEN_HOME;
      break;
      
    case SCREEN_ENROLL_MODE:
      if (button == 2) {
        if (enrollState == ENROLL_PENDING) {
          enrollState = ENROLL_CAPTURING;
          fpState = FP_IDLE;
          studentInProgress = true;
          enrollmentStartTime = millis();
          showNotificationMsg("Start!");
          logToSD("Enrollment started for Roll: " + String(pendingStudentRoll));
        } else if (enrollState == ENROLL_ERROR) {
          enrollState = ENROLL_PENDING;
          showNotificationMsg("Retry");
        }
      } else if (button == 3) {
        resetEnrollmentState();
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (button == 0) wifiSelectedIndex = (wifiSelectedIndex > 0) ? wifiSelectedIndex - 1 : wifiNetworkCount - 1;
      else if (button == 1) wifiSelectedIndex = (wifiSelectedIndex < wifiNetworkCount - 1) ? wifiSelectedIndex + 1 : 0;
      else if (button == 2) {
        if (wifiNetworkCount > 0 && wifiSelectedIndex < wifiNetworkCount) {
          String selectedSSID = wifiNetworks[wifiSelectedIndex];
          if (selectedSSID.indexOf("[OPEN]") != -1) {
            connectToWiFi(selectedSSID, "");
            currentScreen = SCREEN_WIFI_STATUS;
          } else {
            currentScreen = SCREEN_PASSWORD_ENTRY;
            memset(passwordChars, 0, sizeof(passwordChars));
            passwordCursorPos = 0;
            passwordChars[0] = charSet[0];
          }
        }
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
        wifiSelectedIndex = 0;
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      if (button == 0) {
        if (passwordCursorPos < 62) {
          for (int i = 0; i < charSetLength; i++) {
            if (passwordChars[passwordCursorPos] == charSet[i]) {
              passwordChars[passwordCursorPos] = charSet[(i + 1) % charSetLength];
              break;
            }
          }
        }
      } else if (button == 1) {
        if (passwordCursorPos < 62) {
          for (int i = 0; i < charSetLength; i++) {
            if (passwordChars[passwordCursorPos] == charSet[i]) {
              passwordChars[passwordCursorPos] = charSet[(i - 1 + charSetLength) % charSetLength];
              break;
            }
          }
        }
      } else if (button == 2) {
        if (passwordCursorPos < 62) {
          passwordCursorPos++;
          if (passwordChars[passwordCursorPos] == 0) {
            passwordChars[passwordCursorPos] = charSet[0];
          }
        }
      } else if (button == 3) {
        if (passwordCursorPos > 0) {
          passwordCursorPos--;
        } else {
          currentScreen = SCREEN_WIFI_SCAN;
        }
      }
      break;
      
    case SCREEN_WIFI_STATUS:
      if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_NETWORK_STATUS:
      if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_SD_MENU:
      if (button == 0) sdMenuIndex = (sdMenuIndex > 0) ? sdMenuIndex - 1 : 2;
      else if (button == 1) sdMenuIndex = (sdMenuIndex < 2) ? sdMenuIndex + 1 : 0;
      else if (button == 2) {
        switch(sdMenuIndex) {
          case 0:
            currentScreen = SCREEN_SD_LOGS;
            sdLogIndex = 0;
            readLogsFromSD();
            break;
          case 1:
            clearSDLogs();
            sdLogIndex = 0;
            readLogsFromSD();
            break;
          case 2:
            currentScreen = SCREEN_ABOUT;
            break;
        }
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_SD_LOGS:
      if (button == 0) sdLogIndex = (sdLogIndex > 0) ? sdLogIndex - 1 : sdLogCount - 1;
      else if (button == 1) sdLogIndex = (sdLogIndex < sdLogCount - 1) ? sdLogIndex + 1 : 0;
      else if (button == 3) {
        currentScreen = SCREEN_SD_MENU;
      }
      break;
      
    case SCREEN_RTC_SETUP:
      if (button == 2) {
        adjustRTC();
        needRefresh = true;
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_ABOUT:
      if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    default:
      break;
  }
}