#include "FileSystemHandler.h"

bool FileSystemHandler::begin()
{
    if (!FILESYSTEM.begin())
    {
        Serial.println("Failed to mount file system");
        FILESYSTEM.format();
        if (!FILESYSTEM.begin())
        {
            Serial.println("LittleFS reformatted, but still unable to mount.");
            return false;
        }
        else
        {
            Serial.println("LittleFS reformatted and mounted successfully.");
        }
    }

    if (!FILESYSTEM.exists(INIT_FILE))
    {
        Serial.println("Performing intial setup");
        performInitialSetup();
    }
    else
    {
        Serial.println("File system is already set up");
    }
    return true;
}

void FileSystemHandler::performInitialSetup()
{
    formatFileSystem();

    File file = FILESYSTEM.open(INIT_FILE, "w");
    if (file)
    {
        file.println("Initialized");
        file.close();
    }
    else
    {
        Serial.println("Init failed");
    }
}

void FileSystemHandler::formatFileSystem()
{
    Serial.println("Formatting file system...");
    FILESYSTEM.format();
    Serial.println("File system formatted");
}

bool FileSystemHandler::removeConfigFile(const char *path)
{
    if (!FILESYSTEM.begin())
    {
        Serial.println("Failed to mount FS for delete");
        return false;
    }
    if (FILESYSTEM.exists(path))
    {
        if (!FILESYSTEM.remove(path))
        {
            Serial.println("Failed to delete config file");
            FILESYSTEM.end();
            return false;
        }
        Serial.println("Config file deleted");
    }
    else
    {
        Serial.println("Config file does not exist");
    }

    FILESYSTEM.end();
    return true;
}

bool FileSystemHandler::loadConfigFromFile(const char *path, JsonDocument &jsonDoc, size_t jsonSize)
{
    if (!FILESYSTEM.begin())
    {
        Serial.println("Failed to mount FS");
        return false;
    }

    if (!FILESYSTEM.exists(path))
    {
        Serial.println("Config file does not exist");
        FILESYSTEM.end();
        return false;
    }

    File configFile = FILESYSTEM.open(path, "r");
    if (!configFile)
    {
        Serial.println("Failed to open config file");
        FILESYSTEM.end();
        return false;
    }

    size_t size = configFile.size();
    if (size > jsonSize)
    {
        Serial.println("Config file is too large");
        configFile.close();
        FILESYSTEM.end();
        return false;
    }

    std::unique_ptr<char[]> buf(new char[size]);
    configFile.readBytes(buf.get(), size);
    configFile.close();
    FILESYSTEM.end();

    DeserializationError error = deserializeJson(jsonDoc, buf.get());
    if (error)
    {
        Serial.println("Failed to parse JSON config file");
        return false;
    }

    Serial.println("Parsed JSON config");
    return true;
}

bool FileSystemHandler::saveConfigToFile(const char *path, const JsonDocument &jsonDoc)
{
    if (!FILESYSTEM.begin())
    {
        Serial.println("Failed to mount FS for save");
        return false;
    }

    File configFile = FILESYSTEM.open(path, "w");
    if (!configFile)
    {
        Serial.println("Failed to open config file for writing");
        FILESYSTEM.end();
        return false;
    }

    if (serializeJson(jsonDoc, configFile) == 0)
    {
        Serial.println("Failed to write JSON to config file");
        configFile.close();
        FILESYSTEM.end();
        return false;
    }

    Serial.println("Config saved successfully");
    configFile.close();
    FILESYSTEM.end();
    return true;
}