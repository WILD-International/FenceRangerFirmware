#include <Arduino.h>
#include <Preferences.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#include "at_commands.h"
#include "comms.h"
#include "config.h"
#include "debug.h"
#include "display.h"
#include "esp32-hal-gpio.h"
#include "hal/gpio_types.h"
#include "helpers.h"
#include "isr_button.h"
#include "measure_program.h"
#include "state.h"

// Global objects
Config config;
State state;
Comms comms(&config);
Display display(&config);

// Programs
MeasureProgram measureProgram;

void setup() {
  int64_t millis_slept = getTimeSleptMs();
  int64_t now_us = rtcTimeNowUs();

  gpio_hold_dis(gpio_num_t(LED));  // release hold on LED so we can change state
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);

  config.load();
  display.begin();

  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    handleWakePress();
    if (shortPressed) {
      shortPressed = false;
      display.trigger();
    }

    // calculate remaining sleep time
    uint32_t remaining_sleep_time = getRemainingSleepTimeMs();
    if (remaining_sleep_time > 1000) {
      display.message("SLEEPING", 800);
      gotoSleep(remaining_sleep_time, false);
    }
  }

  display.message("MEASURE");
  measureProgram.begin();
  while (!measureProgram.isFinished()) {
    measureProgram.update(state);
    delay(1);
  }
  display.showMeasurement(state);

  Serial.begin(115200);
  delay(2000);
  digitalWrite(LED, LOW);
  LOG("Awake after sleeping for %lld ms\n", millis_slept);
  LOG("RTC time now: %lld ms\n", now_us / 1000);

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      LOG("Wakeup caused by external signal using RTC_IO\n");
      if (shortPressed) {
        LOG("Short press detected on wakeup\n");
      }
      if (longPressed) {
        LOG("Long press detected on wakeup\n");
      }
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      LOG("Wakeup caused by external signal using RTC_CNTL\n");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      LOG("Wakeup caused by timer\n");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      LOG("Wakeup caused by touchpad\n");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      LOG("Wakeup caused by ULP program\n");
      break;
    default:
      LOG("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
      break;
  }

  // buttonSetup(Boot_Key);
  // displayMenu.begin();
  LOG("Firmware version: %s\n", GIT_TAG);

  // This will restore the LoRaWAN session from RTC memory, or join the network on first boot
  // comms.begin();

  // now save session to RTC memory
  // comms.saveSession();

  delay(2000);

  display.message("SLEEP", 500);

  gotoSleep(30 * 1000);
}

void loop() {
  // static uint32_t last_measurement_finish_time = 0;
  // if (measureProgram.isFinished() && state.measurement_running) {
  //   last_measurement_finish_time = millis();
  //   state.measurement_running = false;
  //   LOGLN("done");
  // }
  // if (millis() - last_measurement_finish_time > 1000 && !state.measurement_running) {
  //   state.measurement_running = true;
  //   LOGLN("starting (again)");
  //   measureProgram.begin();
  // }
  //
  // measureProgram.update(state);
  // ButtonEvent event = button.update();
  // if (event != ButtonEvent::None) {
  //   LOG("Button event: %d\n", (int)event);
  //   displayMenu.buttonEvent(event);
  // }

  while (Serial.available()) {
    char c = Serial.read();
    handleATSerial(c, config, state);
  }

  // displayMenu.update(state);
  delay(1);
}

// Serial.println("Program finished, going to sleep");
// while (true) {
//   delay(1000);
// }
//
// // put your setup code here, to run once:
// // Serial.begin(115200);
// pinMode(Boot_Key, INPUT_PULLUP);
// pinMode(Led, OUTPUT);
// pinMode(LORA_PA_POWER, OUTPUT);
// digitalWrite(LORA_PA_POWER, LOW);
// pinMode(LORA_PA_EN, OUTPUT);
// digitalWrite(LORA_PA_EN, LOW);
// pinMode(LORA_PA_TX_EN, OUTPUT);
// digitalWrite(LORA_PA_TX_EN, LOW);
//
// // if (preferences.isKey("app_key")) {
// //   size_t len = preferences.getBytes("app_key", (void*)appKey, 16);
// //   for (int i = 0; i < sizeof(appKey); i++) {
// //     printHex(appKey[i]);
// //   }
// //   Serial.println();
// // } else {
// //   preferences.putBytes("app_key", appKey, 16);
// // }
//
// // Start I2C on the correct pins
// Wire.begin(OLED_SDA, OLED_SCL);
//
// // Initialize display (0x3C is the typical I2C address)
// if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
//   Serial.println("SSD1306 init failed");
//   while (true);
// }
//
// while (true) {
//   display.clearDisplay();
//   display.setTextSize(1);
//   display.setTextColor(SSD1306_WHITE);
//   display.setCursor(0, 0);
//   if (Serial.isConnected()) {
//     display.println("HAS SERIAL");
//   } else {
//     display.println("NO SERIAL");
//   }
//   display.display();
// }
//
// while (true) {
//   console.update();
// }

// Serial.println("[SX1262] Initializing ... ");
//
// // Heltec library uses 1.8V for TCXO, instead of RadioLib default of 1.6V
// int state = radio.begin(868.1, 125.0, 12, 7, RADIOLIB_SX126X_SYNC_WORD_PUBLIC, 10, 8, 1.8,
// false); if (state == RADIOLIB_ERR_NONE) {
//   Serial.println(F("success!"));
// } else {
//   Serial.print(F("failed, code "));
//   Serial.println(state);
//   while (true) {
//     delay(10);
//   }
// }
//
// // #if defined(ARDUINO_heltec_wifi_lora_32_V3)
// if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE) {
//   Serial.println(F("Failed to set DIO2 as RF switch!"));
//   while (true) {
//     delay(10);
//   }
// }
// // #endif
//
// // Start I2C on the correct pins
// Wire.begin(OLED_SDA, OLED_SCL);
//
// // Initialize display (0x3C is the typical I2C address)
// if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
//   Serial.println("SSD1306 init failed");
//   while (true);
// }
//
// display.clearDisplay();
// display.setTextSize(1);
// display.setTextColor(SSD1306_WHITE);
// display.setCursor(0, 0);
// display.println(arrayToUint64(devEUI), HEX);
// display.display();
//
// while (true) {
//   delay(5000);
//   Serial.println("Sleeping");
// }
//
// Serial.println(F("Beginning OTAA"));
// node.beginOTAA(joinEUI, arrayToUint64(devEUI), NULL, appKey);
//
// Serial.println(F("Activate OTAA"));
// node.activateOTAA();
// Serial.println(F("Done"));
//
// // Enable the ADR algorithm (on by default which is preferable)
// node.setADR(true);
// }

// // helper function to display a byte array
// void arrayDump(uint8_t* buffer, uint16_t len) {
//   for (uint16_t c = 0; c < len; c++) {
//     char b = buffer[c];
//     if (b < 0x10) {
//       Serial.print('0');
//     }
//     Serial.print(b, HEX);
//   }
//   Serial.println();
// }

// void loop() {
//   digitalWrite(LORA_PA_EN, HIGH);
//   digitalWrite(LORA_PA_TX_EN, HIGH);
//
//   int16_t state = node.sendReceive(dataUp, sizeof(dataUp), 1, dataDown, &downlinkLen, false,
//                                    &uplinkDetails, &downlinkDetails);
//
//   digitalWrite(LORA_PA_EN, LOW);
//   digitalWrite(LORA_PA_TX_EN, LOW);
//
//   // Check if a downlink was received
//   // (state 0 = no downlink, state 1/2 = downlink in window Rx1/Rx2)
//   if (state > 0) {
//     Serial.println(F("Received a downlink"));
//     // Did we get a downlink with data for us
//     if (downlinkLen > 0) {
//       Serial.println(F("Downlink data: "));
//       arrayDump(dataDown, downlinkLen);
//     } else {
//       Serial.println(F("<MAC commands only>"));
//     }
//
//     // print RSSI (Received Signal Strength Indicator)
//     Serial.print(F("[LoRaWAN] RSSI:\t\t"));
//     Serial.print(radio.getRSSI());
//     Serial.println(F(" dBm"));
//
//     // print SNR (Signal-to-Noise Ratio)
//     Serial.print(F("[LoRaWAN] SNR:\t\t"));
//     Serial.print(radio.getSNR());
//     Serial.println(F(" dB"));
//
//     // print extra information about the event
//     Serial.println(F("[LoRaWAN] Event information:"));
//     Serial.print(F("[LoRaWAN] Confirmed:\t"));
//     Serial.println(downlinkDetails.confirmed);
//     Serial.print(F("[LoRaWAN] Confirming:\t"));
//     Serial.println(downlinkDetails.confirming);
//     Serial.print(F("[LoRaWAN] Datarate:\t"));
//     Serial.println(downlinkDetails.datarate);
//     Serial.print(F("[LoRaWAN] Frequency:\t"));
//     Serial.print(downlinkDetails.freq, 3);
//     Serial.println(F(" MHz"));
//     Serial.print(F("[LoRaWAN] Frame count:\t"));
//     Serial.println(downlinkDetails.fCnt);
//     Serial.print(F("[LoRaWAN] Port:\t\t"));
//     Serial.println(downlinkDetails.fPort);
//     Serial.print(F("[LoRaWAN] Time-on-air: \t"));
//     Serial.print(node.getLastToA());
//     Serial.println(F(" ms"));
//     Serial.print(F("[LoRaWAN] Rx window: \t"));
//     Serial.println(state);
//
//     uint8_t margin = 0;
//     uint8_t gwCnt = 0;
//     if (node.getMacLinkCheckAns(&margin, &gwCnt) == RADIOLIB_ERR_NONE) {
//       Serial.print(F("[LoRaWAN] LinkCheck margin:\t"));
//       Serial.println(margin);
//       Serial.print(F("[LoRaWAN] LinkCheck count:\t"));
//       Serial.println(gwCnt);
//     }
//
//     uint32_t timestamp = 0;
//     uint16_t milliseconds = 0;
//     if (node.getMacDeviceTimeAns(&timestamp, &milliseconds, true) == RADIOLIB_ERR_NONE) {
//       Serial.print(F("[LoRaWAN] DeviceTime Unix:\t"));
//       Serial.println(timestamp);
//       Serial.print(F("[LoRaWAN] DeviceTime frac:\t"));
//       Serial.print(milliseconds);
//       Serial.println(F(" ms"));
//     }
//
//   } else {
//     Serial.println(F("[LoRaWAN] No downlink received"));
//   }
//
//   // // Serial.println("Loop");
//   // if (digitalRead(Boot_Key) == 0) {
//   //   digitalWrite(Led, HIGH);
//   //   Serial.print("hello world !!! \n");
//   //   delay(1000);
//   //   digitalWrite(Led, LOW);
//   // }
//   delay(30000);
// }
