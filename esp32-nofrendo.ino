/* Arduino Nofrendo - ESP32-C3 + ST7789 240x240
 * Please check hw_config.h and display.cpp for configuration details
 */
#include <esp_task_wdt.h>

#include <Arduino_GFX_Library.h>

#include "hw_config.h"

extern "C"
{
#include "src/nofrendo.h"
}

// BLE 测试模式：定义此宏后只测试 BLE，不进游戏
// #define BLE_TEST_ONLY

extern "C" void controller_init();
extern "C" uint32_t controller_read_input();
extern "C" void controller_ble_preinit();

int16_t bg_color;
extern Arduino_TFT *gfx;
extern void display_begin();

void setup()
{
    Serial.begin(115200);
    delay(500);

    // disable task watchdog for ESP32-C3 single core
    esp_task_wdt_deinit();

    Serial.printf("\n=== Boot, free_heap=%d ===\n", esp_get_free_heap_size());

#ifdef BLE_TEST_ONLY
    // 仅测试 BLE 连接，不进游戏
    Serial.println("=== BLE TEST MODE ===");
    Serial.println("Press Z in serial monitor to start BLE scan");
    controller_init();
    return; // 不启动游戏
#endif

    // start display
    display_begin();

    // ROM 直接从 "spiffs" 分区 mmap（使用 Default 4MB with spiffs 分区方案）
    char *argv[1];
    char romName[] = "@spiffs";
    argv[0] = romName;

    Serial.println("NoFrendo start (ROM mmap from spiffs partition)!\n");
    nofrendo_main(1, argv);
    Serial.println("NoFrendo end!\n");
}

void loop()
{
#ifdef BLE_TEST_ONLY
    // BLE 测试模式：在 loop 中读取串口字符触发扫描
    controller_read_input();
    delay(20);
#endif
}
