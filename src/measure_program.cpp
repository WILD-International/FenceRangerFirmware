#include "measure_program.h"

#include <Arduino.h>

#include "board.h"
#include "carrier_board.h"
#include "logging.h"

MeasureProgram::MeasureProgram() {
  adc_cal_init();
}

MeasureProgram::~MeasureProgram() {}

Measurement MeasureProgram::run(Config& config) {
  begin(config);
  Measurement measurement;
  while (!isFinished()) {
    update(config, measurement);
    delay(1);
  }
  return measurement;
}

void MeasureProgram::begin(Config& config) {
  measureDurationMs = config.getMeasureDurationMs();
  CarrierBoardConfig carrier = carrierBoardConfig(config.getCarrierRev());
  opampEnablePin = carrier.opampEnablePin;
  opampActiveHigh = carrier.opampActiveHigh;
  LOGLN("MeasureProgram::begin()");
  finished = false;

  for (int i = 0; i < NUM_CHANNELS; i++) {
    min_val[i] = 4095;
    max_val[i] = 0;
    sample_count[i] = 0;
  }
  error_count = 0;

  // configure the ADC
  pinMode(ADC_CTRL_VBAT, OUTPUT);
  digitalWrite(ADC_CTRL_VBAT, HIGH);  // Enable mosfet for battery voltage measurement
  pinMode(opampEnablePin, OUTPUT);
  digitalWrite(opampEnablePin, opampActiveHigh ? HIGH : LOW);  // power on the opamp
  delay(1);

  adc_continuous_init();
  ESP_ERROR_CHECK(adc_digi_start());
  start = millis();
}

void MeasureProgram::update(Config& config, Measurement& measurement) {
  if (finished) {
    return;
  }

  if (millis() - start < measureDurationMs) {
    uint32_t out_len = 0;
    esp_err_t ret = adc_digi_read_bytes(dma_buf, CONV_FRAME_SIZE, &out_len, 100);
    if (ret != ESP_OK) {
      error_count++;
      return;
    }

    // Skip the first half of the window: the opening fence pulse(s) draw inrush current that
    // inflates the peak, and since we report the max it would stick for the whole window. Keep
    // draining the ADC (so the DMA pool doesn't overflow) but don't fold these samples into
    // min/max until past the warmup.
    bool warm = (millis() - start) >= (measureDurationMs / 2);

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

      if (!warm)
        continue;

      if (val < min_val[idx])
        min_val[idx] = val;
      if (val > max_val[idx])
        max_val[idx] = val;
      sample_count[idx]++;
    }
  } else {
    teardown();

    measurement.adcErrorCount = error_count;
    if (error_count > 0) {
      LOG("Errors: %d\n", error_count);
    }
    // Samples are only counted after the warmup, so the rate is over the recorded half-window.
    uint32_t samplingMs = measureDurationMs - (measureDurationMs / 2);
    for (int i = 0; i < NUM_CHANNELS; i++) {
      uint32_t min_mv = esp_adc_cal_raw_to_voltage(min_val[i], &adc_chars);
      uint32_t max_mv = esp_adc_cal_raw_to_voltage(max_val[i], &adc_chars);

      LOG("CH%d: %d samples (~%d Hz), Min: %d, Max: %d\n", channels[i], sample_count[i],
          sample_count[i] * 1000 / samplingMs, min_mv, max_mv);

      if (channels[i] == ADC_CHANNEL_VBAT) {
        measurement.battMinMv = (min_mv * 490 / 100);
        measurement.battMaxMv = (max_mv * 490 / 100);
      } else if (channels[i] == ADC_CHANNEL_FENCE_NEG) {
        measurement.fenceNegMinV = min_mv * (config.getFenceVoltageMultiplier() * 0.001f);
        measurement.fenceNegMaxV = max_mv * (config.getFenceVoltageMultiplier() * 0.001f);
      } else if (channels[i] == ADC_CHANNEL_FENCE_POS) {
        measurement.fencePosMinV = min_mv * (config.getFenceVoltageMultiplier() * 0.001f);
        measurement.fencePosMaxV = max_mv * (config.getFenceVoltageMultiplier() * 0.001f);
      }
    }
  }
}

void MeasureProgram::teardown() {
  finished = true;
  digitalWrite(opampEnablePin, opampActiveHigh ? LOW : HIGH);  // power off the opamp
  digitalWrite(ADC_CTRL_VBAT, LOW);
  ESP_ERROR_CHECK(adc_digi_stop());
  ESP_ERROR_CHECK(adc_digi_deinitialize());
}

void MeasureProgram::stop() {
  if (finished) {
    return;
  }
  teardown();
}

bool MeasureProgram::isFinished() const {
  return finished;
}

static esp_adc_cal_characteristics_t adc_chars;

void MeasureProgram::adc_cal_init() {
  esp_adc_cal_value_t cal_type =
      esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12,
                               0,  // default_vref — ignored on S3 when eFuse data exists
                               &adc_chars);

  // Check what calibration source was used
  switch (cal_type) {
    case ESP_ADC_CAL_VAL_EFUSE_TP_FIT:
      LOGLN("ADC cal: curve fitting (eFuse)");
      break;
    case ESP_ADC_CAL_VAL_EFUSE_TP:
      LOGLN("ADC cal: two point (eFuse)");
      break;
    case ESP_ADC_CAL_VAL_EFUSE_VREF:
      LOGLN("ADC cal: Vref (eFuse)");
      break;
    case ESP_ADC_CAL_VAL_DEFAULT_VREF:
      LOGLN("ADC cal: default Vref (no eFuse data!)");
      break;
    default:
      LOGLN("ADC cal: unknown");
  }
}

void MeasureProgram::adc_continuous_init() {
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
