/*
 * Copyright (C) 2026 QuanChan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

enum class LogLevel {
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger {
public:
    static void Log(LogLevel level, const std::string& message, const std::string& component = "Server") {
        std::lock_guard<std::mutex> lock(mutex_);

        json log_entry;
        log_entry["timestamp"] = GetTimestamp();
        log_entry["level"] = LevelToString(level);
        log_entry["component"] = component;
        log_entry["message"] = message;

        std::cout << log_entry.dump() << std::endl;
    }

    static void Info(const std::string& message, const std::string& component = "Server") {
        Log(LogLevel::INFO, message, component);
    }

    static void Warn(const std::string& message, const std::string& component = "Server") {
        Log(LogLevel::WARN, message, component);
    }

    static void Error(const std::string& message, const std::string& component = "Server") {
        Log(LogLevel::ERROR, message, component);
    }

    static void Fatal(const std::string& message, const std::string& component = "Server") {
        Log(LogLevel::FATAL, message, component);
    }

private:
    static std::mutex mutex_;

    static std::string LevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARN: return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    static std::string GetTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%S");
        ss << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z";
        return ss.str();
    }
};

#endif // LOGGER_HPP
