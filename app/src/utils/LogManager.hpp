#pragma once

#include <string>
#include <cstdio>
#include <memory>

class LogManager {
public:
    static LogManager& instance();
    
    bool initialize(const std::string& log_dir);
    void shutdown();
    
    void installCrashHandlers();
    
    const std::string& getLogPath() const { return m_current_log_path; }
    
private:
    LogManager() = default;
    ~LogManager();
    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;
    
    std::string generateTimestampedLogPath(const std::string& log_dir);
    void cleanupOldLogs(const std::string& log_dir);
    
    static void crashSignalHandler(int sig);
    
    std::string m_current_log_path;
    std::FILE* m_log_file = nullptr;
    
    static const int MAX_LOG_FILES = 10;
    static inline LogManager* s_instance = nullptr;
};