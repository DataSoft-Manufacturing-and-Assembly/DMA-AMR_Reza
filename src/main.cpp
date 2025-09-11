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
  #if Fast_LED
    leds[0] = CRGB::Red;
    FastLED.show();
  #endif
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiAttemptCount > 0) {
      DEBUG_PRINTLN("Attempting WiFi connection...");
      WiFi.begin();  // Use saved credentials
      wifiAttemptCount--;
      DEBUG_PRINTLN("Remaining WiFi attempts: " + String(wifiAttemptCount));
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
  #if Fast_LED
    leds[0] = CRGB::Blue;
    FastLED.show();
    vTaskDelay(pdMS_TO_TICKS(250)); // Short delay to indicate status
    leds[0] = CRGB::Black;
    FastLED.show();
  #endif
  // Remove all spaces (before, after, and in the middle)
  message.trim();
  message.replace(" ", "");

  DEBUG_PRINTLN("Message arrived on topic: " + String(topic));
  DEBUG_PRINTLN("Message content: " + message);

  if (message.startsWith("set:nc_sensor=")) {
    int equalIndex = message.indexOf('=');
    if (equalIndex > 0) {
      String valStr = message.substring(equalIndex + 1);
      valStr.trim();

      NC_Sensor = valStr.toInt() != 0;  // safer conversion: 0=false, anything else=true

      // Save to Preferences as a boolean
      preferences.begin("WaterInfo", false);
      preferences.putBool("nc_sensor", NC_Sensor);
      preferences.end();

      Serial.print("Saved nc_sensor: ");
      Serial.println(NC_Sensor ? "true" : "false");

      if (client.connected()) {
        bool acLineState = digitalRead(AC_LINE_PIN);
        char hb_data[64];
        long totalWaterinLiter = totalWater*1000;
        snprintf(hb_data,sizeof(hb_data), "%s,C:%d,S:%d,TW:%d,F:%ld,connected",
          DEVICE_ID,
          acLineState ? 1 : 0,
          NC_Sensor ? 1 : 0,
          totalWaterinLiter,
          K
        );

        client.publish(mqtt_pub_topic, hb_data);
        DEBUG_PRINTLN("Heartbeat sent Successfully");

        #if Fast_LED
          leds[0] = CRGB::Blue;
          FastLED.show();
          vTaskDelay(pdMS_TO_TICKS(500)); // Short delay to indicate status
          leds[0] = CRGB::Black;
          FastLED.show();
        #endif
      } else {
        DEBUG_PRINTLN("Failed to publish Heartbeat on MQTT");
      }

    }
  }

  if (message.startsWith("set:water")) {
    int equalIndex = message.indexOf('=');
    if (equalIndex > 0) {
      String valStr = message.substring(equalIndex + 1);
      valStr.trim();

      long setTotalWater = valStr.toInt(); // Read as integer (in Liters)
      totalWater = setTotalWater / 1000.0; // Convert to MeterCube

      // Save to Preferences
      preferences.begin("WaterInfo", false);
      preferences.putFloat("totalWater", totalWater);
      preferences.end();
      Serial.println("Saved totalWater: " + String(totalWater));

      setWaterValue = true;

      if (client.connected()) {
        bool acLineState = digitalRead(AC_LINE_PIN);
        char hb_data[64];
        long totalWaterinLiter = totalWater*1000;
        snprintf(hb_data,sizeof(hb_data), "%s,C:%d,S:%d,TW:%d,F:%ld,connected",
          DEVICE_ID,
          acLineState ? 1 : 0,
          NC_Sensor ? 1 : 0,
          totalWaterinLiter,
          K
        );

        client.publish(mqtt_pub_topic, hb_data);
        DEBUG_PRINTLN("Heartbeat sent Successfully");

        #if Fast_LED
          leds[0] = CRGB::Blue;
          FastLED.show();
          vTaskDelay(pdMS_TO_TICKS(500)); // Short delay to indicate status
          leds[0] = CRGB::Black;
          FastLED.show();
        #endif
      } else {
        DEBUG_PRINTLN("Failed to publish Heartbeat on MQTT");
      }
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

      if(prevK != K){
        prevK = K;
        lcd.setCursor(12, 1);
        lcd.print(K);
      }

      if (client.connected()) {
        bool acLineState = digitalRead(AC_LINE_PIN);
        char hb_data[64];
        long totalWaterinLiter = totalWater*1000;
        snprintf(hb_data,sizeof(hb_data), "%s,C:%d,S:%d,TW:%d,F:%ld,connected",
          DEVICE_ID,
          acLineState ? 1 : 0,
          NC_Sensor ? 1 : 0,
          totalWaterinLiter,
          K
        );

        client.publish(mqtt_pub_topic, hb_data);
        DEBUG_PRINTLN("Heartbeat sent Successfully");

        #if Fast_LED
          leds[0] = CRGB::Blue;
          FastLED.show();
          vTaskDelay(pdMS_TO_TICKS(500)); // Short delay to indicate status
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
    if(client.connected()) {
      char waterData[32];
      snprintf(waterData, sizeof(waterData), "%s,water:%ld", DEVICE_ID, (long)totalWater*1000);
      client.publish(mqtt_pub_topic, waterData);
      Serial.println("Sent water data: " + String(waterData));
    }
  }

  if(message == "query:flow"){
    // Respond with current water value
    if(client.connected()) {
      char flowData[32];
      snprintf(flowData, sizeof(flowData), "%s,flow:%d", DEVICE_ID, K);
      client.publish(mqtt_pub_topic, flowData);
      Serial.println("Sent flow data: " + String(flowData));
    }
  }

  if(message == "query:heartbeat"){
    if (client.connected()) {
      bool acLineState = digitalRead(AC_LINE_PIN);
      char hb_data[64];
      long totalWaterinLiter = totalWater*1000;
      snprintf(hb_data,sizeof(hb_data), "%s,C:%d,S:%d,TW:%d,F:%ld,connected",
        DEVICE_ID,
        acLineState ? 1 : 0,
        NC_Sensor ? 1 : 0,
        totalWaterinLiter,
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
  
  if (message == "ping") {
    #if Fast_LED
      vTaskDelay(pdMS_TO_TICKS(100)); // Short delay to indicate status
      leds[0] = CRGB::Green;
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS(250)); // Short delay to indicate status
      leds[0] = CRGB::Black;
      FastLED.show();
    #endif
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
    #if Fast_LED
      vTaskDelay(pdMS_TO_TICKS(100)); // Short delay to indicate status
      leds[0] = CRGB::DeepPink;
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS(250)); // Short delay to indicate status
      leds[0] = CRGB::Black;
      FastLED.show();
    #endif
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

      while (digitalRead(WIFI_RESET_BUTTON_PIN) == LOW) {
        if (millis() - pressStartTime >= 5000) {
          DEBUG_PRINTLN("5 seconds holding time reached, starting WiFiManager...");
          #if Fast_LED
            leds[0] = CRGB::Green;
            FastLED.show();
          #endif
          vTaskSuspend(networkTaskHandle);
          vTaskSuspend(mainTaskHandle);
          wm.resetSettings();
          wm.autoConnect("DMA_AMR_V-5.0");
          ESP.restart();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
//=============================

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
  for (;;) {
    if (digitalRead(LCD_BACKLIGHT_PIN) == LOW) {
      #if Fast_LED
        leds[0] = CRGB::WhiteSmoke;
        FastLED.show();
        vTaskDelay(pdMS_TO_TICKS(250)); // Short delay to indicate status
        leds[0] = CRGB::Black;
        FastLED.show();
      #endif
      lcd.clear();
      lcd.backlight();
      lcd.setCursor(0, 0);
      lcd.print("AMR Version-5.0");
      lcd.setCursor(1, 1);
      lcd.print("Powered by DMA");

      delay(2000); // short pause to show startup screen

      // Display sensor data
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("TW: ");
      char totalWaterBuffer[10];
      sprintf(totalWaterBuffer, "%-7.1f", totalWater);
      lcd.setCursor(3, 0);
      lcd.print(totalWaterBuffer);
      lcd.print(" m3");

      lcd.setCursor(0, 1);
      lcd.print("Raw:");
      char avgValueBuffer[5];
      sprintf(avgValueBuffer, "%-4d", avgValue);
      lcd.setCursor(4, 1);
      lcd.print(avgValueBuffer);

      lcd.setCursor(9, 1);
      lcd.print(" K:");
      char kBuffer[4];
      sprintf(kBuffer, "%-3d", K);
      lcd.setCursor(12, 1);
      lcd.print(kBuffer);

      // Start backlight timer
      backlightOn = true;
      backlightTimer = millis();
    }

    // Auto turn-off after BACKLIGHT_TIME
    if (backlightOn && (millis() - backlightTimer >= BACKLIGHT_TIME)) {
      lcd.noBacklight();
      backlightOn = false;
    }

    // Sampling Logic
    unsigned long currentMillis = millis();
    if (currentMillis - lastNow >= sampleInterval) {
      lastNow = currentMillis;

      rawValue[9] = analogRead(Analog_Pin);
      // Shift array left by 1
      for (int i = 0; i < 9; i++) {
        rawValue[i] = rawValue[i + 1];
      }

      long sum = 0;
      for (int i = 0; i < 10; i++) {    
        sum += rawValue[i];
      }
      avgValue = sum / 10;   // averaged analog value
      
      lcd.setCursor(0, 1);
      lcd.print("Raw:");
      if(prevAvgValue != avgValue){
        prevAvgValue = avgValue;
        char avgValueBuffer[5];
        sprintf(avgValueBuffer, "%-4d", avgValue);
        lcd.setCursor(4, 1);
        lcd.print(avgValueBuffer);
      }
      
      if(NC_Sensor == false){
        if(avgValue > 3000 && swt == false){
        swt = true;
        totalWater = totalWater + (float)(K/1000.0);
        
        preferences.begin("WaterInfo", false);
        preferences.putFloat("totalWater", totalWater);
        preferences.end();
        Serial.println("Saved totalWater: " + String(totalWater));

        #if Fast_LED
          leds[0] = CRGB::Green;
          FastLED.show();
          vTaskDelay(pdMS_TO_TICKS(500)); // Short delay to indicate status
          leds[0] = CRGB::Black;
          FastLED.show();
        #endif

        lcd.setCursor(0, 0);
        lcd.print("TW:            ");
        if(totalWater != prevTotalWater){
          prevTotalWater = totalWater;
          char totalWaterBuffer[10];
          sprintf(totalWaterBuffer, "%-7.1f", totalWater);
          lcd.setCursor(3, 0);
          lcd.print(totalWaterBuffer);
          lcd.print(" m3");
        }
        
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

        #if Fast_LED
          leds[0] = CRGB::Green;
          FastLED.show();
          vTaskDelay(pdMS_TO_TICKS(500)); // Short delay to indicate status
          leds[0] = CRGB::Black;
          FastLED.show();
        #endif

        lcd.setCursor(0, 0);
        lcd.print("TW:            ");
        if(totalWater != prevTotalWater){
          prevTotalWater = totalWater;
          char totalWaterBuffer[10];
          sprintf(totalWaterBuffer, "%-7.1f", totalWater);
          lcd.setCursor(3, 0);
          lcd.print(totalWaterBuffer);
          lcd.print(" m3");
        }
      }
      else if(avgValue > 3000 && swt == true){
        swt = false;
      }
    }
  }

    // Update water value if set command received
    if(setWaterValue == true){
      setWaterValue = false;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("TW:            ");
      if(totalWater != prevTotalWater){
        prevTotalWater = totalWater;
        char totalWaterBuffer[10];
        sprintf(totalWaterBuffer, "%-7.1f", totalWater);
        lcd.setCursor(3, 0);
        lcd.print(totalWaterBuffer);
        lcd.print(" m3");
      }
    }

    
    // PUB = 1146002409090027,C:1,S:0,TW:37295200,F:100,connected
    static unsigned long last_hb_send_time = 0;
    unsigned long now = millis();
    
    // **Send Heartbeat Every HB_INTERVAL**
    if (now - last_hb_send_time >= HB_INTERVAL) {
      last_hb_send_time = now;
      
      if (client.connected()) {
        bool acLineState = digitalRead(AC_LINE_PIN);
        char hb_data[64];
        long totalWaterinLiter = totalWater*1000;
        snprintf(hb_data,sizeof(hb_data), "%s,C:%d,S:%d,TW:%d,F:%ld,connected",
          DEVICE_ID,
          acLineState ? 1 : 0,
          NC_Sensor ? 1 : 0,
          totalWaterinLiter,
          K
        );

        client.publish(mqtt_pub_topic, hb_data);
        DEBUG_PRINTLN("Heartbeat sent Successfully");

        #if Fast_LED
          leds[0] = CRGB::Green;
          FastLED.show();
          vTaskDelay(pdMS_TO_TICKS(250)); // Short delay to indicate status
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
  pinMode(AC_LINE_PIN, INPUT);
  pinMode(LCD_BACKLIGHT_PIN, INPUT_PULLUP);

  preferences.begin("WaterInfo", false);
  totalWater = preferences.getFloat("totalWater", 0.0);
  Serial.println("Restored totalWater: " + String(totalWater));
  K = preferences.getInt("flow", 100);
  Serial.println("Restored K (flow): " + String(K));
  NC_Sensor = preferences.getBool("nc_sensor", false);
  Serial.println("Restored NC_Sensor: " + String(NC_Sensor ? "true" : "false"));
  preferences.end();
  
  if(NC_Sensor == true){

    // Clear rawValue buffer
    for (int i = 0; i <= 9; i++) {
      rawValue[i] = 4095;
    }

    long sum = 0;
    for (int i = 0; i < 10; i++) {
      rawValue[i] = analogRead(Analog_Pin);
      sum += rawValue[i];
      delay(250);
    }
    avgValue = sum / 10;   // averaged analog value

    if(avgValue < 1000){
      swt = true;
    }
    else{
      swt = false;
    }
  }
  else{

    // Clear rawValue buffer
    for (int i = 0; i <= 9; i++) {
      rawValue[i] = 0;
    }

    long sum = 0;
    for (int i = 0; i < 10; i++) {
      rawValue[i] = analogRead(Analog_Pin);
      sum += rawValue[i];
      delay(250);
    }
    avgValue = sum / 10;   // averaged analog value

    if(avgValue > 3000){
      swt = true;
    }
    else{
      swt = false;
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

  delay(2000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TW:            ");
  if(totalWater != prevTotalWater){
    prevTotalWater = totalWater;
    char totalWaterBuffer[10];
    sprintf(totalWaterBuffer, "%-7.1f", totalWater);
    lcd.setCursor(3, 0);
    lcd.print(totalWaterBuffer);
    lcd.print(" m3");
  }
  
  lcd.setCursor(0, 1);
  lcd.print("Raw:");
  if(prevAvgValue != avgValue){
    prevAvgValue = avgValue;
    char avgValueBuffer[5];
    sprintf(avgValueBuffer, "%-4d", avgValue);
    lcd.setCursor(4, 1);
    lcd.print(avgValueBuffer);
  }

  lcd.setCursor(9, 1);
  lcd.print(" K:");
  if(prevK != K){
    prevK = K;
    char kBuffer[4];
    sprintf(kBuffer, "%-3d", K);
    lcd.setCursor(12, 1);
    lcd.print(kBuffer);
  }

  delay(3000);
  lcd.noBacklight();

  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);

  xTaskCreatePinnedToCore(networkTask, "Network Task", 8*1024, NULL, 1, &networkTaskHandle, 0);
  xTaskCreatePinnedToCore(mainTask, "Main Task", 16*1024, NULL, 1, &mainTaskHandle, 1);
  xTaskCreatePinnedToCore(wifiResetTask, "WiFi Reset Task", 8*1024, NULL, 1, &wifiResetTaskHandle, 1);
}

void loop(){

}