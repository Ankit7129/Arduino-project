#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <RF24.h>

#define CE_PIN 4
#define CSN_PIN 5

RF24 radio(CE_PIN, CSN_PIN);
const byte address[6] = "00001";

const char* ssid = "NARZO N65 5G";
const char* password = "12345678";

const int moistureSensorPin = A0;
const int relayPin = 0;

int lowerThreshold = 400;
int upperThreshold = 500;

String mode = "Manual";
bool pumpStatus = false;  // false means motor is off
unsigned long timerDuration = 0;
unsigned long startTime = 0;

ESP8266WebServer server(80);
unsigned long lastWiFiCheck = 0;
unsigned long checkInterval = 30000; // 30 seconds to check WiFi connection

int receivedSoilMoisture = 0;

int readSoilMoisture() {
    int rawValue = analogRead(moistureSensorPin);
    rawValue = 1024 - rawValue; // Subtract from 1024 to invert the reading

    return rawValue; // Directly return the raw reading
}

void controlPump(bool state) {
    pumpStatus = state;
    Serial.print("Control Pump: ");
    Serial.println(state ? "ON" : "OFF");
    digitalWrite(relayPin, state ? HIGH : LOW); // LOW turns motor ON, HIGH turns motor OFF
}

String getMotorStatus() {
    return pumpStatus ? "ON" : "OFF";
}

void handleStatusUpdate() {
    String json = "{";
    json += "\"soilMoisture\":" + String(readSoilMoisture()) + ",";
    json += "\"receivedSoilMoisture\":" + String(receivedSoilMoisture) + ",";
    json += "\"motorStatus\":\"" + getMotorStatus() + "\",";
    json += "\"lowerThreshold\":" + String(lowerThreshold) + ",";
    json += "\"upperThreshold\":" + String(upperThreshold) + ",";
    json += "\"mode\":\"" + mode + "\",";

    // Calculate remaining time if a timer is set
    unsigned long remainingTime = (timerDuration > 0) ? (startTime + timerDuration - millis()) / 1000 : 0;
    json += "\"remainingTime\":" + String(remainingTime);
    
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

void handlePumpControl() {
    if (server.hasArg("action")) {
        String action = server.arg("action");
        if (action == "on") controlPump(true);   // Turns motor ON
        if (action == "off") controlPump(false); // Turns motor OFF
        if (action == "auto") mode = "Auto";
        if (action == "manual") mode = "Manual";
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "OK");
}

void handleSetTimer() {
    if (server.hasArg("timer")) {
        // Convert minutes to milliseconds
        unsigned long minutes = server.arg("timer").toInt();
        timerDuration = minutes * 60000; // Convert minutes to milliseconds
        startTime = millis();
        controlPump(true);  // Turn on the motor for the duration of the timer
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Timer set");
}

void handleTimer() {
    if (millis() - startTime >= timerDuration && timerDuration > 0) {
        controlPump(false);  // Turn off the motor when the timer is done
        timerDuration = 0;
    }
}

// Automatically reconnect to WiFi
void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected, attempting reconnection...");
        WiFi.begin(ssid, password);
        while (WiFi.status() != WL_CONNECTED) {
            delay(1000);
            Serial.print(".");
        }
        Serial.println("Reconnected to WiFi.");
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(relayPin, OUTPUT);
    digitalWrite(relayPin, LOW); // Ensure motor is off initially

    // Set static IP configuration
    IPAddress staticIP(192, 168, 182, 99); // Desired IP address
    IPAddress gateway(192, 168, 216, 1);   // Replace with your network's gateway
    IPAddress subnet(255, 255, 255, 0);    // Subnet mask
    IPAddress dns(8, 8, 8, 8);             // DNS server (you can use Google's DNS or your router's DNS)

    WiFi.config(staticIP, gateway, subnet, dns); // Set static IP configuration
    WiFi.begin(ssid, password);                  // Connect to WiFi
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("Connected to WiFi");

    // Print the IP address
    Serial.print("Web server IP address: ");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", generatePage());
        server.sendHeader("Access-Control-Allow-Origin", "*");
    });

    server.on("/status", HTTP_GET, handleStatusUpdate);
    server.on("/control", HTTP_POST, handlePumpControl);
    server.on("/set_timer", HTTP_POST, handleSetTimer);

    server.begin();
    if (!radio.begin()) {
        Serial.println("NRF24L01 is NOT ready or not connected!");
        while (1); // Halt further execution
    }
    
    radio.setPALevel(RF24_PA_LOW);
    radio.setChannel(108);
    radio.openReadingPipe(1, address);
    radio.startListening(); // Start listening for messages

    Serial.println("NRF24L01 Receiver is ready. Waiting for messages...");
}

void loop() {
    server.handleClient();
    handleTimer();
    checkAutoMode();
    if (millis() - lastWiFiCheck > checkInterval) {
        lastWiFiCheck = millis();
        checkWiFiConnection();
    }
    if (radio.available()) {
        int soilMoistureValue = 0; // Change this to int to match sender
        radio.read(&soilMoistureValue, sizeof(soilMoistureValue)); // Read the soil moisture level
        receivedSoilMoisture = soilMoistureValue; // Store the value to be displayed on the webpage
    }
    delay(100); // Slight delay to prevent flooding the Serial Monitor
}

void checkAutoMode() {
    if (mode == "Auto") {
        int soilMoisture = readSoilMoisture(); // Read current soil moisture
        // Use the global value stored from the radio
        int currentReceivedSoilMoisture = receivedSoilMoisture; 
         Serial.print("Inverted Soil Moisture Level: ");
          Serial.println(currentReceivedSoilMoisture);

        // Debugging output
        //Serial.print("Soil Moisture: ");
        //Serial.print(soilMoisture);
        //Serial.print(", Received Soil Moisture: ");
        //Serial.println(currentReceivedSoilMoisture);
        //Serial.print("Lower Threshold: ");
        //Serial.println(lowerThreshold);
        //Serial.print("Upper Threshold: ");
        //Serial.println(upperThreshold);

        // Check conditions to control the pump
        if (soilMoisture < lowerThreshold || currentReceivedSoilMoisture < lowerThreshold) {
            //Serial.println("Turning ON the pump.");
            controlPump(true);  // Turn ON the pump if either reading is below the lower threshold
        } 
        else if (soilMoisture > upperThreshold && currentReceivedSoilMoisture > upperThreshold) {
            //Serial.println("Turning OFF the pump.");
            controlPump(false); // Turn OFF the pump only if both readings are above the upper threshold
        }
    }
}

String generatePage() {
  String html = "<html><head>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 0; display: flex; flex-direction: column; align-items: center; background-color: #e0f7e0; }"; // Light green background
  html += "h1 { color: #333; font-size: 48px; margin-bottom: 10px; }";  // Increased font size for company name
  html += "h2 { color: #666; font-size: 32px; margin-bottom: 20px; }";  // Increased font size for title
  html += "p { font-size: 22px; margin: 5px 0; }";  // Increased font size for paragraphs
  html += "button { font-size: 22px; padding: 20px 40px; margin: 10px; border: none; border-radius: 8px; cursor: pointer; }";  // Increased button size
  html += ".btn-on { background-color: #4CAF50; color: white; }";
  html += ".btn-off { background-color: #f44336; color: white; }";
  html += ".btn-mode { background-color: #2196F3; color: white; }";
  html += "input[type=text] { padding: 15px; font-size: 18px; border-radius: 8px; border: 1px solid #ccc; width: 200px; text-align: center; }";
  html += ".container { max-width: 900px; width: 100%; padding: 20px; box-sizing: border-box; text-align: center; }";
  html += ".timer-section { margin: 20px 0; }";  // Margin for timer section
  html += ".timer-input { font-size: 18px; padding: 15px; border-radius: 8px; border: 2px solid #2196F3; width: 220px; text-align: center; }";
  html += ".timer-button { font-size: 22px; padding: 20px 40px; border: none; border-radius: 8px; cursor: pointer; background-color: #2196F3; color: white; }";
  html += ".timer-button:hover { background-color: #1976D2; }";  // Hover effect for timer button
  html += "@media (max-width: 600px) {";
  html += "  body { font-size: 16px; }";
  html += "  button { font-size: 18px; padding: 15px 30px; }";
  html += "  input[type=text] { font-size: 16px; padding: 12px; }";
  html += "  h1 { font-size: 36px; }";  // Adjusted size for smaller screens
  html += "  h2 { font-size: 24px; }";  // Adjusted size for smaller screens
  html += "}";
  html += "</style>";
  html += "<script>";
  html += "function updateStatus() {";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.onreadystatechange = function() {";
  html += "    if (xhr.readyState == 4 && xhr.status == 200) {";
  html += "      var json = JSON.parse(xhr.responseText);";
  html += "      document.getElementById('soilMoisture').innerHTML = json.soilMoisture;";
  html += "      document.getElementById('receivedSoilMoisture').innerHTML = json.receivedSoilMoisture;";
  html += "      document.getElementById('motorStatus').innerHTML = json.motorStatus;";
  html += "      document.getElementById('lowerThreshold').innerHTML = json.lowerThreshold;";
  html += "      document.getElementById('upperThreshold').innerHTML = json.upperThreshold;";
  html += "      document.getElementById('mode').innerHTML = json.mode;";
  html += "      document.getElementById('remainingTime').innerHTML = json.remainingTime;";
  html += "    }";
  html += "  };";
  html += "  xhr.open('GET', '/status', true);";
  html += "  xhr.send();";
  html += "}";

  html += "function controlPump(action) {";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('POST', '/control', true);";
  html += "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');";
  html += "  xhr.send('action=' + action);";
  html += "}";

  html += "function setTimer() {";
  html += "  var timer = document.getElementById('timerInput').value;";
  html += "  var xhr = new XMLHttpRequest();";
  html += "  xhr.open('POST', '/set_timer', true);";
  html += "  xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');";
  html += "  xhr.send('timer=' + timer);";
  html += "}";

  html += "setInterval(updateStatus, 1000);";  
  html += "</script></head><body>";
  html += "<div class='container'>";
  html += "<h1>JAMBAVANTHA</h1>";  // Company name
  html += "<h2>AI Powered Smart Irrigation and Crop Management System</h2>";  // Page title
  html += "<p>Soil Moisture: <span id='soilMoisture'></span></p>";
  html += "<p>Received Soil Moisture: <span id='receivedSoilMoisture'></span></p>"; // Add this line for received soil moisture
  html += "<p>Motor Status: <span id='motorStatus'></span></p>";
  html += "<p>Lower Threshold: <span id='lowerThreshold'></span></p>";
  html += "<p>Upper Threshold: <span id='upperThreshold'></span></p>";
  html += "<p>Mode: <span id='mode'></span></p>";
  html += "<p>Remaining Time: <span id='remainingTime'></span> seconds</p>";
  html += "<button class='btn-mode' onclick=\"controlPump('manual')\">Switch to Manual Mode</button>";
  html += "<button class='btn-mode' onclick=\"controlPump('auto')\">Switch to Auto Mode</button>";
  html += "<br><button class='btn-on' onclick=\"controlPump('on')\">Turn Pump ON</button>";
  html += "<button class='btn-off' onclick=\"controlPump('off')\">Turn Pump OFF</button>";
  html += "<div class='timer-section'>";
  html += "<p>Set Timer (minutes):</p>";
  html += "<input type=\"text\" id=\"timerInput\" class='timer-input'>";
  html += "<button class='timer-button' onclick=\"setTimer()\">Set Timer</button>";
  html += "</div>";
  html += "</div></body></html>";
  return html;
}
