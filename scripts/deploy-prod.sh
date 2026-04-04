#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

SECRETS_DIR="${QUANCHAN_SECRETS_DIR:-/etc/quanchan}"
ENV_FILE="${QUANCHAN_ENV_FILE:-${SECRETS_DIR}/.env}"
CERTS_DIR="${QUANCHAN_CERTS_DIR:-${SECRETS_DIR}}"

if [ ! -f "${ENV_FILE}" ]; then
  echo "Missing production env file: ${ENV_FILE}"
  echo "Store your real server-only .env outside the repo, for example in ${SECRETS_DIR}/.env."
  exit 1
fi

for required in server.crt server.key ca.crt; do
  if [ ! -f "${CERTS_DIR}/${required}" ]; then
    echo "Missing required production TLS file: ${CERTS_DIR}/${required}"
    echo "Store your fixed origin certificate set outside the repo, for example in ${SECRETS_DIR}."
    exit 1
  fi
done

echo "Using env file: ${ENV_FILE}"
echo "Using certificate directory: ${CERTS_DIR}"

export QUANCHAN_CERTS_DIR="${CERTS_DIR}"

docker compose --env-file "${ENV_FILE}" up -d db
nice -n 19 docker compose --env-file "${ENV_FILE}" build pqc-backend
docker compose --env-file "${ENV_FILE}" up -d pqc-backend --force-recreate --no-deps
docker compose --env-file "${ENV_FILE}" ps
