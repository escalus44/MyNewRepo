// Smart Drip Controller (Uno/Nano + DS3231 + moisture + optional flow)
// Hardware: D8 -> MOSFET/relay for valve/pump; A0 -> moisture; D2 -> flow; I2C -> DS3231
// Water at 6:00 AM for WATER_SECONDS if soil is dry; always stop early if flow is zero (dry tank / closed faucet).
// Includes a simple “rain delay” by holding the button at power-up.




#include <Wire.h>
#include "RTClib.h"

RTC_DS3231 rtc;

// --------- USER SETTINGS ----------
const int    VALVE_PIN       = 8;      // MOSFET/relay control
const int    MOISTURE_PIN    = A0;     // analog 0-1023 (lower = wetter for many capacitive sensors)
const int    FLOW_PIN        = 2;      // YF-S201 (hall) on interrupt
const int    RAIN_BUTTON_PIN = 7;      // hold LOW at boot to set rain delay

const int    MOISTURE_DRY_THRESHOLD = 600;  // tweak after calibration
const int    START_HOUR       = 6;     // 06:00 local time
const int    START_MINUTE     = 0;
const uint32_t WATER_SECONDS  = 900;   // 15 minutes
const uint32_t MIN_FLOW_PULSES_PER_MIN = 5; // adjust to your sensor
const uint32_t RAIN_DELAY_HOURS = 24;  // skip for a day when rain delay is set
// ----------------------------------

volatile uint32_t flowPulses = 0;
uint32_t lastFlowCheckMs = 0;
bool valveOn = false;
DateTime nextAllowedRun;

void IRAM_ATTR flowISR() { flowPulses++; }

void valve(bool on) {
  digitalWrite(VALVE_PIN, on ? HIGH : LOW);
  valveOn = on;
}

void setup() {
  pinMode(VALVE_PIN, OUTPUT);
  valve(false);

  pinMode(FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowISR, FALLING);

  pinMode(RAIN_BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(9600);
  Wire.begin();

  if (!rtc.begin()) {
    // Fallback: run without RTC (waters immediately on boot-time schedule check)
  }
  if (rtc.lostPower()) {
    // Set RTC to compile time once (adjust to your locale as needed)
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Optional "rain delay" if button held at boot
  if (digitalRead(RAIN_BUTTON_PIN) == LOW) {
    nextAllowedRun = rtc.now() + TimeSpan(RAIN_DELAY_HOURS * 3600);
  } else {
    nextAllowedRun = rtc.now();
  }
}

bool timeIs(int h, int m, const DateTime& now) {
  return (now.hour() == h && now.minute() == m);
}

void loop() {
  DateTime now = rtc.now();

  // 1) Decide whether to start
  static bool ranThisMinute = false;
  if (timeIs(START_HOUR, START_MINUTE, now)) {
    if (!ranThisMinute && now >= nextAllowedRun) {
      int moist = analogRead(MOISTURE_PIN);
      Serial.print("Moisture: "); Serial.println(moist);
      if (moist >= MOISTURE_DRY_THRESHOLD) {
        // soil is dry -> water
        runWateringCycle();
      } else {
        Serial.println("Soil moist enough; skipping.");
      }
      ranThisMinute = true;
    }
  } else {
    ranThisMinute = false;
  }

  // Non-blocking tasks can go here
  delay(200);
}

void runWateringCycle() {
  Serial.println("Starting watering...");
  flowPulses = 0;
  lastFlowCheckMs = millis();
  valve(true);

  uint32_t startMs = millis();
  while ((millis() - startMs) / 1000UL < WATER_SECONDS) {
    // Early stop if no flow for ~60s (dry supply / closed faucet)
    if (millis() - lastFlowCheckMs >= 60000UL) {
      uint32_t pulses = flowPulses; flowPulses = 0;
      lastFlowCheckMs = millis();
      if (pulses < MIN_FLOW_PULSES_PER_MIN) {
        Serial.println("No/low flow detected. Stopping early.");
        break;
      }
    }
    // Optional: also stop if soil gets wet enough mid-cycle
    int moist = analogRead(MOISTURE_PIN);
    if (moist < (MOISTURE_DRY_THRESHOLD - 50)) {
      Serial.println("Soil reached target moisture. Stopping.");
      break;
    }
    delay(200);
  }

  valve(false);
  Serial.println("Watering done.");
  // Block next run if "rain delay" button is pressed at end
  if (digitalRead(RAIN_BUTTON_PIN) == LOW) {
    nextAllowedRun = rtc.now() + TimeSpan(RAIN_DELAY_HOURS * 3600);
  }
}
