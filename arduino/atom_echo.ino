#include <driver/i2s.h>
#include "M5Atom.h"
#include "AudioTools.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <FastLED.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// Global web server (on port 80)
WebServer server(80);

///////////////////////
// LED Status Setup
///////////////////////
enum AudioState {
  IDLE,
  RECORDING,
  WAITING,
  PLAYING
};

AudioState currentState = IDLE;

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

void blinkLED(CRGB color, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    M5.dis.drawpix(0, color);
    delay(delayMs);
    M5.dis.clear();
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
    blinkLED(CRGB(0, 255, 0), 3, 300); // Green blinks for success
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
      M5.dis.clear();
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
  // Wait for 3 seconds to see if the button is pressed
  unsigned long start = millis();
  Serial.println("Hold button for 5 seconds to force config mode...");
  while(millis() - start < 5000) {
    M5.update();
    if(M5.Btn.isPressed()) {
      Serial.println("Force config mode triggered!");
      return true;
    }
    delay(10);
  }
  return false;
}

void updateLED(AudioState state) {
  switch(state) {
    case RECORDING:
      M5.dis.drawpix(0, CRGB(255, 0, 0));  // Red for recording
      break;
    case WAITING:
      M5.dis.drawpix(0, CRGB(0, 0, 255));  // Blue for waiting for response
      break;
    case PLAYING:
      M5.dis.drawpix(0, CRGB(0, 255, 0));  // Green for playing audio
      break;
    case IDLE:
    default:
      M5.dis.clear();
      break;
  }
}

///////////////////////
// Pin Definitions
///////////////////////
// Built-in mic pins (PDM)
#define MIC_BCK_PIN     14
#define MIC_WS_PIN      33
#define MIC_DATA_IN_PIN 23

// Speaker pins
#define SPK_BCK_PIN     19
#define SPK_WS_PIN      33
#define SPK_DATA_OUT_PIN 22

// I2S port
#define I2S_PORT I2S_NUM_0

///////////////////////
// Global Variables
///////////////////////
#define AUDIO_BUFFER_SIZE (1024 * 256)
uint8_t* audioBuffer = NULL;
int audioBufferOffset = 0;
bool isRecording = false;

///////////////////////
// I2S Mic (RX/PDM) Setup
///////////////////////
void initI2SMic() {
  // Clean up any previous I2S setup
  if (i2s_driver_uninstall(I2S_PORT) == ESP_ERR_INVALID_STATE) {
    Serial.println("I2S driver not installed, skipping uninstallation.");
  }

  // Configure I2S for mic capture with PDM enabled
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    // Use only one channel (typically left for the built-in mic)
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed to install I2S driver: %d\n", err);
  }

  // Configure I2S pins for mic (we only care about data_in)
  i2s_pin_config_t pin_config = {
    .bck_io_num = MIC_BCK_PIN,
    .ws_io_num  = MIC_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = MIC_DATA_IN_PIN
  };

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S pins: %d\n", err);
  }

  err = i2s_set_clk(I2S_PORT, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S clock: %d\n", err);
  }
}

///////////////////////
// I2S Speaker (TX) Setup
///////////////////////
void initI2SSpeaker() {
  // Configure I2S for speaker output (TX mode)
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = 60,
    .use_apll = false,
    .tx_desc_auto_clear = true  // Auto clear underflow
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("Failed to install I2S driver for speaker: %d\n", err);
  }

  // Configure I2S pins for speaker (only data out is used)
  i2s_pin_config_t pin_config = {
    .bck_io_num = SPK_BCK_PIN,
    .ws_io_num  = SPK_WS_PIN,
    .data_out_num = SPK_DATA_OUT_PIN,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S pins for speaker: %d\n", err);
  }

  err = i2s_set_clk(I2S_PORT, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  if (err != ESP_OK) {
    Serial.printf("Failed to set I2S clock for speaker: %d\n", err);
  }
}

///////////////////////
// Setup Function
///////////////////////
void setup() {
  M5.begin(true, false, true);
  Serial.begin(115200);
  M5.dis.clear();

  // Check if we want to force config mode
  bool forceConfig = forceConfigMode();

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
      while(1);
    }
  }
  Serial.printf("audioBuffer allocated, size: %d bytes\n", AUDIO_BUFFER_SIZE);


  WifiCredentials creds;
  bool configMode = false;

  // If forced, skip loading config
  if (forceConfig || !loadWifiConfig(creds)) {
    configMode = true;
  } else {
    globalCreds = creds;
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
    Serial.println("Initializing I2S in mic (PDM) mode...");
    initI2SMic();
  }
}

// Add a new function to properly reset the audio state
void resetAudioState() {
  isRecording = false;
  audioBufferOffset = 0;
  currentState = IDLE;
  updateLED(currentState);
  delay(100);  // Give hardware time to stabilize
  initI2SMic();
}

///////////////////////
// Main Loop
///////////////////////

void loop() {

  // If in configuration mode (AP), handle web client requests
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
    // Blink red LED
    static unsigned long lastBlink = 0;
    static bool ledOn = false;
    if (millis() - lastBlink >= 500) { // change state every 500ms
      lastBlink = millis();
      if (ledOn) {
        M5.dis.clear();
        ledOn = false;
      } else {
        M5.dis.drawpix(0, CRGB(255, 0, 0));
        ledOn = true;
      }
    }
    server.handleClient();
    delay(1); // small delay to ease CPU load
    return; // Skip audio tasks
  }

  M5.update();

  // --- Recording Phase ---
  if (M5.Btn.wasPressed()) {
    Serial.println("Button pressed: Starting recording & VBAN streaming.");

    // Reset everything first
    if (currentState == PLAYING) {
      // If we were playing, stop the client connection
      client.stop();
      delay(100);  // Give network time to clean up
    }

    resetAudioState();

    isRecording = true;
    currentState = RECORDING;
    updateLED(currentState);
  }

  if (isRecording && M5.Btn.isPressed()) {
    size_t bytesRead = 0;
    uint8_t tempBuffer[512];
    esp_err_t err = i2s_read(I2S_PORT, tempBuffer, sizeof(tempBuffer), &bytesRead, 100 / portTICK_PERIOD_MS);
    if (err == ESP_OK && bytesRead > 0) {
      // Parse server configuration for UDP & TCP connections
      String host;
      uint16_t port;
      bool useTLS;
      parseServerConfig(globalCreds.server, host, port, useTLS);

      // Send directly via UDP
        udp.beginPacket(host.c_str(), 6980);
        udp.write(tempBuffer, bytesRead);
        udp.endPacket();

        //Serial.printf("Sent %d bytes\n", bytesRead);

      // Save locally if there's room
      if (audioBufferOffset + bytesRead < AUDIO_BUFFER_SIZE) {
        memcpy(audioBuffer + audioBufferOffset, tempBuffer, bytesRead);
        audioBufferOffset += bytesRead;
      }
    }
    delay(10);
  }

  // --- End Recording & Start Transcription ---
  if (isRecording && M5.Btn.wasReleased()) {
    Serial.println("Button released: Stopping recording.");
    isRecording = false;

    // Stop mic mode
    i2s_driver_uninstall(I2S_PORT);

    // Check for a cancellation gesture immediately after release
    unsigned long cancelStart = millis();
    bool cancelTranscription = false;
    while (millis() - cancelStart < 200) { // 200ms window
      M5.update();
      if (M5.Btn.isPressed()) {
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
    //Serial.print("Checking Wi-Fi before connecting to transcription server...");
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
    // client.setInsecure(); // Use this if you're not validating certificates
    if (client.connect(host.c_str(), port)) {
      Serial.println("Connected to server.");

      // First send the configuration JSON
      DynamicJsonDocument configDoc(64);
      configDoc["includeText"] = false;
      String configStr;
      serializeJson(configDoc, configStr);
      client.println(configStr);
      delay(100); // Short delay to ensure config is processed

      // Then send the audio data
      client.write(audioBuffer, audioBufferOffset);

      // --- Playback Phase with Cancellation Support ---
      while (client.connected()) {
        M5.update();

        if (M5.Btn.isPressed()) {
          Serial.println("Interruption detected, starting new recording...");
          client.stop();
          resetAudioState();
          return;
        }

        if (client.available()) {
          if (currentState != PLAYING) {
            currentState = PLAYING;
            updateLED(currentState);
          }

          uint8_t outBuffer[4096];
          int len = client.read(outBuffer, sizeof(outBuffer));
          if (len > 0) {
            size_t bytesWritten = 0;
            while (bytesWritten < len) {
              M5.update();
              if (M5.Btn.isPressed()) {
                Serial.println("Interruption during playback, starting new recording...");
                client.stop();
                resetAudioState();
                return;
              }

              size_t chunkWritten = 0;
              esp_err_t result = i2s_write(I2S_PORT, outBuffer + bytesWritten, len - bytesWritten, &chunkWritten, 10 / portTICK_PERIOD_MS);
              if (result != ESP_OK) {
                Serial.printf("I2S write error: %d\n", result);
                break;
              }
              bytesWritten += chunkWritten;
              delay(1);
            }
          }
        }
        delay(1);
      }

      Serial.println("Audio stream ended.");
      client.stop();
    } else {
      Serial.println("Failed to connect to transcription server.");
    }

    // Reset to recording mode
    resetAudioState();
    delay(500);  // Short delay before the next recording cycle
  }
}
