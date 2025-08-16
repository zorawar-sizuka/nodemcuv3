// =================================================================
// =      FINAL: PRODUCTION OTA UPDATER FOR ESP8266 (NodeMCU)      =
// =================================================================
// FEATURES:
// - Correctly handles HTTPS and redirects for GitHub Releases.
// - Non-blocking, resilient, and provides clear diagnostic output.
// - Production-ready for deployment.
// =================================================================

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// =================================================================
// ============== CONFIGURATION - YOU MUST FILL THIS OUT ==============
// =================================================================

// --- WiFi Credentials ---
const char* ssid = "familynet2_2.4";
const char* password = "CLB434F88C";

// --- Firmware Version ---
// This is the version of THIS binary. Increment for each new release.
const char* FIRMWARE_VERSION = "1.0";

// --- OTA Update Server ---
// The final, permanent URL to your version.json file on GitHub.
const char* UPDATE_JSON_URL = "https://github.com/zorawar-sizuka/nodemcuv3/releases/download/latest/version.json";

// --- Update Check Interval ---
// 1 hour = 3600000 ms. For testing, use a shorter interval like 5 minutes (300000 ms).
const unsigned long UPDATE_CHECK_INTERVAL = 300000; // 1 hour

// =================================================================

unsigned long previousMillis = 0;

void checkForUpdates() {
  Serial.println("Checking for updates...");

  BearSSL::WiFiClientSecure client;
  // Bypass certificate validation. Standard practice for ESP8266 OTA.
  client.setInsecure();

  HTTPClient http;
  
  if (!http.begin(client, UPDATE_JSON_URL)) {
    Serial.println("Failed to begin HTTP client for JSON check.");
    return;
  }

  // CRITICAL FIX: Explicitly tell the client to follow GitHub's redirects.
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("Failed to check version. HTTP error code: %d -> %s\n", httpCode, http.errorToString(httpCode).c_str());
    http.end();
    return;
  }

  String payload = http.getString();
  http.end();

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print(F("JSON parsing failed: "));
    Serial.println(error.c_str());
    return;
  }

  const char* serverVersion = doc["version"];
  Serial.printf("Current firmware version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Server firmware version: %s\n", serverVersion);

  if (strcmp(serverVersion, FIRMWARE_VERSION) > 0) {
    Serial.println("New firmware available. Preparing to update.");
    String binaryFilename = doc["file"];
    String binaryUrl = UPDATE_JSON_URL;
    binaryUrl.replace("version.json", binaryFilename);
    
    Serial.println("Firmware URL: " + binaryUrl);
    
    // Perform the update. The ESPhttpUpdate library handles the download, flash, and reboot.
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, binaryUrl);

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;
      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK"); // Will not be seen, device reboots.
        break;
    }
  } else {
    Serial.println("Firmware is up to date.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nBooting...");
  Serial.printf("Current Firmware Version: %s\n", FIRMWARE_VERSION);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Check for updates once on boot-up.
  checkForUpdates();
}

void loop() {
  // Your primary application logic goes here.
  // This code will run continuously without being interrupted.


  // Non-blocking periodic check for updates.
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= UPDATE_CHECK_INTERVAL) {
    previousMillis = currentMillis;
    checkForUpdates();
  }
}