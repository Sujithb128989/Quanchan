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
#ifndef SECURE_STORAGE_HPP
#define SECURE_STORAGE_HPP

#include <string>
#include <vector>
#include <mutex>
#include <openssl/evp.h>
#include <openssl/rand.h>

class SecureStorage {
public:
    SecureStorage(const std::string& key_path);
    ~SecureStorage();

    // Encrypts plaintext using AES-256-GCM.
    // Returns: IV (12 bytes) + Ciphertext + Tag (16 bytes)
    std::string Encrypt(const std::string& plaintext);

    // Decrypts data. Expects: IV (12 bytes) + Ciphertext + Tag (16 bytes)
    std::string Decrypt(const std::string& ciphertext_blob);

    // Encrypts a file from src path to dst path using AES-256-GCM (Streaming).
    // Format: [IV 12b][Ciphertext...][Tag 16b]
    void EncryptFile(const std::string& src_path, const std::string& dst_path);

    // Decrypts a file from src path to dst path (Streaming).
    void DecryptFile(const std::string& src_path, const std::string& dst_path);

    // Generates a new random 256-bit key.
    static std::vector<unsigned char> GenerateKey();

    // Saves a key to disk securely.
    static void SaveKey(const std::string& path, const std::vector<unsigned char>& key);

    // Helper to re-key the instance
    void SetKey(const std::vector<unsigned char>& new_key);

    // Getter for the current key (needed by DBManager for rotation)
    const unsigned char* GetKey() const { return key_; }

private:
    std::string key_path_;
    mutable std::mutex mutex_;
    unsigned char key_[32]; // AES-256 key

    void LoadKey();
};

#endif // SECURE_STORAGE_HPP
