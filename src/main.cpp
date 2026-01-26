/*
 * ============================================================
 *  ATTENDIFY - PHASE 1 ESP32 FIRMWARE
 *  Complete Attendance System with Fingerprint Authentication
 * ============================================================
 *  Hardware: ESP32-WROOM-32
 *  Features: OLED UI, RTC, SD Card, Fingerprint, WiFi
 *  Backend:  http://192.168.0.119:3002
 * ============================================================
 */

// ============================================================
// LIBRARY INCLUDES
// ============================================================
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <RTClib.h>
#include <Adafruit_Fingerprint.h>
#include <SD.h>
#include <SPI.h>
#include <Preferences.h>

// ============================================================
// PIN DEFINITIONS (FIXED - DO NOT CHANGE)
// ============================================================

// OLED Display (SH1106/SSD1306 - I2C)
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_ADDRESS    0x3C
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64

// RTC Module (DS3231 - I2C, shared bus)
#define RTC_ADDRESS     0x68

// Fingerprint Sensor (R307 - UART)
#define FP_TX           4
#define FP_RX           2
#define FP_BAUD         57600

// Navigation Buttons (INPUT_PULLUP)
#define BTN_UP          32
#define BTN_DOWN        33
#define BTN_SELECT      25
#define BTN_BACK        26

// SD Card Module (SPI)
#define SD_CS           5
#define SD_MOSI         23
#define SD_MISO         19
#define SD_SCK          18

// ============================================================
// BACKEND CONFIGURATION
// ============================================================
#define BACKEND_HOST    "192.168.0.119"
#define BACKEND_PORT    3002
#define BACKEND_URL     "http://192.168.0.119:3002"

// ============================================================
// TIMING CONSTANTS
// ============================================================
#define POLL_INTERVAL       2000    // Poll backend every 2 seconds
#define DEBOUNCE_DELAY      200     // Button debounce
#define LONG_PRESS_TIME     1500    // Long press threshold
#define WIFI_SCAN_INTERVAL  10000   // WiFi scan interval
#define SCREEN_TIMEOUT      30000   // Screen timeout (not used in Phase 1)

// ============================================================
// SCREEN STATES (State Machine)
// ============================================================
enum ScreenState {
    SCREEN_BOOT,
    SCREEN_HOME,
    SCREEN_MAIN_MENU,
    SCREEN_ENROLL_MODE,
    SCREEN_ENROLL_WAITING,
    SCREEN_ENROLL_FINGERPRINT,
    SCREEN_ATTENDANCE_MODE,
    SCREEN_WIFI_SCAN,
    SCREEN_WIFI_STATUS,
    SCREEN_ABOUT
};

// ============================================================
// ENROLLMENT STATES
// ============================================================
enum EnrollState {
    ENROLL_IDLE,
    ENROLL_POLLING,
    ENROLL_STUDENT_FOUND,
    ENROLL_WAITING_SELECT,
    ENROLL_FIRST_SCAN,
    ENROLL_REMOVE_FINGER,
    ENROLL_SECOND_SCAN,
    ENROLL_CREATE_MODEL,
    ENROLL_STORE,
    ENROLL_CONFIRM_BACKEND,
    ENROLL_SUCCESS,
    ENROLL_FAILED
};

// ============================================================
// MENU DEFINITIONS
// ============================================================
const char* menuItems[] = {
    "Enroll Mode",
    "Attendance Mode",
    "WiFi Scan",
    "WiFi Status",
    "About"
};
const int menuItemCount = 5;

// ============================================================
// GLOBAL OBJECTS
// ============================================================
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
RTC_DS3231 rtc;
HardwareSerial fpSerial(2);
Adafruit_Fingerprint finger(&fpSerial);
Preferences preferences;

// ============================================================
// GLOBAL STATE VARIABLES
// ============================================================
ScreenState currentScreen = SCREEN_BOOT;
EnrollState enrollState = ENROLL_IDLE;
int menuIndex = 0;

// Button states
bool btnUpPressed = false;
bool btnDownPressed = false;
bool btnSelectPressed = false;
bool btnBackPressed = false;
unsigned long btnSelectPressTime = 0;

// Timing
unsigned long lastPollTime = 0;
unsigned long lastButtonTime = 0;
unsigned long lastWifiScanTime = 0;

// Enrollment data
String enrollStudentName = "";
String enrollStudentRoll = "";
int enrollFingerprintId = 0;

// System status
bool wifiConnected = false;
bool rtcAvailable = false;
bool sdAvailable = false;
bool fpAvailable = false;
String currentWifiSSID = "";

// Attendance mode
bool attendanceActive = false;

// ============================================================
// FUNCTION PROTOTYPES
// ============================================================
void initHardware();
void initDisplay();
void initRTC();
void initSD();
void initFingerprint();
void scanAndConnectWifi();
void handleButtons();
void updateScreen();
void drawBootScreen();
void drawHomeScreen();
void drawMainMenu();
void drawEnrollMode();
void drawEnrollWaiting();
void drawEnrollFingerprint();
void drawAttendanceMode();
void drawWifiScan();
void drawWifiStatus();
void drawAboutScreen();
void pollBackendForEnrollment();
void handleEnrollmentFlow();
void handleAttendanceFlow();
void confirmEnrollmentToBackend();
void uploadAttendanceToBackend(String roll, String timestamp);
void saveAttendanceToSD(String roll, String timestamp, bool uploaded);
String getTimestamp();
int getFingerprintEnroll(int id);
int getFingerprintMatch();
void showMessage(const char* line1, const char* line2 = "", const char* line3 = "");
void showError(const char* message);
void showSuccess(const char* message);
void handleUpButton();
void handleDownButton();
void handleSelectButton();
void handleSelectLongPress();
void handleBackButton();

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ATTENDIFY Phase 1 ===");
    
    initHardware();
}

// ============================================================
// MAIN LOOP (Non-blocking)
// ============================================================
void loop() {
    // Handle button inputs
    handleButtons();
    
    // Update display based on current screen
    updateScreen();
    
    // Handle enrollment flow if active
    if (currentScreen == SCREEN_ENROLL_MODE || 
        currentScreen == SCREEN_ENROLL_WAITING ||
        currentScreen == SCREEN_ENROLL_FINGERPRINT) {
        handleEnrollmentFlow();
    }
    
    // Handle attendance flow if active
    if (currentScreen == SCREEN_ATTENDANCE_MODE && attendanceActive) {
        handleAttendanceFlow();
    }
    
    // Small delay to prevent watchdog issues
    delay(10);
}

// ============================================================
// HARDWARE INITIALIZATION
// ============================================================
void initHardware() {
    // Initialize I2C
    Wire.begin(OLED_SDA, OLED_SCL);
    
    // Initialize buttons
    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);
    pinMode(BTN_BACK, INPUT_PULLUP);
    
    // Initialize display first for status messages
    initDisplay();
    currentScreen = SCREEN_BOOT;
    drawBootScreen();
    
    // Initialize other hardware
    showMessage("Initializing...", "RTC Module");
    initRTC();
    delay(500);
    
    showMessage("Initializing...", "SD Card");
    initSD();
    delay(500);
    
    showMessage("Initializing...", "Fingerprint");
    initFingerprint();
    delay(500);
    
    showMessage("Scanning...", "Open WiFi Networks");
    scanAndConnectWifi();
    delay(1000);
    
    // Boot complete
    currentScreen = SCREEN_HOME;
}

// ============================================================
// DISPLAY INITIALIZATION
// ============================================================
bool displayAvailable = false;

void initDisplay() {
    if (!display.begin(OLED_ADDRESS, true)) {
        Serial.println("ERROR: OLED not found! Continuing without display...");
        displayAvailable = false;
        return;  // Don't halt, continue without display
    }
    displayAvailable = true;
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);
    display.setTextSize(1);
    display.display();
    Serial.println("OLED initialized");
}

// ============================================================
// RTC INITIALIZATION
// ============================================================
void initRTC() {
    if (!rtc.begin()) {
        Serial.println("ERROR: RTC not found!");
        rtcAvailable = false;
    } else {
        rtcAvailable = true;
        if (rtc.lostPower()) {
            Serial.println("RTC lost power, setting time...");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
        Serial.println("RTC initialized");
    }
}

// ============================================================
// SD CARD INITIALIZATION
// ============================================================
void initSD() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    if (!SD.begin(SD_CS)) {
        Serial.println("ERROR: SD Card not found!");
        sdAvailable = false;
    } else {
        sdAvailable = true;
        Serial.println("SD Card initialized");
        
        // Create attendance directory if not exists
        if (!SD.exists("/attendance")) {
            SD.mkdir("/attendance");
        }
    }
}

// ============================================================
// FINGERPRINT SENSOR INITIALIZATION
// ============================================================
void initFingerprint() {
    fpSerial.begin(FP_BAUD, SERIAL_8N1, FP_RX, FP_TX);
    finger.begin(FP_BAUD);
    
    if (finger.verifyPassword()) {
        fpAvailable = true;
        Serial.println("Fingerprint sensor initialized");
        Serial.print("Sensor contains ");
        Serial.print(finger.templateCount);
        Serial.println(" templates");
    } else {
        fpAvailable = false;
        Serial.println("ERROR: Fingerprint sensor not found!");
    }
}

// ============================================================
// WIFI - SCAN AND CONNECT TO OPEN NETWORKS
// ============================================================
void scanAndConnectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    Serial.println("Scanning for open WiFi networks...");
    
    int networkCount = WiFi.scanNetworks();
    
    if (networkCount == 0) {
        Serial.println("No networks found");
        wifiConnected = false;
        return;
    }
    
    // Find open networks (no encryption)
    for (int i = 0; i < networkCount; i++) {
        if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {
            String ssid = WiFi.SSID(i);
            Serial.print("Found open network: ");
            Serial.println(ssid);
            
            showMessage("Connecting to:", ssid.c_str());
            
            WiFi.begin(ssid.c_str());
            
            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 20) {
                delay(500);
                Serial.print(".");
                attempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                wifiConnected = true;
                currentWifiSSID = ssid;
                Serial.println("\nConnected!");
                Serial.print("IP: ");
                Serial.println(WiFi.localIP());
                showMessage("Connected!", ssid.c_str(), WiFi.localIP().toString().c_str());
                delay(1000);
                return;
            }
        }
    }
    
    wifiConnected = false;
    Serial.println("No open networks available");
    showMessage("No open WiFi", "networks found");
    delay(2000);
}

// ============================================================
// BUTTON HANDLING (Non-blocking with debounce)
// ============================================================
void handleButtons() {
    unsigned long currentTime = millis();
    
    // Debounce check
    if (currentTime - lastButtonTime < DEBOUNCE_DELAY) {
        return;
    }
    
    // Read button states (active LOW due to INPUT_PULLUP)
    bool upState = !digitalRead(BTN_UP);
    bool downState = !digitalRead(BTN_DOWN);
    bool selectState = !digitalRead(BTN_SELECT);
    bool backState = !digitalRead(BTN_BACK);
    
    // UP Button
    if (upState && !btnUpPressed) {
        btnUpPressed = true;
        lastButtonTime = currentTime;
        handleUpButton();
    } else if (!upState) {
        btnUpPressed = false;
    }
    
    // DOWN Button
    if (downState && !btnDownPressed) {
        btnDownPressed = true;
        lastButtonTime = currentTime;
        handleDownButton();
    } else if (!downState) {
        btnDownPressed = false;
    }
    
    // SELECT Button
    if (selectState && !btnSelectPressed) {
        btnSelectPressed = true;
        btnSelectPressTime = currentTime;
        lastButtonTime = currentTime;
    } else if (!selectState && btnSelectPressed) {
        btnSelectPressed = false;
        unsigned long pressDuration = currentTime - btnSelectPressTime;
        if (pressDuration >= LONG_PRESS_TIME) {
            handleSelectLongPress();
        } else {
            handleSelectButton();
        }
    }
    
    // BACK Button
    if (backState && !btnBackPressed) {
        btnBackPressed = true;
        lastButtonTime = currentTime;
        handleBackButton();
    } else if (!backState) {
        btnBackPressed = false;
    }
}

void handleUpButton() {
    switch (currentScreen) {
        case SCREEN_MAIN_MENU:
            menuIndex = (menuIndex - 1 + menuItemCount) % menuItemCount;
            break;
        default:
            break;
    }
}

void handleDownButton() {
    switch (currentScreen) {
        case SCREEN_MAIN_MENU:
            menuIndex = (menuIndex + 1) % menuItemCount;
            break;
        default:
            break;
    }
}

void handleSelectButton() {
    switch (currentScreen) {
        case SCREEN_HOME:
            currentScreen = SCREEN_MAIN_MENU;
            menuIndex = 0;
            break;
            
        case SCREEN_MAIN_MENU:
            switch (menuIndex) {
                case 0: // Enroll Mode
                    currentScreen = SCREEN_ENROLL_MODE;
                    enrollState = ENROLL_POLLING;
                    break;
                case 1: // Attendance Mode
                    currentScreen = SCREEN_ATTENDANCE_MODE;
                    attendanceActive = true;
                    break;
                case 2: // WiFi Scan
                    currentScreen = SCREEN_WIFI_SCAN;
                    scanAndConnectWifi();
                    currentScreen = SCREEN_WIFI_STATUS;
                    break;
                case 3: // WiFi Status
                    currentScreen = SCREEN_WIFI_STATUS;
                    break;
                case 4: // About
                    currentScreen = SCREEN_ABOUT;
                    break;
            }
            break;
            
        case SCREEN_ENROLL_WAITING:
            // Teacher presses SELECT to start fingerprint capture
            if (enrollState == ENROLL_WAITING_SELECT) {
                enrollState = ENROLL_FIRST_SCAN;
                currentScreen = SCREEN_ENROLL_FINGERPRINT;
            }
            break;
            
        default:
            break;
    }
}

void handleSelectLongPress() {
    // Long press actions (for future use)
}

void handleBackButton() {
    switch (currentScreen) {
        case SCREEN_MAIN_MENU:
            currentScreen = SCREEN_HOME;
            break;
            
        case SCREEN_ENROLL_MODE:
        case SCREEN_ENROLL_WAITING:
        case SCREEN_ENROLL_FINGERPRINT:
            enrollState = ENROLL_IDLE;
            enrollStudentName = "";
            enrollStudentRoll = "";
            currentScreen = SCREEN_MAIN_MENU;
            break;
            
        case SCREEN_ATTENDANCE_MODE:
            attendanceActive = false;
            currentScreen = SCREEN_MAIN_MENU;
            break;
            
        case SCREEN_WIFI_SCAN:
        case SCREEN_WIFI_STATUS:
        case SCREEN_ABOUT:
            currentScreen = SCREEN_MAIN_MENU;
            break;
            
        default:
            currentScreen = SCREEN_HOME;
            break;
    }
}

// ============================================================
// SCREEN UPDATE (Non-blocking)
// ============================================================
void updateScreen() {
    switch (currentScreen) {
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
            drawEnrollMode();
            break;
        case SCREEN_ENROLL_WAITING:
            drawEnrollWaiting();
            break;
        case SCREEN_ENROLL_FINGERPRINT:
            drawEnrollFingerprint();
            break;
        case SCREEN_ATTENDANCE_MODE:
            drawAttendanceMode();
            break;
        case SCREEN_WIFI_SCAN:
            drawWifiScan();
            break;
        case SCREEN_WIFI_STATUS:
            drawWifiStatus();
            break;
        case SCREEN_ABOUT:
            drawAboutScreen();
            break;
    }
}

// ============================================================
// SCREEN DRAWING FUNCTIONS
// ============================================================

void drawBootScreen() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(10, 10);
    display.println("ATTENDIFY");
    display.setTextSize(1);
    display.setCursor(30, 35);
    display.println("Phase 1");
    display.setCursor(20, 50);
    display.println("Initializing...");
    display.display();
}

void drawHomeScreen() {
    if (!displayAvailable) return;
    display.clearDisplay();
    
    // Header
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ATTENDIFY");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    
    // Status icons
    display.setCursor(0, 16);
    display.print("WiFi: ");
    display.println(wifiConnected ? "OK" : "OFF");
    
    display.print("RTC:  ");
    display.println(rtcAvailable ? "OK" : "ERR");
    
    display.print("SD:   ");
    display.println(sdAvailable ? "OK" : "ERR");
    
    display.print("FP:   ");
    display.println(fpAvailable ? "OK" : "ERR");
    
    // Current time
    if (rtcAvailable) {
        DateTime now = rtc.now();
        display.setCursor(70, 16);
        char timeStr[9];
        sprintf(timeStr, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
        display.println(timeStr);
    }
    
    // Navigation hint
    display.setCursor(0, 56);
    display.println("[SELECT] - Menu");
    
    display.display();
}

void drawMainMenu() {
    if (!displayAvailable) return;
    display.clearDisplay();
    
    // Header
    display.setTextSize(1);
    display.setCursor(30, 0);
    display.println("MAIN MENU");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    
    // Menu items
    for (int i = 0; i < menuItemCount; i++) {
        display.setCursor(10, 16 + (i * 10));
        if (i == menuIndex) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        display.println(menuItems[i]);
    }
    
    display.display();
}

void drawEnrollMode() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(20, 0);
    display.println("ENROLL MODE");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    
    display.setCursor(0, 20);
    display.println("Waiting for student");
    display.println("from QR page...");
    
    display.setCursor(0, 45);
    display.println("Polling backend...");
    
    display.setCursor(0, 56);
    display.println("[BACK] - Cancel");
    
    display.display();
}

void drawEnrollWaiting() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(20, 0);
    display.println("ENROLL MODE");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    
    display.setCursor(0, 16);
    display.println("Student Found:");
    
    display.setTextSize(1);
    display.setCursor(0, 28);
    display.print("Name: ");
    display.println(enrollStudentName.substring(0, 12));
    
    display.setCursor(0, 40);
    display.print("Roll: ");
    display.println(enrollStudentRoll);
    
    display.setCursor(0, 56);
    display.println("[SELECT] Start Enroll");
    
    display.display();
}

void drawEnrollFingerprint() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(15, 0);
    display.println("FINGERPRINT");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    
    display.setCursor(0, 20);
    
    switch (enrollState) {
        case ENROLL_FIRST_SCAN:
            display.println("Place finger on");
            display.println("sensor for 1st scan");
            break;
        case ENROLL_REMOVE_FINGER:
            display.println("Remove finger...");
            break;
        case ENROLL_SECOND_SCAN:
            display.println("Place same finger");
            display.println("for 2nd scan");
            break;
        case ENROLL_CREATE_MODEL:
            display.println("Creating model...");
            break;
        case ENROLL_STORE:
            display.println("Storing fingerprint");
            display.print("ID: ");
            display.println(enrollFingerprintId);
            break;
        case ENROLL_CONFIRM_BACKEND:
            display.println("Confirming with");
            display.println("backend...");
            break;
        case ENROLL_SUCCESS:
            display.println("ENROLLMENT SUCCESS!");
            display.setCursor(0, 40);
            display.print("ID: ");
            display.println(enrollFingerprintId);
            break;
        case ENROLL_FAILED:
            display.println("ENROLLMENT FAILED!");
            display.println("Please try again.");
            break;
        default:
            break;
    }
    
    display.display();
}

void drawAttendanceMode() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 0);
    display.println("ATTENDANCE MODE");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    
    display.setCursor(0, 20);
    display.println("Place registered");
    display.println("finger on sensor");
    display.println("to mark attendance");
    
    // Show time
    if (rtcAvailable) {
        DateTime now = rtc.now();
        display.setCursor(0, 45);
        char timeStr[20];
        sprintf(timeStr, "%02d/%02d %02d:%02d:%02d", 
                now.day(), now.month(), now.hour(), now.minute(), now.second());
        display.println(timeStr);
    }
    
    display.setCursor(0, 56);
    display.println("[BACK] - Exit");
    
    display.display();
}

void drawWifiScan() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(20, 0);
    display.println("WIFI SCAN");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    
    display.setCursor(0, 25);
    display.println("Scanning for open");
    display.println("WiFi networks...");
    
    display.display();
}

void drawWifiStatus() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(15, 0);
    display.println("WIFI STATUS");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    
    display.setCursor(0, 16);
    display.print("Status: ");
    display.println(wifiConnected ? "Connected" : "Disconnected");
    
    if (wifiConnected) {
        display.print("SSID: ");
        display.println(currentWifiSSID.substring(0, 12));
        
        display.print("IP: ");
        display.println(WiFi.localIP());
        
        display.print("RSSI: ");
        display.print(WiFi.RSSI());
        display.println(" dBm");
    } else {
        display.setCursor(0, 35);
        display.println("No open network");
        display.println("connected.");
    }
    
    display.setCursor(0, 56);
    display.println("[BACK] - Return");
    
    display.display();
}

void drawAboutScreen() {
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(35, 0);
    display.println("ABOUT");
    display.drawLine(0, 10, 128, 10, SH110X_WHITE);
    
    display.setCursor(0, 16);
    display.println("ATTENDIFY v1.0");
    display.println("Phase 1 - Local");
    display.println("");
    display.println("Fingerprint-based");
    display.println("Attendance System");
    
    display.setCursor(0, 56);
    display.println("[BACK] - Return");
    
    display.display();
}

// ============================================================
// ENROLLMENT FLOW HANDLER
// ============================================================
void handleEnrollmentFlow() {
    unsigned long currentTime = millis();
    
    switch (enrollState) {
        case ENROLL_POLLING:
            // Poll backend every 2 seconds
            if (currentTime - lastPollTime >= POLL_INTERVAL) {
                lastPollTime = currentTime;
                pollBackendForEnrollment();
            }
            break;
            
        case ENROLL_STUDENT_FOUND:
            // Transition to waiting for SELECT press
            currentScreen = SCREEN_ENROLL_WAITING;
            enrollState = ENROLL_WAITING_SELECT;
            break;
            
        case ENROLL_FIRST_SCAN:
            // First fingerprint scan
            {
                int result = finger.getImage();
                if (result == FINGERPRINT_OK) {
                    result = finger.image2Tz(1);
                    if (result == FINGERPRINT_OK) {
                        enrollState = ENROLL_REMOVE_FINGER;
                        showMessage("Good!", "Remove finger");
                        delay(1000);
                    } else {
                        showError("Image convert failed");
                        delay(2000);
                    }
                } else if (result == FINGERPRINT_NOFINGER) {
                    // Keep waiting
                }
            }
            break;
            
        case ENROLL_REMOVE_FINGER:
            // Wait for finger removal
            if (finger.getImage() == FINGERPRINT_NOFINGER) {
                delay(500);
                enrollState = ENROLL_SECOND_SCAN;
            }
            break;
            
        case ENROLL_SECOND_SCAN:
            // Second fingerprint scan
            {
                int result = finger.getImage();
                if (result == FINGERPRINT_OK) {
                    result = finger.image2Tz(2);
                    if (result == FINGERPRINT_OK) {
                        enrollState = ENROLL_CREATE_MODEL;
                    } else {
                        showError("Image convert failed");
                        enrollState = ENROLL_FAILED;
                        delay(2000);
                    }
                }
            }
            break;
            
        case ENROLL_CREATE_MODEL:
            // Create fingerprint model
            {
                int result = finger.createModel();
                if (result == FINGERPRINT_OK) {
                    enrollState = ENROLL_STORE;
                } else if (result == FINGERPRINT_ENROLLMISMATCH) {
                    showError("Prints don't match");
                    enrollState = ENROLL_FAILED;
                    delay(2000);
                } else {
                    showError("Model creation failed");
                    enrollState = ENROLL_FAILED;
                    delay(2000);
                }
            }
            break;
            
        case ENROLL_STORE:
            // Store fingerprint with roll number as ID
            {
                enrollFingerprintId = enrollStudentRoll.toInt();
                int result = finger.storeModel(enrollFingerprintId);
                if (result == FINGERPRINT_OK) {
                    Serial.print("Stored fingerprint ID: ");
                    Serial.println(enrollFingerprintId);
                    enrollState = ENROLL_CONFIRM_BACKEND;
                } else {
                    showError("Store failed");
                    enrollState = ENROLL_FAILED;
                    delay(2000);
                }
            }
            break;
            
        case ENROLL_CONFIRM_BACKEND:
            // Confirm enrollment with backend
            confirmEnrollmentToBackend();
            break;
            
        case ENROLL_SUCCESS:
            // Show success for 3 seconds then reset
            delay(3000);
            enrollState = ENROLL_IDLE;
            enrollStudentName = "";
            enrollStudentRoll = "";
            currentScreen = SCREEN_ENROLL_MODE;
            enrollState = ENROLL_POLLING;
            break;
            
        case ENROLL_FAILED:
            // Show failure then reset
            delay(3000);
            enrollState = ENROLL_IDLE;
            enrollStudentName = "";
            enrollStudentRoll = "";
            currentScreen = SCREEN_ENROLL_MODE;
            enrollState = ENROLL_POLLING;
            break;
            
        default:
            break;
    }
}

// ============================================================
// POLL BACKEND FOR ENROLLMENT QUEUE
// ============================================================
void pollBackendForEnrollment() {
    if (!wifiConnected) {
        Serial.println("WiFi not connected, skipping poll");
        return;
    }
    
    HTTPClient http;
    String url = String(BACKEND_URL) + "/api/poll-status";
    
    http.begin(url);
    http.setTimeout(5000);
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.print("Poll response: ");
        Serial.println(payload);
        
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, payload);
        
        if (!error) {
            const char* status = doc["status"];
            
            if (strcmp(status, "ENROLL") == 0) {
                enrollStudentName = doc["name"].as<String>();
                enrollStudentRoll = doc["roll"].as<String>();
                
                Serial.print("Student to enroll: ");
                Serial.print(enrollStudentName);
                Serial.print(" (");
                Serial.print(enrollStudentRoll);
                Serial.println(")");
                
                enrollState = ENROLL_STUDENT_FOUND;
            }
        }
    } else {
        Serial.print("Poll failed, HTTP code: ");
        Serial.println(httpCode);
    }
    
    http.end();
}

// ============================================================
// CONFIRM ENROLLMENT TO BACKEND
// ============================================================
void confirmEnrollmentToBackend() {
    if (!wifiConnected) {
        showError("WiFi disconnected");
        enrollState = ENROLL_FAILED;
        return;
    }
    
    HTTPClient http;
    String url = String(BACKEND_URL) + "/api/confirm-enrollment";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    
    DynamicJsonDocument doc(256);
    doc["roll"] = enrollStudentRoll;
    doc["fingerprintId"] = enrollFingerprintId;
    
    String payload;
    serializeJson(doc, payload);
    
    int httpCode = http.POST(payload);
    
    if (httpCode == HTTP_CODE_OK) {
        Serial.println("Enrollment confirmed with backend");
        enrollState = ENROLL_SUCCESS;
    } else {
        Serial.print("Confirm failed, HTTP code: ");
        Serial.println(httpCode);
        showError("Backend confirm failed");
        enrollState = ENROLL_FAILED;
    }
    
    http.end();
}

// ============================================================
// ATTENDANCE FLOW HANDLER
// ============================================================
void handleAttendanceFlow() {
    if (!fpAvailable) {
        return;
    }
    
    int result = finger.getImage();
    
    if (result != FINGERPRINT_OK) {
        return; // No finger detected
    }
    
    // Convert image
    result = finger.image2Tz();
    if (result != FINGERPRINT_OK) {
        showError("Image error");
        delay(1000);
        return;
    }
    
    // Search for match
    result = finger.fingerSearch();
    
    if (result == FINGERPRINT_OK) {
        int matchedId = finger.fingerID;
        int confidence = finger.confidence;
        
        Serial.print("Fingerprint match! ID: ");
        Serial.print(matchedId);
        Serial.print(", Confidence: ");
        Serial.println(confidence);
        
        // Get timestamp
        String timestamp = getTimestamp();
        String roll = String(matchedId);
        
        // Show success on display
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(10, 0);
        display.println("ATTENDANCE");
        display.drawLine(0, 10, 128, 10, SH110X_WHITE);
        display.setCursor(0, 20);
        display.println("MARKED!");
        display.print("Roll: ");
        display.println(roll);
        display.print("Time: ");
        display.println(timestamp.substring(11, 19)); // Just time part
        display.display();
        
        // Save to SD card first (offline-first)
        bool uploaded = false;
        saveAttendanceToSD(roll, timestamp, false);
        
        // Try to upload to backend
        if (wifiConnected) {
            uploadAttendanceToBackend(roll, timestamp);
            uploaded = true;
            // Update SD record as uploaded
            // (simplified: in production, update the specific record)
        }
        
        delay(2000);
        
    } else if (result == FINGERPRINT_NOTFOUND) {
        showError("Not registered!");
        delay(1500);
    }
}

// ============================================================
// GET RTC TIMESTAMP
// ============================================================
String getTimestamp() {
    if (!rtcAvailable) {
        return "1970-01-01T00:00:00";
    }
    
    DateTime now = rtc.now();
    char buffer[25];
    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    return String(buffer);
}

// ============================================================
// SAVE ATTENDANCE TO SD CARD
// ============================================================
void saveAttendanceToSD(String roll, String timestamp, bool uploaded) {
    if (!sdAvailable) {
        Serial.println("SD card not available");
        return;
    }
    
    // Create filename based on date
    DateTime now = rtc.now();
    char filename[30];
    sprintf(filename, "/attendance/%04d%02d%02d.csv", 
            now.year(), now.month(), now.day());
    
    bool fileExists = SD.exists(filename);
    File file = SD.open(filename, FILE_APPEND);
    
    if (!file) {
        Serial.println("Failed to open file for writing");
        return;
    }
    
    // Write header if new file
    if (!fileExists) {
        file.println("roll,timestamp,uploaded");
    }
    
    // Write attendance record
    file.print(roll);
    file.print(",");
    file.print(timestamp);
    file.print(",");
    file.println(uploaded ? "true" : "false");
    
    file.close();
    
    Serial.print("Saved attendance to SD: ");
    Serial.println(filename);
}

// ============================================================
// UPLOAD ATTENDANCE TO BACKEND
// ============================================================
void uploadAttendanceToBackend(String roll, String timestamp) {
    if (!wifiConnected) {
        Serial.println("WiFi not connected, skipping upload");
        return;
    }
    
    HTTPClient http;
    String url = String(BACKEND_URL) + "/api/upload-attendance";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    
    DynamicJsonDocument doc(256);
    doc["roll"] = roll;
    doc["timestamp"] = timestamp;
    
    String payload;
    serializeJson(doc, payload);
    
    Serial.print("Uploading attendance: ");
    Serial.println(payload);
    
    int httpCode = http.POST(payload);
    
    if (httpCode == HTTP_CODE_OK) {
        Serial.println("Attendance uploaded successfully");
    } else {
        Serial.print("Upload failed, HTTP code: ");
        Serial.println(httpCode);
    }
    
    http.end();
}

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

void showMessage(const char* line1, const char* line2, const char* line3) {
    if (!displayAvailable) {
        Serial.print("MSG: "); Serial.print(line1); Serial.print(" "); Serial.println(line2);
        return;
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 15);
    display.println(line1);
    display.println(line2);
    display.println(line3);
    display.display();
}

void showError(const char* message) {
    Serial.print("ERROR: "); Serial.println(message);
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("ERROR:");
    display.setCursor(0, 20);
    display.println(message);
    display.display();
}

void showSuccess(const char* message) {
    Serial.print("SUCCESS: "); Serial.println(message);
    if (!displayAvailable) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("SUCCESS:");
    display.setCursor(0, 20);
    display.println(message);
    display.display();
}
