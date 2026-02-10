#ifndef BSY_ESP32_UNIVERSALSTORAGES_H
#define BSY_ESP32_UNIVERSALSTORAGES_H

#define BASAY_UNIVERSALSTORAGES_VERSION "v1.1.0"

//если нужно сохранять только мелкие данные - то файловую систему можно отключить 
/* 
build_flags =
     -D BSY_STORAGE_DISABLE_LITTLEFS
 */

 #ifndef BSY_STORAGE_DISABLE_LITTLEFS
#define BSY_STORAGE_USE_LITTLEFS 1
#else
#define BSY_STORAGE_USE_LITTLEFS 0
#endif

#include <Arduino.h>
#include <Preferences.h>
#if BSY_STORAGE_USE_LITTLEFS
    #include <LittleFS.h>
    #include <vector>
#endif
#include <nvs_flash.h>

// --- КОНФИГУРАЦИЯ БИБЛИОТЕКИ ---
#define STORAGE_LOG_NONE    0
#define STORAGE_LOG_ERROR   1
#define STORAGE_LOG_WARNING 2
#define STORAGE_LOG_INFO    3
#define STORAGE_LOG_DEBUG   4

#ifndef STORAGE_LOG_LEVEL
#define STORAGE_LOG_LEVEL STORAGE_LOG_INFO
#endif

#define STORAGE_DEBUG_ENABLE
#define STORAGE_CHECK_OTA
#define NVS_MAX_SIZE 3000

#ifdef STORAGE_DEBUG_ENABLE
    #define ST_LOG(level, x, ...) \
        if (level <= STORAGE_LOG_LEVEL) \
            Serial.printf("[%lu][STORAGE][%s] " x "\n", millis(), \
                         level == 1 ? "ERROR" : level == 2 ? "WARN" : \
                         level == 3 ? "INFO" : "DEBUG", ##__VA_ARGS__)
#else
    #define ST_LOG(level, x, ...)
#endif

extern "C" uint32_t crc32_le(uint32_t crc, unsigned char const *p, size_t len);


// Подключаем модули
#include "BSY_UNISTOR_a_NVS_part.h"

#if BSY_STORAGE_USE_LITTLEFS
    #include "BSY_UNISTOR_b_LITTLEFS_part.h"
    #include "BSY_UNISTOR_c_LITTLEFS_util_part.h"
#endif






#endif