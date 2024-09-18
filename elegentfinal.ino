#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <DHT.h>
#include <WebSocketsServer.h> // Add this library for WebSocket support

#define DHTPIN 4
#define DHTTYPE DHT22
#define WATER_LEVEL_SENSOR_PIN A0
#define PUMP_PIN 14

const char* ssid = "NARZO N65 5G";
const char* password = "v2ar6b3q";

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);
ESP8266WebServer server(80);
WebSocketsServer webSocket(81); // WebSocket server on port 81
DHT dht(DHTPIN, DHTTYPE);

int irrigationStartYear = 0, irrigationStartMonth = 0, irrigationStartDay = 0;
int irrigationStartHour = 0, irrigationStartMinute = 0;
int irrigationDurationMinutes = 0;
bool pumpStatus = false;

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  pinMode(WATER_LEVEL_SENSOR_PIN, INPUT);
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");
  
  // Print the IP address to the Serial Monitor
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  timeClient.begin();
  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/schedule", HTTP_GET, handleSchedule);
  server.on("/pump/on", HTTP_GET, handlePumpOn);
  server.on("/pump/off", HTTP_GET, handlePumpOff);
  server.on("/pump/state", HTTP_GET, handlePumpState);
  server.on("/temperature", HTTP_GET, handleTemperature);
  server.on("/humidity", HTTP_GET, handleHumidity);
  server.on("/water-level", HTTP_GET, handleWaterLevel);
  
  server.begin();
  Serial.println("HTTP server started");

  webSocket.begin();  // Initialize WebSocket
  webSocket.onEvent(webSocketEvent); // Register WebSocket event handler
}

void loop() {
  server.handleClient();
  webSocket.loop();  // Handle WebSocket events

  timeClient.update();
  
  // Get current time
  time_t epochTime = timeClient.getEpochTime();
  struct tm* timeInfo = localtime(&epochTime);

  int currentYear = timeInfo->tm_year + 1900;
  int currentMonth = timeInfo->tm_mon + 1;
  int currentDay = timeInfo->tm_mday;
  int currentHour = timeInfo->tm_hour;
  int currentMinute = timeInfo->tm_min;

  // Auto-off feature: check water level and stop the pump if it exceeds the threshold
  if (pumpStatus) {
    int waterLevel = analogRead(WATER_LEVEL_SENSOR_PIN);
    if (waterLevel > 800) { // Replace with your high threshold value
      turnPumpOff(); // Turn off the pump if water level exceeds the threshold
      Serial.println("Pump turned off due to high water level.");
    }
  }

  // Check if it's time to start or stop the irrigation based on the schedule
  if (irrigationStartYear == currentYear && 
      irrigationStartMonth == currentMonth && 
      irrigationStartDay == currentDay &&
      irrigationStartHour == currentHour && 
      irrigationStartMinute == currentMinute) {
    turnPumpOn(); // Start the pump at the scheduled time
    Serial.println("Pump started according to schedule.");
    delay(irrigationDurationMinutes * 60000); // Run the pump for the scheduled duration
    turnPumpOff(); // Stop the pump after the scheduled duration
    Serial.println("Pump stopped after scheduled duration.");
  }

  // Broadcast updated sensor data via WebSocket
  if (webSocket.connectedClients() > 0) { // Ensure at least one client is connected
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int waterLevel = analogRead(WATER_LEVEL_SENSOR_PIN);
    
    if (isnan(temperature)) temperature = 0;
    if (isnan(humidity)) humidity = 0;

    String data = "{";
    data += "\"temperature\":\"" + String(temperature) + "\",";
    data += "\"humidity\":\"" + String(humidity) + "\",";
    data += "\"waterLevel\":\"" + String(waterLevel) + "\"";
    data += "}";

    webSocket.broadcastTXT(data); // Send data to all connected WebSocket clients
  }
}

void turnPumpOn() {
  digitalWrite(PUMP_PIN, HIGH);
  pumpStatus = true;
}

void turnPumpOff() {
  digitalWrite(PUMP_PIN, LOW);
  pumpStatus = false;
}

void handleRoot() {
  String html = "<!DOCTYPE html>\
  <html lang='en'>\
  <head>\
    <meta charset='UTF-8'>\
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>\
    <title>Smart Irrigation System</title>\
    <style>\
      body { font-family: Arial, sans-serif; background-color: #e8f5e9; }\
      h1 { color: #388e3c; }\
      p { margin: 5px 0; }\
      button { margin: 5px; }\
    </style>\
  </head>\
  <body>\
    <h1>JAMBAVANTHA Smart Irrigation System</h1>\
    <p>Temperature: <span id='temperature'>Loading...</span></p>\
    <p>Humidity: <span id='humidity'>Loading...</span></p>\
    <p>Water Level: <span id='waterLevel'>Loading...</span></p>\
    <p>Motor Status: <span id='motorState'>Loading...</span></p>\
    <p>Schedule: <span id='schedule'>Not Set</span></p>\
    <button onclick='turnOnPump()'>Turn Pump On</button><br>\
    <button onclick='turnOffPump()'>Turn Pump Off</button><br>\
    <form action='/schedule' method='get' onsubmit='return validateSchedule()'>\
      <label for='date'>Irrigation Date (YYYY-MM-DD):</label>\
      <input type='date' id='date' name='date' required><br>\
      <label for='time'>Irrigation Start Time (HH:MM):</label>\
      <input type='time' id='time' name='time' required><br>\
      <label for='duration'>Duration (minutes):</label>\
      <input type='number' id='duration' name='duration' required><br>\
      <input type='submit' value='Set Schedule'>\
    </form><br>\
    <p>Your ESP8266 IP Address: <span id='ipAddress'>" + WiFi.localIP().toString() + "</span></p>\
    <script>\
      var ws = new WebSocket('ws://' + location.hostname + ':81');\
      ws.onmessage = function(event) {\
        var data = JSON.parse(event.data);\
        document.getElementById('temperature').innerHTML = data.temperature;\
        document.getElementById('humidity').innerHTML = data.humidity;\
        document.getElementById('waterLevel').innerHTML = data.waterLevel;\
      };\
      function turnOnPump() {\
        var xhr = new XMLHttpRequest();\
        xhr.open('GET', '/pump/on', true);\
        xhr.send();\
      }\
      function turnOffPump() {\
        var xhr = new XMLHttpRequest();\
        xhr.open('GET', '/pump/off', true);\
        xhr.send();\
      }\
      function updateMotorState() {\
        var xhr = new XMLHttpRequest();\
        xhr.onreadystatechange = function() {\
          if (xhr.readyState == 4 && xhr.status == 200) {\
            document.getElementById('motorState').innerHTML = xhr.responseText;\
          }\
        };\
        xhr.open('GET', '/pump/state', true);\
        xhr.send();\
      }\
      function updateSchedule() {\
        var xhr = new XMLHttpRequest();\
        xhr.onreadystatechange = function() {\
          if (xhr.readyState == 4 && xhr.status == 200) {\
            document.getElementById('schedule').innerHTML = xhr.responseText;\
          }\
        };\
        xhr.open('GET', '/schedule', true);\
        xhr.send();\
      }\
      function validateSchedule() {\
        // Optional: Add custom validation if needed\
        return true;\
      }\
      // Update every 5 seconds\
      setInterval(updateMotorState, 5000);\
      setInterval(updateSchedule, 5000);\
    </script>\
  </body>\
  </html>";
  server.send(200, "text/html", html);
}

void handleSchedule() {
  String dateParam = server.arg("date");
  String timeParam = server.arg("time");
  String durationParam = server.arg("duration");
  
  if (dateParam.length() > 0 && timeParam.length() > 0 && durationParam.length() > 0) {
    // Extract year, month, day, hour, minute from the parameters
    irrigationStartYear = dateParam.substring(0, 4).toInt();
    irrigationStartMonth = dateParam.substring(5, 7).toInt();
    irrigationStartDay = dateParam.substring(8, 10).toInt();
    irrigationStartHour = timeParam.substring(0, 2).toInt();
    irrigationStartMinute = timeParam.substring(3, 5).toInt();
    irrigationDurationMinutes = durationParam.toInt();
    server.send(200, "text/plain", "Schedule updated");
  } else {
    server.send(400, "text/plain", "Invalid schedule parameters");
  }
}

void handlePumpOn() {
  turnPumpOn();
  server.send(200, "text/plain", "Pump turned on");
}

void handlePumpOff() {
  turnPumpOff();
  server.send(200, "text/plain", "Pump turned off");
}

void handlePumpState() {
  server.send(200, "text/plain", pumpStatus ? "On" : "Off");
}

void handleTemperature() {
  float temperature = dht.readTemperature();
  server.send(200, "text/plain", isnan(temperature) ? "Error" : String(temperature));
}

void handleHumidity() {
  float humidity = dht.readHumidity();
  server.send(200, "text/plain", isnan(humidity) ? "Error" : String(humidity));
}

void handleWaterLevel() {
  int waterLevel = analogRead(WATER_LEVEL_SENSOR_PIN);
  server.send(200, "text/plain", String(waterLevel));
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    String message = String((char*)payload);
    Serial.printf("WebSocket message received: %s\n", message.c_str());
    // Handle WebSocket message if needed
  }
}
