#ifndef BOARD_H
#define BOARD_H

// Based on selected board, automatically pick the correct radio config
#define RADIO_BOARD_AUTO

#define Boot_Key 0
#define Led 35
#define DISPLAY_POWER 36
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

#endif  // BOARD_H
