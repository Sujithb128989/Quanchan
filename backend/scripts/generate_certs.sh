#!/bin/sh
set -e

OPENSSL_BIN="${OPENSSL_BIN:-/opt/openssl/bin/openssl}"
SIG_ALG="${SIG_ALG:-ML-DSA-87}"
CA_COMMON_NAME="${CA_COMMON_NAME:-PQC-CA}"

if [ -z "$1" ]; then
    echo "Usage: $0 <certificate_directory> [san1,san2,...]"
    echo "Example: $0 ./certs localhost,192.168.1.50,quanchan.demo"
    exit 1
fi

CERT_DIR="$1"
SAN_LIST="${2:-localhost,127.0.0.1}"
DAYS_VALID=365

mkdir -p "${CERT_DIR}"

echo "Using OpenSSL version:"
"${OPENSSL_BIN}" version
echo ""

echo "Generating certificates using ${SIG_ALG}..."

if [ "${SIG_ALG}" = "RSA" ]; then
  "${OPENSSL_BIN}" genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "${CERT_DIR}/ca.key"
else
  "${OPENSSL_BIN}" genpkey -algorithm "${SIG_ALG}" -out "${CERT_DIR}/ca.key"
fi
"${OPENSSL_BIN}" req -new -x509 \
  -key "${CERT_DIR}/ca.key" \
  -out "${CERT_DIR}/ca.crt" \
  -subj "/CN=${CA_COMMON_NAME}" \
  -days "${DAYS_VALID}"

if [ "${SIG_ALG}" = "RSA" ]; then
  "${OPENSSL_BIN}" genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "${CERT_DIR}/server.key"
else
  "${OPENSSL_BIN}" genpkey -algorithm "${SIG_ALG}" -out "${CERT_DIR}/server.key"
fi
"${OPENSSL_BIN}" req -new \
  -key "${CERT_DIR}/server.key" \
  -out "${CERT_DIR}/server.csr" \
  -subj "/CN=$(printf "%s" "${SAN_LIST}" | cut -d',' -f1)"

cat <<EOF > "${CERT_DIR}/san.cnf"
[req]
distinguished_name=dn
[dn]
[v3_req]
subjectAltName=$(printf "%s" "${SAN_LIST}" | awk -F',' '
  BEGIN { out="" }
  {
    for (i = 1; i <= NF; i++) {
      value = $i
      gsub(/^[[:space:]]+|[[:space:]]+$/, "", value)
      if (value == "") continue
      prefix = (value ~ /^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/) ? "IP" : "DNS"
      out = out (out ? "," : "") prefix ":" value
    }
  }
  END { print out }
')
basicConstraints=CA:FALSE
keyUsage=digitalSignature
extendedKeyUsage=serverAuth
EOF

"${OPENSSL_BIN}" x509 -req \
  -in "${CERT_DIR}/server.csr" \
  -out "${CERT_DIR}/server.crt" \
  -CA "${CERT_DIR}/ca.crt" \
  -CAkey "${CERT_DIR}/ca.key" \
  -CAcreateserial \
  -days "${DAYS_VALID}" \
  -extfile "${CERT_DIR}/san.cnf" \
  -extensions v3_req

if [ "${SIG_ALG}" = "RSA" ]; then
  "${OPENSSL_BIN}" genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "${CERT_DIR}/client.key"
else
  "${OPENSSL_BIN}" genpkey -algorithm "${SIG_ALG}" -out "${CERT_DIR}/client.key"
fi
"${OPENSSL_BIN}" req -new \
  -key "${CERT_DIR}/client.key" \
  -out "${CERT_DIR}/client.csr" \
  -subj "/CN=pqc-client"

"${OPENSSL_BIN}" x509 -req \
  -in "${CERT_DIR}/client.csr" \
  -out "${CERT_DIR}/client.crt" \
  -CA "${CERT_DIR}/ca.crt" \
  -CAkey "${CERT_DIR}/ca.key" \
  -CAcreateserial \
  -days "${DAYS_VALID}"

rm -f "${CERT_DIR}"/*.csr "${CERT_DIR}"/*.srl "${CERT_DIR}/san.cnf"

echo ""
echo "Certificates successfully generated using ${SIG_ALG}:"
ls -lh "${CERT_DIR}"

echo ""
echo "Certificate algorithm verification:"
"${OPENSSL_BIN}" x509 -in "${CERT_DIR}/server.crt" -noout -text | grep -A2 "Public Key Algorithm"
