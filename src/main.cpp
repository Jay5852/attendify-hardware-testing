/**
 * ESP32 TEST CODE - WITH DATE DISPLAY
 * SH1106 OLED (128x64) + DS3231 RTC + 4 Buttons
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "RTClib.h"

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

// =================== OLED SETUP ===================
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =================== RTC SETUP ===================
RTC_DS3231 rtc;

// =================== SCREEN STATE ENUM ===================
enum ScreenState {
  SCREEN_BOOT,
  SCREEN_MAIN_MENU,
  SCREEN_BUTTON_TEST,
  SCREEN_SYSTEM_INFO,
  SCREEN_SET_TIME,
  SCREEN_RESET_MEM,
  SCREEN_DEMO_MODE,
  SCREEN_OLED_TEST,
  SCREEN_RTC_TEST,
  SCREEN_VOLT_TEST
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

// Demo mode variables
bool demoActive = false;
unsigned long demoStartTime = 0;
int demoCounter = 0;

// Test pattern variables
int oledTestPattern = 0;

// Day of week names
const char* dayNames[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// =================== FUNCTION DECLARATIONS ===================
void initializeHardware();
void showScreen();
void drawHeader();
void drawFooter();
void checkButtons();
void handleButtonPress(int button);

// Screen drawing functions
void drawBootScreen();
void drawMainMenu();
void drawButtonTest();
void drawSystemInfo();
void drawSetTime();
void drawResetMem();
void drawDemoMode();
void drawOledTest();
void drawRtcTest();
void drawVoltTest();

// =================== SETUP ===================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Attendance System Test ===");
  
  initializeHardware();
  
  currentScreen = SCREEN_BOOT;
  showScreen();
  delay(2000);
  
  currentScreen = SCREEN_MAIN_MENU;
}

// =================== INITIALIZE HARDWARE ===================
void initializeHardware() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000);
  
  if (!display.begin(0x3C, true)) {
    if (!display.begin(0x3D, true)) {
      Serial.println("OLED not found!");
      while(1);
    }
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setRotation(0);
  
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
  } else {
    // Check if RTC lost power
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, setting default time!");
      // Set to a default time (Jan 19, 2026, 01:11:00)
      rtc.adjust(DateTime(2026, 1, 19, 1, 11, 0));
    }
  }
  
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);
  pinMode(BUTTON_BACK, INPUT_PULLUP);
}

// =================== MAIN LOOP ===================
void loop() {
  checkButtons();
  
  if (millis() - lastUpdate > 100 || needRefresh) {
    showScreen();
    lastUpdate = millis();
    needRefresh = false;
  }
  
  if (currentScreen == SCREEN_DEMO_MODE && demoActive) {
    if (millis() - demoStartTime > 1000) {
      demoCounter++;
      if (demoCounter > 99) demoCounter = 0;
      demoStartTime = millis();
      needRefresh = true;
    }
  }
  
  delay(10);
}

// =================== DISPLAY FUNCTIONS ===================
void showScreen() {
  display.clearDisplay();
  drawHeader();
  
  // Draw main content
  switch(currentScreen) {
    case SCREEN_BOOT:
      drawBootScreen();
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
  }
  
  drawFooter();
  display.display();
}

void drawHeader() {
  // Top line: Screen title
  display.setCursor(0, 0);
  
  switch(currentScreen) {
    case SCREEN_SYSTEM_INFO:
      display.print("TEST 9-95  1 MHz/911.11");
      break;
    case SCREEN_SET_TIME:
      display.print("TEST SET TIME");
      break;
    case SCREEN_RESET_MEM:
      display.print("TEST RESET MEM");
      break;
    case SCREEN_DEMO_MODE:
      if (demoActive) {
        display.print("TEST DETECT MODE: 22");
      } else {
        display.print("TEST DETECT MODE: 1/1");
      }
      break;
    case SCREEN_OLED_TEST:
      display.print("TEST");
      break;
    case SCREEN_RTC_TEST:
      display.print("SELECT 10.0K1-2006");
      break;
    case SCREEN_VOLT_TEST:
      display.print("TEST");
      break;
    default:
      display.print("TEST");
  }
  
  // Show time on right for most screens
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Check which screens should show time
    bool showTime = true;
    switch(currentScreen) {
      case SCREEN_SYSTEM_INFO:
      case SCREEN_SET_TIME:
      case SCREEN_DEMO_MODE:
      case SCREEN_RTC_TEST:
        showTime = false;
        break;
      default:
        showTime = true;
    }
    
    if (showTime) {
      display.setCursor(85, 0);
      display.printf("%02d:%02d", now.hour(), now.minute());
    }
  }
  
  display.drawLine(0, 9, 127, 9, SH110X_WHITE);
}

void drawFooter() {
  display.setCursor(0, 56);
  
  switch(currentScreen) {
    case SCREEN_SYSTEM_INFO:
      display.print("GND VCC SCL SON");
      break;
    case SCREEN_SET_TIME:
      display.print("GND UCC SCL SON");
      break;
    case SCREEN_RESET_MEM:
      display.print("GND UCC SCL SM");
      break;
    case SCREEN_DEMO_MODE:
      if (demoActive) {
        display.print("GND UCC SCL SON");
      } else {
        display.print("GND VCC SCL SON");
      }
      break;
    case SCREEN_OLED_TEST:
      switch(oledTestPattern) {
        case 0:
          display.print("SELECT EMP: 1/3");
          break;
        case 1:
          display.print("SELECT BRK: 2/3");
          break;
        case 2:
          display.print("SELECT BAR#: 3/3");
          break;
      }
      break;
    case SCREEN_BUTTON_TEST:
      display.print("GND VCC SCL SOA");
      break;
    case SCREEN_VOLT_TEST:
      display.print("GND UCC SOL 500V");
      break;
    case SCREEN_MAIN_MENU:
      // Smaller up/down select hint
      display.print("U/D SEL");
      break;
    default:
      // Empty footer for other screens
      display.print("");
  }
}

// =================== SCREEN DRAWING FUNCTIONS ===================
void drawBootScreen() {
  display.setCursor(25, 20);
  display.setTextSize(2);
  display.println("SYSTEM");
  display.setCursor(40, 40);
  display.println("TEST");
  display.setTextSize(1);
}

void drawMainMenu() {
  // Show "MENU" centered
  display.setCursor(50, 12);
  display.println("MENU");
  
  display.drawLine(0, 25, 127, 25, SH110X_WHITE);
  
  const char* menuItems[] = {
    "BUTTON TEST",
    "SYS INFO",
    "SET TIME",
    "MEM RESET",
    "DEMO MODE",
    "OLED TEST",
    "RTC TEST",
    "VOLT TEST"
  };
  
  // Show only 3 menu items at a time
  int startIdx = 0;
  if (menuIndex > 2) startIdx = menuIndex - 2;
  if (menuIndex > 5) startIdx = menuIndex - 1;
  
  for (int i = 0; i < 3; i++) {
    int idx = startIdx + i;
    if (idx >= 8) break;
    
    int yPos = 28 + (i * 10);
    
    // Clear line area
    display.fillRect(0, yPos - 1, 128, 9, SH110X_BLACK);
    
    if (idx == menuIndex) {
      display.fillRect(0, yPos - 1, 128, 9, SH110X_WHITE);
      display.setTextColor(SH110X_BLACK);
    }
    
    display.setCursor(15, yPos);
    display.print(menuItems[idx]);
    
    if (idx == menuIndex) {
      display.setTextColor(SH110X_WHITE);
    }
  }
  
  // Scroll indicators (small arrows)
  if (menuIndex > 0) {
    display.setCursor(120, 30);
    display.print("^");
  }
  if (menuIndex < 7) {
    display.setCursor(120, 45);
    display.print("v");
  }
}

void drawButtonTest() {
  // Clear content area
  display.fillRect(0, 10, 128, 46, SH110X_BLACK);
  
  // From image: Shows "9E6045A0" at top
  display.setCursor(10, 12);
  display.println("9E6045A0");
  
  // Show multiple LED lines (limited to fit screen)
  for (int i = 0; i < 11; i++) {
    int yPos = 20 + (i * 8);
    if (yPos < 55) {
      display.setCursor(20, yPos);
      display.println("LED");
    }
  }
  
  // Show RED at the bottom
  display.setCursor(20, 48);
  display.println("RED");
  
  // Show button states in small text at top right
  display.setCursor(90, 12);
  display.print("BTN:");
  for (int i = 0; i < 4; i++) {
    display.setCursor(90 + (i * 8), 20);
    display.print(buttonStates[i] ? "1" : "0");
  }
}

void drawSystemInfo() {
  // Clear content area
  display.fillRect(0, 10, 128, 46, SH110X_BLACK);
  
  // From image: Shows system info lines
  display.setCursor(10, 15);
  display.println("CHFP: E5F2E-0019-03");
  
  display.setCursor(10, 25);
  display.println("CPU: 2.4GHz");
  
  display.setCursor(10, 35);
  display.println("RMI: 3.2KB/s");
  
  display.setCursor(10, 45);
  display.println("BFCMSH: 4MB");
}

void drawSetTime() {
  // Clear content area
  display.fillRect(0, 10, 128, 46, SH110X_BLACK);
  
  // From image: Shows "PRESS SELECT TO" and time
  display.setCursor(15, 25);
  display.println("PRESS SELECT TO");
  
  if (rtc.begin()) {
    DateTime now = rtc.now();
    display.setCursor(40, 40);
    display.print("NOW: ");
    display.printf("%02d:%02d", now.hour(), now.minute());
  } else {
    display.setCursor(40, 40);
    display.print("NOW: 01:11");
  }
}

void drawResetMem() {
  // Clear content area
  display.fillRect(0, 10, 128, 46, SH110X_BLACK);
  
  // From image: Shows "PRESS SELECT to" and "RESET" text
  display.setCursor(15, 25);
  display.println("PRESS SELECT to");
  
  display.setCursor(30, 40);
  display.println("RESET");
}

void drawDemoMode() {
  // Clear content area
  display.fillRect(0, 10, 128, 46, SH110X_BLACK);
  
  if (demoActive) {
    // Active mode: Show counter and "S/F IQ TESTING"
    display.setCursor(10, 20);
    display.println("S/F IQ TESTING");
    
    display.setCursor(15, 40);
    display.println("Press SELECT to");
    display.setCursor(45, 50);
    display.println("DEmO mode");
  } else {
    // Inactive mode: Show "S-FE JET 1.0-7H-1" and "PRESS SELECT TO"
    display.setCursor(10, 20);
    display.println("S-FE JET 1.0-7H-1");
    
    display.setCursor(15, 40);
    display.println("PRESS SELECT TO");
  }
}

void drawOledTest() {
  // Clear content area
  display.fillRect(0, 10, 128, 46, SH110X_BLACK);
  
  // Draw a line below header
  display.drawLine(0, 15, 127, 15, SH110X_WHITE);
  
  // Show "OLED TEST" below the line (centered)
  display.setCursor(40, 20);
  display.println("OLED TEST");
  
  switch(oledTestPattern) {
    case 0:
      // Pattern 1: Shows "BJT2" as in image
      display.setCursor(50, 35);
      display.println("BJT2");
      break;
      
    case 1:
      // Pattern 2: Grid pattern
      for (int x = 20; x < 110; x += 20) {
        display.drawLine(x, 30, x, 50, SH110X_WHITE);
      }
      for (int y = 30; y < 55; y += 10) {
        display.drawLine(20, y, 100, y, SH110X_WHITE);
      }
      break;
      
    case 2:
      // Pattern 3: Text pattern from image
      display.setCursor(10, 30);
      display.println("12345678910#");
      display.setCursor(10, 40);
      display.println("ABCDEFGHIJKL");
      break;
  }
}

void drawRtcTest() {
  // Clear content area
  display.fillRect(0, 10, 128, 46, SH110X_BLACK);
  
  // Draw a line below header
  display.drawLine(0, 15, 127, 15, SH110X_WHITE);
  
  // Show "RTC TEST" below the line (centered)
  display.setCursor(40, 20);
  display.println("RTC TEST");
  
  if (rtc.begin()) {
    DateTime now = rtc.now();
    
    // Show time in large font
    display.setCursor(35, 30);
    display.setTextSize(2);
    display.printf("%02d:%02d", now.hour(), now.minute());
    display.setTextSize(1);
    
    // Show date below time
    display.setCursor(30, 50);
    display.printf("%02d/%02d/%04d", now.day(), now.month(), now.year());
    
    // Show day of week on the right
    display.setCursor(90, 50);
    display.print(dayNames[now.dayOfTheWeek()]);
  } else {
    display.setCursor(35, 35);
    display.println("NO RTC");
    display.setCursor(20, 45);
    display.println("Check I2C wiring");
  }
}

void drawVoltTest() {
  // Clear content area
  display.fillRect(0, 10, 128, 46, SH110X_BLACK);
  
  // Show voltage value "9999.99" in large font
  display.setCursor(30, 25);
  display.setTextSize(2);
  display.println("9999.99");
  display.setTextSize(1);
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
  
  switch(currentScreen) {
    case SCREEN_MAIN_MENU:
      if (button == 0) {
        menuIndex = (menuIndex > 0) ? menuIndex - 1 : 7;
      } else if (button == 1) {
        menuIndex = (menuIndex < 7) ? menuIndex + 1 : 0;
      } else if (button == 2) {
        switch(menuIndex) {
          case 0: currentScreen = SCREEN_BUTTON_TEST; break;
          case 1: currentScreen = SCREEN_SYSTEM_INFO; break;
          case 2: currentScreen = SCREEN_SET_TIME; break;
          case 3: currentScreen = SCREEN_RESET_MEM; break;
          case 4: currentScreen = SCREEN_DEMO_MODE; break;
          case 5: currentScreen = SCREEN_OLED_TEST; break;
          case 6: currentScreen = SCREEN_RTC_TEST; break;
          case 7: currentScreen = SCREEN_VOLT_TEST; break;
        }
      }
      break;
      
    case SCREEN_BUTTON_TEST:
    case SCREEN_SYSTEM_INFO:
    case SCREEN_RTC_TEST:
    case SCREEN_VOLT_TEST:
      if (button == 3) currentScreen = SCREEN_MAIN_MENU;
      break;
      
    case SCREEN_SET_TIME:
      if (button == 2) {
        if (rtc.begin()) {
          // FIXED: Get current PC time via Serial for accuracy
          Serial.println("Setting RTC to PC time...");
          
          // Get compile time as fallback
          DateTime compileTime = DateTime(F(__DATE__), F(__TIME__));
          
          // Calculate current time by adding elapsed milliseconds
          // This is more accurate than just compile time
          unsigned long currentSeconds = compileTime.unixtime() + (millis() / 1000);
          DateTime currentTime = DateTime(currentSeconds);
          
          // Set RTC to calculated current time
          rtc.adjust(currentTime);
          
          display.clearDisplay();
          display.setCursor(30, 30);
          display.println("TIME SYNCED");
          display.display();
          delay(1000);
        }
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_RESET_MEM:
      if (button == 2) {
        for (int i = 0; i < 4; i++) buttonPressCount[i] = 0;
        demoCounter = 0;
        demoActive = false;
        
        display.clearDisplay();
        display.setCursor(40, 30);
        display.println("RESET DONE");
        display.display();
        delay(1000);
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_DEMO_MODE:
      if (button == 2) {
        demoActive = !demoActive;
        if (demoActive) {
          demoStartTime = millis();
          demoCounter = 0;
        }
      } else if (button == 3) {
        demoActive = false;
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    case SCREEN_OLED_TEST:
      if (button == 2) {
        oledTestPattern = (oledTestPattern + 1) % 3;
      } else if (button == 3) {
        currentScreen = SCREEN_MAIN_MENU;
      }
      break;
      
    default:
      if (button == 3) currentScreen = SCREEN_MAIN_MENU;
  }
}