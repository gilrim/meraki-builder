#!/usr/bin/env bash
set -Eeuo pipefail
shopt -s nullglob

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "$SCRIPT_DIR/../.." && pwd)
ARTIFACTS_DIR=${ARTIFACTS_DIR:-"$REPO_ROOT/artifacts"}
TFTP_PORT=${TFTP_PORT:-1069}
CONFIG_DIR=${XDG_CONFIG_HOME:-"$HOME/.config"}/meraki-fw-flasher
KNOWN_HOSTS="$CONFIG_DIR/known_hosts"
LOG_DIR=${MERAKI_FLASH_LOG_DIR:-"$REPO_ROOT/logs"}
TMP=""
TFTP_PID=""
TAIL_PID=""
SSH_PASSWORD=""
SSH_AUTH_MODE="key"

usage() {
    cat <<'USAGE'
usage: firmware-flasher.sh [options]

Interactively selects a valid MS42/MS42P firmware artifact, publishes it from a
temporary read-only TFTP server, and starts fw_update_tftp through either SSH or
the hardware serial console.

Options:
  --artifacts DIR    artifact directory (default: ../../artifacts)
  --tftp-port PORT   unprivileged TFTP port (default: 1069)
  --self-test        test the built-in TFTP server locally and exit
  --help             show this help

The script does not store SSH passwords in plaintext. When secret-tool and a
working desktop keyring are available, it offers to save/retrieve the password
there. SSH keys continue to work without a password prompt.
USAGE
}

log() { printf '[flasher] %s\n' "$*"; }
warn() { printf '[flasher] warning: %s\n' "$*" >&2; }
die() { printf '[flasher] error: %s\n' "$*" >&2; exit 1; }

cleanup() {
    local rc=$?
    trap - EXIT INT TERM
    [[ -n "$TAIL_PID" ]] && kill "$TAIL_PID" 2>/dev/null || true
    [[ -n "$TFTP_PID" ]] && kill "$TFTP_PID" 2>/dev/null || true
    [[ -n "$TFTP_PID" ]] && wait "$TFTP_PID" 2>/dev/null || true
    SSH_PASSWORD=""
    [[ -n "$TMP" && -d "$TMP" ]] && rm -rf -- "$TMP"
    exit "$rc"
}
trap cleanup EXIT INT TERM

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "required command is missing: $1"
}

prompt_default() {
    local prompt=$1 default=$2 value
    read -r -p "$prompt [$default]: " value
    printf '%s' "${value:-$default}"
}

prompt_yes_no() {
    local prompt=$1 default=${2:-y} answer suffix
    if [[ $default == y ]]; then suffix='[Y/n]'; else suffix='[y/N]'; fi
    read -r -p "$prompt $suffix " answer
    answer=${answer:-$default}
    [[ $answer =~ ^[Yy]$ ]]
}

safe_tftp_name() {
    local name=$1
    name=${name//[^A-Za-z0-9._-]/_}
    [[ -n $name ]] || name=firmware.bin
    printf '%s' "$name"
}

read_magic_hex() {
    local file=$1 offset=$2
    dd if="$file" bs=1 skip="$offset" count=4 status=none 2>/dev/null | od -An -tx1 | tr -d ' \n'
}

classify_firmware() {
    local file=$1 size magic0 magic_kernel magic_root
    size=$(stat -c %s -- "$file" 2>/dev/null) || return 1
    magic0=$(read_magic_hex "$file" 0)
    if (( size == 16777216 )); then
        magic_kernel=$(read_magic_hex "$file" $((0x40000)))
        magic_root=$(read_magic_hex "$file" $((0x300000)))
        if [[ $magic_kernel == 5350494d && $magic_root == 68737173 ]]; then
            printf 'full'
            return 0
        fi
    fi
    if (( size > 0 && size <= 8388608 )) && [[ $magic0 == 68737173 ]]; then
        printf 'squashfs'
        return 0
    fi
    return 1
}

human_size() {
    local bytes=$1
    awk -v b="$bytes" 'BEGIN {
        if (b >= 1073741824) printf "%.2f GiB", b/1073741824;
        else if (b >= 1048576) printf "%.2f MiB", b/1048576;
        else if (b >= 1024) printf "%.2f KiB", b/1024;
        else printf "%d B", b;
    }'
}

expected_rootfs_hash() {
    local file=$1 type=$2
    if [[ $type == full ]]; then
        dd if="$file" bs=1048576 skip=3 count=8 status=none | sha256sum | awk '{print $1}'
    else
        python3 - "$file" <<'PY'
import hashlib, os, sys
path = sys.argv[1]
limit = 8 * 1024 * 1024
h = hashlib.sha256()
size = 0
with open(path, 'rb') as f:
    while True:
        block = f.read(1024 * 1024)
        if not block:
            break
        size += len(block)
        h.update(block)
if size > limit:
    raise SystemExit('SquashFS is larger than 8 MiB')
fill = b'\xff' * (1024 * 1024)
remaining = limit - size
while remaining:
    chunk = fill[:min(len(fill), remaining)]
    h.update(chunk)
    remaining -= len(chunk)
print(h.hexdigest())
PY
    fi
}

write_tftp_server() {
    cat > "$TMP/tftp_server.py" <<'PY'
#!/usr/bin/env python3
import os
import socket
import struct
import sys
import threading
from pathlib import Path

bind_ip = sys.argv[1]
port = int(sys.argv[2])
root = Path(sys.argv[3]).resolve()

OP_RRQ, OP_DATA, OP_ACK, OP_ERROR, OP_OACK = 1, 3, 4, 5, 6


def log(message):
    print(f"[TFTP] {message}", flush=True)


def error(sock, peer, code, message):
    packet = struct.pack("!HH", OP_ERROR, code) + message.encode("ascii", "replace") + b"\0"
    try:
        sock.sendto(packet, peer)
    except OSError:
        pass


def parse_rrq(data):
    if len(data) < 4 or struct.unpack("!H", data[:2])[0] != OP_RRQ:
        return None
    fields = data[2:].split(b"\0")
    if len(fields) < 3:
        return None
    filename = fields[0].decode("utf-8", "strict")
    mode = fields[1].decode("ascii", "ignore").lower()
    opts = {}
    rest = fields[2:]
    for i in range(0, len(rest) - 1, 2):
        if not rest[i]:
            break
        key = rest[i].decode("ascii", "ignore").lower()
        value = rest[i + 1].decode("ascii", "ignore")
        opts[key] = value
    return filename, mode, opts


def wait_for_ack(sock, peer, expected, packet, timeout, attempts=6):
    sock.settimeout(timeout)
    for _ in range(attempts):
        sock.sendto(packet, peer)
        try:
            while True:
                reply, sender = sock.recvfrom(65535)
                if sender != peer or len(reply) < 4:
                    continue
                opcode, block = struct.unpack("!HH", reply[:4])
                if opcode == OP_ACK and block == expected:
                    return True
                if opcode == OP_ERROR:
                    return False
        except socket.timeout:
            continue
    return False


def serve_rrq(data, peer):
    parsed = parse_rrq(data)
    if not parsed:
        return
    filename, mode, requested = parsed
    if mode not in ("octet", "netascii"):
        return
    relative = Path(filename.lstrip("/"))
    if relative.is_absolute() or ".." in relative.parts:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
            error(tx, peer, 2, "Access violation")
        return
    path = (root / relative).resolve()
    try:
        path.relative_to(root)
    except ValueError:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
            error(tx, peer, 2, "Access violation")
        return
    if not path.is_file():
        log(f"not found: {filename} requested by {peer[0]}")
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
            error(tx, peer, 1, "File not found")
        return

    block_size = 512
    timeout = 2
    accepted = {}
    if "blksize" in requested:
        try:
            candidate = int(requested["blksize"])
            if 8 <= candidate <= 65464:
                block_size = candidate
                accepted["blksize"] = str(candidate)
        except ValueError:
            pass
    if "timeout" in requested:
        try:
            candidate = int(requested["timeout"])
            if 1 <= candidate <= 10:
                timeout = candidate
                accepted["timeout"] = str(candidate)
        except ValueError:
            pass
    if "tsize" in requested:
        accepted["tsize"] = str(path.stat().st_size)

    log(f"serving {filename} ({path.stat().st_size} bytes) to {peer[0]}:{peer[1]}")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as tx:
        tx.bind((bind_ip, 0))
        if accepted:
            payload = b"".join(k.encode() + b"\0" + v.encode() + b"\0" for k, v in accepted.items())
            oack = struct.pack("!H", OP_OACK) + payload
            if not wait_for_ack(tx, peer, 0, oack, timeout):
                log(f"option negotiation failed for {filename}")
                return

        block = 1
        sent = 0
        with path.open("rb") as f:
            while True:
                chunk = f.read(block_size)
                packet = struct.pack("!HH", OP_DATA, block) + chunk
                if not wait_for_ack(tx, peer, block, packet, timeout):
                    log(f"transfer timed out: {filename}, block {block}")
                    return
                sent += len(chunk)
                if len(chunk) < block_size:
                    break
                block = (block + 1) & 0xffff
        log(f"completed {filename}: {sent} bytes sent to {peer[0]}")


server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind((bind_ip, port))
log(f"read-only server listening on {bind_ip}:{port}; root={root}")
while True:
    data, peer = server.recvfrom(65535)
    if len(data) >= 2 and struct.unpack("!H", data[:2])[0] == OP_RRQ:
        threading.Thread(target=serve_rrq, args=(data, peer), daemon=True).start()
PY
    chmod 700 "$TMP/tftp_server.py"
}

start_tftp_server() {
    local bind_ip=$1 root=$2
    write_tftp_server
    python3 "$TMP/tftp_server.py" "$bind_ip" "$TFTP_PORT" "$root" &
    TFTP_PID=$!
    sleep 0.3
    kill -0 "$TFTP_PID" 2>/dev/null || die "the TFTP server failed to start on $bind_ip:$TFTP_PORT"
}

self_test_tftp() {
    need_cmd python3
    TMP=$(mktemp -d)
    mkdir -p "$TMP/root"
    printf 'Meraki TFTP self-test\n' > "$TMP/root/test.bin"
    TFTP_PORT=$((20000 + RANDOM % 20000))
    start_tftp_server 127.0.0.1 "$TMP/root"
    python3 - "$TFTP_PORT" <<'PY'
import socket, struct, sys
port = int(sys.argv[1])
server = ('127.0.0.1', port)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(3)
rrq = struct.pack('!H', 1) + b'test.bin\x00octet\x00blksize\x001468\x00tsize\x000\x00'
s.sendto(rrq, server)
data, peer = s.recvfrom(65535)
op = struct.unpack('!H', data[:2])[0]
if op != 6:
    raise SystemExit(f'expected OACK, received opcode {op}')
s.sendto(struct.pack('!HH', 4, 0), peer)
out = bytearray()
expected = 1
while True:
    data, peer2 = s.recvfrom(65535)
    op, block = struct.unpack('!HH', data[:4])
    if op != 3 or block != expected:
        raise SystemExit('unexpected DATA packet')
    payload = data[4:]
    out.extend(payload)
    s.sendto(struct.pack('!HH', 4, block), peer2)
    if len(payload) < 1468:
        break
    expected = (expected + 1) & 0xffff
if bytes(out) != b'Meraki TFTP self-test\n':
    raise SystemExit('downloaded content mismatch')
print('Built-in TFTP self-test passed.')
PY
}

list_host_ipv4() {
    ip -o -4 addr show up scope global 2>/dev/null | awk '{split($4,a,"/"); print $2 "|" a[1]}'
}

select_host_ip() {
    local preferred=${1:-} entries=() line iface addr choice i
    mapfile -t entries < <(list_host_ipv4)
    ((${#entries[@]})) || die "no active global IPv4 address was found"
    printf '\nHost IPv4 addresses available for TFTP:\n' >&2
    for i in "${!entries[@]}"; do
        IFS='|' read -r iface addr <<< "${entries[$i]}"
        if [[ $addr == "$preferred" ]]; then
            printf '  %d) %-16s %s (route-selected)\n' "$((i+1))" "$addr" "$iface" >&2
        else
            printf '  %d) %-16s %s\n' "$((i+1))" "$addr" "$iface" >&2
        fi
    done
    printf '  m) Enter an address manually\n' >&2
    while :; do
        read -r -p 'Select the address reachable by the switch: ' choice
        if [[ $choice == m || $choice == M ]]; then
            read -r -p 'Host IPv4 address: ' addr
            [[ $addr =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || { warn "invalid IPv4 syntax"; continue; }
            printf '%s' "$addr"
            return
        fi
        [[ $choice =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#entries[@]} )) || {
            warn "invalid selection"
            continue
        }
        IFS='|' read -r iface addr <<< "${entries[$((choice-1))]}"
        printf '%s' "$addr"
        return
    done
}

select_firmware() {
    local files=() types=() sizes=() file type size i choice
    [[ -d "$ARTIFACTS_DIR" ]] || die "artifact directory not found: $ARTIFACTS_DIR"
    while IFS= read -r -d '' file; do
        if type=$(classify_firmware "$file"); then
            files+=("$file")
            types+=("$type")
            sizes+=("$(stat -c %s -- "$file")")
        fi
    done < <(find "$ARTIFACTS_DIR" -maxdepth 1 -type f -print0 | sort -z)
    ((${#files[@]})) || die "no valid 16 MiB MS42/MS42P images or SquashFS files were found in $ARTIFACTS_DIR"

    printf '\nAvailable firmware artifacts:\n'
    for i in "${!files[@]}"; do
        printf '  %d) %-9s %-10s %s\n' \
            "$((i+1))" "${types[$i]}" "$(human_size "${sizes[$i]}")" "$(basename -- "${files[$i]}")"
    done
    while :; do
        read -r -p 'Select firmware number: ' choice
        [[ $choice =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#files[@]} )) || {
            warn "invalid selection"
            continue
        }
        SELECTED_FIRMWARE=${files[$((choice-1))]}
        SELECTED_TYPE=${types[$((choice-1))]}
        SELECTED_SIZE=${sizes[$((choice-1))]}
        return
    done
}

select_operation() {
    local choice
    printf '\nOperation:\n'
    printf '  1) Verify TFTP download, checksum, board, and layout only\n'
    printf '  2) Dry run; prepare the update but do not stop services or write flash\n'
    printf '  3) Flash firmware and reboot\n'
    printf '  4) Flash firmware and reboot (Force)\n'
    while :; do
        read -r -p 'Select operation [1]: ' choice
        choice=${choice:-1}
        case "$choice" in
            1) OPERATION=verify; OPERATION_ARGS=(--verify-only --yes); return ;;
            2) OPERATION=dry-run; OPERATION_ARGS=(--dry-run --yes); return ;;
            3) OPERATION=flash; OPERATION_ARGS=(--yes); return ;;
            4) OPERATION=flash; OPERATION_ARGS=(--force --yes); return ;;
            *) warn "invalid selection" ;;
        esac
    done
}

select_overlay_policy() {
    local choice
    printf '\nWritable-overlay policy:\n'
    printf '  1) preserve - leave the current JFFS2 partition byte-for-byte unchanged\n'
    printf '  2) migrate  - make a clean JFFS2 image containing preserve.list paths\n'
    printf '  3) reset    - flash a clean empty JFFS2 overlay\n'
    [[ $SELECTED_TYPE == full ]] && printf '  4) image    - flash the JFFS2 region embedded in the selected full image\n'
    while :; do
        read -r -p 'Select overlay policy [1]: ' choice
        choice=${choice:-1}
        case "$choice" in
            1) OVERLAY_POLICY=preserve; return ;;
            2) OVERLAY_POLICY=migrate; return ;;
            3) OVERLAY_POLICY=reset; return ;;
            4) [[ $SELECTED_TYPE == full ]] && { OVERLAY_POLICY=image; return; } ;;&
            *) warn "invalid selection" ;;
        esac
    done
}

stage_firmware() {
    local base hash candidate n=1
    TFTP_ROOT="$TMP/tftp-root"
    mkdir -p "$TFTP_ROOT"
    base=$(safe_tftp_name "$(basename -- "$SELECTED_FIRMWARE")")
    candidate=$base
    while [[ -e "$TFTP_ROOT/$candidate" ]]; do
        candidate="${base%.*}-$n.${base##*.}"
        ((n++))
    done
    STAGED_NAME=$candidate
    cp --reflink=auto -- "$SELECTED_FIRMWARE" "$TFTP_ROOT/$STAGED_NAME" 2>/dev/null || \
        cp -- "$SELECTED_FIRMWARE" "$TFTP_ROOT/$STAGED_NAME"
    (
        cd "$TFTP_ROOT"
        sha256sum "$STAGED_NAME" > "$STAGED_NAME.sha256"
    )
    hash=$(awk '{print $1}' "$TFTP_ROOT/$STAGED_NAME.sha256")
    EXPECTED_ROOTFS_HASH=$(expected_rootfs_hash "$SELECTED_FIRMWARE" "$SELECTED_TYPE")
    log "staged $STAGED_NAME"
    log "firmware SHA-256: $hash"
    log "expected installed SquashFS-region SHA-256: $EXPECTED_ROOTFS_HASH"
}

remote_command() {
    local args=(fw_update_tftp --server "$HOST_TFTP_IP" --port "$TFTP_PORT" \
        --file "$STAGED_NAME" --overlay "$OVERLAY_POLICY")
    args+=("${OPERATION_ARGS[@]}")
    printf '%q ' "${args[@]}"
}

secret_lookup() {
    command -v secret-tool >/dev/null 2>&1 || return 1
    secret-tool lookup service meraki-fw-flasher host "$SSH_TARGET" user "$SSH_USER" port "$SSH_PORT" 2>/dev/null
}

secret_store() {
    command -v secret-tool >/dev/null 2>&1 || return 1
    printf '%s' "$SSH_PASSWORD" | secret-tool store \
        --label="Meraki firmware flasher: $SSH_USER@$SSH_TARGET:$SSH_PORT" \
        service meraki-fw-flasher host "$SSH_TARGET" user "$SSH_USER" port "$SSH_PORT"
}

prepare_askpass() {
    printf '%s' "$SSH_PASSWORD" > "$TMP/ssh-password"
    chmod 600 "$TMP/ssh-password"
    cat > "$TMP/ssh-askpass" <<EOF
#!/usr/bin/env sh
cat '$TMP/ssh-password'
EOF
    chmod 700 "$TMP/ssh-askpass"
}

ssh_options() {
    SSH_OPTS=(
        -p "$SSH_PORT"
        -o ConnectTimeout=10
        -o ServerAliveInterval=5
        -o ServerAliveCountMax=3
        -o StrictHostKeyChecking=accept-new
        -o UserKnownHostsFile="$KNOWN_HOSTS"
        -o GlobalKnownHostsFile=/dev/null
        -o LogLevel=ERROR
    )
}

ssh_exec() {
    local quiet=0
    [[ ${1:-} == --quiet ]] && { quiet=1; shift; }
    if [[ $SSH_AUTH_MODE == key ]]; then
        if (( quiet )); then
            ssh "${SSH_OPTS[@]}" -o BatchMode=yes "$SSH_USER@$SSH_TARGET" "$@" >/dev/null 2>&1
        else
            ssh "${SSH_OPTS[@]}" "$SSH_USER@$SSH_TARGET" "$@"
        fi
    else
        prepare_askpass
        if (( quiet )); then
            DISPLAY=:0 SSH_ASKPASS_REQUIRE=force SSH_ASKPASS="$TMP/ssh-askpass" \
                setsid -w ssh "${SSH_OPTS[@]}" "$SSH_USER@$SSH_TARGET" "$@" >/dev/null 2>&1
        else
            DISPLAY=:0 SSH_ASKPASS_REQUIRE=force SSH_ASKPASS="$TMP/ssh-askpass" \
                setsid -w ssh "${SSH_OPTS[@]}" "$SSH_USER@$SSH_TARGET" "$@"
        fi
    fi
}

prepare_ssh_auth() {
    local saved=""
    mkdir -p "$CONFIG_DIR" "$LOG_DIR"
    chmod 700 "$CONFIG_DIR"
    touch "$KNOWN_HOSTS"
    chmod 600 "$KNOWN_HOSTS"
    ssh_options

    log "checking SSH key authentication"
    if ssh "${SSH_OPTS[@]}" -o BatchMode=yes "$SSH_USER@$SSH_TARGET" true >/dev/null 2>&1; then
        SSH_AUTH_MODE=key
        log "SSH key authentication succeeded"
        return
    fi

    SSH_AUTH_MODE=password
    if saved=$(secret_lookup) && [[ -n $saved ]]; then
        if prompt_yes_no "Use the password saved in the desktop keyring for $SSH_USER@$SSH_TARGET?" y; then
            SSH_PASSWORD=$saved
        fi
    fi
    if [[ -z $SSH_PASSWORD ]]; then
        read -r -s -p "SSH password for $SSH_USER@$SSH_TARGET: " SSH_PASSWORD
        printf '\n'
    fi
    [[ -n $SSH_PASSWORD ]] || die "SSH password may not be empty"

    log "testing SSH password authentication"
    ssh_exec --quiet true || die "SSH authentication failed"

    if command -v secret-tool >/dev/null 2>&1; then
        if prompt_yes_no 'Save this password in the desktop keyring for future runs?' y; then
            secret_store && log "password saved in the desktop keyring" || warn "the desktop keyring rejected the password"
        fi
    else
        warn "secret-tool is unavailable; the password will only be kept in memory for this run"
    fi
}

clear_expected_changed_host_key() {
    [[ $OVERLAY_POLICY == reset || $OVERLAY_POLICY == image ]] || return 0
    command -v ssh-keygen >/dev/null 2>&1 || return 0
    if [[ $SSH_PORT == 22 ]]; then
        ssh-keygen -q -R "$SSH_TARGET" -f "$KNOWN_HOSTS" >/dev/null 2>&1 || true
    else
        ssh-keygen -q -R "[$SSH_TARGET]:$SSH_PORT" -f "$KNOWN_HOSTS" >/dev/null 2>&1 || true
    fi
    log "cleared the dedicated saved host key because the selected overlay policy can replace SSH host keys"
}

wait_for_ssh_return() {
    local deadline=$((SECONDS + 900)) announced=0
    clear_expected_changed_host_key
    while (( SECONDS < deadline )); do
        if ssh_exec --quiet true; then
            log "the switch is reachable by SSH again"
            return 0
        fi
        if (( !announced )); then
            log "the switch is rebooting or SSH is temporarily unavailable"
            announced=1
        fi
        sleep 5
    done
    return 1
}

remote_rootfs_hash() {
    local command
    command="mtd=\$(awk '\$4==\"\\\"squashfs\\\"\" || \$4==\"\\\"rootfs\\\"\" {sub(/:$/,\"\",\$1); print \$1; exit}' /proc/mtd); [ -n \"\$mtd\" ] && dd if=/dev/\$mtd bs=1048576 count=8 2>/dev/null | sha256sum | awk '{print \$1}'"
    ssh_exec "$command"
}

run_ssh_mode() {
    local route_ip command rc=0 actual_hash=""
    SSH_TARGET=$(prompt_default 'Switch IP address or hostname' '169.254.0.10')
    SSH_USER=$(prompt_default 'SSH username' 'root')
    SSH_PORT=$(prompt_default 'SSH port' '22')
    [[ $SSH_PORT =~ ^[0-9]+$ ]] && (( SSH_PORT >= 1 && SSH_PORT <= 65535 )) || die "invalid SSH port"
    route_ip=$(ip route get "$SSH_TARGET" 2>/dev/null | awk '{for (i=1;i<=NF;i++) if ($i=="src") {print $(i+1); exit}}' || true)
    HOST_TFTP_IP=$(select_host_ip "$route_ip")

    prepare_ssh_auth
    stage_firmware
    start_tftp_server "$HOST_TFTP_IP" "$TFTP_ROOT"
    command=$(remote_command)

    printf '\nSelected firmware: %s\n' "$SELECTED_FIRMWARE"
    printf 'Control path:      SSH to %s@%s:%s\n' "$SSH_USER" "$SSH_TARGET" "$SSH_PORT"
    printf 'TFTP source:       tftp://%s:%s/%s\n' "$HOST_TFTP_IP" "$TFTP_PORT" "$STAGED_NAME"
    printf 'Operation:         %s\n' "$OPERATION"
    printf 'Overlay policy:    %s\n\n' "$OVERLAY_POLICY"

    if [[ $OPERATION == flash ]] && ! prompt_yes_no 'Start the firmware flash now?' n; then
        die "cancelled"
    fi

    log "starting remote updater; its status output follows"
    set +e
    ssh_exec "$command"
    rc=$?
    set -e

    if [[ $OPERATION != flash ]]; then
        (( rc == 0 )) || die "remote updater exited with status $rc"
        log "$OPERATION completed successfully; flash was not modified"
        return
    fi

    if (( rc != 0 )); then
        warn "the SSH command ended with status $rc; a disconnect is normal during reboot, but an earlier FWUPDATE error is not"
    fi
    if ! wait_for_ssh_return; then
        die "the switch did not become reachable by SSH after the update; use the hardware serial console to inspect it"
    fi

    set +e
    actual_hash=$(remote_rootfs_hash 2>/dev/null | tr -d '\r\n')
    set -e
    if [[ $actual_hash == "$EXPECTED_ROOTFS_HASH" ]]; then
        log "post-reboot SquashFS readback hash matches the selected firmware"
    elif [[ -n $actual_hash ]]; then
        warn "post-reboot SquashFS hash mismatch"
        warn "expected: $EXPECTED_ROOTFS_HASH"
        warn "actual:   $actual_hash"
        return 1
    else
        warn "the switch returned, but the installed SquashFS hash could not be read"
    fi

    log "post-reboot firmware information:"
    ssh_exec "cat /etc/lsb-release 2>/dev/null || true; uname -a"
}

select_serial_device() {
    local devices=(/dev/serial/by-id/* /dev/ttyUSB* /dev/ttyACM*) unique=() dev seen choice i
    for dev in "${devices[@]}"; do
        [[ -c $dev ]] || continue
        seen=0
        for existing in "${unique[@]}"; do
            [[ $(readlink -f "$existing") == $(readlink -f "$dev") ]] && { seen=1; break; }
        done
        (( seen )) || unique+=("$dev")
    done
    ((${#unique[@]})) || die "no USB serial device was found"
    printf '\nSerial devices:\n'
    for i in "${!unique[@]}"; do
        printf '  %d) %s\n' "$((i+1))" "${unique[$i]}"
    done
    printf '  m) Enter a device manually\n'
    while :; do
        read -r -p 'Select serial device: ' choice
        if [[ $choice == m || $choice == M ]]; then
            read -r -p 'Serial device path: ' SERIAL_DEVICE
            [[ -c $SERIAL_DEVICE ]] || { warn "not a character device"; continue; }
            return
        fi
        [[ $choice =~ ^[0-9]+$ ]] && (( choice >= 1 && choice <= ${#unique[@]} )) || {
            warn "invalid selection"
            continue
        }
        SERIAL_DEVICE=${unique[$((choice-1))]}
        return
    done
}

ensure_serial_access() {
    [[ -r $SERIAL_DEVICE && -w $SERIAL_DEVICE ]] && return
    warn "the current user cannot read and write $SERIAL_DEVICE"
    if command -v setfacl >/dev/null 2>&1 && prompt_yes_no 'Grant this user temporary serial access using sudo setfacl?' y; then
        sudo setfacl -m "u:$USER:rw" "$SERIAL_DEVICE"
    fi
    [[ -r $SERIAL_DEVICE && -w $SERIAL_DEVICE ]] || \
        die "serial access is still unavailable; add your user to the serial-device group (commonly uucp) and sign in again"
}

write_serial_runner() {
    cat > "$TMP/serial_runner.py" <<'PY'
#!/usr/bin/env python3
import argparse
import fcntl
import os
import re
import select
import sys
import termios
import time

p = argparse.ArgumentParser()
p.add_argument('--device', required=True)
p.add_argument('--username', default='root')
p.add_argument('--password-file', required=True)
p.add_argument('--command', required=True)
p.add_argument('--operation', choices=('verify', 'dry-run', 'flash'), required=True)
p.add_argument('--timeout', type=int, default=1800)
a = p.parse_args()

with open(a.password_file, 'r', encoding='utf-8') as f:
    password = f.read()

fd = os.open(a.device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
try:
    try:
        fcntl.ioctl(fd, termios.TIOCEXCL)
    except OSError:
        pass
    attrs = termios.tcgetattr(fd)
    attrs[0] = termios.IGNBRK | termios.IXON | termios.IXOFF
    attrs[1] = 0
    attrs[2] = termios.CLOCAL | termios.CREAD | termios.CS8
    attrs[3] = 0
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIFLUSH)

    def send(text):
        os.write(fd, text.encode('utf-8'))

    send('\r')
    started = time.monotonic()
    last_nudge = started
    buffer = ''
    command_sent = False
    username_sent = False
    password_sent = False
    reboot_seen = False
    last_error = None
    shell_probe_sent = False

    print(f"[serial] opened {a.device} at 115200 baud, 8N1, XON/XOFF", flush=True)
    print("[serial] press Ctrl+C to stop monitoring", flush=True)

    while time.monotonic() - started < a.timeout:
        readable, _, _ = select.select([fd], [], [], 0.25)
        if readable:
            try:
                data = os.read(fd, 65536)
            except BlockingIOError:
                data = b''
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
                text = data.decode('utf-8', 'replace')
                buffer = (buffer + text)[-16384:]
                lower = buffer.lower()

                if command_sent and 'fwupdate|error|' in lower:
                    last_error = 'the updater reported an error'
                if command_sent and ('linuxloader built' in lower or 'linux version 3.18' in lower):
                    reboot_seen = True
                if command_sent and a.operation != 'flash':
                    match = re.search(r'__MFW_RC__:(\d+)', buffer)
                    if match:
                        rc = int(match.group(1))
                        print(f"\n[serial] updater returned status {rc}", flush=True)
                        raise SystemExit(rc)
                if command_sent and a.operation == 'flash' and reboot_seen:
                    if re.search(r'(?i)(?:^|[\r\n]).*login:\s*$', buffer) or 'starting dropbear sshd: ok' in lower:
                        print("\n[serial] reboot completed and the operating system reached its login/services stage", flush=True)
                        if last_error:
                            print(f"[serial] warning: {last_error}", flush=True)
                            raise SystemExit(1)
                        raise SystemExit(0)

                if not command_sent:
                    if re.search(r'(?i)login:\s*$', buffer):
                        send(a.username + '\r')
                        username_sent = True
                        buffer = ''
                        continue
                    if re.search(r'(?i)password:\s*$', buffer):
                        send(password + '\r')
                        password_sent = True
                        buffer = ''
                        continue
                    if re.search(r'(?:^|[\r\n])[^\r\n]*[#\$]\s*$', buffer):
                        if not shell_probe_sent:
                            send("printf '__MFW_SHELL_READY__\\n'\r")
                            shell_probe_sent = True
                            buffer = ''
                            continue
                    if '__MFW_SHELL_READY__' in buffer:
                        wrapped = f"{a.command}; rc=$?; echo __MFW_RC__:$rc"
                        print(f"\n[serial] launching: {a.command}\n", flush=True)
                        send(wrapped + '\r')
                        command_sent = True
                        buffer = ''
                        continue

        now = time.monotonic()
        if not command_sent and now - last_nudge >= 5:
            send('\r')
            last_nudge = now

    if not command_sent:
        print("\n[serial] timed out before a shell prompt was detected", file=sys.stderr)
    elif a.operation == 'flash':
        print("\n[serial] timed out before the post-flash reboot completed", file=sys.stderr)
    else:
        print("\n[serial] timed out waiting for the updater to return", file=sys.stderr)
    raise SystemExit(124)
finally:
    try:
        os.close(fd)
    except OSError:
        pass
PY
    chmod 700 "$TMP/serial_runner.py"
}

run_serial_mode() {
    local command log_file rc
    select_serial_device
    ensure_serial_access
    SERIAL_USER=$(prompt_default 'Serial-console username' 'root')
    read -r -s -p "Serial-console password for $SERIAL_USER: " SERIAL_PASSWORD
    printf '\n'
    HOST_TFTP_IP=$(select_host_ip)

    stage_firmware
    start_tftp_server "$HOST_TFTP_IP" "$TFTP_ROOT"
    command=$(remote_command)
    write_serial_runner
    printf '%s' "$SERIAL_PASSWORD" > "$TMP/serial-password"
    chmod 600 "$TMP/serial-password"
    mkdir -p "$LOG_DIR"
    log_file="$LOG_DIR/firmware-flash-serial-$(date +%Y%m%d-%H%M%S).log"

    printf '\nSelected firmware: %s\n' "$SELECTED_FIRMWARE"
    printf 'Control path:      serial %s, 115200 8N1 XON/XOFF\n' "$SERIAL_DEVICE"
    printf 'TFTP source:       tftp://%s:%s/%s\n' "$HOST_TFTP_IP" "$TFTP_PORT" "$STAGED_NAME"
    printf 'Operation:         %s\n' "$OPERATION"
    printf 'Overlay policy:    %s\n' "$OVERLAY_POLICY"
    printf 'Serial log:        %s\n\n' "$log_file"

    if [[ $OPERATION == flash ]] && ! prompt_yes_no 'Start the firmware flash now?' n; then
        die "cancelled"
    fi

    set +e
    python3 "$TMP/serial_runner.py" \
        --device "$SERIAL_DEVICE" \
        --username "$SERIAL_USER" \
        --password-file "$TMP/serial-password" \
        --command "$command" \
        --operation "$OPERATION" 2>&1 | tee "$log_file"
    rc=${PIPESTATUS[0]}
    set -e
    (( rc == 0 )) || die "serial-controlled update ended with status $rc; inspect $log_file"
    log "serial-controlled $OPERATION completed"
}

main() {
    local self_test=0 method
    while (($#)); do
        case "$1" in
            --artifacts) (($# >= 2)) || die "--artifacts requires a directory"; ARTIFACTS_DIR=$2; shift 2 ;;
            --tftp-port) (($# >= 2)) || die "--tftp-port requires a port"; TFTP_PORT=$2; shift 2 ;;
            --self-test) self_test=1; shift ;;
            --help|-h) usage; return 0 ;;
            *) die "unknown option: $1" ;;
        esac
    done

    need_cmd bash
    need_cmd python3
    need_cmd sha256sum
    need_cmd dd
    need_cmd awk
    need_cmd stat
    need_cmd od
    need_cmd tr
    need_cmd ip
    [[ $TFTP_PORT =~ ^[0-9]+$ ]] && (( TFTP_PORT >= 1024 && TFTP_PORT <= 65535 )) || \
        die "TFTP port must be an unprivileged numeric port from 1024 through 65535"

    TMP=$(mktemp -d)
    chmod 700 "$TMP"
    (( self_test )) && { self_test_tftp; return; }

    select_firmware
    select_operation
    select_overlay_policy

    printf '\nControl path:\n'
    printf '  1) SSH - initiate remotely, stream updater output, reconnect, and verify flash\n'
    printf '  2) Hardware serial - log in over UART and monitor through the reboot\n'
    while :; do
        read -r -p 'Select control path [1]: ' method
        method=${method:-1}
        case "$method" in
            1)
                need_cmd ssh
                need_cmd setsid
                run_ssh_mode
                return
                ;;
            2)
                run_serial_mode
                return
                ;;
            *) warn "invalid selection" ;;
        esac
    done
}

main "$@"
