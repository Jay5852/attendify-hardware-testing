/**
 * ESP32 SMART ENROLL SYSTEM - COMPLETE FIXED VERSION
 * IoT Biometric Attendance System with Smart Enroll Feature
 * Version: 3.1 Production - Compatible Version
 * Date: January 2025
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
const unsigned long ENROLLMENT_TIMEOUT = 30000; // 30 seconds
int enrollmentRetryCount = 0;
const int MAX_ENROLLMENT_RETRIES = 3;

// Fingerprint enrollment tracking
unsigned long fpStateStartTime = 0;
int fpRetryCount = 0;
const int MAX_FP_RETRIES = 2;
String lastFingerprintError = "";

// Server Polling
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 2000;

// Refresh & Notifications
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
void manualRefreshWiFi();
void attemptEmptyPasswordConnection(String ssid);
void pollServerForEnrollment();
void handleFingerprintEnrollment();
void sendEnrollmentConfirmation(int rollNo, int fingerprintId);
void resetEnrollmentState();
void resetFingerprintState();
void testFingerprintSensor();
void showDetailedError(int errorCode);

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
  
  Serial.println("\n╔══════════════════════════════════╗");
  Serial.println("║   SMART ENROLL SYSTEM v3.1      ║");
  Serial.println("║   COMPATIBLE VERSION            ║");
  Serial.println("╚══════════════════════════════════╝");
  
  initializeHardware();
  initializePreferences();
  
  currentScreen = SCREEN_BOOT;
  showScreen();
  delay(2000);
  
  currentScreen = SCREEN_HOME;
  needRefresh = true;
  
  lastWifiStatus = WiFi.status();
  
  Serial.println("System ready!");
}

// =================== INITIALIZE PREFERENCES ===================
void initializePreferences() {
  preferences.begin("enroll_system", false);
  
  // Load saved WiFi credentials if any
  String savedSSID = preferences.getString("wifi_ssid", "");
  String savedPass = preferences.getString("wifi_pass", "");
  
  if (savedSSID.length() > 0) {
    Serial.println("Found saved WiFi credentials for: " + savedSSID);
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
    Serial.println("⚠️ RTC not found - using system time");
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
  
  // Initialize Fingerprint Sensor with robust initialization
  Serial.println("Initializing fingerprint sensor...");
  fingerSerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
  delay(1500); // Give sensor extra time to initialize
  
  // Try multiple times to connect to fingerprint sensor
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.print("Fingerprint sensor attempt ");
    Serial.print(attempt);
    Serial.print("/3... ");
    
    if (finger.verifyPassword()) {
      fingerprintInitialized = true;
      Serial.println("✅ SUCCESS");
      
      // Get template count
      int templateCount = finger.getTemplateCount();
      Serial.print("Templates in sensor: ");
      Serial.println(templateCount);
      
      break;
    } else {
      Serial.println("❌ FAILED");
      delay(1000);
    }
  }
  
  if (!fingerprintInitialized) {
    Serial.println("❌ FINGERPRINT SENSOR NOT FOUND!");
    Serial.println("Check wiring: TX->GPIO4, RX->GPIO2");
    Serial.println("Check power: 3.3V stable supply required");
  }
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.println("✅ Hardware initialization complete");
}

// =================== TEST FINGERPRINT SENSOR ===================
void testFingerprintSensor() {
  if (!fingerprintInitialized) return;
  
  Serial.println("\n🔍 Fingerprint Sensor Diagnostic:");
  Serial.println("══════════════════════════════════");
  
  // Check communication
  if (finger.verifyPassword()) {
    Serial.println("✓ Communication: OK");
  } else {
    Serial.println("✗ Communication: FAILED");
    return;
  }
  
  // Get template count
  int templateCount = finger.getTemplateCount();
  Serial.print("✓ Templates stored: ");
  Serial.println(templateCount);
  
  Serial.println("══════════════════════════════════\n");
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  
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
  
  // Handle fingerprint enrollment process
  if (enrollState == ENROLL_CAPTURING) {
    handleFingerprintEnrollment();
  }
  
  // Check for enrollment timeout
  if (studentInProgress && (millis() - enrollmentStartTime > ENROLLMENT_TIMEOUT)) {
    Serial.println("⏰ Enrollment timeout - resetting");
    showNotificationMsg("Timeout - Reset");
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
  if (millis() - lastUpdate > 250 || needRefresh) {
    showScreen();
    lastUpdate = millis();
    needRefresh = false;
  }
  
  delay(30); // Reduced delay for more responsive buttons
}

// =================== WIFI FUNCTIONS ===================
void checkWifiStatusChange() {
  wl_status_t currentStatus = WiFi.status();
  
  if (currentStatus != lastWifiStatus) {
    needRefresh = true;
    lastWifiStatus = currentStatus;
    
    // Show connection status notifications
    if (currentStatus == WL_CONNECTED && lastWifiStatus != WL_CONNECTED) {
      showNotificationMsg("WiFi Connected");
    } else if (currentStatus == WL_DISCONNECTED && lastWifiStatus == WL_CONNECTED) {
      showNotificationMsg("WiFi Disconnected");
    }
  }
}

void showNotificationMsg(String message) {
  notificationMessage = message;
  notificationActive = true;
  notificationStartTime = millis();
  needRefresh = true;
  
  // Also log to serial for debugging
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
  
  showNotificationMsg("Scanning WiFi...");
  
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
    showNotificationMsg("No networks found");
  } else {
    showNotificationMsg(String(wifiNetworkCount) + " networks");
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
  Serial.println("Connecting to WiFi: " + ssid);
  
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (attempts < 25) { // Increased timeout
    if (WiFi.status() == WL_CONNECTED) {
      connectedSSID = ssid;
      wifiConnecting = false;
      needRefresh = true;
      
      // Save credentials
      preferences.putString("wifi_ssid", ssid);
      if (password.length() > 0) {
        preferences.putString("wifi_pass", password);
      }
      
      showNotificationMsg("Connected!");
      Serial.println("WiFi connected! IP: " + WiFi.localIP().toString());
      break;
    }
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnecting = false;
    needRefresh = true;
    showNotificationMsg("Connection failed");
    Serial.println("WiFi connection failed");
  }
}

// =================== ENROLLMENT FUNCTIONS ===================
void pollServerForEnrollment() {
  if (WiFi.status() != WL_CONNECTED) {
    showNotificationMsg("No WiFi");
    return;
  }
  
  Serial.println("Polling server for enrollment...");
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/next-enrollment");
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    Serial.println("Server response: " + payload);
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
      return;
    }
    
    bool pending = doc["pending"];
    
    if (pending) {
      String newName = doc["name"].as<String>();
      int newRoll = doc["rollNo"];
      
      // Only update if it's a different student
      if (pendingStudentRoll != newRoll || pendingStudentName != newName) {
        pendingStudentName = newName;
        pendingStudentRoll = newRoll;
        enrollState = ENROLL_PENDING;
        studentInProgress = false;
        needRefresh = true;
        
        Serial.print("New student ready: ");
        Serial.print(newRoll);
        Serial.print(" - ");
        Serial.println(newName);
        
        showNotificationMsg("Student ready!");
      }
    } else {
      // No pending students
      if (enrollState != ENROLL_IDLE) {
        enrollState = ENROLL_IDLE;
        needRefresh = true;
      }
    }
  } else {
    Serial.print("HTTP error: ");
    Serial.println(httpCode);
    showNotificationMsg("Server error");
  }
  
  http.end();
}

// =================== ROBUST FINGERPRINT ENROLLMENT ===================
void handleFingerprintEnrollment() {
  static unsigned long lastFingerCheck = 0;
  static unsigned long stateStartTime = 0;
  static int result; // Declare outside switch to avoid cross-initialization
  
  // Initialize state start time
  if (fpState == FP_IDLE) {
    stateStartTime = millis();
    fpStateStartTime = millis();
    Serial.println("Starting fingerprint enrollment...");
  }
  
  // Check for overall timeout
  if (millis() - fpStateStartTime > ENROLLMENT_TIMEOUT) {
    Serial.println("Fingerprint enrollment timeout");
    showNotificationMsg("Timeout - Start over");
    resetFingerprintState();
    enrollState = ENROLL_ERROR;
    return;
  }
  
  // Check for state timeout (15 seconds per state)
  if (millis() - stateStartTime > 15000) {
    Serial.print("State timeout in state: ");
    Serial.println(fpState);
    showNotificationMsg("Too slow - Retry");
    resetFingerprintState();
    fpState = FP_WAIT_FOR_FIRST;
    stateStartTime = millis();
    fpRetryCount++;
    
    if (fpRetryCount >= MAX_FP_RETRIES) {
      enrollState = ENROLL_ERROR;
      showNotificationMsg("Max retries reached");
      return;
    }
    return;
  }
  
  switch(fpState) {
    case FP_IDLE:
      fpState = FP_WAIT_FOR_FIRST;
      showNotificationMsg("Place finger");
      Serial.println("State: Waiting for first finger");
      stateStartTime = millis();
      break;
      
    case FP_WAIT_FOR_FIRST:
      if (millis() - lastFingerCheck > 500) {
        result = finger.getImage();
        
        if (result == FINGERPRINT_OK) {
          Serial.println("First finger detected");
          fpState = FP_CAPTURE_FIRST;
          showNotificationMsg("Hold steady...");
          stateStartTime = millis();
        } 
        else if (result != FINGERPRINT_NOFINGER) {
          Serial.print("Sensor error: ");
          Serial.println(result);
        }
        
        lastFingerCheck = millis();
      }
      break;
      
    case FP_CAPTURE_FIRST:
      delay(400); // Important: Wait for stable reading
      
      result = finger.image2Tz(1);
      if (result == FINGERPRINT_OK) {
        Serial.println("First image captured successfully");
        showNotificationMsg("Remove finger");
        fpState = FP_WAIT_FOR_REMOVAL;
        stateStartTime = millis();
      } else {
        Serial.println("First image capture failed");
        showNotificationMsg("Capture failed");
        fpState = FP_FAILED;
      }
      break;
      
    case FP_WAIT_FOR_REMOVAL:
      if (millis() - lastFingerCheck > 500) {
        result = finger.getImage();
        
        if (result == FINGERPRINT_NOFINGER) {
          Serial.println("Finger removed");
          showNotificationMsg("Place SAME finger");
          delay(1200); // CRITICAL: Wait for sensor to reset completely
          fpState = FP_WAIT_FOR_SECOND;
          stateStartTime = millis();
        } 
        else if (result == FINGERPRINT_OK) {
          showNotificationMsg("Remove finger now");
        }
        
        lastFingerCheck = millis();
      }
      break;
      
    case FP_WAIT_FOR_SECOND:
      if (millis() - lastFingerCheck > 500) {
        result = finger.getImage();
        
        if (result == FINGERPRINT_OK) {
          Serial.println("Second finger detected");
          fpState = FP_CAPTURE_SECOND;
          showNotificationMsg("Capturing...");
          stateStartTime = millis();
        } 
        else if (result != FINGERPRINT_NOFINGER) {
          Serial.print("Sensor error: ");
          Serial.println(result);
        }
        
        lastFingerCheck = millis();
      }
      break;
      
    case FP_CAPTURE_SECOND:
      delay(400); // Wait for stable reading
      
      result = finger.image2Tz(2);
      if (result == FINGERPRINT_OK) {
        Serial.println("Second image captured");
        showNotificationMsg("Processing...");
        fpState = FP_PROCESSING;
        stateStartTime = millis();
      } else {
        Serial.println("Second image capture failed");
        showNotificationMsg("Capture failed");
        fpState = FP_FAILED;
      }
      break;
      
    case FP_PROCESSING:
      Serial.println("Creating fingerprint model...");
      
      result = finger.createModel();
      
      if (result == FINGERPRINT_OK) {
        Serial.println("Model created successfully");
        showNotificationMsg("Storing...");
        
        // Store with student's roll number as ID
        result = finger.storeModel(pendingStudentRoll);
        
        if (result == FINGERPRINT_OK) {
          Serial.print("Fingerprint stored with ID: ");
          Serial.println(pendingStudentRoll);
          fpState = FP_COMPLETE;
          enrollState = ENROLL_UPLOADING;
          sendEnrollmentConfirmation(pendingStudentRoll, pendingStudentRoll);
        } 
        else {
          Serial.print("Store failed, error code: ");
          Serial.println(result);
          showDetailedError(result);
          fpState = FP_FAILED;
        }
      } 
      else if (result == FINGERPRINT_ENROLLMISMATCH) {
        Serial.println("ERROR: Fingerprints don't match");
        showNotificationMsg("Fingers don't match");
        fpState = FP_FAILED;
      }
      else {
        Serial.print("ERROR: Create model failed, code: ");
        Serial.println(result);
        showNotificationMsg("Model failed");
        fpState = FP_FAILED;
      }
      break;
      
    case FP_COMPLETE:
      // Enrollment complete, waiting for upload
      break;
      
    case FP_FAILED:
      // Wait and reset
      delay(2000);
      resetFingerprintState();
      enrollState = ENROLL_ERROR;
      needRefresh = true;
      break;
  }
}

void showDetailedError(int errorCode) {
  // Simple error mapping for older library versions
  switch(errorCode) {
    case FINGERPRINT_OK:
      lastFingerprintError = "Success";
      break;
    case FINGERPRINT_NOFINGER:
      lastFingerprintError = "No finger";
      break;
    case FINGERPRINT_IMAGEFAIL:
      lastFingerprintError = "Image capture failed";
      break;
    case FINGERPRINT_IMAGEMESS:
      lastFingerprintError = "Image too messy";
      break;
    case FINGERPRINT_PACKETRESPONSEFAIL:
      lastFingerprintError = "Packet response failed";
      break;
    case FINGERPRINT_ENROLLMISMATCH:
      lastFingerprintError = "Finger mismatch";
      break;
    case FINGERPRINT_BADLOCATION:
      lastFingerprintError = "Bad location";
      break;
    case FINGERPRINT_FLASHERR:
      lastFingerprintError = "Flash error";
      break;
    default:
      lastFingerprintError = "Error code: " + String(errorCode);
      break;
  }
  
  Serial.print("Fingerprint error: ");
  Serial.println(lastFingerprintError);
  showNotificationMsg("Error: " + lastFingerprintError);
}

void sendEnrollmentConfirmation(int rollNo, int fingerprintId) {
  if (WiFi.status() != WL_CONNECTED) {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("No WiFi connection");
    return;
  }
  
  Serial.print("Sending enrollment confirmation for Roll ");
  Serial.println(rollNo);
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/enroll-confirm");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  
  JsonDocument doc;
  doc["rollNo"] = rollNo;
  doc["fingerprintId"] = fingerprintId;
  
  String json;
  serializeJson(doc, json);
  
  Serial.println("Sending JSON: " + json);
  int httpCode = http.POST(json);
  
  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("Server response: " + response);
    
    JsonDocument resDoc;
    DeserializationError error = deserializeJson(resDoc, response);
    
    if (!error && resDoc["success"]) {
      enrollState = ENROLL_SUCCESS;
      showNotificationMsg("✅ Enrollment Complete!");
      Serial.println("Enrollment successful!");
      
      // Reset after showing success
      delay(2000);
      resetEnrollmentState();
    } else {
      enrollState = ENROLL_ERROR;
      showNotificationMsg("Server error");
      Serial.println("Server returned error");
    }
  } else {
    Serial.print("HTTP Error: ");
    Serial.println(httpCode);
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
  
  // Keep student info for a moment so user can see completion
  delay(1500);
  pendingStudentName = "";
  pendingStudentRoll = -1;
  
  Serial.println("Enrollment state reset");
}

void resetFingerprintState() {
  fpState = FP_IDLE;
  fpRetryCount = 0;
  Serial.println("Fingerprint state reset");
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
  
  // Show notification overlay
  if (notificationActive && notificationMessage.length() > 0) {
    display.fillRect(0, 50, 128, 14, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(4, 52);
    
    String displayMsg = notificationMessage;
    if (displayMsg.length() > 18) {
      displayMsg = displayMsg.substring(0, 16) + "..";
    }
    display.print(displayMsg);
    
    display.setTextColor(SH110X_WHITE);
  }
  
  display.display();
}

void drawFooter() {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
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
      } else if (enrollState == ENROLL_SUCCESS) {
        display.setCursor(45, 56);
        display.print("DONE");
      } else if (enrollState == ENROLL_ERROR) {
        display.setCursor(40, 56);
        display.print("SEL=RETRY");
      } else {
        display.setCursor(45, 56);
        display.print("READY");
      }
      break;
      
    case SCREEN_WIFI_SCAN:
      if (!wifiScanning) {
        display.setCursor(5, 56);
        display.print("SEL=REF");
        display.setCursor(50, 56);
        display.print("L=C");
        display.setCursor(85, 56);
        display.print("B=MENU");
      }
      break;
      
    case SCREEN_PASSWORD_ENTRY:
      display.setCursor(5, 56);
      display.print("U/D=CHR");
      display.setCursor(50, 56);
      display.print("SEL=NXT");
      display.setCursor(90, 56);
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
  display.setCursor(25, 15);
  display.setTextSize(2);
  display.println("SMART");
  display.setCursor(30, 35);
  display.println("ENROLL");
  display.setTextSize(1);
  display.setCursor(45, 56);
  display.print("v3.1 COMPATIBLE");
}

void drawHomeScreen() {
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Date line (top)
    display.setCursor(5, 0);
    display.printf("%s %02d %s", 
                  dayNames[now.dayOfTheWeek()], 
                  now.day(),
                  monthNames[now.month()-1]);
    
    // Year (right aligned)
    display.setCursor(90, 0);
    display.printf("%04d", now.year());
    
    // Time (center)
    display.setCursor(30, 15);
    display.setTextSize(2);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    // Status line
    display.setCursor(5, 35);
    display.print("Status: ");
    if (WiFi.status() == WL_CONNECTED) {
      display.print("ONLINE");
    } else {
      display.print("OFFLINE");
    }
    
    // Fingerprint sensor status
    display.setCursor(5, 45);
    display.print("Sensor: ");
    if (fingerprintInitialized) {
      display.print("READY");
    } else {
      display.print("ERROR");
    }
    
    // Enrollment status
    display.setCursor(5, 55);
    display.print("Enroll: IDLE");
    
  } else {
    // RTC not found
    display.setCursor(35, 20);
    display.println("RTC ERROR");
    display.setCursor(30, 35);
    display.println("Check RTC Module");
  }
}

void drawMainMenu() {
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
    
    if (i == menuIndex) {
      display.fillRect(0, yPos - 1, 128, 12, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    display.print(menuItems[i]);
    
    if (i == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawEnrollmentScreen() {
  display.setCursor(45, 2);
  display.println("ENROLL");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // WiFi indicator
  display.setCursor(100, 0);
  if (WiFi.status() == WL_CONNECTED) {
    display.print("WIFI");
  } else {
    display.print("NO NET");
  }
  
  // Sensor indicator
  display.setCursor(0, 0);
  if (fingerprintInitialized) {
    display.print("FP:OK");
  } else {
    display.print("FP:ERR");
  }
  
  switch(enrollState) {
    case ENROLL_IDLE:
      display.setCursor(25, 25);
      display.println("Waiting...");
      display.setCursor(20, 40);
      display.println("Polling server");
      break;
      
    case ENROLL_PENDING:
      display.setCursor(35, 15);
      display.println("STUDENT");
      display.drawLine(30, 23, 98, 23, SH110X_WHITE);
      
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
      
    case ENROLL_CAPTURING:
      // Show step-by-step guidance
      display.setCursor(35, 15);
      
      switch(fpState) {
        case FP_WAIT_FOR_FIRST:
          display.println("STEP 1/3");
          display.setCursor(15, 30);
          display.println("Place finger on");
          display.setCursor(40, 42);
          display.println("sensor");
          break;
        case FP_WAIT_FOR_REMOVAL:
          display.println("STEP 2/3");
          display.setCursor(10, 30);
          display.println("Remove finger");
          display.setCursor(30, 42);
          display.println("now");
          break;
        case FP_WAIT_FOR_SECOND:
          display.println("STEP 3/3");
          display.setCursor(10, 30);
          display.println("Place same");
          display.setCursor(30, 42);
          display.println("finger");
          break;
        case FP_PROCESSING:
          display.println("PROCESSING");
          display.setCursor(25, 30);
          display.println("Creating");
          display.setCursor(35, 42);
          display.println("template");
          break;
        default:
          display.println("CAPTURING");
          display.setCursor(30, 35);
          display.println("Fingerprint");
          break;
      }
      break;
      
    case ENROLL_UPLOADING:
      display.setCursor(30, 25);
      display.println("UPLOADING");
      display.setCursor(20, 40);
      display.println("Please wait...");
      break;
      
    case ENROLL_SUCCESS:
      display.setCursor(40, 25);
      display.println("SUCCESS");
      display.setCursor(15, 40);
      display.println("Enrollment complete!");
      break;
      
    case ENROLL_ERROR:
      display.setCursor(45, 15);
      display.println("ERROR");
      display.setCursor(20, 30);
      display.println("Please try again");
      
      // Show specific error if available
      if (lastFingerprintError.length() > 0) {
        display.setCursor(10, 45);
        if (lastFingerprintError.length() > 16) {
          display.print(lastFingerprintError.substring(0, 14) + "..");
        } else {
          display.print(lastFingerprintError);
        }
      }
      break;
  }
}

void drawWifiScanScreen() {
  display.setCursor(45, 0);
  display.println("WIFI");
  
  if (wifiScanning) {
    display.setCursor(35, 25);
    display.println("SCANNING...");
    return;
  }
  
  if (wifiNetworkCount == 0) {
    display.setCursor(20, 20);
    display.println("No networks");
    display.setCursor(10, 35);
    display.println("Press SELECT");
    display.setCursor(25, 45);
    display.println("to refresh");
    return;
  }
  
  int startY = 12;
  
  // Refresh option (always first)
  if (wifiSelectedIndex == 0) {
    display.fillRect(0, startY - 1, 128, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  }
  
  display.setCursor(2, startY);
  display.print(">>> REFRESH <<<");
  
  if (wifiSelectedIndex == 0) {
    display.setTextColor(SH110X_WHITE);
  }
  
  // Networks (3 at a time)
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
  display.setCursor(15, 2);
  display.println("NETWORK STATUS");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  wl_status_t wifiStatus = WiFi.status();
  
  display.setCursor(10, 20);
  display.print("WiFi: ");
  
  if (wifiConnecting) {
    display.print("Connecting...");
  } else if (wifiStatus == WL_CONNECTED) {
    display.print("Connected");
  } else {
    display.print("Disconnected");
  }
  
  if (wifiStatus == WL_CONNECTED) {
    display.setCursor(10, 30);
    display.print("SSID: ");
    String ssid = WiFi.SSID();
    if (ssid.length() > 10) {
      display.print(ssid.substring(0, 8) + "..");
    } else {
      display.print(ssid);
    }
    
    display.setCursor(10, 40);
    display.print("IP: ");
    String ip = WiFi.localIP().toString();
    if (ip.length() > 15) {
      ip = ip.substring(0, 13) + "..";
    }
    display.print(ip);
    
    display.setCursor(10, 50);
    display.print("RSSI: ");
    display.print(WiFi.RSSI());
    display.print(" dBm");
  }
}

void drawWifiConnectScreen() {
  display.setCursor(35, 2);
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
  
  // Show network name
  if (wifiSelectedIndex > 0 && wifiSelectedIndex <= wifiNetworkCount) {
    display.setCursor(10, 40);
    display.print("To: ");
    String ssid = wifiNetworks[wifiSelectedIndex - 1];
    int bracketPos = ssid.indexOf(" [");
    if (bracketPos != -1) {
      ssid = ssid.substring(0, bracketPos);
    }
    if (ssid.length() > 12) {
      display.print(ssid.substring(0, 10) + "..");
    } else {
      display.print(ssid);
    }
  }
}

void drawWifiStatusScreen() {
  display.setCursor(45, 2);
  display.println("WIFI");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(25, 25);
    display.print("Connected");
    
    display.setCursor(5, 35);
    display.print("SSID: ");
    String ssid = connectedSSID;
    if (ssid.length() > 12) {
      ssid = ssid.substring(0, 10) + "..";
    }
    display.print(ssid);
    
    display.setCursor(5, 45);
    display.print("IP: ");
    String ip = WiFi.localIP().toString();
    if (ip.length() > 15) {
      ip = ip.substring(0, 13) + "..";
    }
    display.print(ip);
  } else {
    display.setCursor(25, 25);
    display.print("No Connection");
  }
}

void drawPasswordEntryScreen() {
  display.setCursor(35, 2);
  display.println("PASSWORD");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  // Show current character
  display.setCursor(5, 25);
  display.print("Char: ");
  if (passwordCursorPos < sizeof(passwordChars) && passwordChars[passwordCursorPos] != 0) {
    display.print(passwordChars[passwordCursorPos]);
  } else {
    display.print("a");
  }
  
  // Show password field
  display.setCursor(5, 40);
  display.print("Pass: ");
  for (int i = 0; i < passwordCursorPos; i++) {
    if (passwordChars[i] != 0) {
      display.print("*");
    }
  }
  
  // Cursor
  display.setCursor(5 + (passwordCursorPos * 6), 40);
  display.print("_");
}

void drawAboutScreen() {
  display.setCursor(50, 2);
  display.println("ABOUT");
  display.drawLine(0, 12, 127, 12, SH110X_WHITE);
  
  display.setCursor(10, 20);
  display.println("Smart Enroll v3.1");
  display.setCursor(10, 30);
  display.println("Fixed Enrollment");
  display.setCursor(10, 40);
  display.println("ESP32 + R307");
  display.setCursor(10, 50);
  display.println("IoT System");
}

// =================== BUTTON HANDLING ===================
void checkButtons() {
  static bool lastUp = HIGH, lastDown = HIGH, lastSel = HIGH, lastBack = HIGH;
  
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
        if (!buttonLongPressed[i]) {
          handleButtonPress(i);
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
  // Long press SELECT on password entry to connect
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
  // Long press SELECT on WiFi screen to connect to open network
  else if (currentScreen == SCREEN_WIFI_SCAN && button == 2 && !wifiScanning && wifiSelectedIndex > 0) {
    String selectedNetwork = wifiNetworks[wifiSelectedIndex - 1];
    
    if (selectedNetwork.indexOf("[OPEN]") != -1) {
      currentScreen = SCREEN_WIFI_CONNECT;
      connectToWiFi(selectedNetwork, "");
    } else {
      currentScreen = SCREEN_PASSWORD_ENTRY;
      passwordCursorPos = 0;
      memset(passwordChars, 0, sizeof(passwordChars));
      passwordChars[0] = 'a';
    }
  }
  // Long press SELECT in enrollment to cancel
  else if (currentScreen == SCREEN_ENROLL_MODE && button == 2 && 
           (enrollState == ENROLL_CAPTURING || enrollState == ENROLL_PENDING)) {
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
          studentInProgress = true;
          enrollmentStartTime = millis();
          showNotificationMsg("Start enrollment");
        } else if (enrollState == ENROLL_ERROR) {
          // Retry enrollment
          enrollState = ENROLL_PENDING;
          showNotificationMsg("Retrying...");
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
          } else if (wifiSelectedIndex > 0) {
            String selectedNetwork = wifiNetworks[wifiSelectedIndex - 1];
            
            if (selectedNetwork.indexOf("[OPEN]") != -1) {
              currentScreen = SCREEN_WIFI_CONNECT;
              connectToWiFi(selectedNetwork, "");
            } else {
              currentScreen = SCREEN_PASSWORD_ENTRY;
              passwordCursorPos = 0;
              memset(passwordChars, 0, sizeof(passwordChars));
              passwordChars[0] = 'a';
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
    case SCREEN_NETWORK_STATUS:
    case SCREEN_ABOUT:
      if (button == 3) {
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