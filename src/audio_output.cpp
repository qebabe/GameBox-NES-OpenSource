#include "audio_output.h"

#include <Arduino.h>
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"

static bool audio_output_started = false;
static int audio_sample_rate = 44100;
static int audio_mclk_pin = -1;
static int audio_bclk_pin = -1;
static int audio_lrclk_pin = -1;
static int audio_data_pin = -1;

bool audioOutputBegin(int sampleRate, int mclkPin, int bclkPin, int lrclkPin, int dataPin) {
    audio_sample_rate = sampleRate;
    audio_mclk_pin = mclkPin;
    audio_bclk_pin = bclkPin;
    audio_lrclk_pin = lrclkPin;
    audio_data_pin = dataPin;

    if (audio_output_started) {
        return true;
    }

    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = (uint32_t)sampleRate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = sampleRate * 256,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
    };

#if SOC_I2S_SUPPORTS_TDM
    i2s_config.chan_mask = I2S_CHANNEL_STEREO;
    i2s_config.total_chan = 2;
    i2s_config.left_align = true;
#endif

    esp_err_t res = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, nullptr);
    if (res != ESP_OK) {
        Serial.printf("I2S legacy install failed: %d\n", res);
        return false;
    }

    i2s_pin_config_t pin_config = {
        .mck_io_num = mclkPin >= 0 ? mclkPin : I2S_PIN_NO_CHANGE,
        .bck_io_num = bclkPin,
        .ws_io_num = lrclkPin,
        .data_out_num = dataPin,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };

    res = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (res != ESP_OK) {
        Serial.printf("I2S legacy set pin failed: %d\n", res);
        return false;
    }

    res = i2s_set_clk(I2S_NUM_0, (uint32_t)sampleRate,
                      I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    if (res != ESP_OK) {
        Serial.printf("I2S legacy set clk failed: %d\n", res);
        return false;
    }

    audio_output_started = true;
    Serial.printf("I2S legacy started: rate=%d MCLK=%d BCLK=%d LRCLK=%d DOUT=%d\n",
                  sampleRate, mclkPin, bclkPin, lrclkPin, dataPin);
    audioOutputDumpInfo();
    return true;
}

esp_err_t audioOutputWrite(const void* data, size_t size, size_t* bytesWritten) {
    if (!audio_output_started) {
        if (bytesWritten) {
            *bytesWritten = 0;
        }
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_write(I2S_NUM_0, data, size, bytesWritten, portMAX_DELAY);
}

void audioOutputZero() {
    if (!audio_output_started) {
        return;
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void audioOutputEnd() {
    if (!audio_output_started) {
        return;
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
    i2s_stop(I2S_NUM_0);
    i2s_driver_uninstall(I2S_NUM_0);
    audio_output_started = false;
}

void audioOutputDumpInfo() {
    if (!audio_output_started) {
        return;
    }
    Serial.printf("I2S legacy config: rate=%d fixed_mclk=%d MCLK=%d BCLK=%d LRCLK=%d DOUT=%d\n",
                  audio_sample_rate,
                  audio_sample_rate * 256,
                  audio_mclk_pin,
                  audio_bclk_pin,
                  audio_lrclk_pin,
                  audio_data_pin);
}
