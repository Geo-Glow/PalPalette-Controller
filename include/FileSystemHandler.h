#ifndef FILESYSTEMHANDLER_H
#define FILESYSTEMHANDLER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <memory>
#include <vector>
#include <LittleFS.h>
#define FILESYSTEM LittleFS

#define INIT_FILE "/init_done"

enum class FileSystemResult
{
    Success,
    MountFailed,
    FileNotFound,
    FileOpenError,
    FileTooLarge,
    ParseError,
    WriteError,
    DeleteError
};

class FileSystemHandler
{
public:
    static bool initialize();

    // Configuration file operations
    static FileSystemResult loadConfigFromFile(const char *path, JsonDocument &jsonDoc, size_t maxAllowedSize = 4096);
    static FileSystemResult saveConfigToFile(const char *path, const JsonDocument &jsonDoc);
    static FileSystemResult removeConfigFile(const char *path);

    // Helper Functions
    static bool exists(const char *path);
    static size_t getFileSize(const char *path);

    // Logging Control
    static void setLogging(bool enabled);

private:
    static bool loggingEnabled;
    static bool isInitialized;

    static void logError(const String &message);
    static void logInfo(const String &message);
    static void logDebug(const String &message);

    class FilesystemGuard
    {
    public:
        FilesystemGuard();
        ~FilesystemGuard();
        bool isMounted() const { return mounted; }

    private:
        bool mounted;
    };

    static void formatFileSystem();
};

#endif // FILESYSTEMHANDLER_H