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

#define LORA_PA_POWER 7        // VFEM_CTRL
#define LORA_PA_EN 2           // PA_CSD
#define LORA_PA_TX_EN_V4_2 46  // PA_CPS on v4.2
#define LORA_PA_TX_EN_V4_3 5   // PA_CPS on V4.3

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
