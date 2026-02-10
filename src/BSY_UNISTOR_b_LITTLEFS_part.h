#ifndef BSY_UNISTOR_B_LITTLEFS_PART_H
#define BSY_UNISTOR_B_LITTLEFS_PART_H
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


#endif