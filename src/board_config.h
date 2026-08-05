#pragma once

#include <stdint.h>

// Every firmware environment must select exactly one board. Keeping the
// original ESP32-S3 hardware explicit prevents a new environment from
// silently inheriting its GPIO map by falling through to a default branch.
#if (defined(DIJI_BOARD_ORIGINAL_ESP32S3) + \
     defined(DIJI_BOARD_LCDWIKI_ES3C28P) + \
     defined(DIJI_BOARD_WOKWI_LCDWIKI)) != 1
#error "Select exactly one DIJI board in the PlatformIO build flags"
#endif

#if defined(DIJI_BOARD_WOKWI_LCDWIKI)

#define DIJI_DISABLE_SD 1
#define DIJI_USE_SD_MMC 0
#define DIJI_LCD_DRIVER_ILI9341 1
#define DIJI_LCD_DRIVER_ST7789 0

// Wokwi preflight uses simulator-friendly SPI pins, but skips SDIO.
constexpr int DIJI_LCD_CS_PIN = 5;
constexpr int DIJI_LCD_DC_PIN = 2;
constexpr int DIJI_LCD_RST_PIN = 13;
constexpr int DIJI_LCD_BL_PIN = -1;    // Wokwi ILI9341 backlight is virtual.
constexpr int DIJI_LCD_SCLK_PIN = 14;
constexpr int DIJI_LCD_MOSI_PIN = 12;
constexpr int DIJI_LCD_MISO_PIN = 4;
constexpr int DIJI_LCD_PANEL_WIDTH = 240;
constexpr int DIJI_LCD_PANEL_HEIGHT = 320;
constexpr int DIJI_LCD_FREQ_WRITE = 40000000;
constexpr int DIJI_LCD_FREQ_READ = 6000000;
constexpr bool DIJI_LCD_SPI_3WIRE = false;
constexpr bool DIJI_LCD_INVERT = false;
constexpr bool DIJI_LCD_RGB_ORDER = false;
constexpr uint8_t DIJI_TFT_ROTATION = 3;

constexpr int DIJI_SD_CS_PIN = -1;
constexpr int DIJI_SD_SCLK_PIN = -1;
constexpr int DIJI_SD_MISO_PIN = -1;
constexpr int DIJI_SD_MOSI_PIN = -1;
constexpr uint32_t DIJI_SD_FREQ = 10000000;

constexpr int DIJI_AMP_ENABLE_PIN = -1;
constexpr int DIJI_AMP_ENABLE_LEVEL = 0;
constexpr bool DIJI_AUDIO_CODEC_ES8311 = false;
constexpr int DIJI_AUDIO_CODEC_I2C_SDA_PIN = -1;
constexpr int DIJI_AUDIO_CODEC_I2C_SCL_PIN = -1;
constexpr uint8_t DIJI_AUDIO_CODEC_ES8311_ADDR = 0x18;
constexpr bool DIJI_TOUCH_FT6336 = false;
constexpr int DIJI_TOUCH_I2C_SDA_PIN = -1;
constexpr int DIJI_TOUCH_I2C_SCL_PIN = -1;
constexpr int DIJI_TOUCH_INT_PIN = -1;
constexpr int DIJI_TOUCH_RST_PIN = -1;
constexpr int DIJI_I2S_MCLK_PIN = -1;
constexpr int DIJI_I2S_BCLK_PIN = 5;
constexpr int DIJI_I2S_LRCLK_PIN = 7;
constexpr int DIJI_I2S_DATA_PIN = 6;
constexpr int DIJI_I2S_INPUT_PIN = -1;

constexpr int DIJI_A_BUTTON_PIN = 6;
constexpr int DIJI_B_BUTTON_PIN = -1;
constexpr int DIJI_LEFT_BUTTON_PIN = -1;
constexpr int DIJI_RIGHT_BUTTON_PIN = -1;
constexpr int DIJI_UP_BUTTON_PIN = 15;
constexpr int DIJI_DOWN_BUTTON_PIN = 16;
constexpr int DIJI_START_BUTTON_PIN = 7;
constexpr int DIJI_SELECT_BUTTON_PIN = -1;

#elif defined(DIJI_BOARD_LCDWIKI_ES3C28P)

#define DIJI_DISABLE_SD 0
#define DIJI_USE_SD_MMC 1
#define DIJI_LCD_DRIVER_ILI9341 1
#define DIJI_LCD_DRIVER_ST7789 0

// LCDWIKI ILI9341 LCD SPI
constexpr int DIJI_LCD_CS_PIN = 10;
constexpr int DIJI_LCD_DC_PIN = 46;
constexpr int DIJI_LCD_RST_PIN = -1;   // LCD reset is tied to CHIP_PU on this board.
constexpr int DIJI_LCD_BL_PIN = 45;    // High = backlight on.
constexpr int DIJI_LCD_SCLK_PIN = 12;
constexpr int DIJI_LCD_MOSI_PIN = 11;
constexpr int DIJI_LCD_MISO_PIN = 13;
constexpr int DIJI_LCD_PANEL_WIDTH = 240;
constexpr int DIJI_LCD_PANEL_HEIGHT = 320;
constexpr int DIJI_LCD_FREQ_WRITE = 40000000;
constexpr int DIJI_LCD_FREQ_READ = 6000000;
constexpr bool DIJI_LCD_SPI_3WIRE = false;
constexpr bool DIJI_LCD_INVERT = true;
constexpr bool DIJI_LCD_RGB_ORDER = false;
constexpr uint8_t DIJI_TFT_ROTATION = 3;

// LCDWIKI MicroSD SDIO wiring.
constexpr int DIJI_SDMMC_CLK_PIN = 38;
constexpr int DIJI_SDMMC_CMD_PIN = 40;
constexpr int DIJI_SDMMC_D0_PIN = 39;
constexpr int DIJI_SDMMC_D1_PIN = 41;
constexpr int DIJI_SDMMC_D2_PIN = 48;
constexpr int DIJI_SDMMC_D3_PIN = 47;
constexpr bool DIJI_SDMMC_1BIT_MODE = false;

// LCDWIKI QuanDong ES8311 codec + SC8002 amplifier wiring.
constexpr int DIJI_AMP_ENABLE_PIN = 1;
constexpr int DIJI_AMP_ENABLE_LEVEL = 0; // Low = enabled, high = disabled.
constexpr bool DIJI_AUDIO_CODEC_ES8311 = true;
constexpr int DIJI_AUDIO_CODEC_I2C_SDA_PIN = 16;
constexpr int DIJI_AUDIO_CODEC_I2C_SCL_PIN = 15;
constexpr uint8_t DIJI_AUDIO_CODEC_ES8311_ADDR = 0x18; // 7-bit I2C address.
constexpr bool DIJI_TOUCH_FT6336 = true;
constexpr int DIJI_TOUCH_I2C_SDA_PIN = 16;
constexpr int DIJI_TOUCH_I2C_SCL_PIN = 15;
constexpr int DIJI_TOUCH_INT_PIN = 17;
constexpr int DIJI_TOUCH_RST_PIN = 18;
constexpr int DIJI_I2S_MCLK_PIN = 4;
constexpr int DIJI_I2S_BCLK_PIN = 5;
constexpr int DIJI_I2S_LRCLK_PIN = 7;
constexpr int DIJI_I2S_DATA_PIN = 8;
constexpr int DIJI_I2S_INPUT_PIN = 6;

// Minimal four-button wiring via the exposed GPIO header.
// This is enough for menu navigation and basic game testing.
constexpr int DIJI_A_BUTTON_PIN = 2;
constexpr int DIJI_B_BUTTON_PIN = -1;
constexpr int DIJI_LEFT_BUTTON_PIN = -1;
constexpr int DIJI_RIGHT_BUTTON_PIN = -1;
constexpr int DIJI_UP_BUTTON_PIN = 14;
constexpr int DIJI_DOWN_BUTTON_PIN = 21;
constexpr int DIJI_START_BUTTON_PIN = 3;
constexpr int DIJI_SELECT_BUTTON_PIN = -1;

#elif defined(DIJI_BOARD_ORIGINAL_ESP32S3)

#define DIJI_DISABLE_SD 0
#define DIJI_USE_SD_MMC 0
#define DIJI_LCD_DRIVER_ILI9341 0
#define DIJI_LCD_DRIVER_ST7789 1

// Original ST7789 LCD SPI.
constexpr int DIJI_LCD_CS_PIN = 10;
constexpr int DIJI_LCD_DC_PIN = 11;
constexpr int DIJI_LCD_RST_PIN = 12;
constexpr int DIJI_LCD_BL_PIN = -1;
constexpr int DIJI_LCD_SCLK_PIN = 14;
constexpr int DIJI_LCD_MOSI_PIN = 13;
constexpr int DIJI_LCD_MISO_PIN = -1;
constexpr int DIJI_LCD_PANEL_WIDTH = 240;
constexpr int DIJI_LCD_PANEL_HEIGHT = 320;
constexpr int DIJI_LCD_FREQ_WRITE = 80000000;
constexpr int DIJI_LCD_FREQ_READ = 6000000;
constexpr bool DIJI_LCD_SPI_3WIRE = true;
constexpr bool DIJI_LCD_INVERT = true;
constexpr bool DIJI_LCD_RGB_ORDER = false;
constexpr uint8_t DIJI_TFT_ROTATION = 3;

// Original SPI MicroSD wiring.
constexpr int DIJI_SD_CS_PIN = 42;
constexpr int DIJI_SD_SCLK_PIN = 40;
constexpr int DIJI_SD_MISO_PIN = 39;
constexpr int DIJI_SD_MOSI_PIN = 41;
constexpr uint32_t DIJI_SD_FREQ = 10000000;

// Original MAX98357A I2S wiring.
constexpr int DIJI_AMP_ENABLE_PIN = -1;
constexpr int DIJI_AMP_ENABLE_LEVEL = 0;
constexpr bool DIJI_AUDIO_CODEC_ES8311 = false;
constexpr int DIJI_AUDIO_CODEC_I2C_SDA_PIN = -1;
constexpr int DIJI_AUDIO_CODEC_I2C_SCL_PIN = -1;
constexpr uint8_t DIJI_AUDIO_CODEC_ES8311_ADDR = 0x18;
constexpr bool DIJI_TOUCH_FT6336 = false;
constexpr int DIJI_TOUCH_I2C_SDA_PIN = -1;
constexpr int DIJI_TOUCH_I2C_SCL_PIN = -1;
constexpr int DIJI_TOUCH_INT_PIN = -1;
constexpr int DIJI_TOUCH_RST_PIN = -1;
constexpr int DIJI_I2S_MCLK_PIN = -1;
constexpr int DIJI_I2S_BCLK_PIN = 5;
constexpr int DIJI_I2S_LRCLK_PIN = 4;
constexpr int DIJI_I2S_DATA_PIN = 6;
constexpr int DIJI_I2S_INPUT_PIN = -1;

// Original eight-button wiring.
constexpr int DIJI_A_BUTTON_PIN = 48;
constexpr int DIJI_B_BUTTON_PIN = 47;
constexpr int DIJI_LEFT_BUTTON_PIN = 8;
constexpr int DIJI_RIGHT_BUTTON_PIN = 18;
constexpr int DIJI_UP_BUTTON_PIN = 17;
constexpr int DIJI_DOWN_BUTTON_PIN = 3;
constexpr int DIJI_START_BUTTON_PIN = 15;
constexpr int DIJI_SELECT_BUTTON_PIN = 16;

#endif
