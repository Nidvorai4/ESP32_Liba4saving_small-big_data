#ifndef UNI_ESP_STORAGES_H
#define UNI_ESP_STORAGES_H

#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <nvs_flash.h> // Требуется для полной очистки NVS

// --- КОНФИГУРАЦИЯ БИБЛИОТЕКИ ---
#define STORAGE_DEBUG_ENABLE  // Раскомментируйте для включения логов в Serial
#define STORAGE_CHECK_OTA     // Проверка статуса OTA перед записью

#ifdef STORAGE_DEBUG_ENABLE
    // Логгирование: [аптайм в мс][STORAGE] сообщение
    #define ST_LOG(x, ...) Serial.printf("[%lu][STORAGE] " x "\n", millis(), ##__VA_ARGS__)
#else
    #define ST_LOG(x, ...)
#endif

// Прототип функции CRC32 из ROM памяти ESP32
extern "C" uint32_t crc32_le(uint32_t crc, unsigned char const *p, size_t len);

/**
 * КЛАСС ДЛЯ РАБОТЫ С NVS (Энергонезависимое хранилище мелких настроек)
 * Подходит для Wi-Fi паролей, флагов состояния и т.д.
 */
class StorageSmallAkaNVS {
private:
    const char* _ns;
    Preferences _prefs;

    // Структура-обертка для хранения данных вместе с контрольной суммой
    template <typename T>
    struct __attribute__((packed)) Package {
        uint32_t crc;
        T data;
    };

public:
    // Конструктор: принимает имя пространства имен (max 15 символов)
    StorageSmallAkaNVS(const char* namespaceName) : _ns(namespaceName) {}

    /**
     * Загрузка данных из NVS
     * @param key Уникальное имя ключа
     * @param data Ссылка на переменную/структуру
     * @param resetFunc Функция, заполняющая дефолты при ошибке
     */
    template <typename T>
    bool load(const char* key, T& data, void (*resetFunc)(T&) = nullptr) {
        ST_LOG("NVS: Load '%s'...", key);
        if (!_prefs.begin(_ns, true)) return false;
        
        Package<T> pkg;
        size_t len = _prefs.getBytes(key, &pkg, sizeof(pkg));
        _prefs.end();

        uint32_t calcCrc = crc32_le(0, (const uint8_t*)&pkg.data, sizeof(T));
        
        // Если размер не совпал или CRC битый — сброс
        if (len != sizeof(pkg) || pkg.crc != calcCrc) {
            ST_LOG("NVS: Integrity fail. Resetting...");
            if (resetFunc) resetFunc(data);
            save(key, data);
            return false;
        }
        
        data = pkg.data;
        ST_LOG("NVS: '%s' loaded OK", key);
        return true;
    }

    // Сохранение данных в NVS
    template <typename T>
    void save(const char* key, const T& data) {
        Package<T> pkg;
        pkg.data = data;
        pkg.crc = crc32_le(0, (const uint8_t*)&data, sizeof(T));
        
        if (_prefs.begin(_ns, false)) {
            _prefs.putBytes(key, &pkg, sizeof(pkg));
            _prefs.end();
            ST_LOG("NVS: '%s' saved", key);
        }
    }
};

/**
 * ОБЪЕКТ ДЛЯ РАБОТЫ С ФАЙЛАМИ В LittleFS
 * Поддерживает Debounce (задержку записи) и проверку CRC
 */
template <typename T>
class StorageBigAkaFileSys {
private:
    const char* _path;        // Путь к файлу (напр. "/config.bin")
    T& _data;                 // Ссылка на структуру данных
    uint32_t _intervalMs;     // Интервал выдержки в мс
    uint32_t _lastChangeTime = 0; // Время последнего вызова update()
    bool _isDirty = false;    // Флаг наличия несохраненных изменений
    
    inline static bool _otaRunning = false; // Общий флаг блокировки записи для всех файлов

    // Проверка наличия свободного места (размер данных + CRC + 512 байт запаса)
    bool hasSpace() {
        size_t free = LittleFS.totalBytes() - LittleFS.usedBytes();
        size_t needed = sizeof(T) + 4 + 512; 
        if (free < needed) {
            ST_LOG("FS ERR: Low space! Free: %u, Need: %u", free, needed);
            return false;
        }
        return true;
    }

public:
    /**
     * Конструктор файлового объекта
     * @param path Путь к файлу
     * @param data Ссылка на структуру данных
     * @param intervalSec Время выдержки перед записью в СЕКУНДАХ
     */
    StorageBigAkaFileSys(const char* path, T& data, uint32_t intervalSec) 
        : _path(path), _data(data), _intervalMs(intervalSec * 1000) {}

    // Установить статус работы OTA (блокирует запись во flash)
    static void setOtaRunning(bool state) { _otaRunning = state; }

    // Загрузка из файла с проверкой целостности
    bool load(void (*resetFunc)(T&) = nullptr) {
        ST_LOG("FS: Read '%s'...", _path);
        File f = LittleFS.open(_path, "r");
        if (!f) {
            ST_LOG("FS: Not found '%s'. Reset...", _path);
            if (resetFunc) resetFunc(_data);
            save();
            return false;
        }

        uint32_t storedCrc;
        bool ok = (f.read((uint8_t*)&storedCrc, 4) == 4) && 
                  (f.read((uint8_t*)&_data, sizeof(T)) == sizeof(T));
        f.close();

        if (!ok || crc32_le(0, (const uint8_t*)&_data, sizeof(T)) != storedCrc) {
            ST_LOG("FS: CRC error in '%s'. Reset...", _path);
            if (resetFunc) resetFunc(_data);
            save();
            return false;
        }
        ST_LOG("FS: '%s' loaded OK", _path);
        return true;
    }

    // Непосредственная запись файла
    void save() {
#ifdef STORAGE_CHECK_OTA
        if (_otaRunning) {
            ST_LOG("FS: Blocked by OTA!");
            return;
        }
#endif
        if (!hasSpace()) return;

        File f = LittleFS.open(_path, "w");
        if (!f) {
            ST_LOG("FS ERR: Can't write '%s'", _path);
            return;
        }
        
        uint32_t crc = crc32_le(0, (const uint8_t*)&_data, sizeof(T));
        f.write((uint8_t*)&crc, 4);
        f.write((uint8_t*)&_data, sizeof(T));
        f.close();
        
        _isDirty = false;
        ST_LOG("FS: '%s' saved (CRC: 0x%08X)", _path, crc);
    }

    // Пометить данные как измененные и обновить таймер отложенной записи
    void update() {
        _isDirty = true;
        _lastChangeTime = millis();
    }

    // Проверка таймера (вызывать в loop)
    void tick() {
        if (_isDirty && (millis() - _lastChangeTime >= _intervalMs)) {
            ST_LOG("FS: Debounce timeout reached for '%s'. Saving...", _path);
            save();
        }
    }

    // Принудительное сохранение (если были изменения)
    void flush() {
        if (_isDirty) save();
    }
};

/**
 * СЕРВИСНЫЙ КЛАСС ДЛЯ УПРАВЛЕНИЯ ФАЙЛОВОЙ СИСТЕМОЙ
 */
class StorageFS {
public:
    // Инициализация LittleFS с автоматическим форматированием при ошибке
    static bool begin() {
        if (!LittleFS.begin(false)) {
            ST_LOG("FS: Mount failed. Trying to format...");
            if (!LittleFS.begin(true)) {
                ST_LOG("FS CRITICAL: Format failed!");
                return false;
            }
            ST_LOG("FS: Format successful.");
        } else {
            ST_LOG("FS: Mount OK.");
        }
        return true;
    }

    // Полная очистка всего устройства (LittleFS + все разделы NVS)
    static void fullReset() {
        ST_LOG("!!! FULL RESET STARTED !!!");
        
        // Форматирование LittleFS
        if (LittleFS.format()) {
            ST_LOG("FS: LittleFS formatted OK.");
        } else {
            ST_LOG("FS ERR: LittleFS format failed.");
        }

        // Полное стирание NVS Flash через системную функцию
        esp_err_t err = nvs_flash_erase();
        if (err == ESP_OK) {
            ST_LOG("NVS: All partitions erased OK.");
            nvs_flash_init(); // Переинициализация для возможности работы без перезагрузки
        } else {
            ST_LOG("NVS ERR: Erase failed code: 0x%X", err);
        }

        ST_LOG("!!! RESET DONE. REBOOT RECOMMENDED !!!");
    }
};

#endif
