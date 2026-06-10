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
#ifndef HTTP_GATEWAY_HPP
#define HTTP_GATEWAY_HPP

#include <vector>
#include <cstdint>
#include <string>
#include "db_manager.hpp"

// Starts the HTTPS server in a blocking loop (intended to be run in a thread).
// cert_dir   â€” path to the directory containing server.crt and server.key
// d_pub/d_sec â€” the server's Dilithium5 public/secret key for batch signing
void RunHTTPServer(DBManager& db_manager, int port,
                   const std::string& cert_dir,
                   const std::vector<uint8_t>& d_pub,
                   const std::vector<uint8_t>& d_sec);

#endif // HTTP_GATEWAY_HPP
