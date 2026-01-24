/**
 * SMART ATTENDANCE SYSTEM - COMPLETE WITH SD CARD
 * Version: 6.0 - SD Card Enhanced
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
#include <vector>
#include <SD.h>
#include <SPI.h>

// =================== PIN CONFIGURATION ===================
#define OLED_SDA 21
#define OLED_SCL 22
#define BUTTON_UP 32
#define BUTTON_DOWN 33
#define BUTTON_SELECT 25
#define BUTTON_BACK 26
#define FINGERPRINT_TX 4
#define FINGERPRINT_RX 2

// SD Card Pins (SPI)
#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18

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
  SCREEN_ADMIN_AUTH,
  SCREEN_TEACHER_AUTH,
  SCREEN_ADMIN_MENU,
  SCREEN_TEACHER_MENU,
  SCREEN_SUBJECT_SELECT,
  SCREEN_ATTENDANCE_MODE,
  SCREEN_ENROLL_MODE,
  SCREEN_ENROLL_TEACHER,
  SCREEN_WIFI_SCAN,
  SCREEN_NETWORK_STATUS,
  SCREEN_WIFI_CONNECT,
  SCREEN_WIFI_STATUS,
  SCREEN_PASSWORD_ENTRY,
  SCREEN_SETUP_WIZARD,
  SCREEN_SETUP_CHOICE,
  SCREEN_SETUP_NETWORK,
  SCREEN_SETUP_ENROLL_GFM,
  SCREEN_SETUP_ENROLL_TEACHERS,
  SCREEN_SETUP_SYNC,
  SCREEN_SETUP_COMPLETE,
  SCREEN_SYSTEM_CONFIG,
  SCREEN_REPORTS,
  SCREEN_MY_CLASSES,
  SCREEN_SD_CARD,
  SCREEN_SD_LOGS,
  SCREEN_SD_BACKUP,
  SCREEN_ABOUT,
  SCREEN_FACTORY_RESET
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

// =================== SYSTEM MODES ===================
enum SystemMode {
  MODE_NORMAL,
  MODE_SETUP,
  MODE_MASTER_BYPASS
};

// =================== USER ROLES ===================
enum UserRole {
  ROLE_NONE = -1,
  ROLE_ADMIN = 0,
  ROLE_TEACHER = 1
};

// =================== SETUP MODES ===================
enum SetupMode {
  SETUP_AUTO,
  SETUP_MANUAL
};

// =================== SD CARD STATUS ===================
enum SDCardStatus {
  SD_NOT_PRESENT,
  SD_INITIALIZED,
  SD_ERROR
};

// =================== STRUCTURES ===================
struct Teacher {
  int fingerprintId;
  String name;
  String subjects;
};

struct Subject {
  String code;
  String name;
  int teacherId;
  bool active;
};

struct AttendanceRecord {
  int studentId;
  String subjectCode;
  int teacherId;
  unsigned long timestamp;
  bool uploaded;
};

struct LogEntry {
  String timestamp;
  String type;
  String message;
};

// =================== GLOBAL VARIABLES ===================
ScreenState currentScreen = SCREEN_BOOT;
EnrollState enrollState = ENROLL_IDLE;
FingerprintState fpState = FP_IDLE;
SystemMode systemMode = MODE_NORMAL;
UserRole currentUserRole = ROLE_NONE;
SetupMode currentSetupMode = SETUP_AUTO;
SDCardStatus sdCardStatus = SD_NOT_PRESENT;

int menuIndex = 0;
int subMenuIndex = 0;
bool needRefresh = true;
bool displayInitialized = false;
bool fingerprintInitialized = false;
bool sdCardInitialized = false;

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
const unsigned long WIFI_CONNECTION_TIMEOUT = 20000;

// Password entry
String wifiPassword = "";
bool passwordEntryMode = false;
int passwordCursorPos = 0;
char passwordChars[63] = {0};
const char* charSet = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()-_=+[]{}|;:,.<>?";
int charSetLength = 84;

// Backend Configuration
String BACKEND_URL = "http://192.168.0.119:5001";
String DEVICE_ID = "";
String SCHOOL_NAME = "Default School";
String ACADEMIC_YEAR = "2024-25";
String DIVISION = "";
bool deviceConfigured = false;

// User management
int currentUserId = -1;
String currentUserName = "";
String currentSubject = "";
std::vector<Subject> subjectList;
Teacher teachers[10];
int teacherCount = 0;
bool gfmEnrolled = false;

// Enrollment Data
String pendingStudentName = "";
int pendingStudentRoll = -1;
bool studentInProgress = false;
unsigned long enrollmentStartTime = 0;
const unsigned long ENROLLMENT_TIMEOUT = 25000;

// Fingerprint enrollment tracking
unsigned long fpStateStartTime = 0;
int fpRetryCount = 0;
const int MAX_FP_RETRIES = 2;
String lastFingerprintError = "";
bool firstCaptureDone = false;
bool secondCaptureDone = false;

// Attendance session
std::vector<AttendanceRecord> attendanceBuffer;
bool attendanceSessionActive = false;
unsigned long sessionStartTime = 0;
const unsigned long SESSION_TIMEOUT = 1800000; // 30 minutes
int attendanceCount = 0;
String sessionFileName = "";

// Setup wizard
int setupStep = 0;
int setupTeacherIndex = 0;
bool setupInProgress = false;
String setupTeacherName = "";
bool setupManualMode = false;
int manualEnrollId = 1;

// Server Polling
unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL = 2000;
unsigned long lastConfigSync = 0;
const unsigned long CONFIG_SYNC_INTERVAL = 300000;

// SD Card
unsigned long lastSDCheck = 0;
const unsigned long SD_CHECK_INTERVAL = 60000; // Check SD every minute
String currentLogFile = "";
int sdCardFilesCount = 0;
unsigned long sdCardFreeSpace = 0;

// Refresh & Notifications
bool notificationActive = false;
String notificationMessage = "";
unsigned long notificationStartTime = 0;
const unsigned long NOTIFICATION_DURATION = 2000;
wl_status_t lastWifiStatus = WL_IDLE_STATUS;

// Factory reset
bool factoryResetPending = false;
unsigned long factoryResetStartTime = 0;

// Day and month names
const char* dayNames[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
const char* monthNames[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", 
                              "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// =================== FUNCTION DECLARATIONS ===================
// Initialization
void initializeHardware();
void initializePreferences();
void loadConfiguration();
void checkSetupStatus();

// SD Card Functions
bool initializeSDCard();
void checkSDCardStatus();
void writeLogToSD(String logType, String message);
void createDailyLogFile();
void writeAttendanceToSD(int studentId, String subjectCode, int teacherId);
void writeAttendanceToSD(String data);
void backupConfigurationToSD();
void restoreConfigurationFromSD(String fileName);
void listFilesOnSD();
void deleteOldLogs(int daysToKeep);
void formatSDCard();
String getCurrentDateTimeString();
String getCurrentDateString();
String getCurrentTimeString();

// Setup Wizard Functions
void startSetupWizard();
void handleSetupWizard();
void handleSetupChoice();
void setupDownloadConfiguration();
void setupEnrollGFM();
void setupEnrollTeachers();
void finishSetup();
void enrollGFMManual();
void enrollTeacherManual(int teacherId, String teacherName);

// Screen drawing functions
void drawBootScreen();
void drawHomeScreen();
void drawAdminAuthScreen();
void drawTeacherAuthScreen();
void drawAdminMenu();
void drawTeacherMenu();
void drawSubjectSelectionScreen();
void drawAttendanceScreen();
void drawEnrollmentScreen();
void drawEnrollTeacherScreen();
void drawWifiScanScreen();
void drawNetworkStatusScreen();
void drawWifiConnectScreen();
void drawWifiStatusScreen();
void drawPasswordEntryScreen();
void drawSetupWizardScreen();
void drawSetupChoiceScreen();
void drawSetupNetworkScreen();
void drawSetupEnrollGFMScreen();
void drawSetupEnrollTeachersScreen();
void drawSetupSyncScreen();
void drawSetupCompleteScreen();
void drawSystemConfigScreen();
void drawReportsScreen();
void drawMyClassesScreen();
void drawSDCardScreen();
void drawSDLogsScreen();
void drawSDBackupScreen();
void drawAboutScreen();
void drawFactoryResetScreen();
void drawFooter();

// Core functions
void showScreen();
void checkButtons();
void handleButtonPress(int button);
void handleLongPress(int button);
void showNotificationMsg(String message);
void clearNotification();
void logout();

// WiFi functions
void scanWiFiNetworks();
void connectToWiFi(String ssid, String password);
void checkWifiStatusChange();
void updateWiFiConnection();
void manualRefreshWiFi();

// Fingerprint functions
void handleFingerprintLogin();
void handleSetupEnrollment(int fingerprintId, String personName);
void handleAttendanceMode();
int getFingerprintID();
void showDetailedError(int errorCode);
void resetFingerprintState();
void handleFingerprintEnrollmentFast();

// Enrollment functions
void pollServerForEnrollment();
void sendEnrollmentConfirmation(int rollNo, int fingerprintId);
void resetEnrollmentState();

// Configuration functions
void syncConfigurationFromServer();
void syncAttendanceToServer();
void saveAttendanceRecord(int studentId, String subjectCode);
std::vector<String> getTeacherSubjects(int teacherId);
String getSelectedSubjectCode();
void saveConfiguration();

// Utility functions
void factoryReset();
void checkFactoryReset();
void performFactoryReset();
bool checkMasterBypass();

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n╔══════════════════════════════════════╗");
  Serial.println("║    SMART ATTENDANCE SYSTEM v6.0     ║");
  Serial.println("║     WITH SD CARD FUNCTIONALITY      ║");
  Serial.println("╚══════════════════════════════════════╝");
  
  // Initialize buttons FIRST for master bypass detection
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);
  
  // Initialize SPI for SD card
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  // Check for master bypass (hold SELECT during boot)
  if (checkMasterBypass()) {
    Serial.println("🔓 MASTER BYPASS ACTIVATED!");
    systemMode = MODE_MASTER_BYPASS;
    currentUserRole = ROLE_ADMIN;
    currentUserId = 1;
    currentUserName = "Master Admin";
    currentScreen = SCREEN_ADMIN_MENU;
    showNotificationMsg("Master Bypass Active");
  }
  
  // Initialize hardware
  initializeHardware();
  
  // Check for factory reset (hold all buttons)
  if (digitalRead(BUTTON_UP) == LOW && digitalRead(BUTTON_DOWN) == LOW &&
      digitalRead(BUTTON_SELECT) == LOW && digitalRead(BUTTON_BACK) == LOW) {
    delay(3000);
    if (digitalRead(BUTTON_UP) == LOW && digitalRead(BUTTON_DOWN) == LOW &&
        digitalRead(BUTTON_SELECT) == LOW && digitalRead(BUTTON_BACK) == LOW) {
      Serial.println("⚠️ FACTORY RESET TRIGGERED!");
      factoryReset();
    }
  }
  
  initializePreferences();
  loadConfiguration();
  
  // Initialize SD Card
  if (initializeSDCard()) {
    writeLogToSD("SYSTEM", "Device booted - SD Card initialized");
    createDailyLogFile();
  }
  
  // If not in master bypass, check normal setup
  if (systemMode != MODE_MASTER_BYPASS) {
    checkSetupStatus();
    
    if (systemMode == MODE_SETUP) {
      currentScreen = SCREEN_SETUP_CHOICE;
      Serial.println("Entering SETUP MODE");
      writeLogToSD("SETUP", "Entering setup mode");
    } else {
      currentScreen = SCREEN_HOME;
      Serial.println("System ready - Normal mode");
      writeLogToSD("SYSTEM", "Normal mode started");
    }
  } else {
    writeLogToSD("SECURITY", "Master bypass activated");
  }
  
  showScreen();
  lastWifiStatus = WiFi.status();
}

bool checkMasterBypass() {
  // Check if SELECT button is held during boot
  if (digitalRead(BUTTON_SELECT) == LOW) {
    delay(1000);
    if (digitalRead(BUTTON_SELECT) == LOW) {
      // Show bypass message on serial
      Serial.println("Hold SELECT for master bypass...");
      
      // Try to initialize display to show message
      Wire.begin(OLED_SDA, OLED_SCL);
      if (display.begin(0x3C, true)) {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SH110X_WHITE);
        display.setCursor(20, 20);
        display.println("MASTER BYPASS");
        display.setCursor(10, 40);
        display.println("Hold SELECT 3s...");
        display.display();
      }
      
      // Wait total 3 seconds
      delay(2000);
      
      if (digitalRead(BUTTON_SELECT) == LOW) {
        Serial.println("✅ Master bypass confirmed!");
        return true;
      }
    }
  }
  return false;
}

// =================== SD CARD FUNCTIONS ===================
bool initializeSDCard() {
  Serial.println("Initializing SD card...");
  
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ SD card initialization failed!");
    sdCardStatus = SD_NOT_PRESENT;
    sdCardInitialized = false;
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  
  if (cardType == CARD_NONE) {
    Serial.println("❌ No SD card found");
    sdCardStatus = SD_NOT_PRESENT;
    sdCardInitialized = false;
    return false;
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
  
  // Calculate free space
  sdCardFreeSpace = SD.totalBytes() - SD.usedBytes();
  
  // Create necessary directories
  if (!SD.exists("/logs")) {
    SD.mkdir("/logs");
  }
  if (!SD.exists("/attendance")) {
    SD.mkdir("/attendance");
  }
  if (!SD.exists("/backup")) {
    SD.mkdir("/backup");
  }
  if (!SD.exists("/config")) {
    SD.mkdir("/config");
  }
  
  sdCardStatus = SD_INITIALIZED;
  sdCardInitialized = true;
  
  Serial.printf("SD Card initialized! Total: %lluMB, Used: %lluMB, Free: %lluMB\n",
                SD.totalBytes() / (1024 * 1024),
                SD.usedBytes() / (1024 * 1024),
                sdCardFreeSpace / (1024 * 1024));
  
  return true;
}

void checkSDCardStatus() {
  if (millis() - lastSDCheck > SD_CHECK_INTERVAL) {
    if (sdCardInitialized) {
      // Verify SD card is still accessible
      if (!SD.exists("/")) {
        Serial.println("⚠️ SD card removed!");
        sdCardStatus = SD_NOT_PRESENT;
        sdCardInitialized = false;
        writeLogToSD("ERROR", "SD card removed unexpectedly");
      } else {
        // Update free space
        sdCardFreeSpace = SD.totalBytes() - SD.usedBytes();
      }
    } else {
      // Try to reinitialize
      if (initializeSDCard()) {
        showNotificationMsg("SD Card Reconnected");
      }
    }
    lastSDCheck = millis();
  }
}

void writeLogToSD(String logType, String message) {
  if (!sdCardInitialized) return;
  
  String timestamp = getCurrentDateTimeString();
  String logEntry = timestamp + "," + logType + "," + message;
  
  // Write to daily log file
  if (currentLogFile.length() > 0) {
    File logFile = SD.open(currentLogFile, FILE_APPEND);
    if (logFile) {
      logFile.println(logEntry);
      logFile.close();
    }
  }
  
  // Also write to system log
  File systemLog = SD.open("/logs/system.csv", FILE_APPEND);
  if (systemLog) {
    systemLog.println(logEntry);
    systemLog.close();
  }
  
  Serial.println("📝 " + logType + ": " + message);
}

void createDailyLogFile() {
  if (!sdCardInitialized) return;
  
  String dateStr = getCurrentDateString();
  currentLogFile = "/logs/" + dateStr + ".csv";
  
  // Create header if file doesn't exist
  if (!SD.exists(currentLogFile)) {
    File logFile = SD.open(currentLogFile, FILE_WRITE);
    if (logFile) {
      logFile.println("Timestamp,Type,Message");
      logFile.close();
      writeLogToSD("SYSTEM", "Created new daily log file");
    }
  }
}

void writeAttendanceToSD(int studentId, String subjectCode, int teacherId) {
  if (!sdCardInitialized) return;
  
  String timestamp = getCurrentDateTimeString();
  String data = timestamp + "," + 
                String(studentId) + "," + 
                subjectCode + "," + 
                String(teacherId) + ",0"; // 0 = not uploaded yet
  
  // Write to daily attendance file
  String dateStr = getCurrentDateString();
  String fileName = "/attendance/" + dateStr + ".csv";
  
  // Create file with header if it doesn't exist
  if (!SD.exists(fileName)) {
    File attFile = SD.open(fileName, FILE_WRITE);
    if (attFile) {
      attFile.println("Timestamp,StudentID,Subject,TeacherID,Uploaded");
      attFile.close();
    }
  }
  
  // Append attendance record
  File attFile = SD.open(fileName, FILE_APPEND);
  if (attFile) {
    attFile.println(data);
    attFile.close();
  }
  
  // Also write to session-specific file if session is active
  if (attendanceSessionActive && sessionFileName.length() > 0) {
    File sessionFile = SD.open(sessionFileName, FILE_APPEND);
    if (sessionFile) {
      sessionFile.println(data);
      sessionFile.close();
    }
  }
}

void writeAttendanceToSD(String data) {
  if (!sdCardInitialized) return;
  
  // Write to daily attendance file
  String dateStr = getCurrentDateString();
  String fileName = "/attendance/" + dateStr + ".csv";
  
  // Create file with header if it doesn't exist
  if (!SD.exists(fileName)) {
    File attFile = SD.open(fileName, FILE_WRITE);
    if (attFile) {
      attFile.println("Timestamp,StudentID,Subject,TeacherID,Uploaded");
      attFile.close();
    }
  }
  
  // Append attendance record
  File attFile = SD.open(fileName, FILE_APPEND);
  if (attFile) {
    attFile.println(data);
    attFile.close();
  }
}

void backupConfigurationToSD() {
  if (!sdCardInitialized) return;
  
  String timestamp = getCurrentDateTimeString();
  timestamp.replace(":", "-");
  String fileName = "/backup/config_" + timestamp + ".json";
  
  File backupFile = SD.open(fileName, FILE_WRITE);
  if (backupFile) {
    // Create JSON document
    JsonDocument doc;
    
    // System info
    doc["device_id"] = DEVICE_ID;
    doc["school_name"] = SCHOOL_NAME;
    doc["academic_year"] = ACADEMIC_YEAR;
    doc["division"] = DIVISION;
    doc["backup_timestamp"] = timestamp;
    
    // Teachers array
    JsonArray teachersArray = doc["teachers"].to<JsonArray>();
    for (int i = 0; i < teacherCount; i++) {
      JsonObject teacher = teachersArray.add<JsonObject>();
      teacher["id"] = teachers[i].fingerprintId;
      teacher["name"] = teachers[i].name;
      teacher["subjects"] = teachers[i].subjects;
    }
    
    // Subjects array
    JsonArray subjectsArray = doc["subjects"].to<JsonArray>();
    for (const Subject& subj : subjectList) {
      JsonObject subject = subjectsArray.add<JsonObject>();
      subject["code"] = subj.code;
      subject["name"] = subj.name;
      subject["teacher_id"] = subj.teacherId;
      subject["active"] = subj.active;
    }
    
    // Serialize JSON to file
    String json;
    serializeJsonPretty(doc, json);
    backupFile.print(json);
    backupFile.close();
    
    writeLogToSD("BACKUP", "Configuration backed up to SD");
    showNotificationMsg("Backup created");
  }
}

void restoreConfigurationFromSD(String fileName) {
  if (!sdCardInitialized) return;
  
  File backupFile = SD.open(fileName);
  if (backupFile) {
    String json = "";
    while (backupFile.available()) {
      json += (char)backupFile.read();
    }
    backupFile.close();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);
    
    if (!error) {
      // Restore configuration
      DEVICE_ID = doc["device_id"].as<String>();
      SCHOOL_NAME = doc["school_name"].as<String>();
      ACADEMIC_YEAR = doc["academic_year"].as<String>();
      DIVISION = doc["division"].as<String>();
      
      // Restore teachers
      JsonArray teachersArray = doc["teachers"].as<JsonArray>();
      teacherCount = 0;
      
      for (JsonObject teacherObj : teachersArray) {
        if (teacherCount < 10) {
          teachers[teacherCount].fingerprintId = teacherObj["id"].as<int>();
          teachers[teacherCount].name = teacherObj["name"].as<String>();
          teachers[teacherCount].subjects = teacherObj["subjects"].as<String>();
          teacherCount++;
        }
      }
      
      // Restore subjects
      JsonArray subjectsArray = doc["subjects"].as<JsonArray>();
      subjectList.clear();
      
      for (JsonObject subjectObj : subjectsArray) {
        Subject subj;
        subj.code = subjectObj["code"].as<String>();
        subj.name = subjectObj["name"].as<String>();
        subj.teacherId = subjectObj["teacher_id"].as<int>();
        subj.active = subjectObj["active"].as<bool>();
        subjectList.push_back(subj);
      }
      
      // Save to preferences
      saveConfiguration();
      
      writeLogToSD("RESTORE", "Configuration restored from SD");
      showNotificationMsg("Restore complete");
    }
  }
}

void listFilesOnSD() {
  if (!sdCardInitialized) return;
  
  Serial.println("=== SD Card Files ===");
  File root = SD.open("/");
  File file = root.openNextFile();
  sdCardFilesCount = 0;
  
  while (file) {
    if (!file.isDirectory()) {
      Serial.print("File: ");
      Serial.print(file.name());
      Serial.print(" Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
      sdCardFilesCount++;
    }
    file = root.openNextFile();
  }
  
  Serial.printf("Total files: %d\n", sdCardFilesCount);
}

void deleteOldLogs(int daysToKeep) {
  if (!sdCardInitialized) return;
  
  File root = SD.open("/logs");
  File file = root.openNextFile();
  int deletedCount = 0;
  
  while (file) {
    if (!file.isDirectory()) {
      String fileName = file.name();
      // Check if file is older than specified days
      // Implementation depends on your naming convention
      // For simplicity, we'll delete all but today's file
      String today = getCurrentDateString();
      if (!fileName.startsWith(today)) {
        SD.remove("/logs/" + fileName);
        deletedCount++;
      }
    }
    file = root.openNextFile();
  }
  
  if (deletedCount > 0) {
    writeLogToSD("CLEANUP", "Deleted " + String(deletedCount) + " old log files");
  }
}

void formatSDCard() {
  if (!sdCardInitialized) return;
  
  writeLogToSD("SYSTEM", "Formatting SD card requested");
  
  // This is a dangerous operation - should only be done via admin
  // Note: Full format is complex, we'll just delete all files
  deleteOldLogs(0); // Delete all logs
  
  // Delete attendance files
  File attDir = SD.open("/attendance");
  File attFile = attDir.openNextFile();
  while (attFile) {
    if (!attFile.isDirectory()) {
      SD.remove("/attendance/" + String(attFile.name()));
    }
    attFile = attDir.openNextFile();
  }
  
  writeLogToSD("SYSTEM", "SD card formatted");
  showNotificationMsg("SD Card Formatted");
}

String getCurrentDateTimeString() {
  if (rtc.begin()) {
    DateTime now = rtc.now();
    char buffer[20];
    sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    return String(buffer);
  }
  return "0000-00-00 00:00:00";
}

String getCurrentDateString() {
  if (rtc.begin()) {
    DateTime now = rtc.now();
    char buffer[11];
    sprintf(buffer, "%04d-%02d-%02d",
            now.year(), now.month(), now.day());
    return String(buffer);
  }
  return "0000-00-00";
}

String getCurrentTimeString() {
  if (rtc.begin()) {
    DateTime now = rtc.now();
    char buffer[9];
    sprintf(buffer, "%02d:%02d:%02d",
            now.hour(), now.minute(), now.second());
    return String(buffer);
  }
  return "00:00:00";
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
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println("✅ RTC initialized");
  }
  
  // Initialize Fingerprint Sensor
  Serial.println("Initializing fingerprint sensor...");
  fingerSerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX, FINGERPRINT_TX);
  delay(1000);
  
  for (int attempt = 1; attempt <= 5; attempt++) {
    Serial.print("Fingerprint sensor attempt ");
    Serial.print(attempt);
    Serial.print("/5... ");
    
    if (finger.verifyPassword()) {
      fingerprintInitialized = true;
      Serial.println("✅ SUCCESS");
      
      finger.LEDcontrol(false);
      
      int templateCount = finger.getTemplateCount();
      Serial.print("Templates found: ");
      Serial.println(templateCount);
      
      // Check if GFM (ID 1) is enrolled
      if (finger.loadModel(1) == FINGERPRINT_OK) {
        gfmEnrolled = true;
        Serial.println("✅ GFM (ID 1) is enrolled");
      } else {
        Serial.println("⚠️ GFM (ID 1) not found");
      }
      
      break;
    } else {
      Serial.println("❌ FAILED");
      delay(500);
    }
  }
  
  if (!fingerprintInitialized) {
    Serial.println("❌ FINGERPRINT SENSOR NOT FOUND!");
  }
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  Serial.println("✅ WiFi initialized");
}

// =================== INITIALIZE PREFERENCES ===================
void initializePreferences() {
  preferences.begin("system", false);
  
  // Load saved WiFi credentials
  String savedSSID = preferences.getString("wifi_ssid", "");
  String savedPassword = preferences.getString("wifi_pass", "");
  
  if (savedSSID.length() > 0) {
    Serial.print("Found saved WiFi: ");
    Serial.println(savedSSID);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
  }
  
  preferences.end();
}

// =================== LOAD CONFIGURATION ===================
void loadConfiguration() {
  preferences.begin("config", true);
  
  deviceConfigured = preferences.getBool("configured", false);
  if (deviceConfigured) {
    DEVICE_ID = preferences.getString("device_id", "");
    SCHOOL_NAME = preferences.getString("school_name", "Default School");
    ACADEMIC_YEAR = preferences.getString("academic_year", "2024-25");
    DIVISION = preferences.getString("division", "");
    
    // Load teachers
    teacherCount = preferences.getInt("teacher_count", 0);
    for (int i = 0; i < teacherCount; i++) {
      String key = "teacher_" + String(i);
      teachers[i].fingerprintId = preferences.getInt((key + "_id").c_str(), 6 + i);
      teachers[i].name = preferences.getString((key + "_name").c_str(), "");
      teachers[i].subjects = preferences.getString((key + "_subjects").c_str(), "");
    }
    
    // Load subjects
    int subjectCount = preferences.getInt("subject_count", 0);
    subjectList.clear();
    for (int i = 0; i < subjectCount; i++) {
      Subject subj;
      String key = "subject_" + String(i);
      subj.code = preferences.getString((key + "_code").c_str(), "");
      subj.name = preferences.getString((key + "_name").c_str(), "");
      subj.teacherId = preferences.getInt((key + "_teacher").c_str(), 0);
      subj.active = preferences.getBool((key + "_active").c_str(), false);
      subjectList.push_back(subj);
    }
  }
  
  preferences.end();
  
  Serial.print("Device configured: ");
  Serial.println(deviceConfigured ? "YES" : "NO");
  if (deviceConfigured) {
    Serial.print("Division: ");
    Serial.println(DIVISION);
    Serial.print("Teachers: ");
    Serial.println(teacherCount);
    Serial.print("Subjects: ");
    Serial.println(subjectList.size());
  }
}

// =================== CHECK SETUP STATUS ===================
void checkSetupStatus() {
  // Check if device is configured and has GFM enrolled
  if (!deviceConfigured || !gfmEnrolled) {
    systemMode = MODE_SETUP;
    Serial.println("System needs setup");
  } else {
    systemMode = MODE_NORMAL;
    Serial.println("System is configured");
  }
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  checkFactoryReset();
  checkSDCardStatus();
  
  // Handle different system modes
  if (systemMode == MODE_SETUP) {
    handleSetupWizard();
  } else if (systemMode == MODE_MASTER_BYPASS) {
    // Master bypass mode - full admin access
  }
  
  // Normal operation functions
  if (systemMode != MODE_SETUP) {
    if (currentScreen == SCREEN_HOME) {
      // Auto-sync when on home screen and connected
      if (WiFi.status() == WL_CONNECTED) {
        if (millis() - lastConfigSync > CONFIG_SYNC_INTERVAL) {
          syncConfigurationFromServer();
          lastConfigSync = millis();
        }
      }
    }
    
    if (currentScreen == SCREEN_ADMIN_AUTH || currentScreen == SCREEN_TEACHER_AUTH) {
      handleFingerprintLogin();
    }
    
    if (currentScreen == SCREEN_ATTENDANCE_MODE) {
      handleAttendanceMode();
      
      // Check session timeout
      if (attendanceSessionActive && millis() - sessionStartTime > SESSION_TIMEOUT) {
        showNotificationMsg("Session timeout");
        attendanceSessionActive = false;
        currentScreen = SCREEN_TEACHER_MENU;
        needRefresh = true;
      }
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
  }
  
  // Update WiFi connection
  if (wifiConnecting) {
    updateWiFiConnection();
  }
  
  checkWifiStatusChange();
  
  if (notificationActive && millis() - notificationStartTime > NOTIFICATION_DURATION) {
    clearNotification();
  }
  
  // Handle factory reset countdown
  if (factoryResetPending) {
    if (millis() - factoryResetStartTime > 3000) {
      performFactoryReset();
    }
  }
  
  // Refresh display
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 200 || needRefresh) {
    showScreen();
    lastUpdate = millis();
    needRefresh = false;
  }
  
  delay(20);
}

// =================== ATTENDANCE FUNCTIONS (Updated for SD Card) ===================
void saveAttendanceRecord(int studentId, String subjectCode) {
  // Save to Preferences (for quick access)
  preferences.begin("attendance", false);
  
  int pendingCount = preferences.getInt("pending_count", 0);
  String key = "record_" + String(pendingCount);
  
  String data = String(studentId) + "," + 
                subjectCode + "," + 
                String(currentUserId) + "," + 
                String(millis());
  
  preferences.putString(key.c_str(), data);
  preferences.putInt("pending_count", pendingCount + 1);
  
  preferences.end();
  
  // Save to SD Card (for backup)
  writeAttendanceToSD(studentId, subjectCode, currentUserId);
  
  // Log the attendance
  writeLogToSD("ATTENDANCE", "Student " + String(studentId) + " marked for " + subjectCode);
}

void syncAttendanceToServer() {
  if (WiFi.status() != WL_CONNECTED) {
    showNotificationMsg("No WiFi - Stored");
    writeLogToSD("SYNC", "No WiFi for attendance sync");
    return;
  }
  
  preferences.begin("attendance", true);
  
  int pendingCount = preferences.getInt("pending_count", 0);
  
  if (pendingCount > 0) {
    JsonDocument doc;
    JsonArray records = doc["records"].to<JsonArray>();
    
    for (int i = 0; i < pendingCount; i++) {
      String key = "record_" + String(i);
      String data = preferences.getString(key.c_str(), "");
      
      if (data.length() > 0) {
        int firstComma = data.indexOf(',');
        int secondComma = data.indexOf(',', firstComma + 1);
        int thirdComma = data.indexOf(',', secondComma + 1);
        
        if (firstComma != -1 && secondComma != -1 && thirdComma != -1) {
          int studentId = data.substring(0, firstComma).toInt();
          String subject = data.substring(firstComma + 1, secondComma);
          int teacherId = data.substring(secondComma + 1, thirdComma).toInt();
          long timestamp = data.substring(thirdComma + 1).toInt();
          
          JsonObject record = records.add<JsonObject>();
          record["student_id"] = studentId;
          record["subject"] = subject;
          record["teacher_id"] = teacherId;
          record["timestamp"] = timestamp;
          record["device_id"] = DEVICE_ID;
        }
      }
    }
    
    preferences.end();
    
    // Upload to server
    HTTPClient http;
    http.begin(BACKEND_URL + "/api/attendance/upload");
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(8000);
    
    String json;
    serializeJson(doc, json);
    
    int httpCode = http.POST(json);
    
    if (httpCode == 200) {
      // Clear pending records on success
      preferences.begin("attendance", false);
      preferences.putInt("pending_count", 0);
      preferences.end();
      
      attendanceBuffer.clear();
      attendanceCount = 0;
      
      writeLogToSD("SYNC", "Uploaded " + String(pendingCount) + " attendance records");
      showNotificationMsg("✅ Uploaded " + String(pendingCount) + " records");
    } else {
      writeLogToSD("ERROR", "Attendance upload failed: " + String(httpCode));
      showNotificationMsg("Upload failed");
    }
    
    http.end();
  } else {
    preferences.end();
  }
}

// =================== DISPLAY FUNCTIONS (SD Card Screens) ===================
void drawSDCardScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(45, 0);
  display.println("SD CARD");
  
  if (!sdCardInitialized) {
    display.setCursor(20, 30);
    display.println("SD Card NOT FOUND");
    display.setCursor(15, 45);
    display.println("Insert SD Card");
    return;
  }
  
  display.setCursor(5, 20);
  display.print("Status: ");
  display.print("READY");
  
  display.setCursor(5, 32);
  display.print("Free: ");
  display.print(sdCardFreeSpace / (1024 * 1024));
  display.print(" MB");
  
  display.setCursor(5, 44);
  display.print("Files: ");
  display.print(sdCardFilesCount);
  
  display.setCursor(5, 56);
  display.print("Logging: ");
  display.print(currentLogFile.length() > 0 ? "ON" : "OFF");
}

void drawSDLogsScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(50, 0);
  display.println("LOGS");
  
  if (!sdCardInitialized) {
    display.setCursor(20, 30);
    display.println("SD Card NOT FOUND");
    return;
  }
  
  display.setCursor(5, 20);
  display.print("Current Log: ");
  
  if (currentLogFile.length() > 0) {
    String fileName = currentLogFile.substring(currentLogFile.lastIndexOf('/') + 1);
    if (fileName.length() > 15) {
      display.println(fileName.substring(0, 13) + "..");
    } else {
      display.println(fileName);
    }
  } else {
    display.println("None");
  }
  
  display.setCursor(5, 35);
  display.print("Today: ");
  
  // Count today's log entries (simplified)
  int todayLogs = attendanceCount; // Use attendance count as proxy
  display.print(todayLogs);
  display.print(" records");
  
  display.setCursor(5, 50);
  display.print("Press SEL to view");
}

void drawSDBackupScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(40, 0);
  display.println("BACKUP/RESTORE");
  
  if (!sdCardInitialized) {
    display.setCursor(20, 30);
    display.println("SD Card NOT FOUND");
    return;
  }
  
  display.setCursor(5, 20);
  display.println("1. Backup Config");
  
  display.setCursor(5, 35);
  display.println("2. Restore Config");
  
  display.setCursor(5, 50);
  display.println("3. Format SD Card");
  
  // Highlight selection
  display.fillRect(0, 18 + (menuIndex * 15), 128, 14, SH110X_WHITE);
  display.setTextColor(SH110X_BLACK);
  
  display.setCursor(5, 20);
  display.println("1. Backup Config");
  
  display.setCursor(5, 35);
  display.println("2. Restore Config");
  
  display.setCursor(5, 50);
  display.println("3. Format SD Card");
  
  display.setTextColor(SH110X_WHITE);
}

// =================== BUTTON HANDLING (Updated for SD Card) ===================
void handleButtonPress(int button) {
  needRefresh = true;
  
  // Cancel factory reset if any button is pressed
  if (factoryResetPending && currentScreen == SCREEN_FACTORY_RESET) {
    factoryResetPending = false;
    currentScreen = SCREEN_HOME;
    showNotificationMsg("Reset cancelled");
    return;
  }
  
  // Handle setup mode
  if (systemMode == MODE_SETUP) {
    switch(currentScreen) {
      case SCREEN_SETUP_CHOICE:
        if (button == 0) menuIndex = (menuIndex > 0) ? menuIndex - 1 : 2;
        else if (button == 1) menuIndex = (menuIndex < 2) ? menuIndex + 1 : 0;
        else if (button == 2) {
          if (menuIndex == 0) {
            // Auto setup
            currentSetupMode = SETUP_AUTO;
            currentScreen = SCREEN_SETUP_SYNC;
          } else if (menuIndex == 1) {
            // Manual setup
            currentSetupMode = SETUP_MANUAL;
            currentScreen = SCREEN_SETUP_ENROLL_GFM;
          } else if (menuIndex == 2) {
            // Skip setup (use master bypass)
            systemMode = MODE_NORMAL;
            currentScreen = SCREEN_HOME;
            showNotificationMsg("Setup skipped");
          }
        }
        break;
        
      case SCREEN_SETUP_SYNC:
        if (button == 3) {
          currentScreen = SCREEN_SETUP_CHOICE;
        }
        break;
        
      case SCREEN_SETUP_ENROLL_GFM:
        if (button == 2 && fpState == FP_FAILED) {
          fpState = FP_IDLE;
        } else if (button == 3) {
          currentScreen = SCREEN_SETUP_CHOICE;
        }
        break;
        
      case SCREEN_SETUP_ENROLL_TEACHERS:
        if (button == 2 && fpState == FP_FAILED) {
          fpState = FP_IDLE;
        } else if (button == 3) {
          // Skip remaining teachers
          finishSetup();
        }
        break;
        
      default:
        if (button == 3) {
          currentScreen = SCREEN_SETUP_CHOICE;
        }
        break;
    }
    return;
  }
  
  // Master bypass mode
  if (systemMode == MODE_MASTER_BYPASS) {
    switch(currentScreen) {
      case SCREEN_ADMIN_MENU:
        if (button == 0) menuIndex = (menuIndex > 0) ? menuIndex - 1 : 6;
        else if (button == 1) menuIndex = (menuIndex < 6) ? menuIndex + 1 : 0;
        else if (button == 2) {
          switch(menuIndex) {
            case 0: currentScreen = SCREEN_ENROLL_MODE; resetEnrollmentState(); break;
            case 1: currentScreen = SCREEN_ENROLL_TEACHER; break;
            case 2: currentScreen = SCREEN_SYSTEM_CONFIG; break;
            case 3: syncConfigurationFromServer(); break;
            case 4: currentScreen = SCREEN_REPORTS; break;
            case 5: currentScreen = SCREEN_SD_CARD; break;
            case 6: currentScreen = SCREEN_WIFI_SCAN; break;
          }
        } else if (button == 3) {
          systemMode = MODE_NORMAL;
          logout();
          showNotificationMsg("Bypass ended");
        }
        break;
        
      default:
        break;
    }
  }
  
  // Normal operation
  switch(currentScreen) {
    case SCREEN_HOME:
      if (button == 0) menuIndex = 0;
      else if (button == 1) menuIndex = 1;
      else if (button == 2) {
        if (menuIndex == 0) {
          currentScreen = SCREEN_ADMIN_AUTH;
        } else if (menuIndex == 1) {
          currentScreen = SCREEN_TEACHER_AUTH;
        }
      }
      break;
      
    case SCREEN_ADMIN_MENU:
      if (button == 0) menuIndex = (menuIndex > 0) ? menuIndex - 1 : 6;
      else if (button == 1) menuIndex = (menuIndex < 6) ? menuIndex + 1 : 0;
      else if (button == 2) {
        switch(menuIndex) {
          case 0: currentScreen = SCREEN_ENROLL_MODE; resetEnrollmentState(); break;
          case 1: currentScreen = SCREEN_ENROLL_TEACHER; break;
          case 2: currentScreen = SCREEN_SYSTEM_CONFIG; break;
          case 3: syncConfigurationFromServer(); break;
          case 4: currentScreen = SCREEN_REPORTS; break;
          case 5: currentScreen = SCREEN_SD_CARD; break;
          case 6: currentScreen = SCREEN_WIFI_SCAN; break;
        }
      } else if (button == 3) {
        logout();
      }
      break;
      
    case SCREEN_SD_CARD:
      if (button == 0) menuIndex = 0;
      else if (button == 1) menuIndex = 1;
      else if (button == 2) {
        if (menuIndex == 0) {
          currentScreen = SCREEN_SD_LOGS;
        } else if (menuIndex == 1) {
          currentScreen = SCREEN_SD_BACKUP;
        }
      } else if (button == 3) {
        currentScreen = SCREEN_ADMIN_MENU;
      }
      break;
      
    case SCREEN_SD_BACKUP:
      if (button == 0) menuIndex = (menuIndex > 0) ? menuIndex - 1 : 2;
      else if (button == 1) menuIndex = (menuIndex < 2) ? menuIndex + 1 : 0;
      else if (button == 2) {
        switch(menuIndex) {
          case 0:
            backupConfigurationToSD();
            break;
          case 1:
            // Restore would need file selection - simplified for now
            showNotificationMsg("Restore not implemented");
            break;
          case 2:
            formatSDCard();
            break;
        }
      } else if (button == 3) {
        currentScreen = SCREEN_SD_CARD;
      }
      break;
      
    case SCREEN_TEACHER_MENU:
      if (button == 0) menuIndex = (menuIndex > 0) ? menuIndex - 1 : 4;
      else if (button == 1) menuIndex = (menuIndex < 4) ? menuIndex + 1 : 0;
      else if (button == 2) {
        switch(menuIndex) {
          case 0: 
            currentScreen = SCREEN_SUBJECT_SELECT;
            menuIndex = 0;
            break;
          case 1: 
            if (!getTeacherSubjects(currentUserId).empty()) {
              currentScreen = SCREEN_ATTENDANCE_MODE;
              attendanceSessionActive = true;
              sessionStartTime = millis();
              attendanceBuffer.clear();
              attendanceCount = 0;
              
              // Create session file on SD card
              if (sdCardInitialized) {
                String timestamp = getCurrentDateTimeString();
                timestamp.replace(" ", "_");
                timestamp.replace(":", "-");
                sessionFileName = "/attendance/session_" + timestamp + ".csv";
                
                File sessionFile = SD.open(sessionFileName, FILE_WRITE);
                if (sessionFile) {
                  sessionFile.println("Timestamp,StudentID,Subject,TeacherID,Uploaded");
                  sessionFile.close();
                  writeLogToSD("SESSION", "Attendance session started: " + currentSubject);
                }
              }
              
              currentSubject = getSelectedSubjectCode();
              showNotificationMsg("Started: " + currentSubject);
            } else {
              showNotificationMsg("No subjects");
            }
            break;
          case 2: syncAttendanceToServer(); break;
          case 3: currentScreen = SCREEN_MY_CLASSES; break;
          case 4: logout(); break;
        }
      } else if (button == 3) {
        logout();
      }
      break;
      
    case SCREEN_ATTENDANCE_MODE:
      if (button == 3) {
        attendanceSessionActive = false;
        
        // Close session file
        if (sessionFileName.length() > 0) {
          writeLogToSD("SESSION", "Attendance session ended");
          sessionFileName = "";
        }
        
        syncAttendanceToServer();
        currentScreen = SCREEN_TEACHER_MENU;
        showNotificationMsg("Session ended");
      }
      break;
      
    // ... [Rest of the button handling remains the same] ...
  }
}

// =================== UPDATED ADMIN MENU ===================
void drawAdminMenu() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(50, 0);
  if (systemMode == MODE_MASTER_BYPASS) {
    display.println("MASTER");
  } else {
    display.println("ADMIN");
  }
  
  // Updated menu items with SD Card option
  String menuItems[7] = {
    "1. ENROLL STUDENT",
    "2. ENROLL TEACHER",
    "3. SYSTEM CONFIG",
    "4. SYNC FROM SERVER",
    "5. VIEW REPORTS",
    "6. SD CARD",
    "7. WIFI SETTINGS"
  };
  
  int startIdx = 0;
  if (menuIndex > 3) {
    startIdx = menuIndex - 3;
  }
  
  for (int i = 0; i < 4; i++) {
    int itemIdx = startIdx + i;
    if (itemIdx >= 7) break;
    
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

// =================== UPDATED FOOTER ===================
void drawFooter() {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  
  display.fillRect(0, 56, 128, 8, SH110X_BLACK);
  
  // SD Card indicator
  if (sdCardInitialized) {
    display.setCursor(115, 56);
    display.print("SD");
  } else {
    display.setCursor(115, 56);
    display.print("NO");
  }
  
  if (systemMode == MODE_MASTER_BYPASS) {
    display.setCursor(5, 56);
    display.print("MASTER BYPASS");
    display.setCursor(90, 56);
    display.print("B: Exit");
    return;
  }
  
  switch(currentScreen) {
    case SCREEN_HOME:
      display.setCursor(5, 56);
      display.print("U/D: Select");
      display.setCursor(80, 56);
      display.print("SEL: Enter");
      break;
      
    case SCREEN_ADMIN_AUTH:
    case SCREEN_TEACHER_AUTH:
      display.setCursor(40, 56);
      display.print("Scan finger");
      break;
      
    case SCREEN_ADMIN_MENU:
    case SCREEN_TEACHER_MENU:
    case SCREEN_SUBJECT_SELECT:
      display.setCursor(5, 56);
      display.print("U/D: Navigate");
      display.setCursor(80, 56);
      display.print("SEL: Select");
      break;
      
    case SCREEN_ATTENDANCE_MODE:
      display.setCursor(40, 56);
      display.print("B: End session");
      break;
      
    case SCREEN_ENROLL_MODE:
      if (enrollState == ENROLL_PENDING || enrollState == ENROLL_ERROR) {
        display.setCursor(35, 56);
        display.print("SEL: Start");
      } else if (enrollState == ENROLL_CAPTURING) {
        display.setCursor(45, 56);
        display.print("BUSY");
      } else {
        display.setCursor(45, 56);
        display.print("READY");
      }
      break;
      
    case SCREEN_SD_CARD:
      display.setCursor(5, 56);
      display.print("U/D: Select");
      display.setCursor(80, 56);
      display.print("SEL: Enter");
      break;
      
    case SCREEN_SD_BACKUP:
      display.setCursor(5, 56);
      display.print("U/D: Select");
      display.setCursor(80, 56);
      display.print("SEL: Execute");
      break;
      
    case SCREEN_SETUP_CHOICE:
      display.setCursor(5, 56);
      display.print("U/D: Select");
      display.setCursor(80, 56);
      display.print("SEL: Choose");
      break;
      
    case SCREEN_SETUP_ENROLL_GFM:
    case SCREEN_SETUP_ENROLL_TEACHERS:
      if (fpState == FP_IDLE || fpState == FP_FAILED) {
        display.setCursor(35, 56);
        display.print("SEL: Start");
      } else {
        display.setCursor(45, 56);
        display.print("BUSY");
      }
      break;
      
    case SCREEN_FACTORY_RESET:
      display.setCursor(40, 56);
      display.print("Release to cancel");
      break;
      
    default:
      if (currentScreen != SCREEN_BOOT && currentScreen != SCREEN_HOME) {
        display.setCursor(50, 56);
        display.print("B: Back");
      }
      break;
  }
}

// =================== MISSING FUNCTION IMPLEMENTATIONS ===================

// Core functions
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

void checkButtons() {
  int buttons[4] = {BUTTON_UP, BUTTON_DOWN, BUTTON_SELECT, BUTTON_BACK};
  
  for (int i = 0; i < 4; i++) {
    if (digitalRead(buttons[i]) == LOW) {
      if (buttonPressTime[i] == 0) {
        buttonPressTime[i] = millis();
      } else if (!buttonLongPressed[i] && millis() - buttonPressTime[i] > 800) {
        handleLongPress(i);
        buttonLongPressed[i] = true;
      }
    } else {
      if (buttonPressTime[i] != 0 && !buttonLongPressed[i]) {
        handleButtonPress(i);
      }
      buttonPressTime[i] = 0;
      buttonLongPressed[i] = false;
    }
  }
}

void handleLongPress(int button) {
  // Handle long press for each button
  switch(button) {
    case 2: // SELECT button long press
      if (currentScreen == SCREEN_HOME) {
        // Quick admin login (for testing)
        currentUserRole = ROLE_ADMIN;
        currentScreen = SCREEN_ADMIN_MENU;
        showNotificationMsg("Quick Admin");
      }
      break;
      
    case 3: // BACK button long press
      if (currentScreen != SCREEN_HOME) {
        logout();
      }
      break;
  }
}

void logout() {
  currentUserRole = ROLE_NONE;
  currentUserId = -1;
  currentUserName = "";
  currentSubject = "";
  currentScreen = SCREEN_HOME;
  menuIndex = 0;
  needRefresh = true;
  showNotificationMsg("Logged out");
}

// Utility functions
void factoryReset() {
  Serial.println("⚠️ Performing factory reset...");
  
  // Clear all preferences
  preferences.begin("system", false);
  preferences.clear();
  preferences.end();
  
  preferences.begin("config", false);
  preferences.clear();
  preferences.end();
  
  preferences.begin("attendance", false);
  preferences.clear();
  preferences.end();
  
  // Reset WiFi credentials
  WiFi.disconnect(true);
  
  // Reset global variables
  deviceConfigured = false;
  gfmEnrolled = false;
  currentUserRole = ROLE_NONE;
  teacherCount = 0;
  subjectList.clear();
  attendanceBuffer.clear();
  
  // Restart device
  ESP.restart();
}

void checkFactoryReset() {
  // Check if all 4 buttons are pressed for 3 seconds
  static unsigned long resetStartTime = 0;
  static bool resetChecking = false;
  
  if (digitalRead(BUTTON_UP) == LOW && digitalRead(BUTTON_DOWN) == LOW &&
      digitalRead(BUTTON_SELECT) == LOW && digitalRead(BUTTON_BACK) == LOW) {
    
    if (!resetChecking) {
      resetStartTime = millis();
      resetChecking = true;
      showNotificationMsg("Hold 3s to reset");
    } else if (millis() - resetStartTime > 3000) {
      currentScreen = SCREEN_FACTORY_RESET;
      factoryResetPending = true;
      factoryResetStartTime = millis();
      resetChecking = false;
    }
  } else {
    resetChecking = false;
  }
}

void performFactoryReset() {
  factoryReset();
}

// WiFi functions
void scanWiFiNetworks() {
  wifiScanning = true;
  wifiNetworkCount = 0;
  
  int n = WiFi.scanNetworks();
  
  for (int i = 0; i < n && i < 20; i++) {
    wifiNetworks[i] = WiFi.SSID(i) + " (" + String(WiFi.RSSI(i)) + "dB)";
    wifiNetworkCount++;
  }
  
  wifiScanning = false;
  wifiSelectedIndex = 0;
}

void connectToWiFi(String ssid, String password) {
  WiFi.begin(ssid.c_str(), password.c_str());
  wifiConnecting = true;
  wifiConnectionStartTime = millis();
  
  // Save credentials
  preferences.begin("system", false);
  preferences.putString("wifi_ssid", ssid);
  preferences.putString("wifi_pass", password);
  preferences.end();
  
  writeLogToSD("WIFI", "Attempting to connect to " + ssid);
}

void updateWiFiConnection() {
  if (!wifiConnecting) return;
  
  wl_status_t status = WiFi.status();
  
  if (status == WL_CONNECTED) {
    wifiConnecting = false;
    connectedSSID = WiFi.SSID();
    showNotificationMsg("Connected: " + connectedSSID);
    writeLogToSD("WIFI", "Connected to " + connectedSSID);
  } else if (millis() - wifiConnectionStartTime > WIFI_CONNECTION_TIMEOUT) {
    wifiConnecting = false;
    showNotificationMsg("Connection failed");
    writeLogToSD("WIFI", "Connection timeout");
  }
}

void checkWifiStatusChange() {
  wl_status_t currentStatus = WiFi.status();
  
  if (currentStatus != lastWifiStatus) {
    if (currentStatus == WL_CONNECTED) {
      Serial.println("WiFi connected");
    } else if (lastWifiStatus == WL_CONNECTED) {
      Serial.println("WiFi disconnected");
      showNotificationMsg("WiFi disconnected");
      writeLogToSD("WIFI", "Disconnected");
    }
    lastWifiStatus = currentStatus;
    needRefresh = true;
  }
}

void manualRefreshWiFi() {
  if (wifiScanning) return;
  
  wifiScanning = true;
  wifiNetworkCount = 0;
  
  // Force a new scan
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(true); // Async scan
  
  // We'll check results in next iteration
}

// Fingerprint functions
void handleFingerprintLogin() {
  if (fpState != FP_IDLE) return;
  
  int fingerId = getFingerprintID();
  
  if (fingerId > 0) {
    // Check if it's GFM (ID 1)
    if (fingerId == 1) {
      currentUserRole = ROLE_ADMIN;
      currentUserId = 1;
      currentUserName = "GFM Admin";
      currentScreen = SCREEN_ADMIN_MENU;
      showNotificationMsg("Welcome Admin");
      writeLogToSD("LOGIN", "Admin logged in via fingerprint");
    } 
    // Check if it's a teacher
    else if (fingerId >= 6 && fingerId <= 15) {
      bool teacherFound = false;
      for (int i = 0; i < teacherCount; i++) {
        if (teachers[i].fingerprintId == fingerId) {
          currentUserRole = ROLE_TEACHER;
          currentUserId = fingerId;
          currentUserName = teachers[i].name;
          currentScreen = SCREEN_TEACHER_MENU;
          teacherFound = true;
          showNotificationMsg("Welcome " + teachers[i].name);
          writeLogToSD("LOGIN", "Teacher " + teachers[i].name + " logged in");
          break;
        }
      }
      if (!teacherFound) {
        showNotificationMsg("Teacher not found");
      }
    } else {
      showNotificationMsg("Unauthorized user");
    }
    needRefresh = true;
  }
}

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;
  
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;
  
  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) {
    lastFingerprintError = "Finger not found";
    return -1;
  }
  
  return finger.fingerID;
}

void handleSetupEnrollment(int fingerprintId, String personName) {
  // This would be called during setup enrollment
  // Implementation depends on your specific needs
}

void handleAttendanceMode() {
  if (!attendanceSessionActive) return;
  
  int fingerId = getFingerprintID();
  
  if (fingerId > 0) {
    // Student IDs are >= 100
    if (fingerId >= 100) {
      // Mark attendance
      saveAttendanceRecord(fingerId, currentSubject);
      
      // Add to buffer
      AttendanceRecord record;
      record.studentId = fingerId;
      record.subjectCode = currentSubject;
      record.teacherId = currentUserId;
      record.timestamp = millis();
      record.uploaded = false;
      attendanceBuffer.push_back(record);
      attendanceCount++;
      
      // Show feedback
      showNotificationMsg("Marked: " + String(fingerId));
      writeLogToSD("ATTENDANCE", "Student " + String(fingerId) + " marked for " + currentSubject);
    } else {
      showNotificationMsg("Not a student");
    }
  }
}

void showDetailedError(int errorCode) {
  // Show detailed error message for fingerprint errors
  String errorMsg = "Error: " + String(errorCode);
  showNotificationMsg(errorMsg);
}

void resetFingerprintState() {
  fpState = FP_IDLE;
  fpRetryCount = 0;
  firstCaptureDone = false;
  secondCaptureDone = false;
  lastFingerprintError = "";
  fpStateStartTime = 0;
}

void handleFingerprintEnrollmentFast() {
  // This is a stub - you need to implement the actual enrollment logic
  // For now, just show a notification
  showNotificationMsg("Enrollment in progress");
}

// Enrollment functions
void pollServerForEnrollment() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/enrollment/pending?device_id=" + DEVICE_ID);
  http.setTimeout(3000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    if (payload.length() > 2) { // Check if not empty array "[]"
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error && doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        if (arr.size() > 0) {
          JsonObject student = arr[0];
          pendingStudentName = student["name"].as<String>();
          pendingStudentRoll = student["roll_no"].as<int>();
          enrollState = ENROLL_PENDING;
          showNotificationMsg("New enrollment: " + pendingStudentName);
          writeLogToSD("ENROLLMENT", "Pending enrollment for " + pendingStudentName);
        }
      }
    }
  }
  
  http.end();
}

void sendEnrollmentConfirmation(int rollNo, int fingerprintId) {
  if (WiFi.status() != WL_CONNECTED) {
    showNotificationMsg("No WiFi for confirm");
    enrollState = ENROLL_ERROR;
    return;
  }
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/enrollment/confirm");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(5000);
  
  JsonDocument doc;
  doc["roll_no"] = rollNo;
  doc["fingerprint_id"] = fingerprintId;
  doc["device_id"] = DEVICE_ID;
  
  String json;
  serializeJson(doc, json);
  
  int httpCode = http.POST(json);
  
  if (httpCode == 200) {
    enrollState = ENROLL_SUCCESS;
    pendingStudentName = "";
    pendingStudentRoll = -1;
    studentInProgress = false;
    showNotificationMsg("Enrollment confirmed");
    writeLogToSD("ENROLLMENT", "Enrollment confirmed for roll " + String(rollNo));
  } else {
    enrollState = ENROLL_ERROR;
    showNotificationMsg("Confirm failed");
    writeLogToSD("ERROR", "Enrollment confirm failed: " + String(httpCode));
  }
  
  http.end();
}

void resetEnrollmentState() {
  enrollState = ENROLL_IDLE;
  pendingStudentName = "";
  pendingStudentRoll = -1;
  studentInProgress = false;
  fpState = FP_IDLE;
}

// Configuration functions
void syncConfigurationFromServer() {
  if (WiFi.status() != WL_CONNECTED) {
    showNotificationMsg("No WiFi connection");
    return;
  }
  
  HTTPClient http;
  http.begin(BACKEND_URL + "/api/config?device_id=" + DEVICE_ID);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      // Parse configuration
      SCHOOL_NAME = doc["school_name"].as<String>();
      ACADEMIC_YEAR = doc["academic_year"].as<String>();
      DIVISION = doc["division"].as<String>();
      
      // Parse subjects
      subjectList.clear();
      JsonArray subjects = doc["subjects"].as<JsonArray>();
      for (JsonObject subject : subjects) {
        Subject subj;
        subj.code = subject["code"].as<String>();
        subj.name = subject["name"].as<String>();
        subj.teacherId = subject["teacher_id"].as<int>();
        subj.active = subject["active"].as<bool>();
        subjectList.push_back(subj);
      }
      
      // Save to preferences
      saveConfiguration();
      
      showNotificationMsg("Config synced");
      writeLogToSD("SYNC", "Configuration synced from server");
    }
  } else {
    showNotificationMsg("Sync failed");
    writeLogToSD("ERROR", "Config sync failed: " + String(httpCode));
  }
  
  http.end();
}

std::vector<String> getTeacherSubjects(int teacherId) {
  std::vector<String> subjects;
  for (const Subject& subj : subjectList) {
    if (subj.teacherId == teacherId && subj.active) {
      subjects.push_back(subj.code + ": " + subj.name);
    }
  }
  return subjects;
}

String getSelectedSubjectCode() {
  std::vector<String> teacherSubjects = getTeacherSubjects(currentUserId);
  if (teacherSubjects.empty() || menuIndex >= teacherSubjects.size()) {
    return "";
  }
  
  String selected = teacherSubjects[menuIndex];
  int colonPos = selected.indexOf(':');
  if (colonPos != -1) {
    return selected.substring(0, colonPos);
  }
  return "";
}

void saveConfiguration() {
  preferences.begin("config", false);
  
  preferences.putBool("configured", true);
  preferences.putString("device_id", DEVICE_ID);
  preferences.putString("school_name", SCHOOL_NAME);
  preferences.putString("academic_year", ACADEMIC_YEAR);
  preferences.putString("division", DIVISION);
  
  // Save teachers
  preferences.putInt("teacher_count", teacherCount);
  for (int i = 0; i < teacherCount; i++) {
    String key = "teacher_" + String(i);
    preferences.putInt((key + "_id").c_str(), teachers[i].fingerprintId);
    preferences.putString((key + "_name").c_str(), teachers[i].name);
    preferences.putString((key + "_subjects").c_str(), teachers[i].subjects);
  }
  
  // Save subjects
  preferences.putInt("subject_count", subjectList.size());
  for (size_t i = 0; i < subjectList.size(); i++) {
    String key = "subject_" + String(i);
    preferences.putString((key + "_code").c_str(), subjectList[i].code);
    preferences.putString((key + "_name").c_str(), subjectList[i].name);
    preferences.putInt((key + "_teacher").c_str(), subjectList[i].teacherId);
    preferences.putBool((key + "_active").c_str(), subjectList[i].active);
  }
  
  preferences.end();
  writeLogToSD("CONFIG", "Configuration saved");
}

// Setup Wizard Functions
void startSetupWizard() {
  setupInProgress = true;
  setupStep = 0;
  currentScreen = SCREEN_SETUP_CHOICE;
  menuIndex = 0;
  needRefresh = true;
  writeLogToSD("SETUP", "Setup wizard started");
}

void handleSetupWizard() {
  // This function handles the setup wizard flow
  // Implementation depends on the current setup state
}

void handleSetupChoice() {
  // Handle setup choice selection
}

void setupDownloadConfiguration() {
  // Download configuration from server
}

void setupEnrollGFM() {
  // Enroll GFM (General Fingerprint Manager)
}

void setupEnrollTeachers() {
  // Enroll teachers during setup
}

void finishSetup() {
  systemMode = MODE_NORMAL;
  deviceConfigured = true;
  saveConfiguration();
  currentScreen = SCREEN_HOME;
  showNotificationMsg("Setup complete!");
  writeLogToSD("SETUP", "Setup completed successfully");
}

void enrollGFMManual() {
  // Manual GFM enrollment
}

void enrollTeacherManual(int teacherId, String teacherName) {
  // Manual teacher enrollment
}

// =================== SCREEN DRAWING FUNCTIONS ===================

void drawBootScreen() {
  display.setCursor(15, 20);
  display.setTextSize(2);
  display.println("ATTENDIFY");
  display.setTextSize(1);
  display.setCursor(30, 45);
  display.println("v6.0 - SD Enhanced");
}

void drawHomeScreen() {
  display.setCursor(40, 0);
  display.println("HOME");
  
  display.setCursor(15, 25);
  display.println("1. ADMIN LOGIN");
  display.setCursor(15, 40);
  display.println("2. TEACHER LOGIN");
  
  if (menuIndex == 0) {
    display.fillRect(10, 22, 108, 14, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(15, 25);
    display.println("1. ADMIN LOGIN");
    display.setTextColor(SH110X_WHITE);
  } else {
    display.fillRect(10, 37, 108, 14, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setCursor(15, 40);
    display.println("2. TEACHER LOGIN");
    display.setTextColor(SH110X_WHITE);
  }
}

void drawAdminAuthScreen() {
  display.setCursor(40, 0);
  display.println("ADMIN AUTH");
  
  display.setCursor(35, 30);
  display.println("Place finger");
  display.setCursor(40, 45);
  display.println("on sensor");
}

void drawTeacherAuthScreen() {
  display.setCursor(35, 0);
  display.println("TEACHER AUTH");
  
  display.setCursor(35, 30);
  display.println("Place finger");
  display.setCursor(40, 45);
  display.println("on sensor");
}

void drawTeacherMenu() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(45, 0);
  display.println("TEACHER");
  
  String menuItems[5] = {
    "1. SELECT SUBJECT",
    "2. START ATTENDANCE",
    "3. SYNC TO SERVER",
    "4. MY CLASSES",
    "5. LOGOUT"
  };
  
  int startIdx = 0;
  if (menuIndex > 2) {
    startIdx = menuIndex - 2;
  }
  
  for (int i = 0; i < 3; i++) {
    int itemIdx = startIdx + i;
    if (itemIdx >= 5) break;
    
    int yPos = 15 + (i * 15);
    
    display.fillRect(0, yPos - 1, 128, 15, SH110X_BLACK);
    
    if (itemIdx == menuIndex) {
      display.fillRect(0, yPos - 1, 128, 15, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    display.print(menuItems[itemIdx]);
    
    if (itemIdx == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawSubjectSelectionScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(40, 0);
  display.println("SUBJECTS");
  
  std::vector<String> teacherSubjects = getTeacherSubjects(currentUserId);
  
  if (teacherSubjects.empty()) {
    display.setCursor(20, 30);
    display.println("No subjects");
    display.setCursor(10, 45);
    display.println("Assigned to you");
    return;
  }
  
  int startIdx = 0;
  if (menuIndex > 2) {
    startIdx = menuIndex - 2;
  }
  
  for (int i = 0; i < 3; i++) {
    int itemIdx = startIdx + i;
    if (itemIdx >= teacherSubjects.size()) break;
    
    int yPos = 15 + (i * 15);
    
    display.fillRect(0, yPos - 1, 128, 15, SH110X_BLACK);
    
    if (itemIdx == menuIndex) {
      display.fillRect(0, yPos - 1, 128, 15, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    
    String subject = teacherSubjects[itemIdx];
    if (subject.length() > 18) {
      subject = subject.substring(0, 16) + "..";
    }
    display.print(subject);
    
    if (itemIdx == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawAttendanceScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(30, 0);
  display.println("ATTENDANCE");
  
  display.setCursor(5, 15);
  display.print("Subject: ");
  display.print(currentSubject);
  
  display.setCursor(5, 30);
  display.print("Teacher: ");
  if (currentUserName.length() > 12) {
    display.print(currentUserName.substring(0, 10) + "..");
  } else {
    display.print(currentUserName);
  }
  
  display.setCursor(5, 45);
  display.print("Count: ");
  display.print(attendanceCount);
  
  display.setCursor(70, 45);
  display.print("Time: ");
  unsigned long sessionTime = (millis() - sessionStartTime) / 60000; // minutes
  display.print(sessionTime);
  display.print("m");
}

void drawEnrollmentScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(40, 0);
  display.println("ENROLLMENT");
  
  switch(enrollState) {
    case ENROLL_IDLE:
      display.setCursor(15, 25);
      display.println("Waiting for");
      display.setCursor(30, 40);
      display.println("new students");
      break;
      
    case ENROLL_PENDING:
      display.setCursor(5, 20);
      display.print("Student: ");
      if (pendingStudentName.length() > 12) {
        display.println(pendingStudentName.substring(0, 10) + "..");
      } else {
        display.println(pendingStudentName);
      }
      
      display.setCursor(5, 35);
      display.print("Roll No: ");
      display.println(pendingStudentRoll);
      
      display.setCursor(30, 50);
      display.println("Press SEL to start");
      break;
      
    case ENROLL_CAPTURING:
      display.setCursor(20, 25);
      display.println("Enrolling:");
      display.setCursor(25, 40);
      display.println(pendingStudentName);
      break;
      
    case ENROLL_SUCCESS:
      display.setCursor(30, 30);
      display.println("SUCCESS!");
      display.setCursor(15, 45);
      display.println("Student enrolled");
      break;
      
    case ENROLL_ERROR:
      display.setCursor(25, 30);
      display.println("ERROR!");
      display.setCursor(5, 45);
      display.println("Press SEL to retry");
      break;
  }
}

void drawEnrollTeacherScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(35, 0);
  display.println("ENROLL TEACHER");
  
  display.setCursor(15, 25);
  display.println("Place teacher's");
  display.setCursor(30, 40);
  display.println("finger");
}

void drawWifiScanScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(40, 0);
  display.println("WIFI SCAN");
  
  if (wifiScanning) {
    display.setCursor(40, 30);
    display.println("Scanning...");
    return;
  }
  
  if (wifiNetworkCount == 0) {
    display.setCursor(20, 30);
    display.println("No networks");
    display.setCursor(15, 45);
    display.println("found");
    return;
  }
  
  int startIdx = 0;
  if (wifiSelectedIndex > 2) {
    startIdx = wifiSelectedIndex - 2;
  }
  
  for (int i = 0; i < 3; i++) {
    int itemIdx = startIdx + i;
    if (itemIdx >= wifiNetworkCount) break;
    
    int yPos = 15 + (i * 15);
    
    display.fillRect(0, yPos - 1, 128, 15, SH110X_BLACK);
    
    if (itemIdx == wifiSelectedIndex) {
      display.fillRect(0, yPos - 1, 128, 15, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(5, yPos);
    
    String network = wifiNetworks[itemIdx];
    if (network.length() > 18) {
      network = network.substring(0, 16) + "..";
    }
    display.print(network);
    
    if (itemIdx == wifiSelectedIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawNetworkStatusScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(35, 0);
  display.println("NETWORK STATUS");
  
  display.setCursor(5, 20);
  display.print("Status: ");
  
  wl_status_t status = WiFi.status();
  switch(status) {
    case WL_CONNECTED:
      display.print("Connected");
      break;
    case WL_NO_SHIELD:
      display.print("No shield");
      break;
    case WL_IDLE_STATUS:
      display.print("Idle");
      break;
    case WL_NO_SSID_AVAIL:
      display.print("No SSID");
      break;
    case WL_SCAN_COMPLETED:
      display.print("Scan done");
      break;
    case WL_CONNECT_FAILED:
      display.print("Failed");
      break;
    case WL_CONNECTION_LOST:
      display.print("Lost");
      break;
    case WL_DISCONNECTED:
      display.print("Disconnected");
      break;
    default:
      display.print("Unknown");
      break;
  }
  
  if (status == WL_CONNECTED) {
    display.setCursor(5, 35);
    display.print("SSID: ");
    String ssid = WiFi.SSID();
    if (ssid.length() > 12) {
      display.println(ssid.substring(0, 10) + "..");
    } else {
      display.println(ssid);
    }
    
    display.setCursor(5, 50);
    display.print("IP: ");
    display.println(WiFi.localIP());
  }
}

void drawWifiConnectScreen() {
  // WiFi connection screen
}

void drawWifiStatusScreen() {
  // WiFi status screen
}

void drawPasswordEntryScreen() {
  // Password entry screen
}

void drawSetupWizardScreen() {
  // Setup wizard screen
}

void drawSetupChoiceScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(35, 0);
  display.println("SETUP CHOICE");
  
  String choices[3] = {
    "1. AUTO SETUP",
    "2. MANUAL SETUP",
    "3. SKIP SETUP"
  };
  
  for (int i = 0; i < 3; i++) {
    int yPos = 20 + (i * 15);
    
    display.fillRect(0, yPos - 1, 128, 15, SH110X_BLACK);
    
    if (i == menuIndex) {
      display.fillRect(0, yPos - 1, 128, 15, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(15, yPos);
    display.print(choices[i]);
    
    if (i == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
}

void drawSetupNetworkScreen() {
  // Setup network screen
}

void drawSetupEnrollGFMScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(30, 0);
  display.println("ENROLL GFM");
  
  display.setCursor(15, 25);
  display.println("Place GFM finger");
  display.setCursor(30, 40);
  display.println("on sensor");
}

void drawSetupEnrollTeachersScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(20, 0);
  display.println("ENROLL TEACHERS");
  
  display.setCursor(15, 25);
  display.println("Place teacher");
  display.setCursor(30, 40);
  display.println("finger");
}

void drawSetupSyncScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(40, 0);
  display.println("SYNCING");
  
  display.setCursor(25, 30);
  display.println("Downloading");
  display.setCursor(35, 45);
  display.println("config...");
}

void drawSetupCompleteScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(30, 0);
  display.println("SETUP COMPLETE");
  
  display.setCursor(25, 30);
  display.println("Setup finished!");
  display.setCursor(20, 45);
  display.println("System ready");
}

void drawSystemConfigScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(30, 0);
  display.println("SYSTEM CONFIG");
  
  display.setCursor(5, 20);
  display.print("Device: ");
  display.println(DEVICE_ID);
  
  display.setCursor(5, 35);
  display.print("School: ");
  if (SCHOOL_NAME.length() > 12) {
    display.println(SCHOOL_NAME.substring(0, 10) + "..");
  } else {
    display.println(SCHOOL_NAME);
  }
  
  display.setCursor(5, 50);
  display.print("Year: ");
  display.println(ACADEMIC_YEAR);
}

void drawReportsScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(45, 0);
  display.println("REPORTS");
  
  display.setCursor(20, 30);
  display.println("No reports");
  display.setCursor(15, 45);
  display.println("available");
}

void drawMyClassesScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(40, 0);
  display.println("MY CLASSES");
  
  display.setCursor(20, 30);
  display.println("No class data");
  display.setCursor(15, 45);
  display.println("available");
}

void drawAboutScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(50, 0);
  display.println("ABOUT");
  
  display.setCursor(15, 20);
  display.println("Attendify v6.0");
  
  display.setCursor(10, 35);
  display.println("SD Card Enhanced");
  
  display.setCursor(25, 50);
  display.println("Smart Attendance");
}

void drawFactoryResetScreen() {
  display.fillRect(0, 11, 128, 45, SH110X_BLACK);
  
  display.setCursor(25, 0);
  display.println("FACTORY RESET");
  
  display.setCursor(10, 25);
  display.println("Resetting system...");
  
  display.setCursor(5, 40);
  display.println("Do not power off!");
  
  // Draw progress bar
  int progress = ((millis() - factoryResetStartTime) * 100) / 3000;
  if (progress > 100) progress = 100;
  
  display.drawRect(10, 55, 108, 8, SH110X_WHITE);
  display.fillRect(12, 57, (progress * 104) / 100, 4, SH110X_WHITE);
}

// =================== COMPLETE SHOWSCREEN FUNCTION ===================
void showScreen() {
  if (!displayInitialized) return;
  
  display.clearDisplay();
  
  if (currentScreen != SCREEN_BOOT && currentScreen != SCREEN_HOME && 
      currentScreen != SCREEN_FACTORY_RESET) {
    display.drawLine(0, 10, 127, 10, SH110X_WHITE);
  }
  
  switch(currentScreen) {
    case SCREEN_BOOT:
      drawBootScreen();
      break;
    case SCREEN_HOME:
      drawHomeScreen();
      break;
    case SCREEN_ADMIN_AUTH:
      drawAdminAuthScreen();
      break;
    case SCREEN_TEACHER_AUTH:
      drawTeacherAuthScreen();
      break;
    case SCREEN_ADMIN_MENU:
      drawAdminMenu();
      break;
    case SCREEN_TEACHER_MENU:
      drawTeacherMenu();
      break;
    case SCREEN_SUBJECT_SELECT:
      drawSubjectSelectionScreen();
      break;
    case SCREEN_ATTENDANCE_MODE:
      drawAttendanceScreen();
      break;
    case SCREEN_ENROLL_MODE:
      drawEnrollmentScreen();
      break;
    case SCREEN_ENROLL_TEACHER:
      drawEnrollTeacherScreen();
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
    case SCREEN_SETUP_WIZARD:
      drawSetupWizardScreen();
      break;
    case SCREEN_SETUP_CHOICE:
      drawSetupChoiceScreen();
      break;
    case SCREEN_SETUP_NETWORK:
      drawSetupNetworkScreen();
      break;
    case SCREEN_SETUP_ENROLL_GFM:
      drawSetupEnrollGFMScreen();
      break;
    case SCREEN_SETUP_ENROLL_TEACHERS:
      drawSetupEnrollTeachersScreen();
      break;
    case SCREEN_SETUP_SYNC:
      drawSetupSyncScreen();
      break;
    case SCREEN_SETUP_COMPLETE:
      drawSetupCompleteScreen();
      break;
    case SCREEN_SYSTEM_CONFIG:
      drawSystemConfigScreen();
      break;
    case SCREEN_REPORTS:
      drawReportsScreen();
      break;
    case SCREEN_MY_CLASSES:
      drawMyClassesScreen();
      break;
    case SCREEN_SD_CARD:
      drawSDCardScreen();
      break;
    case SCREEN_SD_LOGS:
      drawSDLogsScreen();
      break;
    case SCREEN_SD_BACKUP:
      drawSDBackupScreen();
      break;
    case SCREEN_ABOUT:
      drawAboutScreen();
      break;
    case SCREEN_FACTORY_RESET:
      drawFactoryResetScreen();
      break;
  }
  
  drawFooter();
  
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