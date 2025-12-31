#!/usr/bin/env bash
set -Eeuo pipefail

# -------- Resolve project root no matter where called from --------
abspath() {
  local p="$1"
  if command -v readlink >/dev/null 2>&1 && readlink -f / >/dev/null 2>&1; then
    readlink -f "$p"
  elif command -v realpath >/dev/null 2>&1; then
    realpath "$p"
  else
    # fallback (no symlink resolution, but works everywhere)
    local d b
    d="$(cd "$(dirname "$p")" && pwd -P)"
    b="$(basename "$p")"
    echo "${d}/${b}"
  fi
}

SCRIPT_PATH="$(abspath "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$SCRIPT_PATH")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BIN_DIR="${ROOT_DIR}/bin"

# Parse arguments
SERVER_TYPE="${1:-rust}"
TRANSPORT="${2:-zmq}"
DURATION_RAW="${3:-5s}"

# Validate server type
case "$SERVER_TYPE" in
  rust|cpp)
    ;;
  *)
    echo "ERROR: Invalid server type '$SERVER_TYPE'. Must be: rust or cpp" >&2
    echo "Usage: $0 [server_type] [transport] [duration]" >&2
    echo "  server_type: rust|cpp (default: rust)" >&2
    echo "  transport:   zmq|tcp|udp (default: zmq)" >&2
    echo "  duration:    5s, 10s, 60s, etc. (default: 5s)" >&2
    exit 1
    ;;
esac

# Validate transport
case "$TRANSPORT" in
  zmq|tcp|udp)
    ;;
  *)
    echo "ERROR: Invalid transport '$TRANSPORT'. Must be: zmq, tcp, or udp" >&2
    echo "Usage: $0 [server_type] [transport] [duration]" >&2
    echo "  server_type: rust|cpp (default: rust)" >&2
    echo "  transport:   zmq|tcp|udp (default: zmq)" >&2
    echo "  duration:    5s, 10s, 60s, etc. (default: 5s)" >&2
    exit 1
    ;;
esac

# Parse duration (strip 's' suffix if present)
DURATION="${DURATION_RAW%s}"
if ! [[ "$DURATION" =~ ^[0-9]+$ ]]; then
  echo "ERROR: Invalid duration '$DURATION_RAW'. Must be a number with optional 's' suffix (e.g., 5s, 10s, 60s)" >&2
  exit 1
fi

# Set bind and host format based on transport
if [[ "$TRANSPORT" == "zmq" ]]; then
  # ZMQ uses tcp:// prefix
  SERVER_BIND="tcp://*:7000"
  CLIENT_HOST="tcp://127.0.0.1"
else
  # TCP and UDP use plain IP:port format
  SERVER_BIND="127.0.0.1:7000"
  CLIENT_HOST="127.0.0.1"
fi

# Select server binary based on type
if [[ "$SERVER_TYPE" == "rust" ]]; then
  SERVER_BIN="${BIN_DIR}/multiverse_server_rust"
else
  SERVER_BIN="${BIN_DIR}/multiverse_server_cpp"
fi

SERVER_CMD=( "$SERVER_BIN" --transport "$TRANSPORT" --bind "$SERVER_BIND" )

C1_CMD=( "${BIN_DIR}/test_multiverse_client_all" --transport "$TRANSPORT" --mode receiver --host "$CLIENT_HOST" --server 7000 --client 8001 --sim sim_1 )
C2_CMD=( "${BIN_DIR}/test_multiverse_client_all" --transport "$TRANSPORT" --mode sender   --host "$CLIENT_HOST" --server 7000 --client 8002 --sim sim_2 )
C3_CMD=( "${BIN_DIR}/test_multiverse_client_all" --transport "$TRANSPORT" --mode both1    --host "$CLIENT_HOST" --server 7000 --client 8003 --sim sim_3 )
C4_CMD=( "${BIN_DIR}/test_multiverse_client_all" --transport "$TRANSPORT" --mode both2    --host "$CLIENT_HOST" --server 7000 --client 8004 --sim sim_4 )

RUN_ID="$(date +'%Y%m%d_%H%M%S')"
LOG_DIR="${ROOT_DIR}/logs/${RUN_ID}"
mkdir -p "$LOG_DIR"

COMBINED_LOG="${LOG_DIR}/combined.log"
: > "$COMBINED_LOG"  # create/truncate

# We store PGIDs so cleanup can kill whole process group (server + any children)
PGIDS=()

log() {
  local line
  line="[$(date +'%F %T')] $*"
  echo "$line" >&2
  echo "$line" >> "$COMBINED_LOG"
}

require_bin() {
  local f="$1"
  if [[ ! -x "$f" ]]; then
    log "ERROR: missing executable: $f"
    log "Project root detected as: $ROOT_DIR"
    exit 1
  fi
}

wait_port_listen() {
  local port="$1"
  local tries=50
  local i
  for ((i=1; i<=tries; i++)); do
    if command -v ss >/dev/null 2>&1; then
      ss -ltn 2>/dev/null | awk '{print $4}' | grep -qE ":${port}\$" && return 0
    elif command -v netstat >/dev/null 2>&1; then
      netstat -ltn 2>/dev/null | awk '{print $4}' | grep -qE ":${port}\$" && return 0
    else
      # no ss/netstat; just wait a bit
      sleep 0.1
      return 0
    fi
    sleep 0.1
  done
  return 1
}

start_proc() {
  local name="$1"; shift
  local logfile="${LOG_DIR}/${name}.log"
  : > "$logfile"

  log "START ${name}: $*"

  # prefix each line with [name] and tee to:
  #  - per-proc log
  #  - combined log
  #  - terminal (stdout)
  #
  # IMPORTANT: we background the REAL binary process and capture its PID/PGID.
  # Use setsid if available so each proc has its own PGID (kill -TERM -PGID works).
  local runner=( "$@" )
  if command -v stdbuf >/dev/null 2>&1; then
    runner=( stdbuf -oL -eL "${runner[@]}" )
  fi

  if command -v setsid >/dev/null 2>&1; then
    setsid "${runner[@]}" \
      > >(awk -v n="$name" '{print "[" n "] " $0; fflush()}' | tee -a "$logfile" | tee -a "$COMBINED_LOG") \
      2>&1 &
  else
    "${runner[@]}" \
      > >(awk -v n="$name" '{print "[" n "] " $0; fflush()}' | tee -a "$logfile" | tee -a "$COMBINED_LOG") \
      2>&1 &
  fi

  local pid=$!
  # For kill -TERM -PGID, PGID is usually PID when started via setsid
  local pgid="$pid"
  if command -v ps >/dev/null 2>&1; then
    pgid="$(ps -o pgid= -p "$pid" 2>/dev/null | tr -d ' ' || echo "$pid")"
  fi

  PGIDS+=( "$pgid" )
  log "RUNNING ${name}: pid=${pid}, pgid=${pgid}"
}

cleanup() {
  set +e
  log "Stopping all processes..."

  # TERM whole groups first (negative pgid)
  for pgid in "${PGIDS[@]}"; do
    kill -TERM "-${pgid}" 2>/dev/null || true
  done

  sleep 2

  # KILL if still alive
  for pgid in "${PGIDS[@]}"; do
    kill -0 "-${pgid}" 2>/dev/null && kill -KILL "-${pgid}" 2>/dev/null || true
  done

  log "Logs saved to: ${LOG_DIR}"
  log "Combined log:  ${COMBINED_LOG}"
}

trap cleanup EXIT INT TERM

# ---- checks ----
require_bin "$SERVER_BIN"
require_bin "${BIN_DIR}/test_multiverse_client_all"

log "Project root: ${ROOT_DIR}"
log "Server type: ${SERVER_TYPE}"
log "Transport: ${TRANSPORT}"
log "Duration: ${DURATION}s"
log "Log dir: ${LOG_DIR}"

# ---- start server then clients ----
start_proc "server" "${SERVER_CMD[@]}"

if wait_port_listen 7000; then
  log "Server port 7000 appears to be listening."
else
  log "WARNING: port 7000 did not show as listening (continuing anyway)."
fi

sleep 0.2
start_proc "client_receiver_sim1" "${C1_CMD[@]}"
sleep 0.2
start_proc "client_sender_sim2"   "${C2_CMD[@]}"
sleep 0.2
start_proc "client_both1_sim3"    "${C3_CMD[@]}"
sleep 0.2
start_proc "client_both2_sim4"    "${C4_CMD[@]}"

log "Running test for ${DURATION}s..."
sleep "${DURATION}"

log "Test duration reached (${DURATION}s). Exiting (cleanup will run)."
exit 0
