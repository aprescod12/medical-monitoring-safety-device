/*
 * ESP32 Button Interrupt Detection - Comprehensive Solution
 * 
 * This code demonstrates three different methods for detecting button presses:
 * 1. Simple Polling - Checks button state in main loop
 * 2. Interrupt-based Detection - Uses hardware interrupts for immediate response
 * 3. Debounced Detection - Includes software debouncing to prevent false triggers
 * 
 * Circuit Connection:
 * - Button: One side to GPIO 2, other side to Ground
 * - Internal pull-up resistor is enabled in software
 * - When button is pressed, GPIO 2 goes LOW
 * 
 * Author: Generated for ESP32 Button Detection
 * Date: October 2024
 */

// ========== CONFIGURATION ==========
#define BUTTON_PIN 2              // GPIO pin for button (commonly used pin)
#define DEBOUNCE_DELAY 50         // Debounce time in milliseconds
#define SERIAL_BAUD 115200        // Serial monitor baud rate

// Enable/disable different detection methods
#define ENABLE_POLLING true       // Enable simple polling method
#define ENABLE_INTERRUPT true     // Enable interrupt-based detection
#define ENABLE_DEBOUNCING true    // Enable debounced detection

// ========== GLOBAL VARIABLES ==========
// Interrupt variables (must be volatile for ISR)
volatile bool interruptTriggered = false;
volatile unsigned long interruptTime = 0;
volatile unsigned long interruptCount = 0;  // Ultra-fast counter

// Debouncing variables
bool lastButtonState = 1;         // Previous button state (1 = HIGH, 0 = LOW)
bool currentButtonState = 1;      // Current button state
unsigned long lastDebounceTime = 0; // Last time button state changed
bool debouncedButtonState = 1;    // Debounced button state
bool lastDebouncedState = 1;      // Previous debounced state

// Polling variables
bool lastPolledState = 1;         // Previous polled state
unsigned long lastPollTime = 0;   // Last polling time

// Statistics and timing
unsigned long bootTime = 0;
unsigned int pollingDetections = 0;
unsigned int interruptDetections = 0;
unsigned int debouncedDetections = 0;

// ========== INTERRUPT SERVICE ROUTINE ==========
// ULTRA-FAST VERSION: Minimal operations for fastest possible ISR
void IRAM_ATTR buttonISR() {
  interruptTriggered = true;     // Set flag (fastest operation)
  interruptCount++;              // Increment counter (very fast)
  // Note: millis() moved to main loop for speed optimization
}

// Alternative MINIMAL ISR (uncomment to use the absolute fastest version):
// void IRAM_ATTR buttonISR() {
//   interruptTriggered = true;   // Only this line for maximum speed
// }

// ========== SETUP FUNCTION ==========
void setup() {
  // Initialize serial communication
  Serial.begin(SERIAL_BAUD);
  delay(1000); // Give serial monitor time to connect
  
  bootTime = millis();
  
  Serial.println("========================================");
  Serial.println("ESP32 Button Detection - Comprehensive Demo");
  Serial.println("========================================");
  Serial.println();
  
  // Configure button pin with internal pull-up resistor
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.print("Button configured on GPIO ");
  Serial.print(BUTTON_PIN);
  Serial.println(" with internal pull-up resistor");
  
  // Setup interrupt if enabled (trigger on FALLING edge - button press)
  if (ENABLE_INTERRUPT) {
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);
    Serial.println("Hardware interrupt enabled (FALLING edge)");
  }
  
  // Display configuration
  Serial.println("\nEnabled Detection Methods:");
  if (ENABLE_POLLING) Serial.println("✓ Polling Method");
  if (ENABLE_INTERRUPT) Serial.println("✓ Interrupt Method");
  if (ENABLE_DEBOUNCING) Serial.println("✓ Debounced Method");
  
  Serial.print("Debounce delay: ");
  Serial.print(DEBOUNCE_DELAY);
  Serial.println(" ms");
  
  Serial.println("\nPress the button to test detection methods!");
  Serial.println("Format: [Method] Button pressed at [time]ms (detection #)");
  Serial.println("----------------------------------------");
}

// ========== MAIN LOOP ==========
void loop() {
  unsigned long currentTime = millis();
  
  // ========== METHOD 1: SIMPLE POLLING ==========
  if (ENABLE_POLLING) {
    bool currentState = digitalRead(BUTTON_PIN);
    
    // Check if button state changed from HIGH to LOW (button pressed)
    if (lastPolledState == 1 && currentState == 0) {
      pollingDetections++;
      Serial.print("[POLLING] Button pressed at ");
      Serial.print(currentTime);
      Serial.print("ms (detection #");
      Serial.print(pollingDetections);
      Serial.println(")");
    }
    
    lastPolledState = currentState;
    lastPollTime = currentTime;
  }
  
  // ========== METHOD 2: INTERRUPT HANDLING ==========
  if (ENABLE_INTERRUPT && interruptTriggered) {
    // Capture timing immediately when we detect the interrupt flag
    interruptTime = currentTime;  // More accurate timing than millis() in ISR
    
    interruptDetections++;
    Serial.print("[INTERRUPT] Button pressed at ");
    Serial.print(interruptTime);
    Serial.print("ms (detection #");
    Serial.print(interruptDetections);
    Serial.print(") - Ultra-fast ISR count: ");
    Serial.print(interruptCount);
    Serial.println();
    
    interruptTriggered = false; // Reset interrupt flag
  }
  
  // ========== METHOD 3: DEBOUNCED DETECTION ==========
  if (ENABLE_DEBOUNCING) {
    // Read current button state
    currentButtonState = digitalRead(BUTTON_PIN);
    
    // Check if button state has changed
    if (currentButtonState != lastButtonState) {
      lastDebounceTime = currentTime; // Reset debounce timer
    }
    
    // If enough time has passed since last state change
    if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
      // If button state has actually changed after debounce period
      if (currentButtonState != debouncedButtonState) {
        debouncedButtonState = currentButtonState;
        
        // Check for button press (HIGH to LOW transition)
        if (lastDebouncedState == 1 && debouncedButtonState == 0) {
          debouncedDetections++;
          Serial.print("[DEBOUNCED] Button pressed at ");
          Serial.print(currentTime);
          Serial.print("ms (detection #");
          Serial.print(debouncedDetections);
          Serial.print(") - Debounce delay: ");
          Serial.print(DEBOUNCE_DELAY);
          Serial.println("ms");
        }
        
        lastDebouncedState = debouncedButtonState;
      }
    }
    
    lastButtonState = currentButtonState;
  }
  
  // ========== PERIODIC STATUS REPORT ==========
  // Print statistics every 30 seconds
  static unsigned long lastStatusTime = 0;
  if (currentTime - lastStatusTime > 30000) {
    printStatusReport(currentTime);
    lastStatusTime = currentTime;
  }
  
  // Small delay to prevent overwhelming the serial monitor
  delay(1);
}

// ========== HELPER FUNCTIONS ==========
void printStatusReport(unsigned long currentTime) {
  Serial.println("\n========== STATUS REPORT ==========");
  Serial.print("Uptime: ");
  Serial.print((currentTime - bootTime) / 1000);
  Serial.println(" seconds");
  
  Serial.println("Detection Statistics:");
  if (ENABLE_POLLING) {
    Serial.print("  Polling detections: ");
    Serial.println(pollingDetections);
  }
  if (ENABLE_INTERRUPT) {
    Serial.print("  Interrupt detections: ");
    Serial.println(interruptDetections);
  }
  if (ENABLE_DEBOUNCING) {
    Serial.print("  Debounced detections: ");
    Serial.println(debouncedDetections);
  }
  
  Serial.print("Current button state: ");
  Serial.println(digitalRead(BUTTON_PIN) == 0 ? "PRESSED" : "RELEASED");
  Serial.println("===================================\n");
}

/*
 * ========== CIRCUIT DIAGRAM ==========
 * 
 *     ESP32                    Button
 *   ┌─────────┐              ┌─────────┐
 *   │         │              │         │
 *   │ GPIO 2  ├──────────────┤    A    │
 *   │         │              │         │
 *   │   GND   ├──────────────┤    B    │
 *   │         │              │         │
 *   └─────────┘              └─────────┘
 * 
 * Internal pull-up resistor is enabled in software
 * When button is pressed: GPIO 2 = LOW (0V)
 * When button is released: GPIO 2 = HIGH (3.3V)
 * 
 * ========== USAGE INSTRUCTIONS ==========
 * 
 * 1. Upload this code to your ESP32 using Arduino IDE
 * 2. Open Serial Monitor at 115200 baud
 * 3. Connect button between GPIO 2 and Ground
 * 4. Press button to see different detection methods in action
 * 5. Observe timing differences between methods
 * 6. Modify configuration constants to experiment with settings
 * 
 * ========== DETECTION METHOD COMPARISON ==========
 * 
 * POLLING:
 * - Pros: Simple, predictable, no interrupt overhead
 * - Cons: May miss very short button presses, polling delay
 * - Best for: Applications where missing brief presses is acceptable
 * 
 * INTERRUPT:
 * - Pros: Immediate response, catches all button presses
 * - Cons: Can be triggered by electrical noise, no debouncing
 * - Best for: Time-critical applications, wake-from-sleep scenarios
 * 
 * DEBOUNCED:
 * - Pros: Eliminates false triggers, reliable detection
 * - Cons: Slight delay due to debounce period
 * - Best for: Production applications requiring reliable button detection
 * 
 * ========== TROUBLESHOOTING ==========
 * 
 * If button presses aren't detected:
 * 1. Check wiring - button should connect GPIO 2 to Ground
 * 2. Verify button is normally open (not normally closed)
 * 3. Try different GPIO pin if GPIO 2 doesn't work
 * 4. Check serial monitor baud rate (should be 115200)
 * 5. Ensure ESP32 is properly powered and programmed
 * 
 * If getting false triggers:
 * 1. Increase DEBOUNCE_DELAY value
 * 2. Check for loose connections
 * 3. Consider adding external pull-up resistor (10kΩ)
 * 4. Shield wires from electromagnetic interference
 * 
 * ========== CUSTOMIZATION OPTIONS ==========
 * 
 * You can modify these settings at the top of the code:
 * - BUTTON_PIN: Change GPIO pin (try 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23)
 * - DEBOUNCE_DELAY: Adjust debounce time (20-100ms typical)
 * - ENABLE_* flags: Turn detection methods on/off for testing
 * - SERIAL_BAUD: Change serial communication speed
 */
