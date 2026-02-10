#ifndef BSY_ESP32_UNIVERSALSTORAGES_H
#define BSY_ESP32_UNIVERSALSTORAGES_H

//если нужно сохранять только мелкие данные - то файловую систему можно отключить
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
    bool load(const char* key, T& data, uint8_t expectedVersion = 1) {
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
        data = pkg.data;
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
    bool save(const char* key, const T& data, uint8_t version = 1, bool force = false) {
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
        pkg.data = data;
        
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
};



#if BSY_STORAGE_USE_LITTLEFS
    /**
     * @class StorageBigAkaFileSys
     * @brief Класс для работы с файлами в LittleFS
     * @tparam T Тип хранимых данных
     */
    template <typename T>
    class StorageBigAkaFileSys {
    private:
        const char* _path;
        T& _data;
        uint32_t _intervalMs;
        uint32_t _lastChangeTime = 0;
        bool _isDirty = false;
        bool _fsMounted = false;
        bool _debounceEnabled = true;
        
        // inline static работает с C++17
        inline static bool _otaRunning = false;

        /**
         * Проверка наличия свободного места
         * @return true если достаточно места для записи
         */
        bool hasSpace() {
            size_t free = LittleFS.totalBytes() - LittleFS.usedBytes();
            size_t needed = sizeof(T) + 4 + 512;
            if (free < needed) {
                ST_LOG(STORAGE_LOG_ERROR, "FS: Low space for '%s'! Free: %u, Need: %u", 
                    _path, free, needed);
                return false;
            }
            return true;
        }

    public:
        /**
         * Конструктор файлового объекта
         * @param path Путь к файлу
         * @param data Ссылка на структуру данных
         * @param intervalSec Время выдержки перед записью в секундах
         * @param debounceEnabled Включить отложенную запись
         */
        StorageBigAkaFileSys(const char* path, T& data, uint32_t intervalSec = 5, 
                            bool debounceEnabled = true) 
            : _path(path), _data(data), _intervalMs(intervalSec * 1000), 
            _debounceEnabled(debounceEnabled) {
            _fsMounted = LittleFS.begin(false);
            if (!_fsMounted) {
                ST_LOG(STORAGE_LOG_WARNING, "FS: Filesystem not mounted for '%s'", _path);
            }
        }

        /**
         * Установить статус работы OTA
         * @param state true если выполняется OTA обновление
         */
        static void setOtaRunning(bool state) { 
            _otaRunning = state;
            ST_LOG(STORAGE_LOG_INFO, "FS: OTA running state set to %s", state ? "true" : "false");
        }

        /**
         * Получить статус OTA
         * @return true если выполняется OTA обновление
         */
        static bool isOtaRunning() { 
            return _otaRunning;
        }

        /**
         * Загрузка из файла с проверкой целостности
         * @param resetFunc Функция для сброса данных при ошибке
         * @return true если данные загружены успешно
         */
        bool load(void (*resetFunc)(T&) = nullptr) {
            if (!_fsMounted) {
                ST_LOG(STORAGE_LOG_ERROR, "FS: Filesystem not mounted for '%s'", _path);
                return false;
            }
            
            ST_LOG(STORAGE_LOG_INFO, "FS: Read '%s'...", _path);
            File f = LittleFS.open(_path, "r");
            if (!f) {
                ST_LOG(STORAGE_LOG_WARNING, "FS: File '%s' not found", _path);
                if (resetFunc) resetFunc(_data);
                save();
                return false;
            }

            uint32_t storedCrc;
            bool ok = (f.read((uint8_t*)&storedCrc, 4) == 4) && 
                    (f.read((uint8_t*)&_data, sizeof(T)) == sizeof(T));
            f.close();

            if (!ok) {
                ST_LOG(STORAGE_LOG_ERROR, "FS: Read error from '%s'", _path);
                if (resetFunc) resetFunc(_data);
                save();
                return false;
            }

            uint32_t calcCrc = crc32_le(0, (const uint8_t*)&_data, sizeof(T));
            if (calcCrc != storedCrc) {
                ST_LOG(STORAGE_LOG_ERROR, "FS: CRC error in '%s' (stored: 0x%08X, calc: 0x%08X)", 
                    _path, storedCrc, calcCrc);
                if (resetFunc) resetFunc(_data);
                save();
                return false;
            }
            
            ST_LOG(STORAGE_LOG_INFO, "FS: '%s' loaded OK (size: %u, CRC: 0x%08X)", 
                _path, sizeof(T), calcCrc);
            return true;
        }

        /**
         * Непосредственная запись файла
         * @return true если файл записан успешно
         */
        bool save() {
    #ifdef STORAGE_CHECK_OTA
            if (_otaRunning) {
                ST_LOG(STORAGE_LOG_WARNING, "FS: Blocked by OTA for '%s'", _path);
                return false;
            }
    #endif
            if (!_fsMounted) {
                ST_LOG(STORAGE_LOG_ERROR, "FS: Filesystem not mounted for '%s'", _path);
                return false;
            }
            
            if (!hasSpace()) {
                return false;
            }

            File f = LittleFS.open(_path, "w");
            if (!f) {
                ST_LOG(STORAGE_LOG_ERROR, "FS: Can't write '%s'", _path);
                return false;
            }
            
            uint32_t crc = crc32_le(0, (const uint8_t*)&_data, sizeof(T));
            size_t writtenCrc = f.write((uint8_t*)&crc, 4);
            size_t writtenData = f.write((uint8_t*)&_data, sizeof(T));
            f.close();
            
            if (writtenCrc != 4 || writtenData != sizeof(T)) {
                ST_LOG(STORAGE_LOG_ERROR, "FS: Write error for '%s'", _path);
                return false;
            }
            
            _isDirty = false;
            ST_LOG(STORAGE_LOG_INFO, "FS: '%s' saved (size: %u, CRC: 0x%08X)", 
                _path, sizeof(T), crc);
            return true;
        }

        /**
         * Пометить данные как измененные
         */
        void update() {
            _isDirty = true;
            _lastChangeTime = millis();
            
            if (!_debounceEnabled) {
                save();
            }
        }

        /**
         * Проверка таймера отложенной записи
         */
        void tick() {
            if (!_debounceEnabled || !_isDirty) return;
            
            uint32_t currentTime = millis();
            uint32_t elapsed = (currentTime >= _lastChangeTime) 
                ? (currentTime - _lastChangeTime) 
                : (UINT32_MAX - _lastChangeTime + currentTime);
            
            if (elapsed >= _intervalMs) {
                ST_LOG(STORAGE_LOG_DEBUG, "FS: Debounce timeout for '%s' (elapsed: %u ms)", 
                    _path, elapsed);
                save();
            }
        }

        /**
         * Принудительное сохранение
         * @return true если данные сохранены
         */
        bool flush() {
            if (_isDirty) {
                return save();
            }
            return true;
        }

        /**
         * Проверка существования файла
         * @return true если файл существует
         */
        bool exists() {
            if (!_fsMounted) return false;
            return LittleFS.exists(_path);
        }

        /**
         * Удаление файла
         * @return true если файл удалён
         */
        bool remove() {
            if (!_fsMounted) return false;
            bool success = LittleFS.remove(_path);
            if (success) {
                _isDirty = false;
                ST_LOG(STORAGE_LOG_INFO, "FS: File '%s' removed", _path);
            } else {
                ST_LOG(STORAGE_LOG_ERROR, "FS: Failed to remove '%s'", _path);
            }
            return success;
        }

        /**
         * Включить/отключить отложенную запись
         * @param enabled true для включения дебаунса
         */
        void setDebounceEnabled(bool enabled) {
            _debounceEnabled = enabled;
            ST_LOG(STORAGE_LOG_DEBUG, "FS: Debounce for '%s' %s", 
                _path, enabled ? "enabled" : "disabled");
        }

        /**
         * Получить статус изменений
         * @return true если есть несохраненные изменения
         */
        bool isDirty() const {
            return _isDirty;
        }

        /**
         * Получить путь к файлу
         * @return Путь к файлу
         */
        const char* getPath() const {
            return _path;
        }

        /**
         * Получить интервал дебаунса
         * @return Интервал в миллисекундах
         */
        uint32_t getDebounceInterval() const {
            return _intervalMs;
        }
    };

    /**
     * @struct StorageStats
     * @brief Статистика использования файловой системы
     */
    struct StorageStats {
        size_t totalBytes;
        size_t usedBytes;
        size_t freeBytes;
        float usedPercent;
    };

    /**
     * @class StorageFS
     * @brief Сервисный класс для управления файловой системой
     */
    class StorageFS {
    public:
        /**
         * Инициализация LittleFS
         * @param formatOnFail Форматировать при ошибке монтирования
         * @return true если файловая система инициализирована
         */
        static bool begin(bool formatOnFail = true) {
            ST_LOG(STORAGE_LOG_INFO, "FS: Mounting...");
            
            if (!LittleFS.begin(false)) {
                if (!formatOnFail) {
                    ST_LOG(STORAGE_LOG_ERROR, "FS: Mount failed");
                    return false;
                }
                
                ST_LOG(STORAGE_LOG_WARNING, "FS: Mount failed. Trying to format...");
                if (!LittleFS.begin(true)) {
                    ST_LOG(STORAGE_LOG_ERROR, "FS: Format failed!");
                    return false;
                }
                ST_LOG(STORAGE_LOG_INFO, "FS: Format successful.");
            } else {
                ST_LOG(STORAGE_LOG_INFO, "FS: Mount OK.");
            }
            
            printStats();
            return true;
        }

        /**
         * Получить статистику использования
         * @return Структура со статистикой
         */
        static StorageStats getStats() {
            StorageStats stats;
            stats.totalBytes = LittleFS.totalBytes();
            stats.usedBytes = LittleFS.usedBytes();
            stats.freeBytes = (stats.totalBytes > stats.usedBytes) 
                ? (stats.totalBytes - stats.usedBytes) 
                : 0;
            stats.usedPercent = (stats.totalBytes > 0) 
                ? (100.0f * stats.usedBytes / stats.totalBytes) 
                : 0.0f;
            return stats;
        }

        /**
         * Вывод статистики в лог
         */
        static void printStats() {
            StorageStats stats = getStats();
            ST_LOG(STORAGE_LOG_INFO, 
                "FS Stats: Total: %u bytes, Used: %u bytes (%.1f%%), Free: %u bytes",
                stats.totalBytes, stats.usedBytes, stats.usedPercent, stats.freeBytes);
        }

        /**
         * Полная очистка всех хранилищ
         * @warning Удаляет все данные! Требует перезагрузки!
         */
        static void fullReset() {
            ST_LOG(STORAGE_LOG_WARNING, "!!! FULL RESET STARTED !!!");
            
            if (LittleFS.format()) {
                ST_LOG(STORAGE_LOG_INFO, "FS: LittleFS formatted OK.");
            } else {
                ST_LOG(STORAGE_LOG_ERROR, "FS: LittleFS format failed.");
            }

            esp_err_t err = nvs_flash_erase();
            if (err == ESP_OK) {
                ST_LOG(STORAGE_LOG_INFO, "NVS: All partitions erased OK.");
                nvs_flash_init();
            } else {
                ST_LOG(STORAGE_LOG_ERROR, "NVS: Erase failed code: 0x%X", err);
            }

            ST_LOG(STORAGE_LOG_WARNING, "!!! RESET COMPLETE. REBOOT REQUIRED !!!");
        }

        /**
         * Создание резервной копии файла
         * @param srcPath Исходный файл
         * @param backupPath Файл для резервной копии (если nullptr, будет создан с .bak)
         * @return true если резервная копия создана
         */
        static bool backupFile(const char* srcPath, const char* backupPath = nullptr) {
            if (!backupPath) {
                backupPath = (String(srcPath) + ".bak").c_str();
            }
            
            if (LittleFS.exists(backupPath)) {
                LittleFS.remove(backupPath);
            }
            
            File src = LittleFS.open(srcPath, "r");
            if (!src) {
                ST_LOG(STORAGE_LOG_ERROR, "FS: Can't open source file '%s'", srcPath);
                return false;
            }
            
            File dst = LittleFS.open(backupPath, "w");
            if (!dst) {
                src.close();
                ST_LOG(STORAGE_LOG_ERROR, "FS: Can't create backup file '%s'", backupPath);
                return false;
            }
            
            uint8_t buffer[512];
            size_t total = 0;
            while (src.available()) {
                size_t len = src.read(buffer, sizeof(buffer));
                dst.write(buffer, len);
                total += len;
            }
            
            src.close();
            dst.close();
            
            ST_LOG(STORAGE_LOG_INFO, "FS: Backup created '%s' -> '%s' (%u bytes)", 
                srcPath, backupPath, total);
            return true;
        }

        /**
         * Получение списка файлов в директории
         * @param path Путь к директории (по умолчанию корень)
         * @return Вектор с именами файлов
         */
        static std::vector<String> listFiles(const char* path = "/") {
            std::vector<String> files;
            File root = LittleFS.open(path);
            
            if (!root || !root.isDirectory()) {
                ST_LOG(STORAGE_LOG_ERROR, "FS: Can't open directory '%s'", path);
                return files;
            }
            
            File file = root.openNextFile();
            while (file) {
                files.push_back(String(file.name()));
                file = root.openNextFile();
            }
            
            root.close();
            return files;
        }

        /**
         * Получение информации о файле
         * @param path Путь к файлу
         * @return Размер файла в байтах, или 0 если файл не существует
         */
        static size_t getFileSize(const char* path) {
            File f = LittleFS.open(path, "r");
            if (!f) return 0;
            size_t size = f.size();
            f.close();
            return size;
        }

        /**
         * Получение свободного места
         * @return Свободное место в байтах
         */
        static size_t getFreeSpace() {
            return LittleFS.totalBytes() - LittleFS.usedBytes();
        }

        /**
         * Проверка существования файла
         * @param path Путь к файлу
         * @return true если файл существует
         */
        static bool fileExists(const char* path) {
            return LittleFS.exists(path);
        }
    };
#endif //юз биг дата
#endif