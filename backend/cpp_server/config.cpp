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
#include "config.hpp"
#include "logger.hpp"
#include <fstream>
#include <iostream>
#include <cstdlib>

Config& Config::Instance() {
    static Config instance;
    return instance;
}

void Config::Load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Logger::Warn("Config file not found at " + path + ". Using defaults.", "Config");
        const char* env_salt = std::getenv("QC_SERVER_SALT");
        if (env_salt) {
            config_.server_salt = env_salt;
        } else {
            std::string fallback = config_.s3_secret_key + config_.s3_access_key;
            if (fallback.empty()) {
                fallback = "quanchan_default_salt_fallback_key_2026";
            }
            config_.server_salt = fallback;
        }
        return;
    }

    try {
        nlohmann::json j;
        file >> j;

        if (j.contains("address")) config_.address = j["address"];
        if (j.contains("storage_path")) config_.storage_path = j["storage_path"];
        if (j.contains("key_path")) config_.key_path = j["key_path"];
        if (j.contains("log_level")) config_.log_level = j["log_level"];
        if (j.contains("http_api_key")) config_.http_api_key = j["http_api_key"];

        if (j.contains("s3")) {
            auto& s3 = j["s3"];
            if (s3.contains("endpoint")) config_.s3_endpoint = s3["endpoint"];
            if (s3.contains("bucket")) config_.s3_bucket = s3["bucket"];
            if (s3.contains("access_key")) config_.s3_access_key = s3["access_key"];
            if (s3.contains("secret_key")) config_.s3_secret_key = s3["secret_key"];
            if (s3.contains("region")) config_.s3_region = s3["region"];
            if (!config_.s3_bucket.empty() && !config_.s3_access_key.empty()) {
                config_.use_s3 = true;
            }
        }

        if (j.contains("server_salt")) {
            config_.server_salt = j["server_salt"];
        } else {
            const char* env_salt = std::getenv("QC_SERVER_SALT");
            if (env_salt) {
                config_.server_salt = env_salt;
            } else {
                std::string fallback = config_.s3_secret_key + config_.s3_access_key;
                if (fallback.empty()) {
                    fallback = "quanchan_default_salt_fallback_key_2026";
                }
                config_.server_salt = fallback;
            }
        }

        Logger::Info("Configuration loaded from " + path, "Config");
    } catch (const std::exception& e) {
        Logger::Error("Failed to parse config file: " + std::string(e.what()), "Config");
    }
}

const Config::ServerConfig& Config::Get() const {
    return config_;
}
