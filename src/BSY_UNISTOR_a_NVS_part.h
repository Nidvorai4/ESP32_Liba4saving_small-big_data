#ifndef BSY_UNISTOR_A_NVS_PART_H
#define BSY_UNISTOR_A_NVS_PART_H
#include <Preferences.h>

//#include "BSY_ESP32_UniversalStorages.h" //чтоб система не ругалась на отсутствие определения ST_LOG

/**
 * @class StorageSmallAkaNVS
 * @brief Класс для работы с энергонезависимой памятью (NVS)
 */
class StorageSmallAkaNVS {
private:
    const char* _ns;
    Preferences _prefs;
    uint32_t _minSaveInterval = 1000;
    uint32_t _lastSaveTime = 0;
    
    #pragma pack(push, 1)
    template <typename T>
    struct Package {
        uint8_t version;
        uint8_t reserved[3];
        uint32_t crc;
        T data;
    };
    #pragma pack(pop)

public:
    /**
     * Конструктор
     * @param namespaceName Имя пространства имен NVS (max 15 символов)
     */
    StorageSmallAkaNVS(const char* namespaceName) : _ns(namespaceName) {}

       /**
     * Загрузка данных из NVS (без функции сброса)
     * @param key Уникальное имя ключа
     * @param data Ссылка на переменную/структуру
     * @param expectedVersion Ожидаемая версия данных
     * @return true если данные загружены успешно и валидны
     */
    template <typename T>
    bool load(const char* key, T& data, uint8_t expectedVersion ) {
        ST_LOG(STORAGE_LOG_INFO, "NVS: Load '%s'...", key);
        
        if (!_prefs.begin(_ns, true)) {
            ST_LOG(STORAGE_LOG_ERROR, "NVS: Failed to open namespace '%s'", _ns);
            return false;
        }
        
        Package<T> pkg;
        size_t len = _prefs.getBytes(key, &pkg, sizeof(pkg));
        _prefs.end();

        // 1. Проверка размера
        if (len != sizeof(pkg)) {
            ST_LOG(STORAGE_LOG_WARNING, "NVS: Size mismatch or key '%s' not found", key);
            return false;
        }

        // 2. Проверка версии
        if (pkg.version != expectedVersion) {
            ST_LOG(STORAGE_LOG_WARNING, "NVS: Version mismatch for '%s' (stored: %d, expected: %d)", 
                   key, pkg.version, expectedVersion);
            return false;
        }

        // 3. Проверка целостности (CRC)
        uint32_t calcCrc = crc32_le(0, (const uint8_t*)&pkg.data, sizeof(T));
        if (pkg.crc != calcCrc) {
            ST_LOG(STORAGE_LOG_ERROR, "NVS: CRC error for '%s'", key);
            return false;
        }
        
        // Если все проверки пройдены, копируем данные
        memcpy(&data, &pkg.data, sizeof(T)); 
        ST_LOG(STORAGE_LOG_INFO, "NVS: '%s' loaded OK (version: %d)", key, pkg.version);
        return true;
    }


    /**
     * Сохранение данных в NVS
     * @param key Уникальное имя ключа
     * @param data Данные для сохранения
     * @param version Версия структуры данных
     * @param force Игнорировать защиту от частых записей
     * @return true если данные сохранены успешно
     */
    template <typename T>
    bool save(const char* key, const T& data, uint8_t version , bool force = false) {
        if (sizeof(Package<T>) > NVS_MAX_SIZE) {
            ST_LOG(STORAGE_LOG_ERROR, "NVS: Data too large for '%s'! Max %u bytes, got %u", 
                   key, NVS_MAX_SIZE, sizeof(Package<T>));
            return false;
        }
        
        if (!force) {
            uint32_t now = millis();
            uint32_t elapsed = (now >= _lastSaveTime) 
                ? (now - _lastSaveTime) 
                : (UINT32_MAX - _lastSaveTime + now);
                
            if (elapsed < _minSaveInterval) {
                ST_LOG(STORAGE_LOG_WARNING, "NVS: Save throttled for '%s' (elapsed: %u ms)", 
                       key, elapsed);
                return false;
            }
            _lastSaveTime = now;
        }
        
        Package<T> pkg;
        pkg.version = version;
        pkg.crc = crc32_le(0, (const uint8_t*)&data, sizeof(T));
        memcpy(&pkg.data, &data, sizeof(T));
        
        if (!_prefs.begin(_ns, false)) {
            ST_LOG(STORAGE_LOG_ERROR, "NVS: Failed to open namespace '%s' for write", _ns);
            return false;
        }
        
        size_t written = _prefs.putBytes(key, &pkg, sizeof(pkg));
        _prefs.end();
        
        if (written != sizeof(pkg)) {
            ST_LOG(STORAGE_LOG_ERROR, "NVS: Failed to write key '%s' (written: %u, expected: %u)", 
                   key, written, sizeof(pkg));
            return false;
        }
        
        ST_LOG(STORAGE_LOG_INFO, "NVS: '%s' saved (version: %d, size: %u, CRC: 0x%08X)", 
               key, version, sizeof(pkg), pkg.crc);
        return true;
    }

    /**
     * Проверка наличия ключа
     * @param key Имя ключа
     * @return true если ключ существует
     */
    bool exists(const char* key) {
        if (!_prefs.begin(_ns, true)) return false;
        bool keyExists = _prefs.isKey(key);
        _prefs.end();
        ST_LOG(STORAGE_LOG_DEBUG, "NVS: Key '%s' exists: %s", key, keyExists ? "yes" : "no");
        return keyExists;
    }

    /**
     * Удаление ключа
     * @param key Имя ключа
     * @return true если ключ удалён успешно
     */
    bool remove(const char* key) {
        if (!_prefs.begin(_ns, false)) return false;
        bool success = _prefs.remove(key);
        _prefs.end();
        if (success) {
            ST_LOG(STORAGE_LOG_INFO, "NVS: Key '%s' removed", key);
        } else {
            ST_LOG(STORAGE_LOG_ERROR, "NVS: Key '%s' remove failed", key);
        }
        return success;
    }

    /**
     * Установить минимальный интервал между записями
     * @param ms Интервал в миллисекундах
     */
    void setMinSaveInterval(uint32_t ms) {
        _minSaveInterval = ms;
        ST_LOG(STORAGE_LOG_DEBUG, "NVS: Min save interval set to %u ms", ms);
    }

    /**
     * Удалить все ключи в текущем пространстве имен
     * @return true если очистка прошла успешно
     */
    bool clearNamespace() {
        if (!_prefs.begin(_ns, false)) {
            ST_LOG(STORAGE_LOG_ERROR, "NVS: Failed to open '%s' for clear", _ns);
            return false;
        }
        bool success = _prefs.clear(); // Удаляет все пары ключ-значение в этом namespace
        _prefs.end();
        
        if (success) {
            ST_LOG(STORAGE_LOG_INFO, "NVS: Namespace '%s' cleared", _ns);
        }
        return success;
    }


    /**
     * Полная очистка NVS памяти
     * @warning Удаляет все данные! Требует перезагрузки!
     */
    static void fullResetNVS() {
        ST_LOG(STORAGE_LOG_WARNING, "!!! FULL RESET STARTED !!!");
        esp_err_t err = nvs_flash_erase();
        if (err == ESP_OK) {
            ST_LOG(STORAGE_LOG_INFO, "NVS: All partitions erased OK.");
            nvs_flash_init();
        } else {
            ST_LOG(STORAGE_LOG_ERROR, "NVS: Erase failed code: 0x%X", err);
        }

        ST_LOG(STORAGE_LOG_WARNING, "!!! RESET COMPLETE. REBOOT REQUIRED !!!");
    }
};




#endif