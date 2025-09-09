#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>  // WiFiManager library
#include <PubSubClient.h>
// #include <FastLED.h>
#include <HTTPClient.h>

#include <Preferences.h>
Preferences preferences; // Create a Preferences object

#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);

int avgValue = 0;
bool swt = false;
float totalWater; //Total Water
int K = 100;
bool NC_Sensor = true; // true for 5/6 digit, false for 7/8 digit
bool setWaterValue = false;
bool setKValue = false;
bool setSensorType = false;

// Sampling setup
const int sampleInterval = 350;     // sample every 400 ms
unsigned long lastNow = 0; 
int rawValue[10];            // buffer for 12 samples   

void otaTask(void *param);

#define Analog_Pin 35  // Analog pin for ESP32
#define AC_LINE_PIN 34  // Pin to read AC line status
#define WIFI_RESET_BUTTON_PIN 0  // Pin for WiFi reset button

// Configuration Section
#define Fast_LED false
#define DEBUG_MODE true
#define DEBUG_PRINT(x)  if (DEBUG_MODE) { Serial.print(x); }
#define DEBUG_PRINTLN(x) if (DEBUG_MODE) { Serial.println(x); }


#define CHANGE_DEICE_ID false // Change to true if you want to change device ID

#if CHANGE_DEICE_ID
    #define WORK_PACKAGE "1146"
    #define GW_TYPE "05"
    #define FIRMWARE_UPDATE_DATE "250904" 
    #define DEVICE_SERIAL "REZA"
    //#define DEVICE_ID WORK_PACKAGE GW_TYPE FIRMWARE_UPDATE_DATE DEVICE_SERIAL
#endif

const char* DEVICE_ID;

#define HB_INTERVAL 1*15*1000
// #define DATA_INTERVAL 15*1000

// WiFi and MQTT reconnection time config
#define WIFI_ATTEMPT_COUNT 60
#define WIFI_ATTEMPT_DELAY 1000
#define WIFI_WAIT_COUNT 60
#define WIFI_WAIT_DELAY 1000
#define MAX_WIFI_ATTEMPTS 2
#define MQTT_ATTEMPT_COUNT 10
#define MQTT_ATTEMPT_DELAY 5000

int wifiAttemptCount = WIFI_ATTEMPT_COUNT;
int wifiWaitCount = WIFI_WAIT_COUNT;
int maxWifiAttempts = MAX_WIFI_ATTEMPTS;
int mqttAttemptCount = MQTT_ATTEMPT_COUNT;

//PUB = 1146002409020018,C:1,S:0,TW:10375800,F:100,connected

const char* mqtt_server = "broker2.dma-bd.com";
const char* mqtt_user = "broker2";
const char* mqtt_password = "Secret!@#$1234";
const char* mqtt_pub_topic = "DMA/AMR/PUB";
const char* mqtt_sub_topic = "DMA/AMR";
const char* ota_url = "https://raw.githubusercontent.com/DataSoft-Manufacturing-and-Assembly/DMA-SmartSwitch_Reza/with-ota/ota/firmware.bin";

#if Fast_LED
#define DATA_PIN 4
#define NUM_LEDS 1
CRGB leds[NUM_LEDS];
#endif