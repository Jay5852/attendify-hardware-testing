/**
 * ESP32 SMART ENROLL SYSTEM - OPTIMIZED ENROLLMENT
 * Fastest possible fingerprint enrollment for R307 sensor
 * Version: 3.2 Fast Enroll
 * OLED UI FIXES APPLIED
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

// Character set
const char* charSet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+[]{}|;:,.<>?";
int charSetLength = 84;

// Backend Configuration - CHANGE THIS TO YOUR IP
String BACKEND_URL = "http://192.168.0.119:5000";

// Enrollment Data
String pendingStudentName = "";
int pendingStudentRoll = -1;
bool studentInProgress = false;
unsigned long enrollmentStartTime = 0;
const unsigned long ENROLLMENT_TIMEOUT = 25000; // Reduced from 30 to 25 seconds
int enrollmentRetryCount = 0;
const int MAX_ENROLLMENT_RETRIES = 3;

// Fingerprint enrollment tracking - OPTIMIZED TIMINGS
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
const unsigned long NOTIFICATION_DURATION = 2000; // Shorter notifications
wl_status_t lastWifiStatus = WL_IDLE_STATUS;

// Day and month names
const char* dayNames[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* monthNames[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", 
                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// UI State Tracking - ADDED FOR FIXES
ScreenState lastScreen = SCREEN_BOOT;
int lastMenuIndex = -1;
bool screenChanged = false;

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
void manualRefreshWiFi();
void attemptEmptyPasswordConnection(String ssid);
void pollServerForEnrollment();
void handleFingerprintEnrollment();
void handleFingerprintEnrollmentFast(); // NEW: Fast enrollment function
void sendEnrollmentConfirmation(int rollNo, int fingerprintId);
void resetEnrollmentState();
void resetFingerprintState();
void showDetailedError(int errorCode);

// Screen drawing functions - UPDATED SIGNATURES
void drawBootScreen(bool forceRedraw);
void drawHomeScreen(bool forceRedraw);
void drawMainMenu(bool forceRedraw);
void drawEnrollmentScreen(bool forceRedraw);
void drawWifiScanScreen(bool forceRedraw);
void drawNetworkStatusScreen(bool forceRedraw);
void drawWifiConnectScreen(bool forceRedraw);
void drawWifiStatusScreen(bool forceRedraw);
void drawPasswordEntryScreen(bool forceRedraw);
void drawAboutScreen(bool forceRedraw);

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔══════════════════════════════════╗");
  Serial.println("║   SMART ENROLL SYSTEM v3.2      ║");
  Serial.println("║   FAST ENROLLMENT VERSION       ║");
  Serial.println("╚══════════════════════════════════╝");
  
  initializeHardware();
  initializePreferences();
  
  currentScreen = SCREEN_BOOT;
  lastScreen = SCREEN_BOOT;
  showScreen();
  delay(1500); // Shorter boot delay
  
  currentScreen = SCREEN_HOME;
  lastScreen = SCREEN_HOME;
  needRefresh = true;
  screenChanged = true;
  
  lastWifiStatus = WiFi.status();
  
  Serial.println("System ready for fast enrollment!");
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
    Serial.println("❌ OLED NOT FOUND!");
    displayInitialized = false;
  } else {
    displayInitialized = true;
    display.clearDisplay();
    display.display(); // Force clear on hardware
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);
    display.setRotation(0);
    
    // Set contrast to reduce ghosting
    display.setContrast(0x7F);
    
    // Disable invert mode if accidentally set
    display.invertDisplay(false);
    
    Serial.println("✅ OLED initialized");
  }
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("⚠️ RTC not found");
  } else {
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println("✅ RTC initialized");
  }
  
  // Initialize Buttons
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  Serial.println("✅ Buttons initialized");
  
  // Initialize Fingerprint Sensor - OPTIMIZED
  Serial.println("Initializing fingerprint sensor...");
  fingerSerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
  delay(1000); // Shorter initialization delay
  
  // Try to connect to fingerprint sensor
  for (int attempt = 1; attempt <= 2; attempt++) { // Only 2 attempts
    Serial.print("Fingerprint sensor attempt ");
    Serial.print(attempt);
    Serial.print("/2... ");
    
    if (finger.verifyPassword()) {
      fingerprintInitialized = true;
      Serial.println("✅ SUCCESS");
      
      // Get template count quickly
      int templateCount = finger.getTemplateCount();
      Serial.print("Templates: ");
      Serial.println(templateCount);
      
      break;
    } else {
      Serial.println("❌ FAILED");
      delay(500); // Shorter retry delay
    }
  }
  
  if (!fingerprintInitialized) {
    Serial.println("❌ FINGERPRINT SENSOR NOT FOUND!");
  }
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50); // Minimal delay
  
  Serial.println("✅ Hardware initialization complete");
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  
  // Check for screen changes
  if (currentScreen != lastScreen) {
    screenChanged = true;
    lastScreen = currentScreen;
    needRefresh = true;
    
    // Force a complete redraw when changing screens
    if (displayInitialized) {
      display.clearDisplay();
      display.display(); // Hardware clear
    }
  }
  
  // Poll server if in enrollment mode, idle, and not currently processing
  if (currentScreen == SCREEN_ENROLL_MODE && 
      enrollState == ENROLL_IDLE && 
      !studentInProgress && 
      WiFi.status() == WL_CONNECTED) {
    
    if (millis() - lastPollTime > POLL_INTERVAL) {
      pollServerForEnrollment();
      lastPollTime = millis();
    }
  }
  
  // Handle fingerprint enrollment process - USING FAST VERSION
  if (enrollState == ENROLL_CAPTURING) {
    handleFingerprintEnrollmentFast(); // Using optimized function
  }
  
  // Check for enrollment timeout
  if (studentInProgress && (millis() - enrollmentStartTime > ENROLLMENT_TIMEOUT)) {
    Serial.println("⏰ Enrollment timeout");
    showNotificationMsg("Timeout");
    resetEnrollmentState();
  }
  
  // Check WiFi status changes
  checkWifiStatusChange();
  
  // Clear notifications after duration
  if (notificationActive && millis() - notificationStartTime > NOTIFICATION_DURATION) {
    clearNotification();
  }
  
  // Screen refresh management
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 200 || needRefresh) { // Faster refresh
    showScreen();
    lastUpdate = millis();
    needRefresh = false;
    screenChanged = false;
  }
  
  delay(20); // Much faster loop for responsiveness
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
  
  // Log to serial for debugging
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
  screenChanged = true;
  
  for (int i = 0; i < 20; i++) {
    wifiNetworks[i] = "";
  }
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);
  
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
  
  if (wifiNetworkCount == 0) {
    showNotificationMsg("No networks");
  } else {
    showNotificationMsg(String(wifiNetworkCount) + " networks");
  }
}

void connectToWiFi(String ssid, String password) {
  connectedSSID = "";
  wifiConnecting = true;
  needRefresh = true;
  screenChanged = true;
  
  int bracketPos = ssid.indexOf(" [");
  if (bracketPos != -1) {
    ssid = ssid.substring(0, bracketPos);
  }
  
  showNotificationMsg("Connecting...");
  
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (attempts < 15) { // Faster timeout
    if (WiFi.status() == WL_CONNECTED) {
      connectedSSID = ssid;
      wifiConnecting = false;
      needRefresh = true;
      screenChanged = true;
      showNotificationMsg("Connected!");
      break;
    }
    delay(300); // Faster checking
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnecting = false;
    needRefresh = true;
    screenChanged = true;
    showNotificationMsg("Failed");
  }
}

// =================== ENROLLMENT FUNCTIONS ===================
void pollServerForEnrollment() {
  if (WiFi.status() != WL_CONNECTED) {
    showNotificationMsg("No WiFi");
    return;
  }
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/next-enrollment");
  http.setTimeout(3000); // Shorter timeout
  
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
        screenChanged = true;
        
        showNotificationMsg("Ready!");
      }
    } else {
      if (enrollState != ENROLL_IDLE) {
        enrollState = ENROLL_IDLE;
        needRefresh = true;
        screenChanged = true;
      }
    }
  }
  
  http.end();
}

// =================== OPTIMIZED FINGERPRINT ENROLLMENT ===================
// This is the FASTEST possible enrollment for R307
void handleFingerprintEnrollmentFast() {
  static unsigned long stateStartTime = 0;
  static int result;
  
  // Initialize
  if (fpState == FP_IDLE) {
    stateStartTime = millis();
    fpStateStartTime = millis();
    firstCaptureDone = false;
    secondCaptureDone = false;
    Serial.println("Starting FAST enrollment...");
    fpState = FP_WAIT_FOR_FIRST;
    showNotificationMsg("Place finger");
  }
  
  // Check timeout
  if (millis() - fpStateStartTime > 20000) { // 20 second timeout
    Serial.println("Enrollment timeout");
    showNotificationMsg("Too slow");
    resetFingerprintState();
    enrollState = ENROLL_ERROR;
    return;
  }
  
  switch(fpState) {
    case FP_WAIT_FOR_FIRST:
      // Check for finger every 200ms (faster)
      if (millis() - stateStartTime > 200) {
        result = finger.getImage();
        
        if (result == FINGERPRINT_OK) {
          Serial.println("First capture");
          fpState = FP_CAPTURE_FIRST;
          showNotificationMsg("Hold...");
        }
        stateStartTime = millis();
      }
      break;
      
    case FP_CAPTURE_FIRST:
      delay(200); // Minimal stabilization
      
      result = finger.image2Tz(1);
      if (result == FINGERPRINT_OK) {
        Serial.println("First image OK");
        firstCaptureDone = true;
        showNotificationMsg("Lift finger");
        fpState = FP_WAIT_FOR_REMOVAL;
        stateStartTime = millis();
      } else {
        Serial.println("First capture failed");
        showNotificationMsg("Failed - Retry");
        fpState = FP_FAILED;
      }
      break;
      
    case FP_WAIT_FOR_REMOVAL:
      // Wait for finger to be lifted - check every 200ms
      if (millis() - stateStartTime > 200) {
        result = finger.getImage();
        
        if (result == FINGERPRINT_NOFINGER) {
          Serial.println("Finger removed");
          showNotificationMsg("Place again");
          delay(600); // MINIMUM wait for sensor reset (cannot be shorter!)
          fpState = FP_WAIT_FOR_SECOND;
          stateStartTime = millis();
        } else if (result == FINGERPRINT_OK) {
          showNotificationMsg("Lift now!");
        }
        stateStartTime = millis();
      }
      break;
      
    case FP_WAIT_FOR_SECOND:
      // Wait for finger to be placed again - check every 200ms
      if (millis() - stateStartTime > 200) {
        result = finger.getImage();
        
        if (result == FINGERPRINT_OK) {
          Serial.println("Second capture");
          fpState = FP_CAPTURE_SECOND;
          showNotificationMsg("Hold...");
        }
        stateStartTime = millis();
      }
      break;
      
    case FP_CAPTURE_SECOND:
      delay(200); // Minimal stabilization
      
      result = finger.image2Tz(2);
      if (result == FINGERPRINT_OK) {
        Serial.println("Second image OK");
        secondCaptureDone = true;
        showNotificationMsg("Processing...");
        fpState = FP_PROCESSING;
        stateStartTime = millis();
      } else {
        Serial.println("Second capture failed");
        showNotificationMsg("Failed - Retry");
        fpState = FP_FAILED;
      }
      break;
      
    case FP_PROCESSING:
      // Process immediately
      result = finger.createModel();
      
      if (result == FINGERPRINT_OK) {
        Serial.println("Model created");
        showNotificationMsg("Storing...");
        
        result = finger.storeModel(pendingStudentRoll);
        
        if (result == FINGERPRINT_OK) {
          Serial.print("Stored ID: ");
          Serial.println(pendingStudentRoll);
          fpState = FP_COMPLETE;
          enrollState = ENROLL_UPLOADING;
          sendEnrollmentConfirmation(pendingStudentRoll, pendingStudentRoll);
        } else {
          Serial.print("Store failed: ");
          Serial.println(result);
          showDetailedError(result);
          fpState = FP_FAILED;
        }
      } else if (result == FINGERPRINT_ENROLLMISMATCH) {
        Serial.println("Fingers don't match");
        showNotificationMsg("Mismatch - Retry");
        fpState = FP_FAILED;
      } else {
        Serial.print("Model failed: ");
        Serial.println(result);
        showNotificationMsg("Failed");
        fpState = FP_FAILED;
      }
      break;
      
    case FP_COMPLETE:
      // Waiting for upload
      break;
      
    case FP_FAILED:
      // Quick reset
      delay(1000);
      resetFingerprintState();
      enrollState = ENROLL_ERROR;
      needRefresh = true;
      screenChanged = true;
      break;
  }
}

void showDetailedError(int errorCode) {
  // Simple error messages
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
    return;
  }
  
  Serial.print("Sending confirmation for Roll ");
  Serial.println(rollNo);
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/enroll-confirm");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(8000); // Faster timeout
  
  JsonDocument doc;
  doc["rollNo"] = rollNo;
  doc["fingerprintId"] = fingerprintId;
  
  String json;
  serializeJson(doc, json);
  
  int httpCode = http.POST(json);
  
  if (httpCode == 200) {
    enrollState = ENROLL_SUCCESS;
    showNotificationMsg("✅ Success!");
    
    // Quick reset
    delay(1000);
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
  studentInProgress = false;
  enrollmentStartTime = 0;
  enrollmentRetryCount = 0;
  fpRetryCount = 0;
  needRefresh = true;
  screenChanged = true;
  
  // Quick clear
  delay(500);
  pendingStudentName = "";
  pendingStudentRoll = -1;
}

void resetFingerprintState() {
  fpState = FP_IDLE;
  fpRetryCount = 0;
}

// =================== DISPLAY FUNCTIONS - FIXED ===================
void showScreen() {
  if (!displayInitialized) return;
  
  // Clear the entire display buffer
  display.clearDisplay();
  
  switch(currentScreen) {
    case SCREEN_BOOT:
      drawBootScreen(screenChanged);
      break;
    case SCREEN_HOME:
      drawHomeScreen(screenChanged);
      break;
    case SCREEN_MAIN_MENU:
      drawMainMenu(screenChanged);
      break;
    case SCREEN_ENROLL_MODE:
      drawEnrollmentScreen(screenChanged);
      break;
    case SCREEN_WIFI_SCAN:
      drawWifiScanScreen(screenChanged);
      break;
    case SCREEN_NETWORK_STATUS:
      drawNetworkStatusScreen(screenChanged);
      break;
    case SCREEN_WIFI_CONNECT:
      drawWifiConnectScreen(screenChanged);
      break;
    case SCREEN_WIFI_STATUS:
      drawWifiStatusScreen(screenChanged);
      break;
    case SCREEN_PASSWORD_ENTRY:
      drawPasswordEntryScreen(screenChanged);
      break;
    case SCREEN_ABOUT:
      drawAboutScreen(screenChanged);
      break;
  }
  
  drawFooter();
  
  // Notification
  if (notificationActive && notificationMessage.length() > 0) {
    // Clear notification area completely
    display.fillRect(0, 50, 128, 14, SH110X_BLACK);
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
  
  // Clear footer area first (bottom 8 pixels)
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
      if (enrollState == ENROLL_PENDING) {
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
      
    default:
      if (currentScreen != SCREEN_BOOT && currentScreen != SCREEN_HOME) {
        display.setCursor(50, 56);
        display.print("B=MENU");
      }
      break;
  }
}

void drawBootScreen(bool forceRedraw) {
  display.setCursor(25, 15);
  display.setTextSize(2);
  display.println("SMART");
  display.setCursor(30, 35);
  display.println("ENROLL");
  display.setTextSize(1);
  display.setCursor(40, 56);
  display.print("v3.2 FAST");
}

void drawHomeScreen(bool forceRedraw) {
  // Clear previous content by drawing background
  if (forceRedraw) {
    display.fillRect(0, 0, 128, 56, SH110X_BLACK);
  }
  
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Clear date area
    display.fillRect(5, 0, 118, 10, SH110X_BLACK);
    
    // Compact date/time
    display.setCursor(5, 0);
    display.printf("%s %02d %s %04d", 
                  dayNames[now.dayOfTheWeek()], 
                  now.day(),
                  monthNames[now.month()-1],
                  now.year());
    
    // Clear time area
    display.fillRect(30, 15, 68, 16, SH110X_BLACK);
    display.setCursor(30, 15);
    display.setTextSize(2);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    // Status - clear each line area
    display.fillRect(5, 35, 60, 8, SH110X_BLACK);
    display.setCursor(5, 35);
    display.print("WiFi: ");
    display.print(WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
    
    display.fillRect(5, 45, 60, 8, SH110X_BLACK);
    display.setCursor(5, 45);
    display.print("Sensor: ");
    display.print(fingerprintInitialized ? "OK" : "ERR");
    
    display.fillRect(5, 55, 60, 8, SH110X_BLACK);
    display.setCursor(5, 55);
    display.print("Enroll: READY");
    
  } else {
    display.fillRect(35, 25, 58, 8, SH110X_BLACK);
    display.setCursor(35, 25);
    display.println("NO RTC");
  }
}

void drawMainMenu(bool forceRedraw) {
  // Clear the menu area
  if (forceRedraw) {
    display.fillRect(0, 0, 128, 56, SH110X_BLACK);
  }
  
  // Header
  display.fillRect(50, 2, 28, 8, SH110X_BLACK);
  display.setCursor(50, 2);
  display.println("MENU");
  display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  
  String menuItems[4] = {
    "1. ENROLL MODE",
    "2. WIFI SCAN",
    "3. NETWORK STATUS",
    "4. ABOUT"
  };
  
  for (int i = 0; i < 4; i++) {
    int yPos = 15 + (i * 12);
    
    // Clear the menu item area before drawing
    display.fillRect(0, yPos - 1, 128, 12, SH110X_BLACK);
    
    if (i == menuIndex) {
      display.fillRect(0, yPos - 1, 128, 12, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    } else {
      display.setTextColor(SH110X_WHITE);
    }
    
    display.setCursor(5, yPos);
    display.print(menuItems[i]);
    
    if (i == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawEnrollmentScreen(bool forceRedraw) {
  if (forceRedraw) {
    display.fillRect(0, 0, 128, 56, SH110X_BLACK);
  }
  
  // Clear header area
  display.fillRect(45, 2, 38, 8, SH110X_BLACK);
  display.setCursor(45, 2);
  display.println("ENROLL");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Clear indicator areas
  display.fillRect(100, 0, 28, 8, SH110X_BLACK);
  display.fillRect(0, 0, 40, 8, SH110X_BLACK);
  
  // Indicators
  display.setCursor(100, 0);
  display.print(WiFi.status() == WL_CONNECTED ? "WIFI" : "NO NET");
  
  display.setCursor(0, 0);
  display.print(fingerprintInitialized ? "FP:OK" : "FP:ERR");
  
  // Clear main content area
  display.fillRect(0, 13, 128, 43, SH110X_BLACK);
  
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
      
      display.fillRect(20, 50, 88, 8, SH110X_BLACK);
      display.setCursor(20, 50);
      display.print("Press SELECT");
      break;
      
    case ENROLL_CAPTURING:
      // Fast enrollment instructions
      display.fillRect(0, 20, 128, 30, SH110X_BLACK);
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
      display.setCursor(45, 25);
      display.println("ERROR");
      display.setCursor(20, 40);
      display.println("Try again");
      break;
      
    default:
      display.setCursor(30, 25);
      display.println("READY");
      break;
  }
}

void drawWifiScanScreen(bool forceRedraw) {
  if (forceRedraw) {
    display.fillRect(0, 0, 128, 56, SH110X_BLACK);
  }
  
  display.fillRect(45, 2, 38, 8, SH110X_BLACK);
  display.setCursor(45, 2);
  display.println("WIFI SCAN");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.fillRect(5, 16, 60, 8, SH110X_BLACK);
  display.setCursor(5, 16);
  display.print("Networks: ");
  display.print(wifiNetworkCount);
  
  if (wifiScanning) {
    display.fillRect(40, 30, 48, 8, SH110X_BLACK);
    display.setCursor(40, 30);
    display.println("SCANNING...");
  } else if (wifiNetworkCount == 0) {
    display.fillRect(25, 30, 78, 8, SH110X_BLACK);
    display.setCursor(25, 30);
    display.println("NO NETWORKS");
  } else {
    // Clear network list area
    display.fillRect(0, 16, 128, 40, SH110X_BLACK);
    
    // Display networks (showing 4 at a time)
    int startIdx = (wifiSelectedIndex / 4) * 4;
    for (int i = 0; i < 4 && (startIdx + i) < wifiNetworkCount; i++) {
      int yPos = 16 + (i * 12);
      int idx = startIdx + i;
      
      // Clear the line area
      display.fillRect(0, yPos - 1, 128, 12, SH110X_BLACK);
      
      if (idx == wifiSelectedIndex) {
        display.fillRect(0, yPos - 1, 128, 12, SH110X_WHITE);
        display.setTextColor(SH110X_BLACK);
      } else {
        display.setTextColor(SH110X_WHITE);
      }
      
      display.setCursor(2, yPos);
      display.print(">");
      
      // Display network name
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

void drawNetworkStatusScreen(bool forceRedraw) {
  if (forceRedraw) {
    display.fillRect(0, 0, 128, 56, SH110X_BLACK);
  }
  
  display.fillRect(15, 2, 98, 8, SH110X_BLACK);
  display.setCursor(15, 2);
  display.println("NETWORK STATUS");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  wl_status_t status = WiFi.status();
  
  // Clear content area
  display.fillRect(10, 20, 108, 36, SH110X_BLACK);
  
  display.setCursor(10, 20);
  display.print("Status: ");
  switch(status) {
    case WL_CONNECTED:
      display.println("CONNECTED");
      display.setCursor(10, 32);
      display.print("SSID: ");
      display.println(WiFi.SSID());
      display.setCursor(10, 44);
      display.print("IP: ");
      display.println(WiFi.localIP());
      break;
    case WL_NO_SHIELD:
      display.println("NO SHIELD");
      break;
    case WL_IDLE_STATUS:
      display.println("IDLE");
      break;
    case WL_NO_SSID_AVAIL:
      display.println("NO SSID");
      break;
    case WL_SCAN_COMPLETED:
      display.println("SCAN DONE");
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
  }
}

void drawWifiConnectScreen(bool forceRedraw) {
  if (forceRedraw) {
    display.fillRect(0, 0, 128, 56, SH110X_BLACK);
  }
  
  display.fillRect(35, 2, 58, 8, SH110X_BLACK);
  display.setCursor(35, 2);
  display.println("CONNECT");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  if (wifiSelectedIndex >= wifiNetworkCount || wifiNetworkCount == 0) {
    display.fillRect(25, 30, 78, 8, SH110X_BLACK);
    display.setCursor(25, 30);
    display.println("NO NETWORK");
    return;
  }
  
  String selectedSSID = wifiNetworks[wifiSelectedIndex];
  int bracketPos = selectedSSID.indexOf(" [");
  String ssidOnly = selectedSSID.substring(0, bracketPos);
  
  display.fillRect(5, 16, 118, 8, SH110X_BLACK);
  display.setCursor(5, 16);
  display.print("SSID: ");
  if (ssidOnly.length() > 15) {
    display.println(ssidOnly.substring(0, 15));
  } else {
    display.println(ssidOnly);
  }
  
  display.fillRect(0, 32, 128, 24, SH110X_BLACK);
  if (selectedSSID.indexOf("[OPEN]") != -1) {
    display.setCursor(20, 32);
    display.println("OPEN NETWORK");
    display.setCursor(15, 45);
    display.println("PRESS SELECT");
  } else {
    display.setCursor(20, 32);
    display.println("PASSWORD:");
    display.setCursor(15, 45);
    display.println("ENTER PASSWORD");
  }
}

void drawWifiStatusScreen(bool forceRedraw) {
  if (forceRedraw) {
    display.fillRect(0, 0, 128, 56, SH110X_BLACK);
  }
  
  display.fillRect(30, 2, 68, 8, SH110X_BLACK);
  display.setCursor(30, 2);
  display.println("WIFI INFO");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  wl_status_t status = WiFi.status();
  
  // Clear content area
  display.fillRect(0, 13, 128, 43, SH110X_BLACK);
  
  if (status == WL_CONNECTED) {
    display.setCursor(10, 20);
    display.print("Connected to:");
    display.setCursor(5, 30);
    
    String ssid = WiFi.SSID();
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

void drawPasswordEntryScreen(bool forceRedraw) {
  if (forceRedraw) {
    display.fillRect(0, 0, 128, 56, SH110X_BLACK);
  }
  
  display.fillRect(30, 2, 68, 8, SH110X_BLACK);
  display.setCursor(30, 2);
  display.println("PASSWORD");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.fillRect(5, 16, 118, 8, SH110X_BLACK);
  display.setCursor(5, 16);
  display.print("SSID: ");
  if (wifiSelectedIndex < wifiNetworkCount) {
    String ssid = wifiNetworks[wifiSelectedIndex];
    int bracketPos = ssid.indexOf(" [");
    if (bracketPos != -1) {
      ssid = ssid.substring(0, bracketPos);
    }
    if (ssid.length() > 12) {
      display.println(ssid.substring(0, 12) + "..");
    } else {
      display.println(ssid);
    }
  }
  
  // Clear password display area
  display.fillRect(5, 30, 118, 8, SH110X_BLACK);
  display.setCursor(5, 30);
  display.print("Pass: ");
  if (passwordCursorPos > 0) {
    for (int i = 0; i < passwordCursorPos; i++) {
      display.print("*");
    }
  } else {
    display.print("<enter>");
  }
  
  // Clear cursor area
  display.fillRect(5, 40, 118, 10, SH110X_BLACK);
  
  // Show cursor
  display.setCursor(5 + (passwordCursorPos * 6), 40);
  display.print("_");
  
  // Show current character
  display.fillRect(50, 50, 40, 8, SH110X_BLACK);
  display.setCursor(50, 50);
  display.print("Char: ");
  if (passwordCursorPos < 63) {
    display.print(passwordChars[passwordCursorPos]);
  } else {
    display.print("MAX");
  }
}

void drawAboutScreen(bool forceRedraw) {
  if (forceRedraw) {
    display.fillRect(0, 0, 128, 56, SH110X_BLACK);
  }
  
  display.fillRect(45, 2, 38, 8, SH110X_BLACK);
  display.setCursor(45, 2);
  display.println("ABOUT");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.fillRect(20, 20, 88, 16, SH110X_BLACK);
  display.setCursor(20, 20);
  display.println("SMART ENROLL");
  display.setCursor(35, 30);
  display.println("SYSTEM v3.2");
  
  display.fillRect(5, 45, 60, 8, SH110X_BLACK);
  display.setCursor(5, 45);
  display.print("Sensor: ");
  display.print(fingerprintInitialized ? "OK" : "ERR");
  
  display.fillRect(5, 55, 60, 8, SH110X_BLACK);
  display.setCursor(5, 55);
  display.print("WiFi: ");
  display.print(WiFi.status() == WL_CONNECTED ? "ON" : "OFF");
}

// =================== BUTTON HANDLING - OPTIMIZED ===================
void checkButtons() {
  static bool lastUp = HIGH, lastDown = HIGH, lastSel = HIGH, lastBack = HIGH;
  
  bool upNow = digitalRead(BUTTON_UP);
  bool downNow = digitalRead(BUTTON_DOWN);
  bool selNow = digitalRead(BUTTON_SELECT);
  bool backNow = digitalRead(BUTTON_BACK);
  
  // Quick button press detection
  if (upNow == LOW && lastUp == HIGH) handleButtonPress(0);
  if (downNow == LOW && lastDown == HIGH) handleButtonPress(1);
  if (selNow == LOW && lastSel == HIGH) handleButtonPress(2);
  if (backNow == LOW && lastBack == HIGH) handleButtonPress(3);
  
  // Long press detection (simplified)
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
    screenChanged = true;
    connectToWiFi(wifiNetworks[wifiSelectedIndex], password);
    
    memset(passwordChars, 0, sizeof(passwordChars));
    passwordCursorPos = 0;
    needRefresh = true;
  } 
  else if (currentScreen == SCREEN_ENROLL_MODE && button == 2 && enrollState == ENROLL_CAPTURING) {
    resetEnrollmentState();
    showNotificationMsg("Cancelled");
  }
}

void handleButtonPress(int button) {
  needRefresh = true;
  
  // Track menu index changes for proper redraw
  int oldMenuIndex = menuIndex;
  
  switch(currentScreen) {
    case SCREEN_HOME:
      if (button == 2) {
        currentScreen = SCREEN_MAIN_MENU;
        menuIndex = 0;
        screenChanged = true;
      }
      break;
      
    case SCREEN_MAIN_MENU:
      if (button == 0) {
        menuIndex = (menuIndex > 0) ? menuIndex - 1 : 3;
        if (oldMenuIndex != menuIndex) needRefresh = true;
      } else if (button == 1) {
        menuIndex = (menuIndex < 3) ? menuIndex + 1 : 0;
        if (oldMenuIndex != menuIndex) needRefresh = true;
      } else if (button == 2) {
        switch(menuIndex) {
          case 0: currentScreen = SCREEN_ENROLL_MODE; resetEnrollmentState(); screenChanged = true; break;
          case 1: currentScreen = SCREEN_WIFI_SCAN; wifiSelectedIndex = 0; scanWiFiNetworks(); screenChanged = true; break;
          case 2: currentScreen = SCREEN_NETWORK_STATUS; screenChanged = true; break;
          case 3: currentScreen = SCREEN_ABOUT; screenChanged = true; break;
        }
      } else if (button == 3) {
        currentScreen = SCREEN_HOME;
        screenChanged = true;
      }
      break;
      
    case SCREEN_ENROLL_MODE:
      if (button == 2) {
        if (enrollState == ENROLL_PENDING) {
          enrollState = ENROLL_CAPTURING;
          fpState = FP_IDLE;
          studentInProgress = true;
          enrollmentStartTime = millis();
          showNotificationMsg("Start!");
          needRefresh = true;
        } else if (enrollState == ENROLL_ERROR) {
          enrollState = ENROLL_PENDING;
          showNotificationMsg("Retry");
          needRefresh = true;
        }
      } else if (button == 3) {
        resetEnrollmentState();
        currentScreen = SCREEN_MAIN_MENU;
        screenChanged = true;
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (button == 0) {
        wifiSelectedIndex = (wifiSelectedIndex > 0) ? wifiSelectedIndex - 1 : wifiNetworkCount - 1;
        needRefresh = true;
      } else if (button == 1) {
        wifiSelectedIndex = (wifiSelectedIndex < wifiNetworkCount - 1) ? wifiSelectedIndex + 1 : 0;
        needRefresh = true;
      } else if (button == 2) {
        if (wifiNetworkCount > 0 && wifiSelectedIndex < wifiNetworkCount) {
          String selectedSSID = wifiNetworks[wifiSelectedIndex];
          if (selectedSSID.indexOf("[OPEN]") != -1) {
            // Open network
            connectToWiFi(selectedSSID, "");
            currentScreen = SCREEN_WIFI_STATUS;
            screenChanged = true;
          } else {
            // Password required
            currentScreen = SCREEN_PASSWORD_ENTRY;
            screenChanged = true;
            memset(passwordChars, 0, sizeof(passwordChars));
            passwordCursorPos = 0;
            passwordChars[0] = charSet[0];
          }
        }
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
        wifiSelectedIndex = 0;
        screenChanged = true;
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      if (button == 0) {
        // Next character
        if (passwordCursorPos < 62) {
          for (int i = 0; i < charSetLength; i++) {
            if (passwordChars[passwordCursorPos] == charSet[i]) {
              passwordChars[passwordCursorPos] = charSet[(i + 1) % charSetLength];
              needRefresh = true;
              break;
            }
          }
        }
      } else if (button == 1) {
        // Previous character
        if (passwordCursorPos < 62) {
          for (int i = 0; i < charSetLength; i++) {
            if (passwordChars[passwordCursorPos] == charSet[i]) {
              passwordChars[passwordCursorPos] = charSet[(i - 1 + charSetLength) % charSetLength];
              needRefresh = true;
              break;
            }
          }
        }
      } else if (button == 2) {
        // Move to next position or confirm
        if (passwordCursorPos < 62) {
          passwordCursorPos++;
          if (passwordChars[passwordCursorPos] == 0) {
            passwordChars[passwordCursorPos] = charSet[0];
          }
          needRefresh = true;
        }
      } else if (button == 3) {
        // Back or cancel
        if (passwordCursorPos > 0) {
          passwordCursorPos--;
          needRefresh = true;
        } else {
          currentScreen = SCREEN_WIFI_SCAN;
          screenChanged = true;
        }
      }
      break;
      
    case SCREEN_WIFI_STATUS:
      if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
        screenChanged = true;
      }
      break;
      
    case SCREEN_NETWORK_STATUS:
      if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
        screenChanged = true;
      }
      break;
      
    case SCREEN_ABOUT:
      if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
        screenChanged = true;
      }
      break;
      
    default:
      break;
  }
}