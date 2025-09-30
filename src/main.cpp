#include <config.h>

WiFiManager wm;
WiFiClient espClient;
PubSubClient client(espClient);

TaskHandle_t networkTaskHandle;
TaskHandle_t mainTaskHandle;
TaskHandle_t wifiResetTaskHandle;
TaskHandle_t otaTaskHandle = NULL;

bool wifiResetFlag = false;

// Helper function to send heartbeat
void sendHeartbeat() {
  if (client.connected()) {
    bool acLineState = digitalRead(AC_LINE_PIN);
    char hb_data[64];
    snprintf(hb_data, sizeof(hb_data), "%s,C:%d,S:%d,TW:%lu,F:%d,connected",
            DEVICE_ID,
            acLineState ? 1 : 0,
            NC_Sensor ? 1 : 0,
            twLiter,
            K);
    client.publish(mqtt_pub_topic, hb_data);
    DEBUG_PRINTLN("Heartbeat sent Successfully");

    #if Fast_LED
      leds[0] = CRGB::Blue;
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS(500));
      leds[0] = CRGB::Black;
      FastLED.show();
    #endif
  } else {
    DEBUG_PRINTLN("Failed to publish Heartbeat on MQTT");
  }
}

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

// MQTT Callback function
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  // FastLED blink for MQTT activity
  #if Fast_LED
    leds[0] = CRGB::Blue;
    FastLED.show();
    vTaskDelay(pdMS_TO_TICKS(250));
    leds[0] = CRGB::Black;
    FastLED.show();
  #endif

  message.trim();
  message.replace(" ", "");

  DEBUG_PRINTLN("Message arrived on topic: " + String(topic));
  DEBUG_PRINTLN("Message content: " + message);

  // ----- Set NC Sensor -----
  if (message.startsWith("set:nc_sensor=")) {
    int equalIndex = message.indexOf('=');
    if (equalIndex > 0) {
      NC_Sensor = message.substring(equalIndex + 1).toInt() != 0;

      preferences.begin("water_info", false);
      preferences.putBool("nc_sensor", NC_Sensor);
      preferences.end();
      delay(100);

      Serial.print("Saved nc_sensor: ");
      Serial.println(NC_Sensor ? "true" : "false");
      sendHeartbeat();
    }
  }

  // ----- Set Total Water -----
  else if (message.startsWith("set:water=")) {
    int equalIndex = message.indexOf('=');
    if (equalIndex > 0) {
      
      unsigned long setTotalWater = strtoul(message.substring(equalIndex + 1).c_str(), NULL, 10);

      lastTW = setTotalWater;
      twLiter = setTotalWater;
      pC = 0;
      setWaterValueToShowLCD = true;

      preferences.begin("water_info", false);
      preferences.putULong("twLiter", twLiter);
      delay(50);
      preferences.putULong("lastTW", lastTW);
      delay(50);
      preferences.putULong("pC", pC);
      preferences.end();
      delay(100);


      Serial.println("Water reset: total=" + String(twLiter) +
                    " lastSet=" + String(lastTW) +
                    " pC=" + String(pC));

      sendHeartbeat();
    }
  }

  // ----- Set Flow (K) -----
  else if (message.startsWith("set:flow=")) {
    int equalIndex = message.indexOf('=');
    if (equalIndex > 0) {
      K = message.substring(equalIndex + 1).toInt();

      preferences.begin("water_info", false);
      preferences.putInt("flow", K);
      preferences.end();
      delay(100);

      Serial.println("Saved flow K: " + String(K));

      if (prevK != K) {
        prevK = K;
        lcd.setCursor(12, 1);
        lcd.print(K);
      }

      sendHeartbeat();
    }
  }

  // ----- Queries -----
  else if (message == "query:water") {
    if (client.connected()) {
      char waterData[32];
      snprintf(waterData, sizeof(waterData), "%s,water:%lu", DEVICE_ID, twLiter);
      client.publish(mqtt_pub_topic, waterData);
      Serial.println("Sent water data: " + String(waterData));
    }
  }

  else if (message == "query:flow") {
    if (client.connected()) {
      char flowData[32];
      snprintf(flowData, sizeof(flowData), "%s,flow:%d", DEVICE_ID, K);
      client.publish(mqtt_pub_topic, flowData);
      Serial.println("Sent flow data: " + String(flowData));
    }
  }

  else if (message == "query:heartbeat") {
    sendHeartbeat();
  }

  // ----- Ping -----
  else if (message == "ping") {
    #if Fast_LED
      leds[0] = CRGB::Green;
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS(250));
      leds[0] = CRGB::Black;
      FastLED.show();
    #endif
    char pingData[100];
    snprintf(pingData, sizeof(pingData), "%s,%s,%s,%d,%d,%lu,%lu,%lu,%s",
            DEVICE_ID, WiFi.SSID().c_str(),
            WiFi.localIP().toString().c_str(), WiFi.RSSI(),
            HB_INTERVAL,twLiter,lastTW,pC,FW_VERSION);
    client.publish(mqtt_pub_topic, pingData);
    DEBUG_PRINTLN("Sent ping response to MQTT: " + String(pingData));
  }

  // ----- Restart -------
  else if (message == "restart") {
    #if Fast_LED
      leds[0] = CRGB::Red;
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS(250));
      leds[0] = CRGB::Black;
      FastLED.show();
    #endif
    if (client.connected()) {
      char message[64];  
      snprintf(message, sizeof(message), "%s,Restarting...", DEVICE_ID);  
      client.publish(mqtt_pub_topic, message);
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    ESP.restart();
  }

  // ----- OTA Update -----
  else if (message == "update_firmware") {
    #if Fast_LED
      leds[0] = CRGB::DeepPink;
      FastLED.show();
      vTaskDelay(pdMS_TO_TICKS(250));
      leds[0] = CRGB::Black;
      FastLED.show();
    #endif
    if (otaTaskHandle == NULL) {
      xTaskCreatePinnedToCore(otaTask, "OTA Task", 8 * 1024, NULL, 1, &otaTaskHandle, 1);
    } else {
      Serial.println("OTA Task already running.");
    }
  }
}

// ====================


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
//================================

// Start Main Task
void mainTask(void *param) {
  for (;;) {
    // Check Backlight Button
    if (digitalRead(LCD_BACKLIGHT_PIN) == LOW) {
      #if Fast_LED
        leds[0] = CRGB::WhiteSmoke;
        FastLED.show();
        vTaskDelay(pdMS_TO_TICKS(250));
        leds[0] = CRGB::Black;
        FastLED.show();
      #endif

      lcd.clear();
      lcd.backlight();
      lcd.setCursor(0, 0);
      lcd.print("AMR Version-5.0");
      lcd.setCursor(1, 1);
      lcd.print("Powered by DMA");

      vTaskDelay(pdMS_TO_TICKS(2000)); // startup pause

      // Display initial sensor data
      unsigned long m3 = twLiter / 1000;
      unsigned long remainder = (twLiter % 1000) / 10;

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("TW: ");
      lcd.setCursor(3, 0);
      lcd.print(m3);
      lcd.print(".");
      if (remainder < 10) lcd.print("0"); // ensures 2 digits
      lcd.print(remainder);
      lcd.print(" m3");

      lcd.setCursor(0, 1);
      lcd.print("Raw:");
      char avgValueBuffer[6];
      snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%-4d", avgValue);
      lcd.setCursor(4, 1);
      lcd.print(avgValueBuffer);

      lcd.setCursor(9, 1);
      lcd.print(" ST:");

      // Print "NC" or "NO" based on boolean
      if (NC_Sensor) {
          lcd.print("NC");
      } else {
          lcd.print("NO");
      }

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
      // Shift array
      for (int i = 0; i < 9; i++) {
        rawValue[i] = rawValue[i + 1];
      }

      long sum = 0;
      for (int i = 0; i < 10; i++) {
        sum += rawValue[i];
      }
      avgValue = sum / 10;

      if (prevAvgValue != avgValue) {
        prevAvgValue = avgValue;
        char avgValueBuffer[6];
        snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%-4d", avgValue);
        lcd.setCursor(4, 1);
        lcd.print(avgValueBuffer);
      }

      // ---- Pulse Detection ----
      if (!NC_Sensor) {  // NO sensor
        if (avgValue > SensorHighThreshold && !swt) {
          swt = true;
          pC++;
          twLiter += K;

          preferences.begin("water_info", false);
          preferences.putULong("twLiter", twLiter);
          delay(50);
          preferences.putULong("pC", pC);
          preferences.end();
          delay(100);

          // Validation: match expected value
          if (twLiter != lastTW + (pC * K)) {
            Serial.println("Warning: twLiter mismatch!");
            if(client.connected()) {
              char message[64];  
              snprintf(message, sizeof(message), "%s,Warning: twLiter mismatch!", DEVICE_ID);  
              client.publish(mqtt_pub_topic, message);
            }
          }

          Serial.printf("Saved twLiter: %lu\n", twLiter);
          Serial.printf("Pulse Count: %lu\n", pC);

          #if Fast_LED
            leds[0] = CRGB::Green;
            FastLED.show();
            vTaskDelay(pdMS_TO_TICKS(500));
            leds[0] = CRGB::Black;
            FastLED.show();
          #endif

          if (twLiter != prevTotalWater) {
            prevTotalWater = twLiter;

            unsigned long m3 = twLiter / 1000;
            unsigned long remainder = (twLiter % 1000) / 10;

            // lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("TW: ");
            lcd.setCursor(3, 0);
            lcd.print(m3);
            lcd.print(".");
            if (remainder < 10) lcd.print("0"); // ensures 2 digits
            lcd.print(remainder);
            lcd.print(" m3");
          }
        } else if (avgValue < SensorLowThreshold && swt) {
          swt = false;
        }
      } else {  // NC sensor
        if (avgValue < SensorLowThreshold && !swt) {
          swt = true;
          pC++;
          twLiter += K;

          preferences.begin("water_info", false);
          preferences.putULong("twLiter", twLiter);
          delay(50);
          preferences.putULong("pC", pC);
          preferences.end();
          delay(100);

          // Validation: match expected value
          if (twLiter != lastTW + (pC * K)) {
            Serial.println("Warning: twLiter mismatch!");
            if(client.connected()) {
              char message[64];  
              snprintf(message, sizeof(message), "%s,Warning: twLiter mismatch!", DEVICE_ID);  
              client.publish(mqtt_pub_topic, message);
            }
          }

          Serial.printf("Saved twLiter: %lu\n", twLiter);
          Serial.printf("Pulse Count: %lu\n", pC);

          #if Fast_LED
            leds[0] = CRGB::Green;
            FastLED.show();
            vTaskDelay(pdMS_TO_TICKS(500));
            leds[0] = CRGB::Black;
            FastLED.show();
          #endif

          if (twLiter != prevTotalWater) {
            prevTotalWater = twLiter;

            unsigned long m3 = twLiter / 1000;
            unsigned long remainder = (twLiter % 1000) / 10;

            // lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("TW: ");
            lcd.setCursor(3, 0);
            lcd.print(m3);
            lcd.print(".");
            if (remainder < 10) lcd.print("0"); // ensures 2 digits
            lcd.print(remainder);
            lcd.print(" m3");
          }
        } else if (avgValue > SensorHighThreshold && swt) {
          swt = false;
        }
      }
    }

    // Update water value if set command received
    if (setWaterValueToShowLCD) {
      setWaterValueToShowLCD = false;
      if (twLiter != prevTotalWater) {
        prevTotalWater = twLiter;
        
        unsigned long m3 = twLiter / 1000;
        unsigned long remainder = (twLiter % 1000) / 10;

        // lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("TW: ");
        lcd.setCursor(3, 0);
        lcd.print(m3);
        lcd.print(".");
        if (remainder < 10) lcd.print("0"); // ensures 2 digits
        lcd.print(remainder);
        lcd.print(" m3");
      }
    }

    // ---- Heartbeat ----
    static unsigned long last_hb_send_time = 0;
    unsigned long now = millis();
    if (now - last_hb_send_time >= HB_INTERVAL) {
      last_hb_send_time = now;

      sendHeartbeat();
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Yield to scheduler
  }
}
//================================

// Setup function
void setup() {
  Serial.begin(115200);

  // --- Preferences for Device ID ---
  preferences.begin("device_data", false);
  static String device_id;

  #if CHANGE_DEICE_ID
    device_id = String(WORK_PACKAGE) + GW_TYPE + FIRMWARE_UPDATE_DATE + DEVICE_SERIAL;
    preferences.putString("device_id", device_id);
    Serial.println("Device ID updated in Preferences: " + device_id);
  #else
    device_id = preferences.getString("device_id", "UNKNOWN");
    Serial.println("Restored Device ID from Preferences: " + device_id);
  #endif

  DEVICE_ID = device_id.c_str();
  preferences.end();
  delay(100);

  DEBUG_PRINT("Device ID: ");
  DEBUG_PRINTLN(DEVICE_ID);

  // --- Preferences for Water Info ---
  preferences.begin("water_info", false);
  twLiter = preferences.getULong("twLiter", 99);
  lastTW = preferences.getULong("lastTW", 99);
  pC = preferences.getULong("pC", 99);
  K = preferences.getInt("flow", 100);
  NC_Sensor = preferences.getBool("nc_sensor", false);
  preferences.end();
  delay(100);

  Serial.printf("Restored: totalWater=%lu, lastSetWater=%lu, pC=%lu, K=%d, NC_Sensor=%s\n",
                twLiter, twLiter, twLiter, K, NC_Sensor ? "true" : "false");

  // --- FastLED Init ---
  #if Fast_LED
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
    leds[0] = CRGB::Black;
    FastLED.show();
  #endif

  // --- Pin Modes ---
  pinMode(WIFI_RESET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(Analog_Pin, INPUT);
  pinMode(AC_LINE_PIN, INPUT);
  pinMode(LCD_BACKLIGHT_PIN, INPUT_PULLUP);


  // --- Initialize Raw Buffer & avgValue ---
  long sum = 0;
  if (NC_Sensor) {
    for (int i = 0; i < 10; i++) {
      rawValue[i] = analogRead(Analog_Pin);
      sum += rawValue[i];
      vTaskDelay(pdMS_TO_TICKS(250)); // non-blocking
    }
    avgValue = sum / 10;
    swt = (avgValue < SensorLowThreshold);
  } else {
    for (int i = 0; i < 10; i++) {
      rawValue[i] = analogRead(Analog_Pin);
      sum += rawValue[i];
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    avgValue = sum / 10;
    swt = (avgValue > SensorHighThreshold);
  }

  // --- LCD Initialization ---
  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("AMR Version-5.0");
  lcd.setCursor(1, 1);
  lcd.print("Powered by DMA");

  vTaskDelay(pdMS_TO_TICKS(2000));

  unsigned long m3 = twLiter / 1000;
  unsigned long remainder = (twLiter % 1000) / 10;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TW: ");
  lcd.setCursor(3, 0);
  lcd.print(m3);
  lcd.print(".");
  if (remainder < 10) lcd.print("0"); // ensures 2 digits
  lcd.print(remainder);
  lcd.print(" m3");

  lcd.setCursor(0, 1);
  lcd.print("Raw:");
  prevAvgValue = avgValue;
  char avgValueBuffer[6];
  snprintf(avgValueBuffer, sizeof(avgValueBuffer), "%-4d", avgValue);
  lcd.setCursor(4, 1);
  lcd.print(avgValueBuffer);

  lcd.setCursor(9, 1);
  lcd.print(" ST:");

  // Print "NC" or "NO" based on boolean
  if (NC_Sensor) {
      lcd.print("NC");
  } else {
      lcd.print("NO");
  }

  vTaskDelay(pdMS_TO_TICKS(3000));
  lcd.noBacklight();

  // --- MQTT Setup ---
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);

  // --- Create FreeRTOS Tasks ---
  xTaskCreatePinnedToCore(networkTask, "Network Task", 8*1024, NULL, 1, &networkTaskHandle, 0);
  xTaskCreatePinnedToCore(mainTask, "Main Task", 16*1024, NULL, 1, &mainTaskHandle, 1);
  xTaskCreatePinnedToCore(wifiResetTask, "WiFi Reset Task", 8*1024, NULL, 1, &wifiResetTaskHandle, 1);
}
//================================
void loop() {
  // Empty because we use FreeRTOS tasks
}
