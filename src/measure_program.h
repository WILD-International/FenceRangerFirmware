#ifndef MEASURE_PROGRAM_H
#define MEASURE_PROGRAM_H

#include <driver/adc.h>
#include <esp_adc_cal.h>

#include "board.h"
#include "config.h"

// ── ADC Configuration ──────────────────────────────────────────────
#define NUM_CHANNELS 3
#define SAMPLE_FREQ_HZ \
  60000                      // lofence used 250khz adc clock, which is approx 19khz per channel.
                             // We use 60khz because that is effectively 20khz per channel
#define CONV_FRAME_SIZE 256  // tested with 16, add large margin
#define POOL_SIZE 1024       // tested with 64, add large margin

struct Measurement {
  uint16_t battMinMv = 0;
  uint16_t battMaxMv = 0;
  uint16_t fenceNegMinV = 0;
  uint16_t fenceNegMaxV = 0;
  uint16_t fencePosMinV = 0;
  uint16_t fencePosMaxV = 0;
  uint32_t adcErrorCount = 0;
};

class MeasureProgram {
 public:
  MeasureProgram();
  ~MeasureProgram();
  Measurement run(Config& config);
  void begin(Config& config);
  void update(Config& config, Measurement& msr);
  void stop();
  bool isFinished() const;

 private:
  void adc_cal_init();
  void adc_continuous_init();
  void measureMinMax();
  void teardown();

  uint32_t start = 0;
  uint32_t measureDurationMs = 3000;  // overwritten by begin(); default never used
  bool finished = true;
  esp_adc_cal_characteristics_t adc_chars;
  const adc_channel_t channels[NUM_CHANNELS] = {ADC_CHANNEL_VBAT, ADC_CHANNEL_FENCE_NEG,
                                                ADC_CHANNEL_FENCE_POS};
  gpio_num_t opampEnablePin =
      GPIO_NUM_2;  // resolved from carrier rev in begin(); see carrier_board.h
  uint8_t dma_buf[CONV_FRAME_SIZE];

  uint32_t min_val[NUM_CHANNELS] = {4095, 4095, 4095};
  uint32_t max_val[NUM_CHANNELS] = {0, 0, 0};
  uint32_t sample_count[NUM_CHANNELS] = {0, 0, 0};
  uint32_t error_count = 0;
};

#endif
