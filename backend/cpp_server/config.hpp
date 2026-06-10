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
#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>
#include <nlohmann/json.hpp>

class Config {
public:
    struct ServerConfig {
        std::string address = "0.0.0.0:50051";
        std::string storage_path = "data/storage.bin";
        std::string key_path = "data/storage.key";
        std::string log_level = "INFO";
        std::string http_api_key = "changeme"; // Simple API Key for HTTP Gateway

        // S3 Configuration
        std::string s3_endpoint = ""; // e.g., "s3.amazonaws.com" or "localhost:9000"
        std::string s3_bucket = "";
        std::string s3_access_key = "";
        std::string s3_secret_key = "";
        std::string s3_region = "us-east-1";
        bool use_s3 = false;
        std::string server_salt = "";
    };

    static Config& Instance();

    void Load(const std::string& path);
    const ServerConfig& Get() const;

private:
    Config() = default;
    ServerConfig config_;
};

#endif // CONFIG_HPP
