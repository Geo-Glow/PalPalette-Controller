#ifndef FILESYSTEMHANDLER_H
#define FILESYSTEMHANDLER_H
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#define FILESYSTEM LittleFS

#define INIT_FILE "/init_done"

class FileSystemHandler
{
public:
    static bool begin();
    static void performInitialSetup();
    static bool loadConfigFromFile(const char *path, JsonDocument &jsonDoc, size_t jsonSize);
    static bool saveConfigToFile(const char *path, const JsonDocument &jsonDoc);
    static bool removeConfigFile(const char *path);

private:
    static void formatFileSystem();
};

#endif // FILESYSTEMHANDLER_H