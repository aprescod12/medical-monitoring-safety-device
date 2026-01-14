/*
 * ESP32 Biometric Monitoring System
 * Real-time heart rate (BPM) and blood oxygen saturation (SpO₂) monitoring
 * 
 * Hardware:
 * - ESP32 Development Board
 * - MAX30102 Pulse Oximeter Sensor (I²C Address: 0x57)
 * - 0.96" OLED Display SSD1306 (I²C Address: 0x3C)
 * 
 * Connections:
 * - GPIO21 (SDA) - Connected to both MAX30102 and OLED SDA
 * - GPIO22 (SCL) - Connected to both MAX30102 and OLED SCL
 * - 3.3V - Power for both devices
 * - GND - Common ground
 * 
 * Libraries Required:
 * - Wire.h (built-in)
 * - MAX30105.h (SparkFun MAX3010x Sensor Library)
 * - heartRate.h (included with MAX3010x library)
 * - Adafruit_SSD1306.h (Adafruit SSD1306 Library)
 * - Adafruit_GFX.h (Adafruit GFX Library - dependency)
 */

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ==================== CONFIGURATION PARAMETERS ====================

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

// ==================== GLOBAL OBJECTS ====================

MAX30105 particleSensor;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== GLOBAL VARIABLES ====================

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

// ==================== SETUP FUNCTION ====================

void setup() {
  // Initialize Serial Communication
  Serial.begin(115200);
  Serial.println("ESP32 Biometric Monitor Starting...");
  
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
    
    // Display startup message
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("Biometric Monitor"));
    display.println(F(""));
    display.println(F("Initializing..."));
    display.println(F("MAX30102 Sensor"));
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
    
    while (1); // Halt execution
  } else {
    sensorInitialized = true;
    Serial.println("MAX30102 Sensor Found");
  }
  
  // Configure MAX30102 Sensor
  byte ledBrightness = LED_CURRENT;  // LED current
  byte sampleAverage = SAMPLE_AVG;   // Sample averaging
  byte ledMode = 2;                  // Mode: Red + IR LEDs
  int sampleRate = SAMPLE_RATE;      // Samples per second
  int pulseWidth = PULSE_WIDTH;      // LED pulse width
  int adcRange = ADC_RANGE;          // ADC range
  
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(LED_CURRENT);
  particleSensor.setPulseAmplitudeIR(LED_CURRENT);
  
  // Enable FIFO rollover
  particleSensor.enableDIETEMPRDY();
  
  Serial.println("MAX30102 Configured Successfully");
  Serial.println("System Ready - Place finger on sensor");
  
  // Display ready message
  if (displayInitialized) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.println(F("System Ready"));
    display.println(F(""));
    display.println(F("Place finger on"));
    display.println(F("sensor..."));
    display.display();
    delay(2000);
  }
  
  statusMessage = "Place Finger";
}

// ==================== MAIN LOOP ====================

void loop() {
  // Read sensor data
  long irValue = particleSensor.getIR();
  long redValue = particleSensor.getRed();
  
  // Check if finger is detected
  if (irValue < FINGER_THRESHOLD) {
    // No finger detected
    statusMessage = "Place Finger";
    beatsPerMinute = 0;
    beatAvg = 0;
    spo2 = 0;
    
    // Update display if interval elapsed
    if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_MS) {
      updateDisplay();
      lastDisplayUpdate = millis();
    }
    
    // Serial output
    Serial.println("No finger detected");
    delay(100);
    return;
  }
  
  // Finger detected - process signal
  statusMessage = "Measuring...";
  
  // Heart Rate Detection using beat detection algorithm
  if (checkForBeat(irValue) == true) {
    // Beat detected
    long delta = millis() - lastBeat;
    lastBeat = millis();
    
    // Calculate BPM
    beatsPerMinute = 60 / (delta / 1000.0);
    
    // Validate BPM range
    if (beatsPerMinute > BPM_MIN && beatsPerMinute < BPM_MAX) {
      // Store valid BPM in circular buffer
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      
      // Calculate average BPM
      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++) {
        beatAvg += rates[x];
      }
      beatAvg /= RATE_SIZE;
      
      statusMessage = "OK";
      
      // Output to Serial
      Serial.print("BPM=");
      Serial.print(beatsPerMinute);
      Serial.print(", Avg BPM=");
      Serial.print(beatAvg);
      Serial.print(", IR=");
      Serial.println(irValue);
    }
  }
  
  // SpO₂ Calculation (simplified R-value method)
  // This is a basic implementation - for production use, consider the
  // more sophisticated spo2_algorithm from Maxim Integrated
  if (beatAvg > 0) {
    // Calculate AC and DC components
    static long irMax = 0, irMin = 1000000;
    static long redMax = 0, redMin = 1000000;
    static int sampleCount = 0;
    
    // Track min/max for AC calculation
    if (irValue > irMax) irMax = irValue;
    if (irValue < irMin) irMin = irValue;
    if (redValue > redMax) redMax = redValue;
    if (redValue < redMin) redMin = redValue;
    
    sampleCount++;
    
    // Calculate SpO₂ every 100 samples
    if (sampleCount >= 100) {
      // Calculate AC components (peak-to-peak)
      long irAC = irMax - irMin;
      long redAC = redMax - redMin;
      
      // Calculate DC components (average)
      long irDC = (irMax + irMin) / 2;
      long redDC = (redMax + redMin) / 2;
      
      // Calculate R-value
      if (irDC > 0 && redDC > 0 && irAC > 0) {
        float R = ((float)redAC / (float)redDC) / ((float)irAC / (float)irDC);
        
        // SpO₂ calculation using empirical formula
        // SpO₂ = -45.060 * R² + 30.354 * R + 94.845
        float spo2Float = -45.060 * R * R + 30.354 * R + 94.845;
        spo2 = (int32_t)spo2Float;
        
        // Validate SpO₂ range
        if (spo2 < SPO2_MIN) spo2 = SPO2_MIN;
        if (spo2 > SPO2_MAX) spo2 = SPO2_MAX;
        
        Serial.print("SpO2=");
        Serial.print(spo2);
        Serial.print("%, R=");
        Serial.println(R);
      }
      
      // Reset for next calculation
      irMax = 0; irMin = 1000000;
      redMax = 0; redMin = 1000000;
      sampleCount = 0;
    }
  }
  
  // Update display at specified interval
  if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_MS) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // Small delay for stability
  delay(20);
}

// ==================== HELPER FUNCTIONS ====================

/**
 * Scan I²C bus and print detected devices
 */
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
      
      // Identify known devices
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

/**
 * Update OLED display with current biometric data
 */
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
  
  // Status Bar
  display.drawLine(0, 48, SCREEN_WIDTH, 48, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(70, 15);
  display.print(F("["));
  display.print(statusMessage);
  display.print(F("]"));
  
  display.display();
}
