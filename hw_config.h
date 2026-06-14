#ifndef _HW_CONFIG_H_
#define _HW_CONFIG_H_

#define FSROOT "/fs"

/* M5Stack */
#if defined(ARDUINO_M5Stack_Core_ESP32) || defined(ARDUINO_M5STACK_FIRE)

// Uncomment one of below, M5Stack support SPIFFS and SD
// #define FILESYSTEM_BEGIN SPIFFS.begin(true, FSROOT); FS filesystem = SPIFFS;
#define FILESYSTEM_BEGIN SD.begin(4, SPI, 40000000, FSROOT); FS filesystem = SD;

/* enable audio */
#define HW_AUDIO
#define HW_AUDIO_SAMPLERATE 22050

/* controller is I2C M5Stack CardKB */
#define HW_CONTROLLER_I2C_M5CARDKB

/* Odroid-Go */
#elif defined(ARDUINO_ODROID_ESP32)

// Uncomment one of below, ODROID support SPIFFS and SD
// #define FILESYSTEM_BEGIN SPIFFS.begin(false, FSROOT); FS filesystem = SPIFFS;
#define FILESYSTEM_BEGIN SD.begin(SS, SPI, 40000000, FSROOT); FS filesystem = SD;

/* enable audio */
#define HW_AUDIO
#define HW_AUDIO_SAMPLERATE 22050

/* controller is GPIO */
#define HW_CONTROLLER_GPIO
#define HW_CONTROLLER_GPIO_ANALOG_JOYSTICK
#define HW_CONTROLLER_GPIO_UP_DOWN 35
#define HW_CONTROLLER_GPIO_LEFT_RIGHT 34
#define HW_CONTROLLER_GPIO_SELECT 27
#define HW_CONTROLLER_GPIO_START 39
#define HW_CONTROLLER_GPIO_A 32
#define HW_CONTROLLER_GPIO_B 33
#define HW_CONTROLLER_GPIO_X 13
#define HW_CONTROLLER_GPIO_Y 0

/* TTGO T-Watch */
#elif defined(ARDUINO_T) || defined(ARDUINO_TWATCH_BASE) || defined(ARDUINO_TWATCH_2020_V1) || defined(ARDUINO_TWATCH_2020_V2) // TTGO T-Watch

// TTGO T-watch with game module only support SPIFFS
#define FILESYSTEM_BEGIN SPIFFS.begin(false, FSROOT); FS filesystem = SPIFFS;

/* buzzer audio */
#define HW_AUDIO_BUZZER
#define HW_AUDIO_BUZZER_PIN 4
#define HW_AUDIO_SAMPLERATE 22050 // nofrendo minimum sample rate

/* controller is GPIO */
#define HW_CONTROLLER_GPIO
#define HW_CONTROLLER_GPIO_ANALOG_JOYSTICK
#define HW_CONTROLLER_GPIO_UP_DOWN 34
#define HW_CONTROLLER_GPIO_LEFT_RIGHT 33
#define HW_CONTROLLER_GPIO_SELECT 15
#define HW_CONTROLLER_GPIO_START 36
#define HW_CONTROLLER_GPIO_A 13
#define HW_CONTROLLER_GPIO_B 25
#define HW_CONTROLLER_GPIO_X 14
#define HW_CONTROLLER_GPIO_Y 26

/* ESP32-C3 + ST7789 240x240 custom hardware */
#else

/* ROM 通过自定义 'rom' 分区 + esp_partition_mmap 加载，不再使用文件系统 */
#define FILESYSTEM_BEGIN /* no filesystem */

/* audio disabled - no sound hardware */
// #define HW_AUDIO
// #define HW_AUDIO_EXTDAC
// #define HW_AUDIO_SAMPLERATE 22050

/* controller is BLE gamepad (PG-9193) + serial fallback */
#define HW_CONTROLLER_BLE

#endif /* ESP32-C3 custom hardware */

#endif /* _HW_CONFIG_H_ */
