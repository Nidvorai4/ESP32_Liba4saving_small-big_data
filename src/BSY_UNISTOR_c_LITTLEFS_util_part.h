#ifndef BSY_UNISTOR_C_LITTLEFS_UTIL_PART_H
#define BSY_UNISTOR_C_LITTLEFS_UTIL_PART_H

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
         * Полная очистка файловой системы
         * @warning Удаляет все данные! Требует перезагрузки!
         */
        static void fullResetFS() {
            ST_LOG(STORAGE_LOG_WARNING, "!!! FULL RESET STARTED !!!");
            
            if (LittleFS.format()) {
                ST_LOG(STORAGE_LOG_INFO, "FS: LittleFS formatted OK.");
            } else {
                ST_LOG(STORAGE_LOG_ERROR, "FS: LittleFS format failed.");
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


#endif