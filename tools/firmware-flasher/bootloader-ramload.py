#!/usr/bin/env python3
"""Validate or run meraki-redboot UART firmware recovery protocol v2."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import sys
import termios

from bootloader_protocol import (
    ProtocolError,
    EmbeddedRecoveryEntryError,
    SerialLink,
    inspect_payload,
    make_package_header,
    make_preflight_header,
    MODEL_FAMILY,
    send_object,
    send_ram_payload,
    validate_bundle,
    validate_recovery_payload,
    OBJECT_IMAGE,
    OBJECT_MANIFEST,
)

READY_SOC = re.compile(r"\bSOC=(luton26|jaguar1)\b")
INFO_SOC = re.compile(r"\bSOC:\s*(luton26|jaguar1)\b")
RECOVERY_DESCRIPTOR = re.compile(
    r"^PMOSREC DESCRIPTOR PMOSRECOVERY2;SOC=(luton26|jaguar1);"
    r"FAMILY=([12]);SPI=([0-9a-fA-F]{8});PROTO=2;PREFLIGHT=2;END$"
)
MENU_BYTE = re.compile(r"\bBYTE:\s*0x([0-9a-fA-F]{8})\b")
MENU_SELECTION = re.compile(r"\bSELECTED:\s*0x([0-9a-fA-F]{8})\b")
CHALLENGE = re.compile(r"^PMOSREC ERASE-CHALLENGE ([0-9a-f]{8})$")


def baud_constant(baud: int) -> int:
    name = f"B{baud}"
    if not hasattr(termios, name):
        raise ProtocolError(f"termios does not support baud rate {baud}")
    return getattr(termios, name)


def configure_serial(fd: int, baud: int) -> list:
    old = termios.tcgetattr(fd)
    attrs = termios.tcgetattr(fd)
    speed = baud_constant(baud)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = speed | termios.CLOCAL | termios.CREAD | termios.CS8
    if hasattr(termios, "CRTSCTS"):
        attrs[2] &= ~termios.CRTSCTS
    attrs[3] = 0
    attrs[4] = speed
    attrs[5] = speed
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return old


def require_soc(line: str, expected: str, stage: str) -> None:
    match = READY_SOC.search(line)
    if not match:
        raise ProtocolError(f"{stage} did not report a parseable SoC family: {line}")
    if match.group(1) != expected:
        raise ProtocolError(f"{stage} reports {match.group(1)}, but the selected model requires {expected}")


def require_info_soc(line: str, expected: str) -> None:
    match = INFO_SOC.search(line)
    if not match:
        raise ProtocolError(f"embedded recovery launch did not report a parseable SoC family: {line}")
    if match.group(1) != expected:
        raise ProtocolError(
            f"embedded recovery launch reports {match.group(1)}, "
            f"but the selected model requires {expected}"
        )


def wait_for_recovery_descriptor(link: SerialLink, expected_family: str) -> str:
    """Synchronize after the target has finished all startup UART output.

    PMOSREC prints READY and then a descriptor using a polling UART. Sending the
    binary package header as soon as READY is seen can overrun the target RX FIFO
    while it is still transmitting the descriptor. Waiting for the descriptor's
    terminating newline creates an explicit half-duplex handoff point.
    """
    line = link.wait_for(("PMOSREC DESCRIPTOR ",), 5.0)
    match = RECOVERY_DESCRIPTOR.fullmatch(line)
    if not match:
        raise ProtocolError(f"recovery stage emitted an invalid descriptor: {line}")
    family = match.group(1)
    family_id = int(match.group(2))
    spi_address = int(match.group(3), 16)
    expected_id = 1 if expected_family == "luton26" else 2
    expected_spi = 0x70000064 if expected_family == "luton26" else 0x70000068
    if family != expected_family or family_id != expected_id or spi_address != expected_spi:
        raise ProtocolError(
            "recovery descriptor mismatch: "
            f"reported family={family} id={family_id} spi=0x{spi_address:08x}; "
            f"expected family={expected_family} id={expected_id} spi=0x{expected_spi:08x}"
        )
    return line


def accept_recovery_ready(link: SerialLink, ready_line: str, expected_family: str,
                          stage: str) -> None:
    require_soc(ready_line, expected_family, stage)
    wait_for_recovery_descriptor(link, expected_family)
    flash_ready = link.wait_for(("PMOSREC FLASH-PREFLIGHT-OK",), 10.0)
    if f"ID=" not in flash_ready:
        raise ProtocolError(f"recovery hardware preflight did not report a JEDEC ID: {flash_ready}")
    link.wait_for(("PMOSREC COMMAND-READY 1",), 5.0)


def require_hex_field(line: str, pattern: re.Pattern[str], expected: int, label: str) -> None:
    match = pattern.search(line)
    if not match:
        raise ProtocolError(f"{label} did not report the expected hexadecimal field: {line}")
    observed = int(match.group(1), 16)
    if observed != expected:
        raise ProtocolError(
            f"{label} reported 0x{observed:08x}; expected 0x{expected:08x}"
        )


def wait_for_embedded_recovery(link: SerialLink, expected_family: str) -> None:
    error_prefixes = (
        "PMOSBOOT FAIL-RECOVERY",
        "PMOSBOOT WARN-MENU-TIMEOUT",
    )
    info = link.wait_for(
        ("PMOSBOOT INFO-RECOVERY",), 5.0, error_prefixes=error_prefixes
    )
    require_info_soc(info, expected_family)
    for marker in (
        "PMOSBOOT PASS-RECOVERY-SIZE",
        "PMOSBOOT PASS-RECOVERY-COPY",
        "PMOSBOOT PASS-RECOVERY-EXEC",
    ):
        link.wait_for((marker,), 10.0, error_prefixes=error_prefixes)


def enter_recovery(
    link: SerialLink,
    path: str,
    expected_family: str,
    timeout: float,
    payload: bytes | None,
    load: int,
    entry: int,
    chunk_size: int,
    frame_retries: int,
    ack_timeout: float,
) -> str:
    """Enter menu option 2, or support an explicit/direct RAM-upload path."""
    prefixes = ("PMOSBOOT MENU-PROBE", "PMOSREC READY 2", "PMOSRAM READY 2")
    line = link.wait_for(prefixes, timeout)
    if line.startswith("PMOSREC READY 2"):
        accept_recovery_ready(link, line, expected_family, "automatic embedded recovery")
        return "embedded"

    if line.startswith("PMOSBOOT MENU-PROBE"):
        # Use carriage return so the host-side trace matches the hardware-tested
        # v0.7.0 sequence (PASS-MENU-TRIGGER BYTE 0x0000000D). The trigger byte
        # is deliberately discarded by stage 1; an explicit fresh 1/2 follows.
        link.write_all(b"\r")
        trigger = link.wait_for(
            ("PMOSBOOT PASS-MENU-TRIGGER",),
            4.0,
            error_prefixes=("PMOSBOOT WARN-MENU-TIMEOUT",),
        )
        require_hex_field(trigger, MENU_BYTE, 0x0D, "menu trigger")
        link.wait_for(
            ("PMOSBOOT MENU 1=UART-RAMLOADER 2=FW-RECOVERY",),
            4.0,
            error_prefixes=("PMOSBOOT WARN-MENU-TIMEOUT",),
        )
        link.wait_for(
            ("PMOSBOOT MENU-READY",),
            4.0,
            error_prefixes=("PMOSBOOT WARN-MENU-TIMEOUT",),
        )
        choice = b"1" if path == "ram-upload" else b"2"
        link.write_all(choice)
        selected = link.wait_for(
            ("PMOSBOOT PASS-MENU-CHOICE",),
            4.0,
            error_prefixes=("PMOSBOOT WARN-MENU-TIMEOUT",),
        )
        require_hex_field(selected, MENU_SELECTION, int(choice), "menu selection")
        if choice == b"2":
            wait_for_embedded_recovery(link, expected_family)
            try:
                line = link.wait_for(
                    ("PMOSREC READY 2",),
                    10.0,
                    error_prefixes=("PMOSBOOT FAIL-RECOVERY",),
                )
            except ProtocolError as exc:
                if "timed out" not in str(exc):
                    raise
                raise EmbeddedRecoveryEntryError(
                    "loader reported PASS-RECOVERY-EXEC but the payload never emitted PMOSREC READY 2; "
                    "this matches the v0.7.0 flat-binary entry-offset defect"
                ) from exc
        else:
            line = link.wait_for(("PMOSRAM READY 2",), 10.0)

    if line.startswith("PMOSREC READY 2"):
        accept_recovery_ready(link, line, expected_family, "embedded recovery")
        return "embedded"

    if not line.startswith("PMOSRAM READY 2"):
        raise ProtocolError(f"unexpected bootloader recovery state: {line}")
    require_soc(line, expected_family, "UART RAM loader")
    if path == "embedded":
        raise ProtocolError(
            "target entered a direct UART RAM-loader; use --recovery-path ram-upload "
            "with the matching external recovery payload"
        )
    if payload is None:
        raise ProtocolError("external RAM-loader recovery requires --payload")
    send_ram_payload(link, payload, load, entry, chunk_size, frame_retries, ack_timeout)
    payload_ready = link.wait_for(("PMOSREC READY 2",), 10.0)
    accept_recovery_ready(link, payload_ready, expected_family, "uploaded recovery payload")
    return "ram-upload"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--operation", choices=("verify", "preflight", "dry-run", "flash"), default="verify")
    parser.add_argument("--port", help="Linux serial character device")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--recovery-path", choices=("embedded", "ram-upload", "auto"), default="embedded")
    parser.add_argument("--payload", type=Path, help="external recovery payload for RAM-loader fallback")
    parser.add_argument("--payload-descriptor", type=Path, help="entry-contract descriptor for --payload")
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--target-model", required=True)
    parser.add_argument("--force", action="store_true", help="permit a manifest status of untested")
    parser.add_argument("--load-address", default="0x81000000")
    parser.add_argument("--entry")
    parser.add_argument("--chunk-size", type=int, default=1024)
    parser.add_argument("--frame-retries", type=int, default=3)
    parser.add_argument("--ack-timeout", type=float, default=5.0)
    parser.add_argument("--ready-timeout", type=float, default=30.0)
    parser.add_argument("--fallback-ready-timeout", type=float, default=180.0)
    parser.add_argument("--operation-timeout", type=float, default=1800.0)
    parser.add_argument("--auto-confirm-erase", action="store_true")
    parser.add_argument("--preflight-scratch", default="0x00ff0000")
    parser.add_argument("--preflight-seed", default="0x504d4f53")
    parser.add_argument("--preflight-receipt", type=Path,
                        help="write an atomic JSON receipt after a successful destructive preflight")
    args = parser.parse_args()

    if args.target_model not in MODEL_FAMILY:
        raise ProtocolError(f"unsupported exact target model: {args.target_model}")
    compatibility_override = args.force or args.operation in ("verify", "preflight", "dry-run")
    bundle = None
    manifest = None
    if args.operation != "preflight":
        if args.firmware is None:
            raise ProtocolError("--firmware is required except for --operation preflight")
        manifest = args.manifest or Path(str(args.firmware) + ".manifest.json")
        bundle = validate_bundle(args.firmware, manifest, args.target_model, force=compatibility_override)

    payload_data: bytes | None = None
    descriptor = None
    if args.payload is not None:
        descriptor = inspect_payload(args.payload, args.payload_descriptor)
        if descriptor.family != MODEL_FAMILY[args.target_model]:
            raise ProtocolError("recovery payload family does not match the selected target")
        if bundle is not None:
            validate_recovery_payload(args.payload, descriptor, bundle)
        payload_data = args.payload.read_bytes()
        if len(payload_data) > 4 * 1024 * 1024:
            raise ProtocolError("recovery payload exceeds the loader's 4 MiB limit")
    if args.recovery_path == "ram-upload" and payload_data is None:
        raise ProtocolError("--recovery-path ram-upload requires --payload")

    load = int(args.load_address, 0)
    entry = int(args.entry, 0) if args.entry else load
    if bundle is not None:
        print(f"firmware: {args.firmware} ({args.firmware.stat().st_size} bytes)")
        print(f"manifest: {manifest} ({len(bundle.manifest_bytes)} bytes)")
        print(f"target: {bundle.model} / boot-family={bundle.family} / status={bundle.model_status}")
    else:
        print(f"target: {args.target_model} / boot-family={MODEL_FAMILY[args.target_model]}")
        print(f"preflight scratch: {int(args.preflight_scratch, 0):#010x} (64 KiB, restored after test)")
    print(f"recovery path: {args.recovery_path}")
    if descriptor is not None:
        print(f"external payload: {args.payload} ({descriptor.family}, {descriptor.size} bytes, sha256 {descriptor.sha256})")
    else:
        print("external payload: not required; meraki-redboot embeds the family recovery stage")
    if bundle is not None:
        print("local validation: loader menu, embedded recovery binding, SPIM alignment/CRC, image digest, and model policy OK")
    else:
        print("local validation: recovery payload entry, SoC family, SPI enable, and preflight contracts OK")
    if args.operation == "verify":
        print("verify completed without opening the serial port or sending data")
        return 0
    if not args.port:
        raise ProtocolError("--port is required for preflight, dry-run and flash operations")

    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    old = configure_serial(fd, args.baud)
    link = SerialLink(fd)
    try:
        print("Reset or power-cycle the switch now; waiting for the meraki-redboot recovery menu...")
        try:
            selected_path = enter_recovery(
                link, args.recovery_path, MODEL_FAMILY[args.target_model], args.ready_timeout, payload_data,
                load, entry, args.chunk_size, args.frame_retries, args.ack_timeout,
            )
        except EmbeddedRecoveryEntryError as exc:
            if args.recovery_path != "auto" or payload_data is None:
                raise ProtocolError(
                    f"{exc}. Power-cycle and retry with --recovery-path ram-upload using a "
                    "flat-binary-byte-zero-v1 recovery payload."
                ) from exc
            print(f"embedded recovery failed to enter: {exc}", file=sys.stderr, flush=True)
            print(
                "AUTO FALLBACK: power-cycle or reset the switch now. The host will wait for the "
                "meraki-redboot menu and select UART RAM-loader option 1.",
                file=sys.stderr, flush=True,
            )
            link.discard_buffer()
            selected_path = enter_recovery(
                link, "ram-upload", MODEL_FAMILY[args.target_model], args.fallback_ready_timeout, payload_data,
                load, entry, args.chunk_size, args.frame_retries, args.ack_timeout,
            )
        print(
            f"target recovery stage ready through {selected_path}; "
            "descriptor complete, beginning package transfer",
            flush=True,
        )

        if args.operation == "preflight":
            preflight_header = make_preflight_header(
                scratch_address=int(args.preflight_scratch, 0),
                pattern_seed=int(args.preflight_seed, 0),
            )
            link.write_all(preflight_header)
            link.wait_for(("PMOSPFT HEADER-ACK",), 5.0)
            result = link.wait_for(("PMOSREC RESULT PREFLIGHT-OK",), args.operation_timeout)
            print(result)
            if args.preflight_receipt is not None:
                receipt = {
                    "format": "postmerkos.bootloader-preflight-receipt.v1",
                    "result": "pass",
                    "completed_utc": datetime.now(timezone.utc).isoformat(),
                    "target_model": args.target_model,
                    "soc_family": MODEL_FAMILY[args.target_model],
                    "recovery_path": selected_path,
                    "serial_device": args.port,
                    "scratch_address": int(args.preflight_scratch, 0),
                    "scratch_bytes": 64 * 1024,
                    "pattern_seed": int(args.preflight_seed, 0),
                    "restore_original": True,
                    "recovery_payload_sha256": descriptor.sha256 if descriptor is not None else None,
                    "hardware_preflight_contract": (
                        descriptor.hardware_preflight_contract if descriptor is not None
                        else "spi-nor-scratch-rw-restore-loader-crc-v2"
                    ),
                    "spi_master_enable_contract": (
                        descriptor.spi_master_enable_contract if descriptor is not None
                        else "preserve-general-ctrl-enable-spi-v1"
                    ),
                    "target_result_line": result,
                }
                args.preflight_receipt.parent.mkdir(parents=True, exist_ok=True)
                temporary = args.preflight_receipt.with_name(args.preflight_receipt.name + ".tmp")
                temporary.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
                os.chmod(temporary, 0o600)
                temporary.replace(args.preflight_receipt)
                print(f"preflight receipt: {args.preflight_receipt}")
            print("pre-kernel UART/NOR preflight completed successfully; original scratch sector restored")
            return 0

        assert bundle is not None
        package_header = make_package_header(
            bundle, dry_run=args.operation == "dry-run", force=compatibility_override,
            chunk_size=args.chunk_size,
        )
        link.write_all(package_header)
        link.wait_for(("PMOSPKG HEADER-ACK",), 5.0)
        send_object(
            link, bundle.image, None, OBJECT_IMAGE, args.chunk_size,
            args.frame_retries, args.ack_timeout,
        )
        send_object(
            link, None, bundle.manifest_bytes, OBJECT_MANIFEST, args.chunk_size,
            args.frame_retries, args.ack_timeout,
        )
        link.wait_for(("PMOSPKG VERIFIED",), 10.0)

        if args.operation == "dry-run":
            result = link.wait_for(("PMOSREC RESULT DRY-RUN-OK",), 10.0)
            print(result)
            return 0

        challenge_line = link.wait_for(("PMOSREC ERASE-CHALLENGE",), 10.0)
        match = CHALLENGE.match(challenge_line)
        if not match:
            raise ProtocolError(f"invalid erase challenge: {challenge_line}")
        nonce = match.group(1)
        if not args.auto_confirm_erase:
            print("\nDANGER: the target validated the bundle and is ready to erase the complete SPI NOR.")
            confirmation = input(f"Type ERASEFLASH {nonce} to continue: ").strip()
            if confirmation != f"ERASEFLASH {nonce}":
                raise ProtocolError("erase confirmation was not provided")
        link.write_all(f"ERASEFLASH {nonce}\n".encode("ascii"))
        link.wait_for(("PMOSREC CONFIRMATION-ACK",), 5.0)
        try:
            result = link.wait_for(
                ("PMOSREC RESULT SUCCESS", "PMOSREC RESULT ABORT", "PMOSREC RESULT ERROR"),
                args.operation_timeout,
            )
        except ProtocolError as exc:
            raise ProtocolError(f"target recovery failed: {exc}") from exc
        if result != "PMOSREC RESULT SUCCESS":
            raise ProtocolError(result)
        print("pre-kernel recovery completed successfully; power-cycle the target")
        return 0
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, old)
        os.close(fd)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ProtocolError as exc:
        print(f"bootloader recovery error: {exc}", file=sys.stderr)
        raise SystemExit(1)
