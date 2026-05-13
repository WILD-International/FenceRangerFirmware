#ifndef BOARD_H
#define BOARD_H

#include <RadioLib.h>

// Based on selected board, automatically pick the correct radio config
#define RADIO_BOARD_AUTO

// now include RadioBoards
// this must be included AFTER RadioLib!
#include "RadioBoards.h"

#define Boot_Key 0
#define Led 35
#define DISPLAY_POWER 36

// V4.2 uses GC1109 PA
#if defined(USE_GC1109_PA)
#define LORA_PA_POWER 7
#define LORA_PA_EN 2
#define LORA_PA_TX_EN 46
#else
// V4.3 uses KCT8103L PA
// set as default
#define USE_KCT8103L_PA
#endif

#if defined(USE_KCT8103L_PA)
#define LORA_PA_POWER 7  // power en
#define LORA_PA_CSD 2
#define LORA_PA_CTX 5  // enable tx

#endif

// https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series/blob/fec2992227bdf1e9d1d7935c702d598a7e835824/boards.txt#L918
#define LORA_RX_LNA 1  // 1 = Disabled (default)

#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21

#define ADC_CTRL_VBAT 37  // Enable ADC for battery voltage measurement

#define ADC_BATT 1
#define ADC_OPAMP 2
#define ADC_FENCE_NEG 3
#define ADC_FENCE_POS 4

#define ADC_CHANNEL_VBAT ADC_CHANNEL_0
#define ADC_CHANNEL_FENCE_NEG ADC_CHANNEL_2
#define ADC_CHANNEL_FENCE_POS ADC_CHANNEL_3

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#endif  // BOARD_H
