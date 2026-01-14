
/*
 * ESP32 Biometric Monitoring System with Deep-Sleep Power Management
 * Real-time heart rate (BPM) and blood oxygen saturation (SpO₂) monitoring
 * Ultra-low-power operation for multi-month battery life
 * 
 * Hardware:
 * - ESP32 Development Board
 * - MAX30102 Pulse Oximeter Sensor (I²C Address: 0x57)
 * - 0.96" OLED Display SSD1306 (I²C Address: 0x3C)
 * - Wake-up button on GPIO 34 (requires external 10kΩ pull-up)
 * 
 * Connections:
 * - GPIO21 (SDA) - Connected to both MAX30102 and OLED SDA
 * - GPIO22 (SCL) - Connected to both MAX30102 and OLED SCL
 * - GPIO34 - Wake-up button (with 10kΩ pull-up to 3.3V, button to GND)
 * - 3.3V - Power for both devices
 * - GND - Common ground
 * 
 * Power Management:
 * - Deep Sleep Current: 10-150 µA
 * - Active Current: 105-120 mA
 * - Average Current: ~0.5-1 mA (5% duty cycle)
 * - Expected Battery Life: 3-9 months (2000-5000 mAh battery)
 * 
 * Libraries Required:
 * - Wire.h (built-in)
 * - MAX30105.h (SparkFun MAX3010x Sensor Library)
 * - heartRate.h (included with MAX3010x library)
 * - Adafruit_SSD1306.h (Adafruit SSD1306 Library)
 * - Adafruit_GFX.h (Adafruit GFX Library - dependency)
 * - esp_sleep.h (ESP32 deep sleep - built-in)
 * - esp_wifi.h (WiFi control - built-in)
 * - esp_bt.h (Bluetooth control - built-in)
 */

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <esp_bt.h>

// ==================== DEEP SLEEP CONFIGURATION ====================

// Sleep and Wake-up Settings
#define SLEEP_DURATION_SECONDS 900    // 15 minutes between measurements
#define MEASUREMENT_DURATION_MS 45000 // 45 seconds active measurement window
#define FINGER_WAIT_TIMEOUT_MS 10000  // 10 seconds to wait for finger
#define WAKEUP_BUTTON_PIN GPIO_NUM_34 // GPIO 34 for external wake-up

// RTC Memory Magic Number for validation
#define RTC_MAGIC_NUMBER 0xDEADBEEF

// ==================== SENSOR CONFIGURATION ====================

// MAX30102 Sensor Settings
#define SAMPLE_RATE 100        // Samples per second
#define PULSE_WIDTH 411        // LED pulse width in microseconds
#define LED_CURRENT 0x1F       // LED brightness (0x00-0xFF, 0x1F = 6.4mA)
#define SAMPLE_AVG 4           // Number of samples to average
#define ADC_RANGE 4096         // 12-bit ADC resolution

// Display Settings
#define SCREEN_WIDTH 128       // OLED width in pixels
#define SCREEN_HEIGHT 64       // OLED height in pixels
#define OLED_RESET -1          // Reset pin (not used, set to -1)
#define SCREEN_ADDRESS 0x3C    // I²C address for OLED
#define DISPLAY_UPDATE_MS 1000 // Display refresh interval (1 second)

// Signal Processing Parameters
#define FINGER_THRESHOLD 50000  // Minimum IR value for finger detection
#define BPM_MIN 40             // Minimum valid heart rate
#define BPM_MAX 200            // Maximum valid heart rate
#define SPO2_MIN 70            // Minimum valid SpO₂
#define SPO2_MAX 100           // Maximum valid SpO₂
#define BUFFER_SIZE 100        // Sample buffer size for averaging
#define RATE_SIZE 4            // Number of heart rate samples to average

// I²C Configuration
#define I2C_SDA 21             // ESP32 I²C SDA pin
#define I2C_SCL 22             // ESP32 I²C SCL pin
#define I2C_FREQ 400000        // I²C clock frequency (400kHz Fast Mode)

// ==================== RTC MEMORY STRUCTURE ====================

// RTC Memory Structure (persists across deep sleep)
typedef struct {
  uint32_t magicNumber;        // Validation magic number
  uint32_t bootCount;          // Total number of wake-ups
  uint32_t measurementCount;   // Successful measurements taken
  int32_t lastHeartRate;       // Last valid BPM reading
  int32_t lastSpO2;            // Last valid SpO₂ reading
  uint8_t lastWakeReason;      // 0=Timer, 1=Button, 2=Reset
  uint32_t totalSleepTime;     // Cumulative sleep time (seconds)
  uint32_t crc32;              // Data integrity check
} RTCData;

// Declare RTC memory variable
RTC_DATA_ATTR RTCData rtcData;

// ==================== MEASUREMENT STATE MACHINE ====================

enum MeasurementState {
  STATE_INIT,              // Initialize sensors and display
  STATE_WAIT_FINGER,       // Wait for finger placement
  STATE_MEASURING,         // Active measurement in progress
  STATE_STORE_DATA,        // Store results to RTC memory
  STATE_PREPARE_SLEEP,     // Shutdown peripherals
  STATE_DEEP_SLEEP         // Enter deep sleep
};

// ==================== GLOBAL OBJECTS ====================

MAX30105 particleSensor;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== GLOBAL VARIABLES ====================

// Current state
MeasurementState currentState = STATE_INIT;

// Heart Rate Detection
byte rates[RATE_SIZE];         // Array of heart rate values for averaging
byte rateSpot = 0;             // Current position in rates array
long lastBeat = 0;             // Time of last detected beat
float beatsPerMinute = 0;      // Calculated BPM
int beatAvg = 0;               // Average BPM

// SpO₂ Calculation
uint32_t irBuffer[BUFFER_SIZE];    // IR LED sensor data buffer
uint32_t redBuffer[BUFFER_SIZE];   // Red LED sensor data buffer
int32_t bufferLength = BUFFER_SIZE; // Buffer length
int32_t spo2 = 0;              // Calculated SpO₂ value
int8_t validSPO2 = 0;          // Flag for valid SpO₂ reading
int32_t heartRate = 0;         // Calculated heart rate from algorithm
int8_t validHeartRate = 0;     // Flag for valid heart rate reading

// Display Update Timing
unsigned long lastDisplayUpdate = 0;

// System Status
bool sensorInitialized = false;
bool displayInitialized = false;
String statusMessage = "Initializing...";

// Timing Variables
unsigned long measurementStartTime = 0;
unsigned long fingerWaitStartTime = 0;
bool fingerDetected = false;

// Wake-up reason
String wakeupReasonStr = "Unknown";

// ==================== CRC32 CALCULATION ====================

uint32_t calculateCRC32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }
  return ~crc;
}

// ==================== RTC MEMORY FUNCTIONS ====================

void initializeRTCMemory() {
  Serial.println("Initializing RTC Memory...");
  rtcData.magicNumber = RTC_MAGIC_NUMBER;
  rtcData.bootCount = 0;
  rtcData.measurementCount = 0;
  rtcData.lastHeartRate = 0;
  rtcData.lastSpO2 = 0;
  rtcData.lastWakeReason = 2; // Reset
  rtcData.totalSleepTime = 0;
  
  // Calculate CRC32 (exclude the crc32 field itself)
  rtcData.crc32 = calculateCRC32((uint8_t*)&rtcData, sizeof(RTCData) - sizeof(uint32_t));
  
  Serial.println("RTC Memory initialized");
}

bool validateRTCMemory() {
  // Check magic number
  if (rtcData.magicNumber != RTC_MAGIC_NUMBER) {
    Serial.println("RTC Memory: Invalid magic number");
    return false;
  }
  
  // Verify CRC32
  uint32_t calculatedCRC = calculateCRC32((uint8_t*)&rtcData, sizeof(RTCData) - sizeof(uint32_t));
  if (calculatedCRC != rtcData.crc32) {
    Serial.println("RTC Memory: CRC32 mismatch");
    return false;
  }
  
  Serial.println("RTC Memory: Validation successful");
  return true;
}

void updateRTCMemory(int32_t bpm, int32_t spo2Value, uint8_t wakeReason) {
  rtcData.bootCount++;
  if (bpm > 0 && spo2Value > 0) {
    rtcData.measurementCount++;
    rtcData.lastHeartRate = bpm;
    rtcData.lastSpO2 = spo2Value;
  }
  rtcData.lastWakeReason = wakeReason;
  rtcData.totalSleepTime += SLEEP_DURATION_SECONDS;
  
  // Recalculate CRC32
  rtcData.crc32 = calculateCRC32((uint8_t*)&rtcData, sizeof(RTCData) - sizeof(uint32_t));
  
  Serial.println("RTC Memory updated");
  Serial.print("Boot Count: "); Serial.println(rtcData.bootCount);
  Serial.print("Measurement Count: "); Serial.println(rtcData.measurementCount);
  Serial.print("Last BPM: "); Serial.println(rtcData.lastHeartRate);
  Serial.print("Last SpO2: "); Serial.println(rtcData.lastSpO2);
}

// ==================== WAKE-UP REASON DETECTION ====================

void detectWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      wakeupReasonStr = "Button Press";
      Serial.println("Wake-up: External button (GPIO 34)");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      wakeupReasonStr = "Scheduled";
      Serial.println("Wake-up: Timer (15 minutes)");
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
      wakeupReasonStr = "Power On/Reset";
      Serial.println("Wake-up: Power on or reset");
      break;
  }
}

// ==================== POWER MANAGEMENT FUNCTIONS ====================

void disableWiFiAndBluetooth() {
  Serial.println("Disabling WiFi and Bluetooth...");
  
  // Disable WiFi
  esp_wifi_stop();
  esp_wifi_deinit();
  
  // Disable Bluetooth
  esp_bt_controller_disable();
  esp_bt_controller_deinit();
  
  Serial.println("WiFi and Bluetooth disabled");
}

void shutdownPeripherals() {
  Serial.println("Shutting down peripherals...");
  
  // Turn off OLED display
  if (displayInitialized) {
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    Serial.println("OLED display powered down");
  }
  
  // Power down MAX30102
  if (sensorInitialized) {
    particleSensor.shutDown();
    Serial.println("MAX30102 sensor powered down");
  }
  
  Serial.println("Peripherals shutdown complete");
}

void configureWakeupSources() {
  Serial.println("Configuring wake-up sources...");
  
  // Configure timer wake-up (15 minutes)
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_SECONDS * 1000000ULL);
  Serial.print("Timer wake-up configured: ");
  Serial.print(SLEEP_DURATION_SECONDS);
  Serial.println(" seconds");
  
  // Configure external wake-up on GPIO 34 (wake on LOW - button press)
  esp_sleep_enable_ext0_wakeup(WAKEUP_BUTTON_PIN, 0);
  Serial.println("External wake-up configured: GPIO 34 (LOW)");
}

void enterDeepSleep() {
  Serial.println("========================================");
  Serial.println("Entering Deep Sleep Mode...");
  Serial.print("Next wake-up in ");
  Serial.print(SLEEP_DURATION_SECONDS / 60);
  Serial.println(" minutes (or button press)");
  Serial.println("========================================");
  Serial.flush(); // Ensure all serial data is sent
  
  delay(100); // Small delay to ensure serial output completes
  
  // Enter deep sleep
  esp_deep_sleep_start();
  
  // Code never reaches here - ESP32 resets on wake-up
}

// ==================== SETUP FUNCTION ====================

void setup() {
  // Initialize Serial Communication
  Serial.begin(115200);
  delay(100); // Allow serial to stabilize
  
  Serial.println("\n\n========================================");
  Serial.println("ESP32 Biometric Monitor with Deep Sleep");
  Serial.println("========================================");
  
  // Disable WiFi and Bluetooth immediately to save power
  disableWiFiAndBluetooth();
  
  // Detect wake-up reason
  detectWakeupReason();
  
  // Validate or initialize RTC memory
  if (!validateRTCMemory()) {
    Serial.println("RTC Memory invalid - initializing...");
    initializeRTCMemory();
  } else {
    Serial.println("RTC Memory restored successfully");
    Serial.print("Boot Count: "); Serial.println(rtcData.bootCount);
    Serial.print("Total Measurements: "); Serial.println(rtcData.measurementCount);
    Serial.print("Last BPM: "); Serial.println(rtcData.lastHeartRate);
    Serial.print("Last SpO2: "); Serial.println(rtcData.lastSpO2);
    Serial.print("Total Sleep Time: "); Serial.print(rtcData.totalSleepTime / 3600.0); Serial.println(" hours");
  }
  
  // Initialize I²C Bus
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_FREQ);
  Serial.println("I²C Bus Initialized");
  
  // Scan I²C Bus for Devices
  scanI2CBus();
  
  // Initialize OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("ERROR: SSD1306 allocation failed"));
    displayInitialized = false;
  } else {
    displayInitialized = true;
    Serial.println("OLED Display Initialized");
    
    // Display startup message with wake-up reason
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("Biometric Monitor"));
    display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
    display.setCursor(0, 15);
    display.print(F("Wake: "));
    display.println(wakeupReasonStr);
    display.setCursor(0, 25);
    display.print(F("Boot: "));
    display.println(rtcData.bootCount);
    display.setCursor(0, 35);
    display.print(F("Measurements: "));
    display.println(rtcData.measurementCount);
    display.setCursor(0, 50);
    display.println(F("Initializing..."));
    display.display();
    delay(2000);
  }
  
  // Initialize MAX30102 Sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("ERROR: MAX30102 not found");
    sensorInitialized = false;
    statusMessage = "Sensor Error!";
    
    if (displayInitialized) {
      display.clearDisplay();
      display.setCursor(0, 20);
      display.setTextSize(2);
      display.println(F("SENSOR"));
      display.println(F("ERROR!"));
      display.display();
    }
    
    // Store error and go to sleep
    updateRTCMemory(0, 0, rtcData.lastWakeReason);
    delay(3000);
    currentState = STATE_PREPARE_SLEEP;
    return;
  } else {
    sensorInitialized = true;
    Serial.println("MAX30102 Sensor Found");
  }
  
  // Configure MAX30102 Sensor
  byte ledBrightness = LED_CURRENT;
  byte sampleAverage = SAMPLE_AVG;
  byte ledMode = 2;
  int sampleRate = SAMPLE_RATE;
  int pulseWidth = PULSE_WIDTH;
  int adcRange = ADC_RANGE;
  
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(LED_CURRENT);
  particleSensor.setPulseAmplitudeIR(LED_CURRENT);
  particleSensor.enableDIETEMPRDY();
  
  Serial.println("MAX30102 Configured Successfully");
  Serial.println("System Ready - Place finger on sensor");
  
  // Display ready message
  if (displayInitialized) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("System Ready"));
    display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
    display.setCursor(0, 15);
    display.println(F("Place finger on"));
    display.println(F("sensor..."));
    display.setCursor(0, 40);
    display.print(F("Timeout: "));
    display.print(FINGER_WAIT_TIMEOUT_MS / 1000);
    display.println(F("s"));
    display.display();
  }
  
  // Initialize state machine
  currentState = STATE_WAIT_FINGER;
  fingerWaitStartTime = millis();
  measurementStartTime = millis();
  statusMessage = "Place Finger";
  
  Serial.println("Entering measurement state machine...");
}

// ==================== MAIN LOOP ====================

void loop() {
  // State machine implementation
  switch (currentState) {
    
    case STATE_WAIT_FINGER:
      handleWaitFingerState();
      break;
      
    case STATE_MEASURING:
      handleMeasuringState();
      break;
      
    case STATE_STORE_DATA:
      handleStoreDataState();
      break;
      
    case STATE_PREPARE_SLEEP:
      handlePrepareSleepState();
      break;
      
    case STATE_DEEP_SLEEP:
      handleDeepSleepState();
      break;
      
    default:
      currentState = STATE_WAIT_FINGER;
      break;
  }
}

// ==================== STATE HANDLERS ====================

void handleWaitFingerState() {
  // Read sensor data
  long irValue = particleSensor.getIR();
  
  // Check for finger detection timeout
  if (millis() - fingerWaitStartTime > FINGER_WAIT_TIMEOUT_MS) {
    Serial.println("Finger detection timeout - no finger detected");
    statusMessage = "No Finger";
    currentState = STATE_STORE_DATA;
    return;
  }
  
  // Check if finger is detected
  if (irValue < FINGER_THRESHOLD) {
    // No finger detected
    statusMessage = "Place Finger";
    
    // Update display periodically
    if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_MS) {
      updateDisplayWaitingForFinger();
      lastDisplayUpdate = millis();
    }
    
    delay(100);
    return;
  }
  
  // Finger detected - transition to measuring state
  Serial.println("Finger detected - starting measurement");
  fingerDetected = true;
  currentState = STATE_MEASURING;
  measurementStartTime = millis();
  statusMessage = "Measuring...";
}

void handleMeasuringState() {
  // Check for measurement timeout
  if (millis() - measurementStartTime > MEASUREMENT_DURATION_MS) {
    Serial.println("Measurement window complete");
    currentState = STATE_STORE_DATA;
    return;
  }
  
  // Read sensor data
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();
  
  // Check if finger is still present
  if (irValue < FINGER_THRESHOLD) {
    Serial.println("Finger removed during measurement");
    statusMessage = "Finger Removed";
    delay(1000);
    currentState = STATE_STORE_DATA;
    return;
  }
  
  // Process signal - Heart Rate Detection
  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    
    beatsPerMinute = 60 / (delta / 1000.0);
    
    if (beatsPerMinute > BPM_MIN && beatsPerMinute < BPM_MAX) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++) {
        beatAvg += rates[x];
      }
      beatAvg /= RATE_SIZE;
      
      statusMessage = "OK";
      
      Serial.print("BPM=");
      Serial.print(beatsPerMinute);
      Serial.print(", Avg BPM=");
      Serial.print(beatAvg);
      Serial.print(", IR=");
      Serial.println(irValue);
    }
  }
  
  // SpO₂ Calculation
  if (beatAvg > 0) {
    static long irMax = 0, irMin = 1000000;
    static long redMax = 0, redMin = 1000000;
    static int sampleCount = 0;
    
    if (irValue > irMax) irMax = irValue;
    if (irValue < irMin) irMin = irValue;
    if (redValue > redMax) redMax = redValue;
    if (redValue < redMin) redMin = redValue;
    
    sampleCount++;
    
    if (sampleCount >= 100) {
      long irAC = irMax - irMin;
      long redAC = redMax - redMin;
      long irDC = (irMax + irMin) / 2;
      long redDC = (redMax + redMin) / 2;
      
      if (irDC > 0 && redDC > 0 && irAC > 0) {
        float R = ((float)redAC / (float)redDC) / ((float)irAC / (float)irDC);
        float spo2Float = -45.060 * R * R + 30.354 * R + 94.845;
        spo2 = (int32_t)spo2Float;
        
        if (spo2 < SPO2_MIN) spo2 = SPO2_MIN;
        if (spo2 > SPO2_MAX) spo2 = SPO2_MAX;
        
        Serial.print("SpO2=");
        Serial.print(spo2);
        Serial.print("%, R=");
        Serial.println(R);
      }
      
      irMax = 0; irMin = 1000000;
      redMax = 0; redMin = 1000000;
      sampleCount = 0;
    }
  }
  
  // Update display
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_MS) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  delay(20);
}

void handleStoreDataState() {
  Serial.println("Storing measurement data to RTC memory...");
  
  // Determine wake reason code
  uint8_t wakeReasonCode = 2; // Default: Reset
  if (wakeupReasonStr == "Scheduled") {
    wakeReasonCode = 0;
  } else if (wakeupReasonStr == "Button Press") {
    wakeReasonCode = 1;
  }
  
  // Update RTC memory with measurement results
  updateRTCMemory(beatAvg, spo2, wakeReasonCode);
  
  // Display final results
  if (displayInitialized) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(F("Measurement Complete"));
    display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
    
    display.setCursor(0, 15);
    display.print(F("BPM: "));
    if (beatAvg > 0) {
      display.println(beatAvg);
    } else {
      display.println(F("---"));
    }
    
    display.setCursor(0, 25);
    display.print(F("SpO2: "));
    if (spo2 > 0) {
      display.print(spo2);
      display.println(F("%"));
    } else {
      display.println(F("---"));
    }
    
    display.setCursor(0, 40);
    display.println(F("Entering sleep..."));
    display.display();
  }
  
  delay(3000); // Show results for 3 seconds
  
  // Transition to sleep preparation
  currentState = STATE_PREPARE_SLEEP;
}

void handlePrepareSleepState() {
  Serial.println("Preparing for deep sleep...");
  
  // Shutdown all peripherals
  shutdownPeripherals();
  
  // Configure wake-up sources
  configureWakeupSources();
  
  // Transition to deep sleep
  currentState = STATE_DEEP_SLEEP;
}

void handleDeepSleepState() {
  // Enter deep sleep (does not return)
  enterDeepSleep();
}

// ==================== HELPER FUNCTIONS ====================

void scanI2CBus() {
  Serial.println("Scanning I²C bus...");
  byte error, address;
  int deviceCount = 0;
  
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.print("I²C device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      
      if (address == 0x3C || address == 0x3D) {
        Serial.print(" (OLED Display)");
      } else if (address == 0x57) {
        Serial.print(" (MAX30102 Sensor)");
      }
      Serial.println();
      deviceCount++;
    }
  }
  
  if (deviceCount == 0) {
    Serial.println("No I²C devices found");
  } else {
    Serial.print("Found ");
    Serial.print(deviceCount);
    Serial.println(" device(s)");
  }
  Serial.println();
}

void updateDisplay() {
  if (!displayInitialized) return;
  
  display.clearDisplay();
  
  // Header
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Biometric Monitor"));
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  // Heart Rate Section
  display.setCursor(0, 15);
  display.setTextSize(1);
  display.print(F("Heart Rate:"));
  
  display.setCursor(0, 26);
  display.setTextSize(2);
  if (beatAvg > 0 && beatAvg >= BPM_MIN && beatAvg <= BPM_MAX) {
    display.print(beatAvg);
    display.setTextSize(1);
    display.print(F(" BPM"));
  } else {
    display.setTextSize(1);
    display.print(F("---"));
  }
  
  // SpO₂ Section
  display.setCursor(0, 42);
  display.setTextSize(1);
  display.print(F("SpO2:"));
  
  display.setCursor(0, 52);
  display.setTextSize(2);
  if (spo2 >= SPO2_MIN && spo2 <= SPO2_MAX && beatAvg > 0) {
    display.print(spo2);
    display.setTextSize(1);
    display.print(F(" %"));
  } else {
    display.setTextSize(1);
    display.print(F("---"));
  }
  
  // Time remaining
  unsigned long timeRemaining = MEASUREMENT_DURATION_MS - (millis() - measurementStartTime);
  display.setTextSize(1);
  display.setCursor(70, 15);
  display.print(timeRemaining / 1000);
  display.print(F("s"));
  
  display.display();
}

void updateDisplayWaitingForFinger() {
  if (!displayInitialized) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Waiting for Finger"));
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
  
  display.setCursor(0, 20);
  display.setTextSize(2);
  display.println(F("PLACE"));
  display.println(F("FINGER"));
  
  display.setTextSize(1);
  display.setCursor(0, 50);
  unsigned long timeRemaining = FINGER_WAIT_TIMEOUT_MS - (millis() - fingerWaitStartTime);
  display.print(F("Timeout: "));
  display.print(timeRemaining / 1000);
  display.print(F("s"));
  
  display.display();
}
