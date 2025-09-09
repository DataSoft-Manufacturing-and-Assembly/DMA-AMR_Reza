#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

// LCD setup (0x27 or 0x3F are common addresses, try 0x27 first)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Analog pin for ESP32
#define ANALOG_PIN 35   

Preferences prefs;      // NVS storage

int value = 0;
bool swt = false;
bool Decrease_value = false;
int water = 0;

// Sampling setup
const int interval = 100;     // sample every 100 ms
unsigned long lastSample = 0; 
int raw_value[12];            // buffer for 12 samples
bool bufferFilled = false;    
int countSamples = 0;         // how many samples stored so far

void setup() {
  Serial.begin(115200);

  // Initial check
  int temp_value = analogRead(ANALOG_PIN);
  if (temp_value > 3000) {
    Decrease_value = true;
  }

  // Start LCD
  lcd.init();
  lcd.backlight();

  // Preferences begin
  prefs.begin("storage", false);   // namespace "storage"
  water = prefs.getInt("water", 0); // read last saved value, default 0

  lcd.setCursor(0, 0);
  lcd.print("ESP32 Analog In");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000);
  lcd.clear();
}

void loop() {
  unsigned long currentMillis = millis();

  // Take a new sample every interval (100ms)
  if (currentMillis - lastSample >= interval) {
    lastSample = currentMillis;

    // Shift array left by 1
    for (int i = 0; i < 11; i++) {
      raw_value[i] = raw_value[i + 1];
    }

    // Add new sample at the end
    raw_value[11] = analogRead(ANALOG_PIN);

    // Count samples until array is full
    if (countSamples < 12) countSamples++;
    if (countSamples == 12) bufferFilled = true;
  }

  // Compute average if buffer has enough data
  if (bufferFilled) {
    long sum = 0;
    for (int i = 0; i < 12; i++) {
      sum += raw_value[i];
      Serial.println(sum);
    }
    value = sum / 12;   // averaged analog value
  } 
  else {
    // Before buffer fills, just use last value
    value = analogRead(ANALOG_PIN);
  }

  // Rising edge detection
  if (value > 3000 && swt == false && Decrease_value == false) {
    water++;
    swt = true;
    prefs.putInt("water", water); // Save new value
  }

  // Reset switch on falling edge
  else if (value < 1000 && swt == true) {
    swt = false;
  }

  // Decrease logic
  else if (value > 3000 && Decrease_value == true) {
    Decrease_value = false;
    water--;
    prefs.putInt("water", water);
  }

  // Print to LCD
  lcd.setCursor(0, 0);
  lcd.print("Avg: ");
  lcd.print(value);
  lcd.print("    "); // clear old digits

  lcd.setCursor(0, 1);
  lcd.print("Water: ");
  lcd.print(water);
  lcd.print("   ");  // clear old digits
}
