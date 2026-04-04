#ifndef HTTP_GATEWAY_HPP
#define HTTP_GATEWAY_HPP

#include <vector>
#include <cstdint>
#include <string>
#include "db_manager.hpp"

// Starts the HTTPS server in a blocking loop (intended to be run in a thread).
// cert_dir   — path to the directory containing server.crt and server.key
// d_pub/d_sec — the server's Dilithium5 public/secret key for batch signing
void RunHTTPServer(DBManager& db_manager, int port,
                   const std::string& cert_dir,
                   const std::vector<uint8_t>& d_pub,
                   const std::vector<uint8_t>& d_sec);

#endif // HTTP_GATEWAY_HPP
