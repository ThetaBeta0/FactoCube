#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <string>

class Logger {
private:
    std::ofstream logFile;
    std::string logFileName;

public:
    Logger(const std::string& filename = "log.txt") : logFileName(filename) {
        logFile.open(filename, std::ios::app);
    }

    ~Logger() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }

    void log(const std::string& message) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::string timestamp = std::ctime(&time);
        timestamp.pop_back(); // Remove newline character

        std::string output = "[" + timestamp + "] " + message;

        std::cout << output << std::endl;

        if (logFile.is_open()) {
            logFile << output << std::endl;
            logFile.flush();
        }
    }

    void logSection(const std::string& section) {
        log("======== " + section + " ========");
    }

    void logError(const std::string& error) {
        log("ERROR: " + error);
    }

    void logSuccess(const std::string& message) {
        log("SUCCESS: " + message);
    }
};

#endif // LOGGER_H