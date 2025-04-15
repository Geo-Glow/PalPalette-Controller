#include "FileSystemHandler.h"

bool FileSystemHandler::loggingEnabled = true;
bool FileSystemHandler::isInitialized = false;

void FileSystemHandler::formatFileSystem()
{
    logInfo("Formatting filesystem...");
    if (FILESYSTEM.format())
    {
        logInfo("Filesystem formatted successfully");
    }
    else
    {
        logError("Formatting failed");
    }
}

bool FileSystemHandler::initialize()
{
    if (isInitialized)
        return true;

    if (!FILESYSTEM.begin())
    {
        logError("Failed to mount file system, attempting to format...");
        formatFileSystem();

        if (!FILESYSTEM.begin())
        {
            logError("Failed to mount file system after formatting");
            return false;
        }
    }

    isInitialized = true;
    logInfo("File system initialized successfully");
    return true;
}

FileSystemHandler::FilesystemGuard::FilesystemGuard() : mounted(false)
{
    if (!isInitialized && !initialize())
    {
        logError("Filesystem not initialized");
        return;
    }
    mounted = true;
}

FileSystemHandler::FilesystemGuard::~FilesystemGuard() {}

FileSystemResult FileSystemHandler::loadConfigFromFile(const char *path, JsonDocument &jsonDoc, size_t maxAllowedSize)
{
    FilesystemGuard guard;
    if (!guard.isMounted())
        return FileSystemResult::MountFailed;

    if (!FILESYSTEM.exists(path))
    {
        logDebug(String("Config file not found: ") + path);
        return FileSystemResult::FileNotFound;
    }

    File configFile = FILESYSTEM.open(path, "r");
    if (!configFile)
    {
        logError(String("Failed to open config file: ") + path);
        return FileSystemResult::FileOpenError;
    }

    size_t size = configFile.size();
    if (size > maxAllowedSize)
    {
        logError(String("Config file too large: ") + size + " (max: " + maxAllowedSize + ")");
        configFile.close();
        return FileSystemResult::FileTooLarge;
    }

    std::vector<char> buf(size);
    configFile.readBytes(buf.data(), size);
    configFile.close();

    DeserializationError error = deserializeJson(jsonDoc, buf.data());
    if (error)
    {
        logError(String("JSON parse error: ") + error.c_str());
        return FileSystemResult::ParseError;
    }

    logDebug(String("Successfully loaded config file: ") + path);
    return FileSystemResult::Success;
}

FileSystemResult FileSystemHandler::saveConfigToFile(const char *path, const JsonDocument &jsonDoc)
{
    FilesystemGuard guard;
    if (!guard.isMounted())
        return FileSystemResult::MountFailed;

    String tempPath = String(path) + ".tmp";

    File configFile = FILESYSTEM.open(tempPath.c_str(), "w");
    if (!configFile)
    {
        logError(String("Failed to open config file for writing: ") + tempPath);
        return FileSystemResult::FileOpenError;
    }

    size_t bytesWritten = serializeJson(jsonDoc, configFile);
    configFile.close();

    if (bytesWritten == 0)
    {
        logError("Failed to write JSON to config file");
        FILESYSTEM.remove(tempPath.c_str());
        return FileSystemResult::WriteError;
    }

    if (FILESYSTEM.exists(path))
    {
        FILESYSTEM.remove(path);
    }

    if (!FILESYSTEM.rename(tempPath.c_str(), path))
    {
        logError("Failed to replace config file with new version");
        FILESYSTEM.remove(tempPath.c_str());
        return FileSystemResult::WriteError;
    }

    logDebug(String("Successfully saved config: ") + path);
    return FileSystemResult::Success;
}

FileSystemResult FileSystemHandler::removeConfigFile(const char *path)
{
    FilesystemGuard guard;
    if (!guard.isMounted())
        return FileSystemResult::MountFailed;

    if (!FILESYSTEM.exists(path))
    {
        logDebug(String("Config file does not exist: ") + path);
        return FileSystemResult::FileNotFound;
    }

    if (!FILESYSTEM.remove(path))
    {
        logError(String("Failed to delete config file: ") + path);
        return FileSystemResult::DeleteError;
    }

    logDebug(String("Successfully deleted config file: ") + path);
    return FileSystemResult::Success;
}

bool FileSystemHandler::exists(const char *path)
{
    FilesystemGuard guard;
    return guard.isMounted() && FILESYSTEM.exists(path);
}

size_t FileSystemHandler::getFileSize(const char *path)
{
    FilesystemGuard guard;
    if (!guard.isMounted())
        return 0;

    File file = FILESYSTEM.open(path, "r");
    if (!file)
        return 0;

    size_t size = file.size();
    file.close();
    return size;
}

void FileSystemHandler::setLogging(bool enabled)
{
    loggingEnabled = enabled;
}

void FileSystemHandler::logError(const String &message)
{
    if (loggingEnabled)
        Serial.println("[ERROR] " + message);
}

void FileSystemHandler::logInfo(const String &message)
{
    if (loggingEnabled)
        Serial.println("[INFO] " + message);
}

void FileSystemHandler::logDebug(const String &message)
{
    if (loggingEnabled)
        Serial.println("[DEBUG] " + message);
}