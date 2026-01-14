#include <WiFi.h>
#include <WebServer.h>

// Network configuration
const char* ssid = "ECE 5900";
const char* password = "";  // No password (open network)

// Static IP configuration
IPAddress local_IP(10, 100, 100, 100);
IPAddress gateway(10, 100, 100, 100);
IPAddress subnet(255, 255, 255, 0);

// Web server on port 80
WebServer server(80);

// Function to get current timestamp (using millis for uptime)
String getTimestamp() {
  unsigned long currentTime = millis();
  unsigned long seconds = currentTime / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  seconds = seconds % 60;
  minutes = minutes % 60;
  hours = hours % 24;
  
  char timestamp[20];
  sprintf(timestamp, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return String(timestamp);
}

// Handle alert requests
void handleAlert() {
  // Check if patient parameter exists
  if (server.hasArg("patient")) {
    String patientName = server.arg("patient");
    String timestamp = getTimestamp();
    
    // Print alert to serial monitor
    Serial.println("[" + timestamp + "] ALERT: Patient " + patientName);
    
    // Send HTTP response
    server.send(200, "text/plain", "Alert received for patient: " + patientName);
  } else {
    // Missing patient parameter
    Serial.println("[" + getTimestamp() + "] ERROR: Alert request missing patient parameter");
    server.send(400, "text/plain", "Error: Missing patient parameter");
  }
}

// Handle root path (optional - for testing)
void handleRoot() {
  String html = "<html><body>";
  html += "<h1>Medical Alert System</h1>";
  html += "<p>ESP32 Access Point Hub</p>";
  html += "<p>IP: 10.100.100.100</p>";
  html += "<p>API Endpoint: /alert?patient=patient_name</p>";
  html += "<p>Example: <a href='/alert?patient=test_patient'>/alert?patient=test_patient</a></p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

// Handle 404 errors
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  
  Serial.println("[" + getTimestamp() + "] 404 Error: " + server.uri());
  server.send(404, "text/plain", message);
}

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== Medical Alert System Starting ===");
  
  // Configure WiFi Access Point
  Serial.print("Setting up Access Point...");
  
  // Configure static IP
  if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    Serial.println("Failed to configure static IP");
  }
  
  // Start Access Point
  if (WiFi.softAP(ssid, password)) {
    Serial.println(" Success!");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("Password: None (Open Network)");
  } else {
    Serial.println(" Failed!");
    return;
  }
  
  // Configure web server routes
  server.on("/", handleRoot);
  server.on("/alert", handleAlert);
  server.onNotFound(handleNotFound);
  
  // Start web server
  server.begin();
  Serial.println("HTTP server started on port 80");
  Serial.println("API Endpoint: http://10.100.100.100/alert?patient=patient_name");
  Serial.println("=== System Ready ===");
  Serial.println();
}

void loop() {
  // Handle incoming client requests
  server.handleClient();
  
  // Small delay to prevent watchdog timer issues
  delay(2);
}
