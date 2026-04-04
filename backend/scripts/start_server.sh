#!/bin/sh
set -eu

OPENSSL_BIN="${OPENSSL_BIN:-/opt/openssl/bin/openssl}"
CERT_DIR="${CERT_DIR:-/app/certs}"
SERVER_CERT="${CERT_DIR}/server.crt"
SERVER_KEY="${CERT_DIR}/server.key"
CA_CERT="${CERT_DIR}/ca.crt"

require_file() {
    path="$1"
    label="$2"
    if [ ! -r "${path}" ]; then
        echo "Missing required ${label}: ${path}" >&2
        exit 1
    fi
}

require_file "${SERVER_CERT}" "server certificate"
require_file "${SERVER_KEY}" "server private key"
require_file "${CA_CERT}" "CA certificate"

cert_pub="$(mktemp)"
key_pub="$(mktemp)"
cleanup() {
    rm -f "${cert_pub}" "${key_pub}"
}
trap cleanup EXIT INT TERM

"${OPENSSL_BIN}" x509 -in "${SERVER_CERT}" -pubkey -noout > "${cert_pub}"
"${OPENSSL_BIN}" pkey -in "${SERVER_KEY}" -pubout > "${key_pub}"

if ! cmp -s "${cert_pub}" "${key_pub}"; then
    echo "Configured TLS certificate and private key do not match." >&2
    exit 1
fi

echo "Using fixed TLS material from ${CERT_DIR}"
exec /app/pqc_server
