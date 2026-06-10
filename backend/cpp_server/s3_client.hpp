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
#ifndef S3_CLIENT_HPP
#define S3_CLIENT_HPP

#include <string>
#include "config.hpp"

class S3Client {
public:
    S3Client(const Config::ServerConfig& config);
    ~S3Client();

    // Uploads a file from local path to S3 bucket
    bool UploadFile(const std::string& local_path, const std::string& s3_key);

    // Downloads a file from S3 bucket to local path
    bool DownloadFile(const std::string& s3_key, const std::string& local_path);

private:
    std::string endpoint_;
    std::string bucket_;
    std::string access_key_;
    std::string secret_key_;
    std::string region_;

    // Generates AWS Signature V4 headers
    // Returns a map of headers or modifies curl headers directly
    // For simplicity in MVP, we will implement a basic header signer inside the Put/Get methods.
};

#endif // S3_CLIENT_HPP
