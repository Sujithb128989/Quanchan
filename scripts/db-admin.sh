#!/usr/bin/env bash
set -euo pipefail

DB_CONTAINER="${DB_CONTAINER:-quanchan-db}"
DB_NAME="${DB_NAME:-quanchan}"
DB_USER="${DB_USER:-quanchan}"

psql_exec() {
  docker exec -i "$DB_CONTAINER" psql -v ON_ERROR_STOP=1 -U "$DB_USER" -d "$DB_NAME" "$@"
}

sql() {
  psql_exec -c "$1"
}

escape_sql() {
  printf "%s" "$1" | sed "s/'/''/g"
}

sha256_hex() {
  if command -v sha256sum >/dev/null 2>&1; then
    printf '%s' "$1" | sha256sum | awk '{print $1}'
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    printf '%s' "$1" | shasum -a 256 | awk '{print $1}'
    return
  fi
  printf '%s' "$1" | openssl dgst -sha256 -r | awk '{print $1}'
}

normalize_phrase() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | tr '\r\n\t' '   ' | xargs
}

founder_token_from_seed() {
  local normalized
  normalized="$(normalize_phrase "$1")"
  sha256_hex "quanchan-founder-token:${normalized}"
}

usage() {
  cat <<'EOF'
Usage:
  scripts/db-admin.sh show-profiles
  scripts/db-admin.sh show-founder
  scripts/db-admin.sh derive-founder-token "your seed phrase"
  scripts/db-admin.sh grant-founder <pub_key_hash> <founder_token>
  scripts/db-admin.sh grant-founder-seed <pub_key_hash> "your seed phrase"
  scripts/db-admin.sh clear-founder
  scripts/db-admin.sh set-role <actor_hash> <target_hash> <user|moderator>

Environment overrides:
  DB_CONTAINER=quanchan-db
  DB_NAME=quanchan
  DB_USER=quanchan

Notes:
  - Founder access is tied to the founder seed phrase via a deterministic founder token.
  - Founder token is stored hashed in profiles.founder_session_hash.
  - grant-founder-seed derives the exact token shape the browser restores from the same seed phrase.
EOF
}

cmd="${1:-}"

case "$cmd" in
  show-profiles)
    sql "SELECT pub_key_hash, COALESCE(username, '') AS username, COALESCE(role, 'user') AS role, COALESCE(role_assigned_by, '') AS role_assigned_by, founder_claimed_at FROM profiles ORDER BY founder_claimed_at DESC NULLS LAST, last_active DESC NULLS LAST;"
    ;;

  show-founder)
    sql "SELECT pub_key_hash, COALESCE(username, '') AS username, COALESCE(role, 'user') AS role, COALESCE(role_assigned_by, '') AS role_assigned_by, founder_claimed_at, CASE WHEN COALESCE(founder_session_hash, '') <> '' THEN 'configured' ELSE '' END AS founder_session FROM profiles WHERE role = 'founder';"
    ;;

  derive-founder-token)
    shift
    seed_phrase="${1:-}"
    if [[ -z "$seed_phrase" ]]; then
      echo "Seed phrase required" >&2
      exit 1
    fi
    founder_token_from_seed "$seed_phrase"
    ;;

  grant-founder)
    shift
    founder_hash="${1:-}"
    founder_token="${2:-}"
    if [[ -z "$founder_hash" || -z "$founder_token" ]]; then
      echo "Usage: scripts/db-admin.sh grant-founder <pub_key_hash> <founder_token>" >&2
      exit 1
    fi
    founder_hash_escaped="$(escape_sql "$founder_hash")"
    founder_token_hash="$(sha256_hex "$founder_token")"
    sql "INSERT INTO profiles (pub_key_hash, username, last_active) VALUES ('$founder_hash_escaped', '', NOW()) ON CONFLICT (pub_key_hash) DO NOTHING;"
    sql "UPDATE profiles SET role = 'founder', role_assigned_by = '$founder_hash_escaped', role_assigned_at = NOW(), founder_claimed_at = COALESCE(founder_claimed_at, NOW()), founder_session_hash = '$(escape_sql "$founder_token_hash")', last_active = NOW() WHERE pub_key_hash = '$founder_hash_escaped';"
    echo "Founder role granted to $founder_hash."
    echo "Keep this founder token safe: $founder_token"
    ;;

  grant-founder-seed)
    shift
    founder_hash="${1:-}"
    seed_phrase="${2:-}"
    if [[ -z "$founder_hash" || -z "$seed_phrase" ]]; then
      echo "Usage: scripts/db-admin.sh grant-founder-seed <pub_key_hash> <seed_phrase>" >&2
      exit 1
    fi
    founder_token="$(founder_token_from_seed "$seed_phrase")"
    founder_hash_escaped="$(escape_sql "$founder_hash")"
    founder_token_hash="$(sha256_hex "$founder_token")"
    sql "INSERT INTO profiles (pub_key_hash, username, last_active) VALUES ('$founder_hash_escaped', '', NOW()) ON CONFLICT (pub_key_hash) DO NOTHING;"
    sql "UPDATE profiles SET role = 'founder', role_assigned_by = '$founder_hash_escaped', role_assigned_at = NOW(), founder_claimed_at = COALESCE(founder_claimed_at, NOW()), founder_session_hash = '$(escape_sql "$founder_token_hash")', last_active = NOW() WHERE pub_key_hash = '$founder_hash_escaped';"
    echo "Founder role granted to $founder_hash using the deterministic token derived from the supplied seed phrase."
    echo "Seed-linked founder token: $founder_token"
    ;;

  clear-founder)
    sql "UPDATE profiles SET role = 'user', role_assigned_by = '', role_assigned_at = NULL, founder_session_hash = '', founder_claimed_at = NULL WHERE role = 'founder';"
    echo "Cleared founder role and founder session hash from all profiles."
    ;;

  set-role)
    shift
    actor_hash="${1:-}"
    target_hash="${2:-}"
    role="${3:-}"
    if [[ -z "$actor_hash" || -z "$target_hash" || -z "$role" ]]; then
      echo "Usage: scripts/db-admin.sh set-role <actor_hash> <target_hash> <user|moderator>" >&2
      exit 1
    fi
    case "$role" in
      user|moderator) ;;
      *)
        echo "Role must be user or moderator" >&2
        exit 1
        ;;
    esac
    actor_hash_escaped="$(escape_sql "$actor_hash")"
    target_hash_escaped="$(escape_sql "$target_hash")"
    sql "INSERT INTO profiles (pub_key_hash, username, last_active) VALUES ('$target_hash_escaped', '', NOW()) ON CONFLICT (pub_key_hash) DO NOTHING;"
    sql "UPDATE profiles SET role = '$(escape_sql "$role")', role_assigned_by = '$actor_hash_escaped', role_assigned_at = NOW(), last_active = NOW() WHERE pub_key_hash = '$target_hash_escaped';"
    echo "Set $target_hash role to $role."
    ;;

  *)
    usage
    exit 1
    ;;
esac
