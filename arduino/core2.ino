#include "M5Core2.h"
#include <driver/i2s.h>
#include "AudioTools.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <FastLED.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <vector>
#include <esp_sleep.h>
#include <esp_pm.h>

// Global web server (on port 80)
WebServer server(80);

// Sleep timeout in milliseconds
#define SLEEP_TIMEOUT_MS 120000  // 2 minutes of inactivity before sleep
unsigned long lastActivityTime = 0;

///////////////////////
// Audio State Setup
///////////////////////
enum AudioState {
  IDLE,
  RECORDING,
  WAITING,
  PLAYING
};

AudioState currentState = IDLE;

///////////////////////
// Pin Definitions for M5Stack Core2
///////////////////////
// Built-in mic pins (I2S) for Core2
#define MIC_BCK_PIN     0  // I2S clock
#define MIC_WS_PIN      34 // I2S WS (Word Select)
#define MIC_DATA_IN_PIN 23 // I2S data
#define MIC_CLK_PIN     0   // Clock pin for PDM
#define MIC_DATA_PIN    34  // Data pin for PDM microphone

// Speaker pins for Core2
#define SPK_BCK_PIN     12
#define SPK_WS_PIN      0
#define SPK_DATA_OUT_PIN 2

// I2S port
#define I2S_PORT I2S_NUM_0

// Define additional colors
#define DARKRED 0x8800  // Darker red color for LED blinking

///////////////////////
// Global Variables
///////////////////////
#define AUDIO_BUFFER_SIZE (1024 * 256)
uint8_t* audioBuffer = NULL;
int audioBufferOffset = 0;
bool isRecording = false;
#define AUDIO_PLAYBACK_BUFFER_SIZE (4096)
uint8_t audioPlaybackBuffer[AUDIO_PLAYBACK_BUFFER_SIZE];
int audioPlaybackBufferPos = 0;
unsigned long lastAudioProcessTime = 0;

String responseText = "";
bool hasDisplayedText = false;
unsigned long lastAudioDataTime = 0;
int textScrollOffset = 0;
int maxLines = 12;

// --- WiFi Credentials Struct ---
struct WifiCredentials {
  String ssid;
  String password;
  String server;
};
WifiCredentials globalCreds;  // Global variable to hold saved credentials

// --- Function Declarations ---
bool loadWifiConfig(WifiCredentials &creds);
bool saveWifiConfig(const WifiCredentials &creds);
void startConfigPortal();
void handleRoot();
void handleSave();
void handleReset();
void parseServerConfig(const String &serverConfig, String &host, uint16_t &port, bool &useTLS);

///////////////////////
// Wi-Fi Configuration
///////////////////////
WiFiUDP udp;
WiFiClient client;

void displayStatus(uint32_t color, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    M5.Lcd.fillCircle(160, 120, 40, color);
    delay(delayMs);
    M5.Lcd.fillCircle(160, 120, 40, BLACK);
    delay(delayMs);
  }
}

bool connectToWiFi(const String &ssid, const String &password) {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected!");
    displayStatus(GREEN, 3, 300); // Green blinks for success
    return true;
  }
  Serial.println("\nFailed to connect.");
  return false;
}

// --- SPIFFS and Settings Functions ---
bool loadWifiConfig(WifiCredentials &creds) {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return false;
  }
  if (!SPIFFS.exists("/settings.json")) {
    Serial.println("No saved settings found.");
    return false;
  }
  File file = SPIFFS.open("/settings.json", "r");
  if (!file) {
    Serial.println("Failed to open settings file.");
    return false;
  }
  size_t size = file.size();
  std::unique_ptr<char[]> buf(new char[size]);
  file.readBytes(buf.get(), size);
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, buf.get())) {
    Serial.println("Failed to parse settings.");
    return false;
  }
  creds.ssid = doc["ssid"].as<String>();
  creds.password = doc["password"].as<String>();
  creds.server = doc["server"].as<String>();
  Serial.println("Loaded saved WiFi config:");
  Serial.println("SSID: " + creds.ssid);
  Serial.println("Server: " + creds.server);
  file.close();
  return true;
}

bool saveWifiConfig(const WifiCredentials &creds) {
  DynamicJsonDocument doc(512);
  doc["ssid"] = creds.ssid;
  doc["password"] = creds.password;
  doc["server"] = creds.server;
  File file = SPIFFS.open("/settings.json", "w");
  if (!file) {
    Serial.println("Failed to open settings file for writing.");
    return false;
  }
  serializeJson(doc, file);
  file.close();
  Serial.println("Settings saved to SPIFFS.");
  return true;
}

// --- Web Server Handlers ---
void handleRoot() {
  Serial.println("handleRoot called");
  int n = WiFi.scanNetworks();
  String html = "<html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Config Portal</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; text-align: center; padding: 20px; }";
  html += "h1 { font-size: 24px; }";
  html += "label { font-size: 18px; }";
  html += "input[type='text'], input[type='password'] { font-size: 16px; padding: 8px; width: 80%; max-width: 300px; }";
  html += "select { font-size: 16px; padding: 8px; width: 84%; max-width: 316px; }";
  html += "input[type='submit'] { font-size: 18px; padding: 10px 20px; margin-top: 20px; }";
  html += "button { font-size: 20px; padding: 10px 20px; margin-top: 20px; }";
  html += "</style></head><body>";
  html += "<h1>WiFi & Server Configuration</h1>";
  html += "<form action='/save' method='POST' enctype='application/x-www-form-urlencoded' onsubmit='return validateForm()'>";
  html += "<label for='ssid'>WiFi Network:</label><br>";
  html += "<select name='ssid' id='ssid'>";
  for (int i = 0; i < n; i++) {
    html += "<option value='" + WiFi.SSID(i) + "'>" + WiFi.SSID(i) + "</option>";
  }
  html += "</select><br><br>";
  html += "<label for='password'>WiFi Password:</label><br>";
  html += "<input type='password' id='password' name='password'><br><br>";
  html += "<label for='server'>Server (IP/URL):</label><br>";
  html += "<input type='text' id='server' name='server' placeholder='e.g., 192.168.88.122 or https://myserver.com:12345' required><br><br>";
  html += "<input type='submit' value='Save'>";
  html += "</form>";
  html += "<script>";
  html += "function validateForm() {";
  html += "  var serverField = document.forms[0]['server'].value;";
  html += "  var pattern = /(https?:\\/\\/)?((([\\da-z\\.-]+)\\.[a-z\\.]{2,6})|((\\d{1,3}\\.){3}\\d{1,3}))(\\:[0-9]{1,5})?/;";
  html += "  if (!pattern.test(serverField)) {";
  html += "    alert('Please enter a valid server address.');";
  html += "    return false;";
  html += "  }";
  html += "  return true;";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  Serial.println("\n\n*** handleSave called ***");

  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("server")) {
    WifiCredentials creds;
    creds.ssid = server.arg("ssid");
    creds.password = server.arg("password");
    creds.server = server.arg("server");
    Serial.println("Received config:");
    Serial.println("SSID: " + creds.ssid);
    Serial.println("Server: " + creds.server);

    if (saveWifiConfig(creds)) {
      String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
      html += "<style>body { font-family: Arial, sans-serif; text-align: center; padding: 20px; }";
      html += "h1 { font-size: 28px; color: green; }</style></head><body>";
      html += "<h1>Settings saved!</h1>";
      html += "<p style='font-size:20px;'>The device is rebooting now...</p>";
      html += "</body></html>";
      server.sendHeader("Connection", "close");
      server.send(200, "text/html", html);
      Serial.println("Settings saved.");
      Serial.println("Restarting now...");
      delay(100);
      M5.Lcd.clear();
      ESP.restart();
    } else {
      server.send(500, "text/html", "Failed to save settings.");
      Serial.println("Failed to save settings.");
    }
  } else {
    server.send(400, "text/html", "Missing WiFi or server details.");
    Serial.println("Missing WiFi or server details.");
  }
}

void startConfigPortal() {
  Serial.println("Starting configuration portal...");
  Serial.printf("Free heap before server start: %u\n", ESP.getFreeHeap());
  // Delete existing settings to avoid conflicts
  if (SPIFFS.exists("/settings.json")) {
    SPIFFS.remove("/settings.json");
    Serial.println("Removed existing settings file.");
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Tico_Config");
  if (!MDNS.begin("tico-config")) {
    Serial.println("Error setting up MDNS responder!");
  } else {
    Serial.println("mDNS responder started: http://tico-config.local");
  }
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound([](){
    Serial.println("404: " + server.uri());
    server.send(404, "text/plain", "Not found");
  });
  server.begin();
  Serial.println("HTTP server started on port 80.");

  // Display configuration instructions on Core2 screen
  M5.Lcd.clear();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setCursor(0, 20);
  M5.Lcd.println("Configuration Mode");
  M5.Lcd.println("");
  M5.Lcd.println("Connect to WiFi:");
  M5.Lcd.println("SSID: Tico_Config");
  M5.Lcd.println("");
  M5.Lcd.println("Then browse to:");
  M5.Lcd.println("http://tico-config.local");
  M5.Lcd.println("or");
  M5.Lcd.println(IP.toString());
}

// --- Helper Function to Parse Server Configuration ---
void parseServerConfig(const String &serverConfig, String &host, uint16_t &port, bool &useTLS) {
  // Default values
  host = serverConfig;
  port = 12345; // default TCP port
  useTLS = false;

  // Remove protocol if present
  if (serverConfig.startsWith("http://")) {
    host = serverConfig.substring(7);
    useTLS = false;
  } else if (serverConfig.startsWith("https://")) {
    host = serverConfig.substring(8);
    useTLS = true;
  }
  // Check for port specification (colon)
  int colonIndex = host.indexOf(':');
  if (colonIndex != -1) {
    String portStr = host.substring(colonIndex + 1);
    host = host.substring(0, colonIndex);
    uint16_t p = portStr.toInt();
    if (p != 0) {
      port = p;
    }
  }
}

bool forceConfigMode() {
  // Wait for 5 seconds to see if the button is pressed (using Core2 BtnA)

  Serial.println("Hold left button for 5 seconds to force config mode...");
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 80);
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Hold left button for");
  M5.Lcd.println("5 seconds to enter");
  M5.Lcd.println("config mode...");
  delay(3000);
  M5.Lcd.clear();
  M5.Lcd.setTextSize(5);
  M5.Lcd.setCursor(150, 160);
  unsigned long start = millis();
  M5.Lcd.println("5");
  while(millis() - start < 5000) {
    M5.update();
    if(M5.BtnA.isPressed()) {
      Serial.println("Force config mode triggered!");
      return true;
    }
    // Update countdown on screen
    M5.Lcd.clear();
    M5.Lcd.setCursor(150, 160);
    M5.Lcd.printf("%d ", 5 - ((millis() - start) / 1000));
    delay(100);
  }
  M5.Lcd.clear();
  M5.Lcd.setTextSize(2);
  return false;
}

void updateLED(AudioState state) {
  // Update the LCD instead of single LED
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(20, 20);

  switch(state) {
    case RECORDING:
      M5.Lcd.fillCircle(160, 120, 40, RED);
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.println("RECORDING");
      break;
    case WAITING:
      M5.Lcd.fillCircle(160, 120, 40, BLUE);
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.println("PROCESSING");
      break;
    case PLAYING:
      M5.Lcd.fillCircle(160, 120, 40, GREEN);
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.println("PLAYING");
      break;
    case IDLE:
    default:
      M5.Lcd.setTextColor(WHITE);
      M5.Lcd.println("I'M READY");
      M5.Lcd.setTextSize(1.5);
      M5.Lcd.setCursor(75, 225);
      M5.Lcd.println("Hold this button to record");
      M5.Lcd.setTextSize(2);
      break;
  }
}

void setVolume(uint8_t volume) {
  // volume should be 0-100
  if (volume > 100) volume = 100;

  // The Core2 uses LDO3 to control speaker output
  // Convert 0-100 to voltage range (approximately 2.5V to 3.3V)
  float voltage = 2.5 + (volume / 100.0) * 0.8;

  // Set LDO3 voltage (which controls the speaker)
  M5.Axp.SetLDOVoltage(3, voltage);
}

void dumpBufferInfo(uint8_t* buffer, size_t len, const char* label) {
  Serial.printf("%s: %d bytes, first 16 bytes: ", label, len);
  for (int i = 0; i < min(16, (int)len); i++) {
    Serial.printf("%02X ", buffer[i]);
  }

  // Show as ASCII if most bytes are printable
  int printable = 0;
  for (int i = 0; i < min(32, (int)len); i++) {
    if (buffer[i] >= 32 && buffer[i] <= 126) printable++;
  }

  if (printable > 16) {
    Serial.print(" ASCII: \"");
    for (int i = 0; i < min(32, (int)len); i++) {
      if (buffer[i] >= 32 && buffer[i] <= 126)
        Serial.print((char)buffer[i]);
      else
        Serial.print('.');
    }
    Serial.println("\"");
  } else {
    Serial.println();
  }
}

bool looksLikeWaveAudio(uint8_t* buffer, size_t len) {
  // Check for common PCM audio patterns:
  // 1. Not too many zero values in sequence
  int zeroCount = 0;
  int maxZeros = 0;

  for (size_t i = 0; i < min(len, (size_t)128); i++) {
    if (buffer[i] == 0) {
      zeroCount++;
    } else {
      if (zeroCount > maxZeros) maxZeros = zeroCount;
      zeroCount = 0;
    }
  }
  if (zeroCount > maxZeros) maxZeros = zeroCount;

  // 2. Not too many repeating patterns
  int repeats = 0;
  for (size_t i = 4; i < min(len, (size_t)128) - 4; i++) {
    if (buffer[i] == buffer[i-4] && buffer[i+1] == buffer[i-3] &&
        buffer[i+2] == buffer[i-2] && buffer[i+3] == buffer[i-1]) {
      repeats++;
    }
  }

  // Audio data shouldn't have too many zeros or repeating patterns
  return (maxZeros < 12 && repeats < 15);
}

// Audio validation function
bool isAudioData(uint8_t* buffer, size_t len) {
  // Quick check - if it starts with '{' it's definitely JSON, not audio
  if (buffer[0] == '{') return false;

  // Count different byte values and printable characters
  int different_values = 0;
  int text_chars = 0;
  bool seen[256] = {false};

  for (size_t i = 0; i < min(len, (size_t)64); i++) {
    // Count unique byte values
    if (!seen[buffer[i]]) {
      seen[buffer[i]] = true;
      different_values++;
    }

    // Count printable ASCII characters
    if (buffer[i] >= 32 && buffer[i] <= 126) {
      text_chars++;
    }
  }

  // Audio data typically has many different byte values
  // and not too high a percentage of printable text characters
  return (different_values >= 12) && (text_chars < min(len, (size_t)64) / 2);
}

// Reset the audio state
void resetAudioState() {
  isRecording = false;
  audioBufferOffset = 0;
  currentState = IDLE;
  updateLED(currentState);

  // Reset audio playback buffer
  audioPlaybackBufferPos = 0;

  delay(100);  // Give hardware time to stabilize
  initI2SMic();

  // Update activity timestamp
  lastActivityTime = millis();
}

void wordWrapText(const String &text, int maxWidth, std::vector<String> &lines) {
  int startPos = 0;
  int textLength = text.length();

  while (startPos < textLength) {
    // If remaining text fits on one line
    if (startPos + maxWidth >= textLength) {
      lines.push_back(text.substring(startPos));
      break;
    }

    // Find the last space within max width
    int endPos = startPos + maxWidth;
    int lastSpace = text.lastIndexOf(' ', endPos);

    // If no space found or space is too far back, break at maxWidth
    if (lastSpace <= startPos || lastSpace < startPos + maxWidth/2) {
      endPos = startPos + maxWidth;
      lines.push_back(text.substring(startPos, endPos));
      startPos = endPos;
    } else {
      // Break at space
      lines.push_back(text.substring(startPos, lastSpace));
      startPos = lastSpace + 1; // Skip the space
    }
  }
}

// Improve button detection - add this function
bool checkButtonPress(Button &btn) {
  static unsigned long lastPressTimeB = 0;
  static unsigned long lastPressTimeC = 0;

  // Track each button separately
  unsigned long *lastPressPtr = (&btn == &M5.BtnB) ?
                               &lastPressTimeB :
                               &lastPressTimeC;

  if (btn.isPressed()) {
    unsigned long now = millis();
    // Use shorter debounce time for scroll buttons (200ms)
    if (now - *lastPressPtr > 200) {
      *lastPressPtr = now;
      return true;
    }
  }
  return false;
}

void displayText(const String &text, int scrollOffset = 0) {
  // Clear screen
  M5.Lcd.fillScreen(BLACK);

  // Display the response text
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);

  // Get word-wrapped lines
  std::vector<String> wrappedLines;
  wordWrapText(text, 25, wrappedLines);

  // Debug information
  int totalLines = wrappedLines.size();

  Serial.printf("Text has %d lines, showing offset %d\n", totalLines, scrollOffset);

  // Calculate the valid maximum scrollable position
  //int maxScrollOffset = max(0, totalLines - maxLines);

  // Ensure scroll offset is in valid range
  //scrollOffset = min(scrollOffset, maxScrollOffset);

  // Ensure scroll offset is valid
  if (scrollOffset > totalLines - maxLines) {
    if (totalLines > maxLines) {
      scrollOffset = totalLines - maxLines;
    } else {
      scrollOffset = 0;
    }
  }

  // Display lines
  int linesToShow = min(maxLines, totalLines);
  for (int i = 0; i < linesToShow; i++) {
    if (scrollOffset + i < totalLines) {
      M5.Lcd.println(wrappedLines[scrollOffset + i]);
    }
  }

  // Show scroll indicators
  if (totalLines > maxLines) {
    if (scrollOffset > 0) {
      M5.Lcd.fillTriangle(160, 230, 170, 240, 150, 240, BLUE);
    }

    // Down arrow when not at bottom
    //if (textScrollOffset + maxLines < totalLines) {
    if (scrollOffset < totalLines - maxLines) {
      M5.Lcd.fillTriangle(265, 240, 275, 230, 255, 230, BLUE);
    }
  }
}

// Update your initI2SMic function
void initI2SMic() {
  // Clean up any previous I2S setup
  i2s_driver_uninstall(I2S_PORT);

  Serial.println("Initializing I2S for Core2 PDM microphone...");

  // PDM microphone configuration for Core2
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,  // Core2 PDM mic is on RIGHT channel
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed to install I2S driver: %d\n", err);
  }

  // PDM mode uses special pin config - CLK and WS pins are the same
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_PIN_NO_CHANGE,  // Not used in PDM
    .ws_io_num = MIC_CLK_PIN,         // Clock pin
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = MIC_DATA_PIN        // Data pin
  };

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S pins: %d\n", err);
  }

  // Set the clock - important for PDM
  err = i2s_set_clk(I2S_PORT, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S clock: %d\n", err);
  }

  Serial.println("I2S PDM microphone initialized");

}

void preprocessAudio(int16_t* samples, size_t sampleCount) {
  // 1. DC offset removal
  int32_t sum = 0;
  for (size_t i = 0; i < sampleCount; i++) {
    sum += samples[i];
  }
  int16_t dcOffset = sum / sampleCount;

  // 2. Apply high-pass filter to remove DC and normalize
  for (size_t i = 0; i < sampleCount; i++) {
    // Remove DC offset
    samples[i] -= dcOffset;

    // Amplify the signal if needed
    samples[i] *= 2;  // Adjust this multiplier as needed

    // Limit to prevent clipping
    if (samples[i] > 32000) samples[i] = 32000;
    if (samples[i] < -32000) samples[i] = -32000;
  }
}

bool isButtonPressedWithDebounce(Button& btn) {
  static unsigned long lastPressTime = 0;
  if (btn.wasPressed()) {
    unsigned long now = millis();
    if (now - lastPressTime > 500) { // 500ms debounce
      lastPressTime = now;
      return true;
    }
  }
  return false;
}

///////////////////////
// I2S Speaker (TX) Setup
///////////////////////
void initI2SSpeaker() {
  // Clean up any previous I2S setup
  i2s_driver_uninstall(I2S_PORT);

  // Configure I2S for speaker output
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,  // Core2 speaker is on RIGHT channel
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,  // Smaller buffers for more responsive playback
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed to install I2S driver for speaker: %d\n", err);
    return;  // Early return on error
  }

  // Configure I2S pins for speaker
  i2s_pin_config_t pin_config = {
    .bck_io_num = SPK_BCK_PIN,
    .ws_io_num = SPK_WS_PIN,
    .data_out_num = SPK_DATA_OUT_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S pins for speaker: %d\n", err);
    return;  // Early return on error
  }

  // Enable the built-in speaker amplifier
  M5.Axp.SetSpkEnable(true);

  // Set volume
  setVolume(90);  // 90% volume

  // Set I2S clock
  err = i2s_set_clk(I2S_PORT, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S clock for speaker: %d\n", err);
    return;  // Early return on error
  }

  // Add a short delay to let the I2S peripheral stabilize
  delay(100);

  Serial.println("Speaker initialized successfully");
}

///////////////////////
// Setup Function
///////////////////////
void setup() {
  M5.begin(true, true, true, true);  // Init LCD, power, SD card and serial
  Serial.begin(115200);

  // Check wake reason first thing
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  // For light sleep we only need to check if wakeup comes from EXT0
  // Light sleep resumes execution after esp_light_sleep_start()
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Wakeup caused by external signal using RTC_IO");
    // We don't call wakeFromSleep() here because light sleep
    // will already resume execution after the sleep function
  } else {
    // Normal startup
    M5.Lcd.clear();
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 20);
    M5.Lcd.println("Starting up...");

    // Initialize SPIFFS (for saving settings)
    if (!SPIFFS.begin(true)) {
      Serial.println("SPIFFS mount failed!");
    }

    // Attempt to allocate the buffer in PSRAM
    audioBuffer = (uint8_t*) heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    if (audioBuffer == NULL) {
      Serial.println("Failed to allocate audioBuffer in PSRAM. Reducing buffer size...");
      // Fallback: try a smaller size in internal DRAM
      #undef AUDIO_BUFFER_SIZE
      #define AUDIO_BUFFER_SIZE (1024 * 80)
      audioBuffer = (uint8_t*) malloc(AUDIO_BUFFER_SIZE);
      if (audioBuffer == NULL) {
        Serial.println("Critical error: unable to allocate audioBuffer!");
        M5.Lcd.println("ERROR: Memory allocation failed");
        while(1);
      }
    }
    Serial.printf("audioBuffer allocated, size: %d bytes\n", AUDIO_BUFFER_SIZE);

    WifiCredentials creds;
    bool configMode = false;

    // If forced, skip loading config
    if (forceConfigMode() || !loadWifiConfig(creds)) {
      configMode = true;
    } else {
      globalCreds = creds;
      M5.Lcd.setCursor(40, 200);
      M5.Lcd.println("Connecting to WiFi...");
      if (!connectToWiFi(creds.ssid, creds.password)) {
        configMode = true;
      }
    }

    if (configMode) {
      // Start configuration portal only
      startConfigPortal();
      Serial.println("In config mode; connect to the AP 'Tico_Config' to configure WiFi & server settings.");
    } else {
      // When configuration is valid, continue with audio processing
      Serial.println("WiFi connected! IP address: " + WiFi.localIP().toString());
      udp.begin(6980);
      Serial.println("Initializing I2S in mic mode...");
      initI2SMic();

      // Set to idle state and update display
      currentState = IDLE;
      updateLED(currentState);
    }
  }

  // Initialize last activity time
  lastActivityTime = millis();
}

///////////////////////
// Main Loop
///////////////////////

void loop() {

  M5.update();

  // If in configuration mode (AP), handle web client requests
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    // Blink indicator in config mode
    static unsigned long lastBlink = 0;
    static bool ledOn = false;
    if (millis() - lastBlink >= 500) { // change state every 500ms
      lastBlink = millis();
      if (ledOn) {
        M5.Lcd.fillCircle(280, 220, 15, BLACK);
        ledOn = false;
      } else {
        M5.Lcd.fillCircle(280, 220, 15, RED);
        ledOn = true;
      }
    }
    server.handleClient();
    delay(1); // small delay to ease CPU load

    // Update activity timestamp in config mode too
    lastActivityTime = millis();
    return; // Skip audio tasks
  }

  // --- Recording Phase ---
  if (isButtonPressedWithDebounce(M5.BtnA)) {
    Serial.println("Button A pressed: Starting recording & streaming.");

    // Reset everything first
    if (currentState == PLAYING) {
      client.stop();
      delay(100);
    }
    // Reset text and scroll position
    responseText = "";
    textScrollOffset = 0;

    resetAudioState();

    isRecording = true;
    currentState = RECORDING;
    updateLED(currentState);

    // Update activity timestamp
    lastActivityTime = millis();

    // Add immediate visual feedback
    M5.Lcd.fillCircle(160, 120, 40, RED);
  }

  if (isRecording && M5.BtnA.isPressed()) {
    // Ensure the LED/display is constantly updated
    static unsigned long lastLedUpdate = 0;
    if (millis() - lastLedUpdate > 250) {  // Update every 250ms
      lastLedUpdate = millis();
      // Toggle the recording indicator to create blinking effect
      static bool toggleState = false;
      toggleState = !toggleState;

      // Update LED with animation
      M5.Lcd.fillCircle(160, 120, 40, toggleState ? RED : 0x8800);
    }
    size_t bytesRead = 0;
    uint8_t tempBuffer[512];
    esp_err_t err = i2s_read(I2S_PORT, tempBuffer, sizeof(tempBuffer), &bytesRead, 100 / portTICK_PERIOD_MS);
    if (err == ESP_OK && bytesRead > 0) {
      // Pre-process audio to ensure we have actual signal
      // For PDM microphones, we may need to convert and scale the samples
      int16_t* samples = (int16_t*)tempBuffer;
      size_t sampleCount = bytesRead / 2;
      preprocessAudio(samples, sampleCount);

      // Occasionally print sample values for debugging
      /* static unsigned long lastDebugTime = 0;
      if (millis() - lastDebugTime > 1000) {
        lastDebugTime = millis();
        int16_t maxSample = 0;
        for (size_t i = 0; i < min(sampleCount, (size_t)100); i++) {
          if (abs(samples[i]) > maxSample) maxSample = abs(samples[i]);
        }
        // Serial.printf("Audio max amplitude: %d\n", maxSample);
      } */

      // Simple audio processing - scale up if needed
      for (int i = 0; i < sampleCount; i++) {
        // For PDM microphones, sometimes we need to scale up the values
        samples[i] = samples[i] * 4; // Amplify the signal

        // Debug: occasionally print some sample values to verify audio is being captured
        //if (i == 0 && millis() % 1000 < 20) {
        //  Serial.printf("Audio sample values: %d, %d, %d, %d\n",
        //              samples[0], samples[1], samples[2], samples[3]);
        //}
      }

      // Parse server configuration for UDP & TCP connections
      String host;
      uint16_t port;
      bool useTLS;
      parseServerConfig(globalCreds.server, host, port, useTLS);

      // Send directly via UDP
      udp.beginPacket(host.c_str(), 6980);
      udp.write(tempBuffer, bytesRead);
      udp.endPacket();

      // Save locally if there's room
      if (audioBufferOffset + bytesRead < AUDIO_BUFFER_SIZE) {
        memcpy(audioBuffer + audioBufferOffset, tempBuffer, bytesRead);
        audioBufferOffset += bytesRead;
      }
    } else if (err != ESP_OK) {
        Serial.printf("I2S read error: %d\n", err);
    }
    delay(10);
  }

  // --- End Recording & Start Transcription ---
  if (isRecording && M5.BtnA.wasReleased()) { // Using BtnA instead of Btn

    Serial.println("Button released: Stopping recording.");
    isRecording = false;

    // Stop mic mode
    i2s_driver_uninstall(I2S_PORT);

    // Check for a cancellation gesture immediately after release
    unsigned long cancelStart = millis();
    bool cancelTranscription = false;
    while (millis() - cancelStart < 200) { // 200ms window
      M5.update();
      if (M5.BtnA.isPressed()) { // Using BtnA instead of Btn
        cancelTranscription = true;
        break;
      }
      delay(10);
    }

    if (cancelTranscription) {
      Serial.println("Transcription cancelled by user. Restarting recording.");
      resetAudioState();
      return;
    }

    // Switch to speaker mode for server playback
    Serial.println("Switching to speaker mode...");
    initI2SSpeaker();
    delay(100);

    currentState = WAITING;
    updateLED(currentState);

    // --- Connect to Server & Send Audio ---
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected. Attempting reconnection...");
        if (!connectToWiFi(globalCreds.ssid, globalCreds.password)) {
            Serial.println("Wi-Fi reconnect failed. Aborting connection to server.");
            resetAudioState();
            return;
        }
    }

    // Parse server configuration for TCP connection
    String host;
    uint16_t port;
    bool useTLS;
    parseServerConfig(globalCreds.server, host, port, useTLS);

    if (client.connect(host.c_str(), port)) {
      Serial.println("Connected to server.");
      delay(100);

      // First send the configuration JSON
      DynamicJsonDocument configDoc(256);
      configDoc["includeText"] = true;
      configDoc["protocolVersion"] = "1.1";          // Increased version
      configDoc["audioFormat"] = "pcm16";
      configDoc["sampleRate"] = 16000;
      configDoc["channels"] = 1;
      //configDoc["separateResponses"] = true;         // Request clear separation between text and audio
      String configStr;
      serializeJson(configDoc, configStr);
      configStr += "\r\n\r\n";                       // Add double newline as separator
      //client.println(configStr);
      delay(200); // Short delay to ensure config is processed

      // Then send the audio data
      client.write(audioBuffer, audioBufferOffset);

      // --- Playback Phase with Cancellation Support ---
      bool jsonProcessed = false;  // Flag to track if we've processed the JSON

      while (client.connected()) {
        M5.update();

        // Handle scrolling
        if (responseText.length() > 0 && hasDisplayedText) { //!isRecording) { //hasDisplayedText) {
          // Get wrapped lines count for accurate calculations
          std::vector<String> wrappedLines;
          wordWrapText(responseText, 25, wrappedLines);
          int totalLines = wrappedLines.size();

          if (checkButtonPress(M5.BtnB)) {
            Serial.println("↑ Scrolling UP during playback");
            if (textScrollOffset > 0) {
              textScrollOffset--;
              displayText(responseText, textScrollOffset);
            }
          }

          if (checkButtonPress(M5.BtnC)) {
            Serial.println("↓ Scrolling DOWN during playback");
            if (textScrollOffset < max(0, totalLines - maxLines)) {
              textScrollOffset++;
              displayText(responseText, textScrollOffset);
            }
          }
        }

        if (M5.BtnA.isPressed()) {
          Serial.println("Interruption detected, starting new recording...");
          client.stop();
          resetAudioState();
          return;
        }

        if (client.available()) {
          if (currentState != PLAYING) {
            currentState = PLAYING;
            updateLED(currentState);
            Serial.println("Starting playback...");
            hasDisplayedText = false;
            responseText = "";
            jsonProcessed = false;
            // Reset audio buffer
            audioPlaybackBufferPos = 0;
          }

          // Buffer to read data in chunks
          uint8_t buffer[2048];
          int len = client.read(buffer, sizeof(buffer));

          if (len > 0) {
            // Check if this looks like JSON or audio data
            if (buffer[0] == '{' && !jsonProcessed) {
              // This is likely JSON data
              // Ensure null termination for parsing
              buffer[min(len, 2047)] = 0;

              Serial.println("Received JSON data:");
              Serial.println((char*)buffer);

              // Parse JSON
              DynamicJsonDocument doc(4096);  // Larger buffer for JSON
              DeserializationError error = deserializeJson(doc, (char*)buffer);

              if (!error) {
                if (doc.containsKey("type") && doc["type"] == "text") {
                  // Process text message
                  responseText = doc["message"].as<String>();  // Note the  type specifier
                  displayText(responseText, 0);
                  hasDisplayedText = true;
                  jsonProcessed = true;

                  Serial.print("Text message: ");
                  Serial.println(responseText);
                } else if (doc.containsKey("error")) {
                  // Handle error
                  String errorMsg = doc["error"].as<String>();  // Note the  type specifier
                  M5.Lcd.fillScreen(BLACK);
                  M5.Lcd.setCursor(20, 100);
                  M5.Lcd.setTextColor(RED);
                  M5.Lcd.println(errorMsg);

                  Serial.print("Error: ");
                  Serial.println(errorMsg);
                }
              } else {
                Serial.print("JSON parse error: ");
                Serial.println(error.c_str());
              }
            }
            else if (isAudioData(buffer, len)) {
              // This is binary audio data
              Serial.printf("Processing %d bytes of audio data\n", len);

              // Update audio activity tracking
              lastAudioDataTime = millis();

              // Use buffered approach for audio playback
              // If there's room in the buffer, add the data
              if (audioPlaybackBufferPos + len <= AUDIO_PLAYBACK_BUFFER_SIZE) {
                memcpy(audioPlaybackBuffer + audioPlaybackBufferPos, buffer, len);
                audioPlaybackBufferPos += len;
              } else {
                // Buffer is full, play what we have
                size_t bytesWritten = 0;
                esp_err_t result = i2s_write(I2S_PORT, audioPlaybackBuffer,
                                          audioPlaybackBufferPos, &bytesWritten,
                                          100 / portTICK_PERIOD_MS);

                if (result != ESP_OK) {
                  Serial.printf("I2S write error: %d\n", result);
                }

                // Reset buffer and add new data
                audioPlaybackBufferPos = 0;
                if (len <= AUDIO_PLAYBACK_BUFFER_SIZE) {
                  memcpy(audioPlaybackBuffer, buffer, len);
                  audioPlaybackBufferPos = len;
                }
              }

              // If we have enough data or it's been a while, play the buffered audio
              unsigned long now = millis();
              if (audioPlaybackBufferPos > 1024 || now - lastAudioProcessTime > 50) {
                if (audioPlaybackBufferPos > 0) {
                  size_t bytesWritten = 0;
                  esp_err_t result = i2s_write(I2S_PORT, audioPlaybackBuffer,
                                            audioPlaybackBufferPos, &bytesWritten,
                                            100 / portTICK_PERIOD_MS);

                  // Reset buffer after playback
                  audioPlaybackBufferPos = 0;
                  lastAudioProcessTime = now;
                }
              }
            }
          }
        }
        delay(1);
      }

      if (responseText.length() > 0) {
        currentState = IDLE;  // Important: reset state to IDLE

        // Explicitly clean up the I2S system and reinitialize the mic
        i2s_driver_uninstall(I2S_PORT);
        delay(100);
        initI2SMic();

        displayText(responseText, textScrollOffset);
        Serial.println("Connection ended, showing response text only");
      }

      Serial.println("Audio stream ended.");
      client.stop();
      } else {
        Serial.println("Failed to connect to transcription server.");
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(20, 100);
        M5.Lcd.setTextColor(RED);
        M5.Lcd.println("Server connection failed!");
        delay(2000);
      }
  }

  if (responseText.length() > 0 && hasDisplayedText) { //!isRecording) { //hasDisplayedText) {
    // Get wrapped lines count for accurate calculations
    std::vector<String> wrappedLines;
    wordWrapText(responseText, 25, wrappedLines);
    int totalLines = wrappedLines.size();

          if (checkButtonPress(M5.BtnB)) {
            if (textScrollOffset > 0) {
              textScrollOffset--;
              displayText(responseText, textScrollOffset);
            }
          }

          if (checkButtonPress(M5.BtnC)) {
            if (textScrollOffset < max(0, totalLines - maxLines)) {
              textScrollOffset++;
              displayText(responseText, textScrollOffset);
            }
          }
  }

  // Sleep mode check - only when idle and after timeout
  if (currentState == IDLE && !isRecording &&
      millis() - lastActivityTime > SLEEP_TIMEOUT_MS &&
      WiFi.getMode() != WIFI_AP && WiFi.getMode() != WIFI_AP_STA) {
    goToSleep();
  }

  // Update activity tracking when buttons are pressed
  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
    lastActivityTime = millis();
  }

  static unsigned long lastButtonCheck = 0;
  if (millis() - lastButtonCheck > 500) {
    lastButtonCheck = millis();

    // Display a small indicator that the device is ready to record
    if (currentState == IDLE && !isRecording) {
      // Small dot in the corner to show ready state
      static bool toggleDot = false;
      toggleDot = !toggleDot;
      if (toggleDot) {
        M5.Lcd.fillCircle(55, 230, 3, GREEN);
      } else {
        M5.Lcd.fillCircle(55, 230, 3, BLACK);
      }

      // Show sleep countdown in the bottom right corner
      unsigned long timeRemaining = 0;
      if (millis() < lastActivityTime + SLEEP_TIMEOUT_MS) {
        timeRemaining = (lastActivityTime + SLEEP_TIMEOUT_MS - millis()) / 1000; // seconds
      }

      // Only update the display when the value changes (every second)
      static unsigned long lastTimeRemaining = 0;
      if (timeRemaining != lastTimeRemaining) {
        lastTimeRemaining = timeRemaining;

        // Clear previous text
        M5.Lcd.fillRect(260, 230, 90, 20, BLACK);

        // Display countdown if less than 30 seconds
        if (timeRemaining <= 10) {
          M5.Lcd.setCursor(260, 230);
          M5.Lcd.setTextSize(1);
          M5.Lcd.setTextColor(ORANGE);
          M5.Lcd.printf("Sleep: %lus", timeRemaining);
        }
      }
    }
  }

}

// New function to put device to sleep
void goToSleep() {
  Serial.println("Going to sleep mode...");

  // Show sleep message
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 100);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.println(" Going to light sleep...");
  M5.Lcd.println(" Press button A to wake");
  delay(1000);

  // Clean up audio but maintain WiFi
  i2s_driver_uninstall(I2S_PORT);
  M5.Axp.SetSpkEnable(false);

  // Don't disconnect WiFi - keep it alive
  Serial.println("Maintaining WiFi during sleep");

  // Power management for Core2
  M5.Axp.SetLDO2(false);      // Turn off LCD backlight
  // M5.Axp.SetLDO3(false);    // Turn off vibration motor
  M5.Axp.SetLcdVoltage(2500); // Lower LCD voltage

  // Turn off screen
  M5.Lcd.sleep();

  // Configure wakeup source (BtnA - GPIO39)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, 0); // Button A is on GPIO39, wake on LOW

  // Enable WiFi to stay connected during light sleep
  esp_wifi_set_ps(WIFI_PS_NONE); // Disable WiFi power saving

  // Use light sleep instead of deep sleep to maintain WiFi
  Serial.println("Entering light sleep mode");
  esp_light_sleep_start();

  // Code will continue here after waking from light sleep
  Serial.println("Woke from light sleep");

  // Call the wake function directly - light sleep returns to this point
  wakeFromSleep();
}

// New function to wake up properly - optimized for light sleep
void wakeFromSleep() {
  // Re-enable power management for Core2
  M5.Axp.SetLDO2(true);         // Turn LCD backlight back on
  M5.Lcd.wakeup();              // Wake up the LCD
  M5.Axp.SetLcdVoltage(3300);   // Restore LCD voltage

  // Check WiFi - should still be connected in light sleep
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 80);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(WHITE);

  if (WiFi.status() == WL_CONNECTED) {
    M5.Lcd.println("WiFi maintained!");
    M5.Lcd.println("IP: " + WiFi.localIP().toString());
    Serial.println("WiFi still connected: " + WiFi.localIP().toString());

    // Make sure UDP is initialized
    udp.begin(6980);
    Serial.println("UDP initialized on port 6980");
  } else {
    // Light reconnect attempt with minimal hardware reset
    M5.Lcd.println("Reconnecting WiFi...");
    Serial.println("Light reconnection attempt");

    // Do a minimal reconnect without full reset
    WiFi.disconnect();
    delay(100);
    WiFi.begin(globalCreds.ssid.c_str(), globalCreds.password.c_str());

    // Short wait for reconnection
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 5000) {
      delay(500);
      M5.Lcd.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      M5.Lcd.setCursor(0, 110);
      M5.Lcd.println("WiFi reconnected!");
      M5.Lcd.println("IP: " + WiFi.localIP().toString());

      // Initialize UDP
      udp.begin(6980);
    } else {
      M5.Lcd.setCursor(0, 110);
      M5.Lcd.println("WiFi failed to reconnect");
      M5.Lcd.println("Status: " + String(WiFi.status()));

      // Still try to initialize UDP
      udp.begin(6980);
    }
  }

  // Reinitialize I2S microphone
  initI2SMic();

  // Reset state and show ready screen
  currentState = IDLE;
  updateLED(currentState);

  // Update activity timestamp
  lastActivityTime = millis();
}
