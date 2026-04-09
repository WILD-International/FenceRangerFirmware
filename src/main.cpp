// #include "config.h"

#define SOMETHING_TO_PREVENT_REORDERING_DEPENDENCIES_BY_FORMATTER
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Console.h>
#include <Preferences.h>
#include <RadioLib.h>
#include <driver/adc.h>
#include <esp_adc_cal.h>

#define Boot_Key 0
#define Led 35
#define VFEM_CTRL 7
#define PA_CSD 2  // PA_CSD
#define PA_CPS 46

#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21

#define ADC_BATT 1
#define ADC_OPAMP 2
#define ADC_FENCE_NEG 3
#define ADC_FENCE_POS 4

#define ADC_CHANNEL_VBAT ADC_CHANNEL_0
#define ADC_CHANNEL_FENCE_NEG ADC_CHANNEL_2
#define ADC_CHANNEL_FENCE_POS ADC_CHANNEL_3

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Based on selected board, automatically pick the correct radio config
#define RADIO_BOARD_AUTO

// now include RadioBoards
// this must be included AFTER RadioLib!
#include <RadioBoards.h>

// create a new RadioLib module
// "Radio" will be set to the correct module type,
// and "RadioModule" will have the correct pins
Radio radio = new RadioModule();

// regional choices: EU868, US915, AU915, AS923, AS923_2, AS923_3, AS923_4, IN865, KR920, CN470
const LoRaWANBand_t Region = EU868;

// subband choice: for US915/AU915 set to 2, for CN470 set to 1, otherwise leave on 0
const uint8_t subBand = 0;

String serialBuffer;

// Max lengths
#define DEVEUI_LEN 8
#define APPKEY_LEN 16
#define DEVNAME_MAX 32

Preferences prefs;

uint8_t devEUI[DEVEUI_LEN];  // Read-only, from chip
uint8_t appKey[APPKEY_LEN];
char devName[DEVNAME_MAX];

enum ProgramState {
  PROGRAM_CONFIGURATION,
  PROGRAM_JOIN,
  PROGRAM_MEASURE,
};

RTC_DATA_ATTR ProgramState programState = PROGRAM_MEASURE;

uint64_t joinEUI = 0x0000000000000000;

uint8_t dataUp[] = {0x42};
uint8_t dataDown[RADIOLIB_LORAWAN_MAX_DOWNLINK_SIZE + 1];
size_t downlinkLen = sizeof(dataDown);
LoRaWANEvent_t uplinkDetails;
LoRaWANEvent_t downlinkDetails;

LoRaWANNode node(&radio, &Region, subBand);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

Console console(115200);

// --- Helpers ---

void bytesToHex(const uint8_t* data, size_t len, char* out) {
  for (size_t i = 0; i < len; i++) {
    sprintf(out + i * 2, "%02X", data[i]);
  }
}

bool hexToBytes(const char* hex, uint8_t* out, size_t expectedLen) {
  size_t hexLen = strlen(hex);
  if (hexLen != expectedLen * 2)
    return false;
  for (size_t i = 0; i < expectedLen; i++) {
    char byte[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
    char* end;
    unsigned long val = strtoul(byte, &end, 16);
    if (*end != '\0')
      return false;
    out[i] = (uint8_t)val;
  }
  return true;
}

void printWakeupReason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Wakeup caused by external signal using RTC_IO");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("Wakeup caused by external signal using RTC_CNTL");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Wakeup caused by timer");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      Serial.println("Wakeup caused by touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      Serial.println("Wakeup caused by ULP program");
      break;
    default:
      Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
      break;
  }
}

// --- DevEUI from chip ---

void loadDevEUI() {
  uint32_t uniqueId[2];
  uint64_t id = ESP.getEfuseMac();
  uniqueId[0] = (uint32_t)(id >> 32);
  uniqueId[1] = (uint32_t)id;
  for (int i = 0; i < 8; i++) {
    if (i < 4)
      devEUI[i] = (uniqueId[1] >> (8 * (3 - i))) & 0xFF;
    else
      devEUI[i] = (uniqueId[0] >> (8 * (7 - i))) & 0xFF;
  }
}

// --- NVS load/save ---

void loadConfig() {
  prefs.begin("fence", true);  // read-only
  prefs.getBytes("appKey", appKey, APPKEY_LEN);
  strlcpy(devName, prefs.getString("devName", "device").c_str(), DEVNAME_MAX);
  prefs.end();
}

void saveAppKey() {
  prefs.begin("fence", false);
  prefs.putBytes("appKey", appKey, APPKEY_LEN);
  prefs.end();
}

void saveDevName() {
  prefs.begin("fence", false);
  prefs.putString("devName", devName);
  prefs.end();
}

// --- AT Command Handler ---

void handleATCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  // AT (ping)
  if (cmd == "AT") {
    Serial.println("OK");
  }
  // AT+DEVEUI?
  else if (cmd == "AT+DEVEUI?") {
    char hex[DEVEUI_LEN * 2 + 1] = {};
    bytesToHex(devEUI, DEVEUI_LEN, hex);
    Serial.printf("+DEVEUI:%s\r\nOK\r\n", hex);
  }
  // AT+APPKEY?
  else if (cmd == "AT+APPKEY?") {
    char hex[APPKEY_LEN * 2 + 1] = {};
    bytesToHex(appKey, APPKEY_LEN, hex);
    Serial.printf("+APPKEY:%s\r\nOK\r\n", hex);
  }
  // AT+APPKEY=<32 hex chars>
  else if (cmd.startsWith("AT+APPKEY=")) {
    String val = cmd.substring(10);
    uint8_t tmp[APPKEY_LEN];
    if (hexToBytes(val.c_str(), tmp, APPKEY_LEN)) {
      memcpy(appKey, tmp, APPKEY_LEN);
      saveAppKey();
      Serial.println("OK");
    } else {
      Serial.println("ERROR: Invalid AppKey (expected 32 hex chars)");
    }
  }
  // AT+DEVNAME?
  else if (cmd == "AT+DEVNAME?") {
    Serial.printf("+DEVNAME:%s\r\nOK\r\n", devName);
  }
  // AT+DEVNAME=<name>
  else if (cmd.startsWith("AT+DEVNAME=")) {
    String val = cmd.substring(11);
    val.trim();
    if (val.length() == 0 || val.length() >= DEVNAME_MAX) {
      Serial.printf("ERROR: Name must be 1-%d chars\r\n", DEVNAME_MAX - 1);
    } else {
      strlcpy(devName, val.c_str(), DEVNAME_MAX);
      saveDevName();
      Serial.println("OK");
    }
  }
  // AT+EXIT
  else if (cmd == "AT+EXIT") {
    Serial.println("Exiting configuration mode");
    programState = PROGRAM_MEASURE;
  }
  // AT+HELP
  else if (cmd == "AT+HELP") {
    Serial.println("AT          - Ping");
    Serial.println("AT+DEVEUI?  - Get DevEUI (read-only)");
    Serial.println("AT+APPKEY?  - Get AppKey");
    Serial.println("AT+APPKEY=  - Set AppKey (32 hex chars)");
    Serial.println("AT+DEVNAME? - Get device name");
    Serial.println("AT+DEVNAME= - Set device name");
    Serial.println("AT+EXIT     - Exit configuration mode");
    Serial.println("OK");
  } else {
    Serial.println("ERROR: Unknown command");
  }
}

void configurationProgram() {
#if defined(DEBUG)
  // Because platformio is stupid we delay booting to see all serial output
  delay(1000);
#endif
  Serial.begin(115200);
  handleATCommand("AT+HELP");  // print help on boot
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (serialBuffer.length() > 0) {
          handleATCommand(serialBuffer);
          if (programState != PROGRAM_CONFIGURATION) {
            break;  // exit loop if we changed state
          }
          serialBuffer = "";
        }
      } else {
        serialBuffer += c;
      }
      if (serialBuffer.length() > 128) {
        serialBuffer = "";  // prevent overflow
      }
    }
    delay(10);
  }
}

// ── Configuration ──────────────────────────────────────────────
#define NUM_CHANNELS 3
#define SAMPLE_FREQ_HZ \
  60000                      // lofence used 250khz adc clock, which is approx 19khz per channel.
                             // We use 60khz because that is effectively 20khz per channel
#define CONV_FRAME_SIZE 256  // tested with 16, add large margin
#define POOL_SIZE 1024       // tested with 64, add large margin
#define MEASURE_MS 1000

static const adc_channel_t channels[NUM_CHANNELS] = {ADC_CHANNEL_VBAT, ADC_CHANNEL_FENCE_NEG,
                                                     ADC_CHANNEL_FENCE_POS};
static uint8_t dma_buf[CONV_FRAME_SIZE];

void adc_continuous_init() {
  adc_digi_init_config_t dma_init = {};
  dma_init.max_store_buf_size = POOL_SIZE;
  dma_init.conv_num_each_intr = CONV_FRAME_SIZE;
  dma_init.adc1_chan_mask =
      (1 << ADC_CHANNEL_VBAT) | (1 << ADC_CHANNEL_FENCE_NEG) | (1 << ADC_CHANNEL_FENCE_POS);
  dma_init.adc2_chan_mask = 0;
  ESP_ERROR_CHECK(adc_digi_initialize(&dma_init));

  adc_digi_pattern_config_t patterns[NUM_CHANNELS] = {
      {
          .atten = ADC_ATTEN_DB_12,
          .channel = ADC_CHANNEL_VBAT,
          .unit = 0,  // This was nasty, 0 is ADC1
          .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,

      },
      {
          .atten = ADC_ATTEN_DB_12,
          .channel = ADC_CHANNEL_FENCE_NEG,
          .unit = 0,  // This was nasty, 0 is ADC1
          .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
      },
      {
          .atten = ADC_ATTEN_DB_12,
          .channel = ADC_CHANNEL_FENCE_POS,
          .unit = 0,  // This was nasty, 0 is ADC1
          .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
      },
  };

  adc_digi_configuration_t dig_cfg = {};
  dig_cfg.sample_freq_hz = SAMPLE_FREQ_HZ;
  dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
  dig_cfg.pattern_num = NUM_CHANNELS;
  dig_cfg.adc_pattern = patterns;
  ESP_ERROR_CHECK(adc_digi_controller_configure(&dig_cfg));
}

void measureMinMax() {
  uint32_t min_val[NUM_CHANNELS] = {4095, 4095};
  uint32_t max_val[NUM_CHANNELS] = {0, 0};
  uint32_t sample_count[NUM_CHANNELS] = {0, 0};
  uint32_t error_count = 0;

  ESP_ERROR_CHECK(adc_digi_start());
  uint32_t start = millis();

  while (millis() - start < MEASURE_MS) {
    uint32_t out_len = 0;
    esp_err_t ret = adc_digi_read_bytes(dma_buf, CONV_FRAME_SIZE, &out_len, 100);
    if (ret != ESP_OK) {
      error_count++;
      continue;
    }

    for (int i = 0; i < out_len; i += sizeof(adc_digi_output_data_t)) {
      adc_digi_output_data_t* s = (adc_digi_output_data_t*)&dma_buf[i];
      uint32_t ch = s->type2.channel;
      uint32_t val = s->type2.data;

      if (ch >= ADC_CHANNEL_MAX)
        continue;

      // Map channel number to our array index
      int idx = -1;
      for (int j = 0; j < NUM_CHANNELS; j++) {
        if (ch == channels[j]) {
          idx = j;
          break;
        }
      }
      if (idx < 0)
        continue;

      if (val < min_val[idx])
        min_val[idx] = val;
      if (val > max_val[idx])
        max_val[idx] = val;
      sample_count[idx]++;
    }
  }

  ESP_ERROR_CHECK(adc_digi_stop());

  Serial.printf("Errors: %d\n", error_count);
  for (int i = 0; i < NUM_CHANNELS; i++) {
    Serial.printf("CH%d: %d samples (~%d Hz), Min: %d, Max: %d\n", channels[i], sample_count[i],
                  sample_count[i] * 1000 / MEASURE_MS, min_val[i], max_val[i]);
  }
}

void measureProgram() {
  // configure the ADC

  pinMode(ADC_OPAMP, OUTPUT);
  digitalWrite(ADC_OPAMP, HIGH);  // power on the opamp
  delay(1);

  adc_continuous_init();
  Serial.println("ADC DMA ready - 2 channels");

  while (true) {
    measureMinMax();
    // delay(1000);
  }

  // sleep testing code below
  // printWakeupReason();
  // // process for a bit
  // delay(20000);
  //
  // // then enter deep sleep
  // esp_sleep_enable_timer_wakeup(5 * 1000000);  // 5 seconds
  //                                              //
  //
  // esp_sleep_wakeup_cause_t wakeup_reason;
  //
  // wakeup_reason = esp_sleep_get_wakeup_cause();
  // if (wakeup_reason == 0) {
  //   Serial.println("Going to sleep now");
  //   delay(1000);
  //   Serial.flush();
  //   esp_deep_sleep_start();
  // }
  //
  // while (true) {
  //   delay(1000);
  //   Serial.println("Staying awake");
  // }
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println("Hello?");
  loadConfig();
  loadDevEUI();
}

void loop() {
  switch (programState) {
    case PROGRAM_CONFIGURATION:
      Serial.println("Entering configuration mode");
      configurationProgram();
      break;
    case PROGRAM_JOIN:
      Serial.println("Entering join mode");
      while (true) {
        delay(1000);
      }
      break;
    case PROGRAM_MEASURE:
      Serial.println("Entering measure mode");
      measureProgram();
      break;
  }
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
// pinMode(VFEM_CTRL, OUTPUT);
// digitalWrite(VFEM_CTRL, LOW);
// pinMode(PA_CSD, OUTPUT);
// digitalWrite(PA_CSD, LOW);
// pinMode(PA_CPS, OUTPUT);
// digitalWrite(PA_CPS, LOW);
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
//   digitalWrite(PA_CSD, HIGH);
//   digitalWrite(PA_CPS, HIGH);
//
//   int16_t state = node.sendReceive(dataUp, sizeof(dataUp), 1, dataDown, &downlinkLen, false,
//                                    &uplinkDetails, &downlinkDetails);
//
//   digitalWrite(PA_CSD, LOW);
//   digitalWrite(PA_CPS, LOW);
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
