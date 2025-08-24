#include "LogManager.hpp"
#include <borealis.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>
#include <signal.h>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

LogManager& LogManager::instance() {
    static LogManager instance;
    s_instance = &instance;
    return instance;
}

LogManager::~LogManager() {
    shutdown();
}

bool LogManager::initialize(const std::string& log_dir) {
    // Generate timestamped log file path
    m_current_log_path = generateTimestampedLogPath(log_dir);
    
    // Open log file
    m_log_file = std::fopen(m_current_log_path.c_str(), "w");
    if (!m_log_file) {
        brls::Logger::error("Failed to open log file: {}", m_current_log_path);
        return false;
    }
    
    // Redirect borealis logger output to file
    brls::Logger::setLogOutput(m_log_file);
    
    // Clean up old logs
    cleanupOldLogs(log_dir);
    
    // Log initialization
    brls::Logger::info("LogManager initialized, logging to: {}", m_current_log_path);
    
    return true;
}

void LogManager::shutdown() {
    if (m_log_file) {
        brls::Logger::info("LogManager shutting down");
        std::fflush(m_log_file);
        std::fclose(m_log_file);
        m_log_file = nullptr;
        
        // Reset borealis logger to stdout
        brls::Logger::setLogOutput(stdout);
    }
}

void LogManager::installCrashHandlers() {
    // Install signal handlers for common crash signals
    signal(SIGSEGV, crashSignalHandler);  // Segmentation fault
    signal(SIGABRT, crashSignalHandler);  // Abort signal
    signal(SIGFPE, crashSignalHandler);   // Floating point exception
    signal(SIGBUS, crashSignalHandler);   // Bus error
    signal(SIGILL, crashSignalHandler);   // Illegal instruction
    
#ifdef PLATFORM_SWITCH
    // Additional Switch-specific signals if needed
    signal(SIGTERM, crashSignalHandler);
#endif
    
    brls::Logger::info("Crash signal handlers installed");
}

std::string LogManager::generateTimestampedLogPath(const std::string& log_dir) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << log_dir << "/log_" 
        << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") 
        << ".log";
    
    return oss.str();
}

void LogManager::cleanupOldLogs(const std::string& log_dir) {
    try {
        struct LogFileInfo {
            std::string filename;
            std::string fullpath;
            time_t mtime;
        };
        
        std::vector<LogFileInfo> log_files;
        
        // Collect all log files
        DIR* dir = opendir(log_dir.c_str());
        if (dir != nullptr) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string filename(entry->d_name);
                if (filename.substr(0, 4) == "log_" && filename.length() > 4 && 
                    filename.substr(filename.length() - 4) == ".log") {
                    std::string fullpath = log_dir + "/" + filename;
                    struct stat file_stat;
                    if (stat(fullpath.c_str(), &file_stat) == 0) {
                        log_files.push_back({filename, fullpath, file_stat.st_mtime});
                    }
                }
            }
            closedir(dir);
        }
        
        // Sort by modification time (newest first)
        std::sort(log_files.begin(), log_files.end(), 
                 [](const LogFileInfo& a, const LogFileInfo& b) {
                     return a.mtime > b.mtime;
                 });
        
        // Remove old log files if we have too many
        if (log_files.size() > MAX_LOG_FILES) {
            for (size_t i = MAX_LOG_FILES; i < log_files.size(); ++i) {
                if (remove(log_files[i].fullpath.c_str()) == 0) {
                    brls::Logger::debug("Removed old log file: {}", log_files[i].filename);
                }
            }
        }
    } catch (const std::exception& e) {
        brls::Logger::warning("Failed to cleanup old logs: {}", e.what());
    }
}

void LogManager::crashSignalHandler(int sig) {
    // Get signal name
    const char* signal_name = strsignal(sig);
    if (!signal_name) {
        signal_name = "Unknown signal";
    }
    
    // Log crash information
    if (s_instance && s_instance->m_log_file) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = *std::localtime(&time_t);
        
        fprintf(s_instance->m_log_file, 
                "\n=== CRASH DETECTED ===\n"
                "Time: %04d-%02d-%02d %02d:%02d:%02d\n"
                "Signal: %d (%s)\n"
                "======================\n",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec,
                sig, signal_name);
        
        fflush(s_instance->m_log_file);
    }
    
    // Reset to default handler and re-raise
    signal(sig, SIG_DFL);
    raise(sig);
}