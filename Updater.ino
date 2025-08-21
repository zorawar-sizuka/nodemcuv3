

// --- Core & WiFi Libraries ---
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>

// --- OTA Update Libraries ---
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>

// --- Weighing System Libraries ---
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "HX711.h"

// =================================================================
// ======================= CONFIGURATION ===========================
// =================================================================

// --- WiFi Configuration ---
const char* ssid = "AdvancedCollege";
const char* password = "acem@123";

// --- OTA Update Configuration ---
const char* FIRMWARE_VERSION = "1.0"; // The version of THIS binary. 
const char* UPDATE_JSON_URL = "https://github.com/zorawar-sizuka/nodemcuv3/releases/download/latest/version.json";
const unsigned long UPDATE_CHECK_INTERVAL = 1800000; // Check for updates every 0.5 hour

// --- Weighing System Configuration ---
const char* serverUrl = "https://farm-main-nine.vercel.app/api/hatchery/weight";
#define DT 12       // GPIO12 (D6) for HX711 DT
#define SCK 14      // GPIO14 (D5) for HX711 SCK
#define TARE_BUTTON_PIN 0    // GPIO0  (D3)
#define NEXT_BUTTON_PIN 13   // GPIO13 (D7) for moving to next branch
#define UPLOAD_BUTTON_PIN 15 // GPIO15 (D8) for uploading data

// =================================================================
// ==================== GLOBAL OBJECTS & VARIABLES =================
// =================================================================

// --- OTA Logic Variables ---
unsigned long previousMillisOTA = 0;

// --- Weighing System Objects & Variables ---
ESP8266WiFiMulti WiFiMulti;
HX711 scale;
LiquidCrystal_I2C lcd(0x27, 16, 2); // LCD I2C address 0x27, 16x2 display

int currentBranch = 1;
const int totalBranches = 20;
float weight = 0;
bool measurementTaken = false;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;

enum SystemState {
  SELECT_BRANCH,
  PLACE_EGGS,
  UPLOAD_DATA,
  COMPLETED
};
SystemState currentState = SELECT_BRANCH;


// =================================================================
// ======================= OTA UPDATE LOGIC ========================
// =================================================================
// This entire function is from your original OTA code.
void checkForUpdates() {
  Serial.println("Checking for updates...");

  BearSSL::WiFiClientSecure client;
  client.setInsecure(); // This is used for both the JSON check and the binary download

  HTTPClient http;
  
  if (!http.begin(client, UPDATE_JSON_URL)) {
    Serial.println("Failed to begin HTTP client for JSON check.");
    return;
  }

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

    // The client is passed to the updater, which will use its security settings
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, binaryUrl);

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES"); // Should not happen in this logic block
        break;
      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK"); // This message is rarely seen as the ESP restarts
        break;
    }
  } else {
    Serial.println("Firmware is up to date.");
  }
}


// =================================================================
// ==================== WEIGHING SYSTEM LOGIC ======================
// =================================================================
// These functions are from your original weighing system code.

void handleButtons() {
  // Debounce logic
  if ((millis() - lastDebounceTime) < debounceDelay) {
    return;
  }

  // Tare button (works in any state)
  if (digitalRead(TARE_BUTTON_PIN) == LOW) {
    scale.tare();
    lcd.clear();
    lcd.print("Scale Tared!");
    lastDebounceTime = millis();
    delay(1500);
    // No need to call updateDisplay() here, the main loop will handle it.
    return;
  }

  // State machine button handling
  switch(currentState) {
    case SELECT_BRANCH:
      if (digitalRead(NEXT_BUTTON_PIN) == LOW) {
        currentState = PLACE_EGGS;
        measurementTaken = false;
        lastDebounceTime = millis();
      }
      break;
      
    case PLACE_EGGS:
      if (digitalRead(UPLOAD_BUTTON_PIN) == LOW) {
        if (scale.is_ready()) {
          weight = scale.get_units(5); // Get average of 5 readings
          measurementTaken = true;
          currentState = UPLOAD_DATA;
          lastDebounceTime = millis();
        }
      }
      break;
      
    case UPLOAD_DATA:
      if (digitalRead(NEXT_BUTTON_PIN) == LOW) {
        uploadData();
        if (currentBranch < totalBranches) {
          currentBranch++;
          currentState = SELECT_BRANCH;
        } else {
          currentState = COMPLETED;
        }
        lastDebounceTime = millis();
      }
      break;
      
    case COMPLETED:
      if (digitalRead(NEXT_BUTTON_PIN) == LOW) {
        // Reset the process
        currentBranch = 1;
        currentState = SELECT_BRANCH;
        lastDebounceTime = millis();
      }
      break;
  }
}

void updateDisplay() {
  lcd.clear();
  switch(currentState) {
    case SELECT_BRANCH:
      lcd.print("Press NEXT for");
      lcd.setCursor(0, 1);
      lcd.print("Branch: " + String(currentBranch) + "/" + String(totalBranches));
      break;
      
    case PLACE_EGGS:
      lcd.print("Place eggs for");
      lcd.setCursor(0, 1);
      lcd.print("Branch " + String(currentBranch));
      // Continuous weight reading is handled in loop()
      break;
      
    case UPLOAD_DATA:
      lcd.print("Branch " + String(currentBranch));
      lcd.setCursor(0, 1);
      lcd.print(String(weight, 2) + "kg - Upload?");
      break;
      
    case COMPLETED:
      lcd.print("All branches done!");
      lcd.setCursor(0, 1);
      lcd.print("Press to restart");
      break;
  }
}

void uploadData() {
  if (WiFiMulti.run() == WL_CONNECTED) {
    BearSSL::WiFiClientSecure client;
    HTTPClient https;

    Serial.print("[HTTPS] begin...\n");
    
    // Using setInsecure() as per your original code for easier HTTPS connection
    client.setInsecure(); 
    
    if (https.begin(client, serverUrl)) {
      https.addHeader("Content-Type", "application/json");
      
      String payload = "{\"branch\":" + String(currentBranch) + 
                      ",\"weight\":" + String(weight, 2) + "}";
      
      Serial.print("[HTTPS] POST...\n");
      Serial.println("Payload: " + payload);
      lcd.clear();
      lcd.print("Uploading...");

      int httpCode = https.POST(payload);

      if (httpCode > 0) {
        Serial.printf("[HTTPS] POST... code: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
          String response = https.getString();
          Serial.println("Response: " + response);
          lcd.clear();
          lcd.print("Upload Success!");
          delay(1500);
        } else {
          lcd.clear();
          lcd.print("Upload Error");
          lcd.setCursor(0, 1);
          lcd.print("Code: " + String(httpCode));
          delay(2000);
        }
      } else {
        Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
        lcd.clear();
        lcd.print("Upload Failed");
        delay(1500);
      }
      https.end();
    } else {
      Serial.println("[HTTPS] Unable to connect");
      lcd.clear();
      lcd.print("HTTPS Error");
      delay(1500);
    }
  } else {
    lcd.clear();
    lcd.print("WiFi Disconnected");
    delay(1500);
  }
}


// =================================================================
// ========================= SETUP & LOOP ==========================
// =================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nBooting Combined Weighing & OTA System...");
  Serial.printf("Current Firmware Version: %s\n", FIRMWARE_VERSION);

  // --- Initialize Weighing System Hardware ---
  pinMode(TARE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(NEXT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(UPLOAD_BUTTON_PIN, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
  lcd.print("Initializing...");
  
  scale.begin(DT, SCK);
  while (!scale.is_ready()) {
    Serial.println("Waiting for HX711...");
    lcd.clear();
    lcd.print("Scale not found");
    delay(1000);
  }
  scale.set_scale(57470); // Your calibrated scale factor
  scale.tare();
  lcd.clear();
  lcd.print("Scale Ready");
  delay(1000);

  // --- Connect to WiFi ---
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(ssid, password);
  
  lcd.clear();
  lcd.print("Connecting WiFi");
  Serial.print("Connecting to WiFi...");
  
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  lcd.clear();
  lcd.print("WiFi Connected");
  delay(1000);

  // --- Initialize OTA System ---
  // Configure the updater library to follow redirects globally for binary downloads.
  ESPhttpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  // Perform an initial check for updates on boot.
  checkForUpdates(); 
}

void loop() {
  // --- Background Task: Check for OTA updates periodically ---
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisOTA >= UPDATE_CHECK_INTERVAL) {
    previousMillisOTA = currentMillis;
    checkForUpdates();
  }

  // --- Primary Task: Run the weighing system logic ---
  static unsigned long lastDisplayUpdateTime = 0;

  handleButtons();
  
  // Continuously read and display weight only in the PLACE_EGGS state
  if (currentState == PLACE_EGGS && scale.is_ready()) {
    float currentWeight = scale.get_units(1);
    lcd.setCursor(0, 1);
    lcd.print(String(currentWeight, 2) + "kg        "); // Add spaces to clear previous longer numbers
  }
  
  // Update the main display text, but not too frequently to avoid flicker
  // This is a small improvement to your original loop structure.
  if (millis() - lastDisplayUpdateTime > 500) { 
      updateDisplay();
      lastDisplayUpdateTime = millis();
  }
}