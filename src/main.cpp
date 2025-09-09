#include <config.h>

WiFiManager wm;
WiFiClient espClient;
PubSubClient client(espClient);

TaskHandle_t networkTaskHandle;
TaskHandle_t mainTaskHandle;
TaskHandle_t wifiResetTaskHandle;
TaskHandle_t otaTaskHandle = NULL;

bool wifiResetFlag = false;

// Function to reconnect to WiFi
void reconnectWiFi() {
  // digitalWrite(LED_PIN, HIGH);
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiAttemptCount > 0) {
      DEBUG_PRINTLN("Attempting WiFi connection...");
      WiFi.begin();  // Use saved credentials
      wifiAttemptCount--;
      DEBUG_PRINTLN("Remaining WiFi attempts: " + String(wifiAttemptCount));
      // vTaskDelay(WIFI_ATTEMPT_DELAY / portTICK_PERIOD_MS);
      vTaskDelay(pdMS_TO_TICKS(WIFI_ATTEMPT_DELAY));
    } else if (wifiWaitCount > 0) {
      wifiWaitCount--;
      DEBUG_PRINTLN("WiFi wait... retrying in a moment");
      DEBUG_PRINTLN("Remaining WiFi wait time: " + String(wifiWaitCount) + " seconds");
      vTaskDelay(pdMS_TO_TICKS(WIFI_WAIT_DELAY));
    } else {
      wifiAttemptCount = WIFI_ATTEMPT_COUNT;
      wifiWaitCount = WIFI_WAIT_COUNT;
      maxWifiAttempts--;
      if (maxWifiAttempts <= 0) {
        DEBUG_PRINTLN("Max WiFi attempt cycles exceeded, restarting...");
        ESP.restart();
      }
    }
  }
}
//=========================================

// Function to reconnect MQTT
void reconnectMQTT() {
  if (!client.connected()) {
    #if Fast_LED
    leds[0] = CRGB::Yellow;
    FastLED.show();
    #endif

    char clientId[24];
    snprintf(clientId, sizeof(clientId), "dma_amr_%04X%04X%04X", random(0xffff), random(0xffff), random(0xffff));

    if (mqttAttemptCount > 0) {
      DEBUG_PRINTLN("Attempting MQTT connection...");
      if (client.connect(clientId, mqtt_user, mqtt_password)) {
        DEBUG_PRINTLN("MQTT connected");

        #if Fast_LED
        leds[0] = CRGB::Black;
        FastLED.show();
        #endif
        char topic[48];

        snprintf(topic, sizeof(topic), "%s/%s", mqtt_sub_topic, DEVICE_ID);
        client.subscribe(topic);
      } else {
        DEBUG_PRINTLN("MQTT connection failed");
        mqttAttemptCount--;
        vTaskDelay(pdMS_TO_TICKS(MQTT_ATTEMPT_DELAY));
      }
    } else {
      DEBUG_PRINTLN("Max MQTT attempts exceeded, restarting...");
      ESP.restart();
    }
  }
}
//===============================================

//MQTT Callback Function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  // Remove all spaces (before, after, and in the middle)
  message.trim();
  message.replace(" ", "");

  DEBUG_PRINTLN("Message arrived on topic: " + String(topic));
  DEBUG_PRINTLN("Message content: " + message);

  if (message.startsWith("set:sensor_type=")) {
    int equalIndex = message.indexOf('=');
    if (equalIndex > 0) {
      String valStr = message.substring(equalIndex + 1);
      valStr.trim();

      setSensorType = valStr.toInt() != 0;  // safer conversion: 0=false, anything else=true

      // Save to Preferences as a boolean
      preferences.begin("WaterInfo", false);
      preferences.putBool("sensor_type", setSensorType);
      preferences.end();

      Serial.print("Saved sensor_type: ");
      Serial.println(setSensorType ? "true" : "false");
    }
  }


  if (message.startsWith("set:water")) {
    int equalIndex = message.indexOf('=');
    if (equalIndex > 0) {
      String valStr = message.substring(equalIndex + 1);
      valStr.trim();

      totalWater = valStr.toFloat();

      // Save to Preferences
      preferences.begin("WaterInfo", false);
      preferences.putFloat("totalWater", totalWater);
      preferences.end();
      Serial.println("Saved totalWater: " + String(totalWater));

      setWaterValue = true;
    }
  }

  if (message.startsWith("set:flow=")) {
    int equalIndex = message.indexOf('=');
    if (equalIndex > 0) {
      String valStr = message.substring(equalIndex + 1);
      valStr.trim();

      K = valStr.toInt();

      // Save to Preferences
      preferences.begin("WaterInfo", false);
      preferences.putInt("flow", K);
      preferences.end();
      Serial.println("Saved totalWater: " + String(totalWater));

      if (client.connected()) {
        bool acLineState = digitalRead(AC_LINE_PIN);
        char hb_data[64];
        snprintf(hb_data,sizeof(hb_data), "%s,C:%d,S:0,TW:%d,F:%d,connected",
          DEVICE_ID,
          acLineState ? 1 : 0,
          (int)totalWater,
          K
        );

        client.publish(mqtt_pub_topic, hb_data);
        DEBUG_PRINTLN("Heartbeat sent Successfully");

        #if Fast_LED
        leds[0] = CRGB::Blue;
        FastLED.show();
        vTaskDelay(pdMS_TO_TICKS(100)); // Short delay to indicate status
        leds[0] = CRGB::Black;
        FastLED.show();
        #endif
      } else {
        DEBUG_PRINTLN("Failed to publish Heartbeat on MQTT");
      }
    }
  }

  if(message == "query:water"){
    // Respond with current water value
    char waterData[32];
    snprintf(waterData, sizeof(waterData), "%s,Water:%d", DEVICE_ID, (int)totalWater);
    client.publish(mqtt_pub_topic, waterData);
  }


  
  if (message == "ping") {
    DEBUG_PRINTLN("Request for ping");
    char pingData[100]; // Increased size for additional info
    snprintf(pingData, sizeof(pingData), "%s,%s,%s,%d,%d",
      DEVICE_ID, WiFi.SSID().c_str(),
      WiFi.localIP().toString().c_str(), WiFi.RSSI(), HB_INTERVAL);
    client.publish(mqtt_pub_topic, pingData);

    DEBUG_PRINT("Sent ping response to MQTT: ");
    DEBUG_PRINTLN(pingData);
  }

  if (message == "update_firmware") {
    if (otaTaskHandle == NULL) {
      xTaskCreatePinnedToCore(otaTask, "OTA Task", 8*1024, NULL, 1, &otaTaskHandle, 1);
    } else {
      Serial.println("OTA Task already running.");
    }
  }
}
//===================================

// Start Network Task
void networkTask(void *param) {
  WiFi.mode(WIFI_STA);
  WiFi.begin();

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!client.connected()) {
        reconnectMQTT();
      }
    } else {
      reconnectWiFi();
    }
    client.loop();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
//================================

//Start WiFi reset task
void wifiResetTask(void *param) {
  for (;;) {
    if (digitalRead(WIFI_RESET_BUTTON_PIN) == LOW) {
      unsigned long pressStartTime = millis();
      DEBUG_PRINTLN("Button Pressed....");

      #if Fast_LED
      leds[0] = CRGB::Blue;
      FastLED.show();
      #endif

      while (digitalRead(WIFI_RESET_BUTTON_PIN) == LOW) {
        if (millis() - pressStartTime >= 5000) {
          DEBUG_PRINTLN("5 seconds holding time reached, starting WiFiManager...");
          vTaskSuspend(networkTaskHandle);
          vTaskSuspend(mainTaskHandle);
          wm.resetSettings();
          wm.autoConnect("DMA_Smart_Switch");
          ESP.restart();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    } else {
      #if Fast_LED
      leds[0] = CRGB::Black;
      FastLED.show();
      #endif
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
//=================================

// OTA Update Task
void otaTask(void *parameter) {
  Serial.println("Starting OTA update...");

  HTTPClient http;
  http.begin(ota_url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();
    Serial.printf("Content-Length: %d bytes\n", contentLength);
    
    if (Update.begin(contentLength)) {
      Update.writeStream(http.getStream());
      if (Update.end() && Update.isFinished()) {
        Serial.println("OTA update completed. Restarting...");
        char message[64];  
        snprintf(message, sizeof(message), "%s,OTA update successful", DEVICE_ID);  
        client.publish(mqtt_pub_topic, message);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        http.end();
        ESP.restart();
      } else {
        Serial.println("OTA update failed!");
        char message[64];  
        snprintf(message, sizeof(message), "%s,OTA Update Failed!", DEVICE_ID);  
        client.publish(mqtt_pub_topic, message);
      }
    } else {
      Serial.println("OTA begin failed!");
      char message[64];  
      snprintf(message, sizeof(message), "%s,OTA Begin Failed!", DEVICE_ID);  
      client.publish(mqtt_pub_topic, message);
    }
  } else {
    Serial.printf("HTTP request failed, error: %s\n", http.errorToString(httpCode).c_str());
    char message[64];  
    snprintf(message, sizeof(message), "%s,HTTP Request Failed", DEVICE_ID);  
    client.publish(mqtt_pub_topic, message);
  }

  http.end();
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  
  ESP.restart();

  otaTaskHandle = NULL;  
  vTaskDelete(NULL);
}


// Start Main Task
void mainTask(void *param) {
  unsigned long lastReceivedTime = 0;  
  unsigned long lastReceivedCode = 0;

  for (;;) {

    unsigned long currentMillis = millis();
    if (currentMillis - lastNow >= sampleInterval) {
      lastNow = currentMillis;

      rawValue[9] = analogRead(Analog_Pin);
      lcd.setCursor(0, 1);
      lcd.print("     "); // Clear previous value
      lcd.setCursor(0, 1);
      lcd.print(rawValue[9]);

      // Shift array left by 1
      for (int i = 0; i < 9; i++) {
        rawValue[i] = rawValue[i + 1];
      }

      long sum = 0;
      for (int i = 0; i < 10; i++) {    
        sum += rawValue[i];
      }
      avgValue = sum / 10;   // averaged analog value

      rawValue[9] = analogRead(Analog_Pin);
      lcd.setCursor(5, 1);
      lcd.print("     "); // Clear previous value
      lcd.setCursor(5, 1);
      lcd.print(avgValue);

      if(NC_Sensor == false){
        if(avgValue > 3000 && swt == false){
        swt = true;
        totalWater = totalWater + (float)(K/1000.0);
        
        preferences.begin("WaterInfo", false);
        preferences.putFloat("totalWater", totalWater);
        preferences.end();
        Serial.println("Saved totalWater: " + String(totalWater));

        lcd.setCursor(0, 0);
        lcd.print("TW: ");
        lcd.setCursor(4, 0);
        lcd.print(totalWater,1);
      }
      else if(avgValue < 1000 && swt == true){
        swt = false;
      }
    }

    if(NC_Sensor == true){
        if(avgValue < 1000 && swt == false){
        swt = true;
        totalWater = totalWater + (float)(K/1000.0);
        
        preferences.begin("WaterInfo", false);
        preferences.putFloat("totalWater", totalWater);
        preferences.end();
        Serial.println("Saved totalWater: " + String(totalWater));

        lcd.setCursor(0, 0);
        lcd.print("TW: ");
        lcd.setCursor(4, 0);
        lcd.print(totalWater,1);
      }
      else if(avgValue > 3000 && swt == true){
        swt = false;
      }
    }
  }

    if(setWaterValue == true){
      setWaterValue = false;
      lcd.setCursor(0, 0);
      lcd.print("TW: ");
      lcd.setCursor(4, 0);
      lcd.print(totalWater,1);
    }

    bool acLineState = digitalRead(AC_LINE_PIN);

    // PUB = 1146002409090027,C:1,S:0,TW:37295200,F:100,connected
    static unsigned long last_hb_send_time = 0;
    unsigned long now = millis();

    // **Send Heartbeat Every HB_INTERVAL**
    if (now - last_hb_send_time >= HB_INTERVAL) {
      last_hb_send_time = now;

      if (client.connected()) {
        char hb_data[64];
        snprintf(hb_data,sizeof(hb_data), "%s,C:%d,S:0,TW:%d,F:%d,connected",
          DEVICE_ID,
          acLineState ? 1 : 0,
          (int)totalWater,
          K
        );

        client.publish(mqtt_pub_topic, hb_data);
        DEBUG_PRINTLN("Heartbeat sent Successfully");

        #if Fast_LED
        leds[0] = CRGB::Blue;
        FastLED.show();
        vTaskDelay(pdMS_TO_TICKS(100)); // Short delay to indicate status
        leds[0] = CRGB::Black;
        FastLED.show();
        #endif
      } else {
        DEBUG_PRINTLN("Failed to publish Heartbeat on MQTT");
      }
    }
        
    vTaskDelay(pdMS_TO_TICKS(10)); // Keep FreeRTOS responsive
  }
}


void setup() {
  Serial.begin(115200);

  preferences.begin("device_data", false);  // Open Preferences (NVS)
  static String device_id; // Static variable to persist scope
  
  #if CHANGE_DEICE_ID
    // Construct new device ID
    device_id = String(WORK_PACKAGE) + GW_TYPE + FIRMWARE_UPDATE_DATE + DEVICE_SERIAL;
    
    // Save device ID to Preferences
    preferences.putString("device_id", device_id);
    Serial.println("Device ID updated in Preferences: " + device_id);
  #else
    // Restore device ID from Preferences
    device_id = preferences.getString("device_id", "UNKNOWN");
    Serial.println("Restored Device ID from Preferences: " + device_id);
  #endif
  
  DEVICE_ID = device_id.c_str(); // Assign to global pointer

  preferences.end();
  
  DEBUG_PRINT("Device ID: ");
  DEBUG_PRINTLN(DEVICE_ID);
  
  #if Fast_LED
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  leds[0] = CRGB::Black;
  FastLED.show();
  #endif

  pinMode(WIFI_RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(Analog_Pin, INPUT);

  preferences.begin("WaterInfo", false);
  totalWater = preferences.getFloat("totalWater", 0.0);
  Serial.println("Restored totalWater: " + String(totalWater));
  K = preferences.getInt("flow", 100);
  Serial.println("Restored K (flow): " + String(K));
  NC_Sensor = preferences.getBool("sensor_type", true); // Default to true if
  Serial.println("Restored sensor_type: " + String(NC_Sensor ? "true" : "false"));
  preferences.end();
  
  if(NC_Sensor == true){

    // Clear rawValue buffer
    for (int i = 0; i <= 9; i++) {
      rawValue[i] = 4095;
    }
  }
  else{
    // Clear rawValue buffer
    for (int i = 0; i <= 9; i++) {
      rawValue[i] = 0;
    }
  } 


  // Start LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("AMR Version-5.0");
  lcd.setCursor(1, 1);
  lcd.print("Powered by DMA");

  delay(3000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TW: ");
  lcd.setCursor(4, 0);
  lcd.print(totalWater,1);
  lcd.setCursor(0, 1);

  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);

  xTaskCreatePinnedToCore(networkTask, "Network Task", 8*1024, NULL, 1, &networkTaskHandle, 0);
  xTaskCreatePinnedToCore(mainTask, "Main Task", 16*1024, NULL, 1, &mainTaskHandle, 1);
  xTaskCreatePinnedToCore(wifiResetTask, "WiFi Reset Task", 8*1024, NULL, 1, &wifiResetTaskHandle, 1);
}

void loop(){

}