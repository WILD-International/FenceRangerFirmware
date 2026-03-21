// #include "config.h"

#define SOMETHING_TO_PREVENT_REORDERING_DEPENDENCIES_BY_FORMATTER
#include <Arduino.h>
#include <Preferences.h>
#include <RadioLib.h>

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

Preferences preferences;

uint64_t joinEUI = 0x0000000000000000;
uint64_t devEUI = 0x70B3D57ED0075374;  // random
// uint64_t devEUI = 0x7B1C000064E83369; // the v3 module that i have
uint8_t appKey[] = {0x90, 0xB8, 0x3E, 0x98, 0xA9, 0x37, 0x54, 0x53,
                    0xDC, 0xDC, 0xEB, 0x72, 0x96, 0xEF, 0x46, 0xB3};

uint8_t dataUp[] = {0x42};
uint8_t dataDown[RADIOLIB_LORAWAN_MAX_DOWNLINK_SIZE + 1];
size_t downlinkLen = sizeof(dataDown);
LoRaWANEvent_t uplinkDetails;
LoRaWANEvent_t downlinkDetails;

LoRaWANNode node(&radio, &Region, subBand);

#define Boot_Key 0
#define Led 35
#define VFEM_CTRL 7
#define PA_CSD 2  // PA_CSD
#define PA_CPS 46

void printHex(uint8_t num) {
  char hexCar[3];

  sprintf(hexCar, "%02X", num);
  Serial.print(hexCar);
}

void setup() {
  // Because platformio is stupid we delay booting to see all serial output
  delay(1000);

  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(Boot_Key, INPUT_PULLUP);
  pinMode(Led, OUTPUT);
  pinMode(VFEM_CTRL, OUTPUT);
  digitalWrite(VFEM_CTRL, LOW);
  // pinMode(PA_CSD, OUTPUT);
  // digitalWrite(PA_CSD, LOW);
  // pinMode(PA_CPS, OUTPUT);
  // digitalWrite(PA_CPS, LOW);

  preferences.begin("fence");

  if (preferences.isKey("app_key")) {
    size_t len = preferences.getBytes("app_key", (void*)appKey, 16);
    for (int i = 0; i < sizeof(appKey); i++) {
      printHex(appKey[i]);
    }
    Serial.println();
  } else {
    preferences.putBytes("app_key", appKey, 16);
  }

  Serial.println("[SX1262] Initializing ... ");

  // Heltec library uses 1.8V for TCXO, instead of RadioLib default of 1.6V
  int state = radio.begin(868.1, 125.0, 12, 7, RADIOLIB_SX126X_SYNC_WORD_PUBLIC, 10, 8, 1.8, false);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("success!"));
  } else {
    Serial.print(F("failed, code "));
    Serial.println(state);
    while (true) {
      delay(10);
    }
  }

  // #if defined(ARDUINO_heltec_wifi_lora_32_V3)
  if (radio.setDio2AsRfSwitch() != RADIOLIB_ERR_NONE) {
    Serial.println(F("Failed to set DIO2 as RF switch!"));
    while (true) {
      delay(10);
    }
  }
  // #endif

  Serial.println(F("Beginning OTAA"));
  node.beginOTAA(joinEUI, devEUI, NULL, appKey);

  Serial.println(F("Activate OTAA"));
  node.activateOTAA();
  Serial.println(F("Done"));

  // Enable the ADR algorithm (on by default which is preferable)
  node.setADR(true);
}

// helper function to display a byte array
void arrayDump(uint8_t* buffer, uint16_t len) {
  for (uint16_t c = 0; c < len; c++) {
    char b = buffer[c];
    if (b < 0x10) {
      Serial.print('0');
    }
    Serial.print(b, HEX);
  }
  Serial.println();
}

void loop() {
  digitalWrite(PA_CSD, HIGH);
  digitalWrite(PA_CPS, HIGH);

  int16_t state = node.sendReceive(dataUp, sizeof(dataUp), 1, dataDown, &downlinkLen, false,
                                   &uplinkDetails, &downlinkDetails);

  digitalWrite(PA_CSD, LOW);
  digitalWrite(PA_CPS, LOW);

  // Check if a downlink was received
  // (state 0 = no downlink, state 1/2 = downlink in window Rx1/Rx2)
  if (state > 0) {
    Serial.println(F("Received a downlink"));
    // Did we get a downlink with data for us
    if (downlinkLen > 0) {
      Serial.println(F("Downlink data: "));
      arrayDump(dataDown, downlinkLen);
    } else {
      Serial.println(F("<MAC commands only>"));
    }

    // print RSSI (Received Signal Strength Indicator)
    Serial.print(F("[LoRaWAN] RSSI:\t\t"));
    Serial.print(radio.getRSSI());
    Serial.println(F(" dBm"));

    // print SNR (Signal-to-Noise Ratio)
    Serial.print(F("[LoRaWAN] SNR:\t\t"));
    Serial.print(radio.getSNR());
    Serial.println(F(" dB"));

    // print extra information about the event
    Serial.println(F("[LoRaWAN] Event information:"));
    Serial.print(F("[LoRaWAN] Confirmed:\t"));
    Serial.println(downlinkDetails.confirmed);
    Serial.print(F("[LoRaWAN] Confirming:\t"));
    Serial.println(downlinkDetails.confirming);
    Serial.print(F("[LoRaWAN] Datarate:\t"));
    Serial.println(downlinkDetails.datarate);
    Serial.print(F("[LoRaWAN] Frequency:\t"));
    Serial.print(downlinkDetails.freq, 3);
    Serial.println(F(" MHz"));
    Serial.print(F("[LoRaWAN] Frame count:\t"));
    Serial.println(downlinkDetails.fCnt);
    Serial.print(F("[LoRaWAN] Port:\t\t"));
    Serial.println(downlinkDetails.fPort);
    Serial.print(F("[LoRaWAN] Time-on-air: \t"));
    Serial.print(node.getLastToA());
    Serial.println(F(" ms"));
    Serial.print(F("[LoRaWAN] Rx window: \t"));
    Serial.println(state);

    uint8_t margin = 0;
    uint8_t gwCnt = 0;
    if (node.getMacLinkCheckAns(&margin, &gwCnt) == RADIOLIB_ERR_NONE) {
      Serial.print(F("[LoRaWAN] LinkCheck margin:\t"));
      Serial.println(margin);
      Serial.print(F("[LoRaWAN] LinkCheck count:\t"));
      Serial.println(gwCnt);
    }

    uint32_t timestamp = 0;
    uint16_t milliseconds = 0;
    if (node.getMacDeviceTimeAns(&timestamp, &milliseconds, true) == RADIOLIB_ERR_NONE) {
      Serial.print(F("[LoRaWAN] DeviceTime Unix:\t"));
      Serial.println(timestamp);
      Serial.print(F("[LoRaWAN] DeviceTime frac:\t"));
      Serial.print(milliseconds);
      Serial.println(F(" ms"));
    }

  } else {
    Serial.println(F("[LoRaWAN] No downlink received"));
  }

  // // Serial.println("Loop");
  // if (digitalRead(Boot_Key) == 0) {
  //   digitalWrite(Led, HIGH);
  //   Serial.print("hello world !!! \n");
  //   delay(1000);
  //   digitalWrite(Led, LOW);
  // }
  delay(30000);
}
