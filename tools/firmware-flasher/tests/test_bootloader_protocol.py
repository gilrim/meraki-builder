from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock
import zlib

HERE = Path(__file__).resolve().parent
TOOL_DIR = HERE.parent
sys.path.insert(0, str(TOOL_DIR))
import bootloader_protocol as bp

RAMLOAD_SPEC = importlib.util.spec_from_file_location("bootloader_ramload", TOOL_DIR / "bootloader-ramload.py")
assert RAMLOAD_SPEC and RAMLOAD_SPEC.loader
br = importlib.util.module_from_spec(RAMLOAD_SPEC)
RAMLOAD_SPEC.loader.exec_module(br)


def read_exact(sock: socket.socket, count: int) -> bytes:
    output = bytearray()
    while len(output) < count:
        block = sock.recv(count - len(output))
        if not block:
            raise RuntimeError("peer closed")
        output.extend(block)
    return bytes(output)


class BundleFixture:
    def __init__(self, root: Path, *, model: str = "MS42P", status: str = "validated") -> None:
        self.image = root / "firmware.bin"
        self.manifest = root / "firmware.bin.manifest.json"
        image = bytearray(b"\0" * bp.FULL_IMAGE_SIZE)
        markers = (b"PMOSRAM READY 2", b"PMOSBOOT MENU-PROBE", b"PMOSBOOT MENU 1=UART-RAMLOADER 2=FW-RECOVERY")
        cursor = 0x1000
        for marker in markers:
            image[cursor:cursor + len(marker)] = marker
            cursor += len(marker) + 16
        kernel = b"test-kernel" + b"\0" * ((-len(b"test-kernel")) % bp.SPIM_ALIGNMENT)
        words = [bp.SPIM_MAGIC, 0x81000000, len(kernel), 0x81000000, 0, 0, 0, 0]
        words[4] = zlib.crc32(bp.SPIM_HEADER.pack(*words) + kernel) & 0xFFFFFFFF
        kernel_image = bp.SPIM_HEADER.pack(*words) + kernel
        image[bp.KERNEL_OFFSET:bp.KERNEL_OFFSET + len(kernel_image)] = kernel_image
        image[bp.ROOTFS_OFFSET:bp.ROOTFS_OFFSET + 4] = b"hsqs"
        self.image.write_bytes(image)
        self.payload_luton = root / "recovery-luton26.bin"
        self.payload_jaguar = root / "recovery-jaguar1.bin"
        self.payload_luton.write_bytes(
            b"xPMOSRECOVERY2;SOC=luton26;FAMILY=1;SPI=70000064;PROTO=2;ENDy"
        )
        self.payload_jaguar.write_bytes(
            b"xPMOSRECOVERY2;SOC=jaguar1;FAMILY=2;SPI=70000068;PROTO=2;ENDy"
        )
        digest = bp.sha256_file(self.image).hex()
        loader_digest = hashlib.sha256(image[:bp.LOADER_REGION_SIZE]).hexdigest()
        kernel_sha = hashlib.sha256(kernel).hexdigest()
        kernel_crc = f"{words[4]:08x}"
        payload_records = {
            "luton26": {
                "filename": self.payload_luton.name,
                "bytes": self.payload_luton.stat().st_size,
                "sha256": hashlib.sha256(self.payload_luton.read_bytes()).hexdigest(),
                "soc_family_id": 1,
                "spi_software_mode_address": 0x70000064,
                "accepted_models": ["MS22", "MS22P", "MS220-8", "MS220-8P", "MS220-24", "MS220-24P"],
            },
            "jaguar1": {
                "filename": self.payload_jaguar.name,
                "bytes": self.payload_jaguar.stat().st_size,
                "sha256": hashlib.sha256(self.payload_jaguar.read_bytes()).hexdigest(),
                "soc_family_id": 2,
                "spi_software_mode_address": 0x70000068,
                "accepted_models": ["MS42P", "MS42", "MS320-24", "MS320-24P", "MS220-48", "MS220-48P", "MS220-48LP", "MS220-48FP", "MS320-48", "MS320-48P", "MS320-48LP", "MS320-48FP"],
            },
        }
        data = {
            "version": "test",
            "target_family": "vcore3",
            "models": {model: status},
            "recovery": {
                "uart_ramloader": {
                    "enabled": True, "protocol_version": 2,
                    "build_manifest_format": "postmerkos.vcoreiii-linuxloader-build.v7",
                    "loader_sha256": loader_digest,
                    "supported_soc_families": ["luton26", "jaguar1"],
                    "boot_menu": {
                        "options": {"1": "uart-ramloader", "2": "embedded-firmware-recovery"},
                        "probe_timeout_ms": 3000, "selection_timeout_ms": 5000,
                    },
                    "image_check_diagnostics": "structured-pass-warn-fail-skip-values-v1",
                    "embedded_recovery": {
                        family: {"bytes": record["bytes"], "sha256": record["sha256"]}
                        for family, record in payload_records.items()
                    },
                },
                "uart_firmware": {
                    "enabled": True,
                    "protocol_version": 2,
                    "full_image_bytes": bp.FULL_IMAGE_SIZE,
                    "flash_geometry": {
                        "bytes": bp.FULL_IMAGE_SIZE,
                        "erase_bytes": 64 * 1024,
                        "page_bytes": 256,
                        "address_bytes": 3,
                    },
                    "accepted_jedec_ids": ["c22018", "ef4018"],
                    "payloads": payload_records,
                },
            },
            "artifact": {
                "filename": self.image.name,
                "bytes": bp.FULL_IMAGE_SIZE,
                "sha256": digest,
                "boot_chain": "vcoreiii-linuxloader-spim-v2",
                "kernel_payload": {
                    "format": "postmerkos.vcoreiii-payload.v1",
                    "header_bytes": bp.SPIM_HEADER.size,
                    "payload_bytes": len(kernel),
                    "alignment_bytes": bp.SPIM_ALIGNMENT,
                    "load_address": 0x81000000,
                    "entry_point": 0x81000000,
                    "crc32": kernel_crc,
                    "sha256": kernel_sha,
                },
            },
        }
        self.manifest.write_text(json.dumps(data) + "\n", encoding="utf-8")


class ProtocolTests(unittest.TestCase):
    def test_write_all_handles_short_writes_and_retryable_errors(self) -> None:
        link = bp.SerialLink(19, echo=False)
        captured = bytearray()
        actions = [BlockingIOError(), 2, InterruptedError(), 3]

        def fake_write(_fd, view):
            action = actions.pop(0)
            if isinstance(action, BaseException):
                raise action
            captured.extend(bytes(view[:action]))
            return action

        with mock.patch.object(bp.select, "select", return_value=([], [19], [])), \
             mock.patch.object(bp.os, "write", side_effect=fake_write):
            link.write_all(b"abcde", timeout=1.0)
        self.assertEqual(captured, b"abcde")
        self.assertFalse(actions)

    def test_wait_for_surfaces_recovery_stage_failure(self) -> None:
        link = bp.SerialLink(19, echo=False)
        with mock.patch.object(
            link, "read_line",
            side_effect=["PMOSBOOT FAIL-RECOVERY-SIZE: MAX: 0x00400000 | GOT: 0x00400020"],
        ):
            with self.assertRaisesRegex(bp.ProtocolError, "FAIL-RECOVERY-SIZE"):
                link.wait_for(
                    ("PMOSBOOT PASS-RECOVERY-SIZE",), 1.0,
                    error_prefixes=("PMOSBOOT FAIL-RECOVERY",),
                )

    def test_ram_transfer_retries_nack_and_completes(self) -> None:
        host, target = socket.socketpair()
        payload = bytes(range(256)) * 5
        errors: list[BaseException] = []

        def simulate() -> None:
            try:
                raw = read_exact(target, bp.RAM_HEADER.size)
                fields = bp.RAM_HEADER.unpack(raw)
                self.assertEqual(fields[0], bp.RAM_MAGIC)
                self.assertEqual(fields[1], 2)
                self.assertEqual(fields[2], 0)
                self.assertEqual(zlib.crc32(raw[:-4]) & 0xFFFFFFFF, fields[-1])
                total, chunk_size = fields[5], fields[6]
                target.sendall(b"PMOSRAM HEADER-ACK\n")
                received = bytearray()
                expected = 0
                nacked = False
                while len(received) < total:
                    frame_header = read_exact(target, bp.RAM_FRAME.size)
                    magic, sequence, length, crc = bp.RAM_FRAME.unpack(frame_header)
                    data = read_exact(target, length)
                    self.assertEqual(magic, bp.RAM_FRAME_MAGIC)
                    self.assertLessEqual(length, chunk_size)
                    self.assertEqual(zlib.crc32(data) & 0xFFFFFFFF, crc)
                    if sequence == 0 and not nacked:
                        nacked = True
                        target.sendall(b"PMOSRAM NACK 00000000\n")
                        continue
                    self.assertEqual(sequence, expected)
                    received.extend(data)
                    expected += 1
                    if len(received) == total:
                        # Completion is a valid implicit final ACK if the explicit ACK is lost.
                        target.sendall(b"PMOSRAM VERIFIED SHA256=test\nPMOSRAM EXEC 81000000\n")
                    else:
                        target.sendall(f"PMOSRAM ACK {sequence:08x}\n".encode())
                self.assertEqual(bytes(received), payload)
            except BaseException as exc:  # pragma: no cover - rethrown in host thread
                errors.append(exc)
            finally:
                target.close()

        thread = threading.Thread(target=simulate)
        thread.start()
        try:
            bp.send_ram_payload(bp.SerialLink(host.fileno(), echo=False), payload,
                                0x81000000, 0x81000000, 256, 2, 1.0)
        finally:
            host.close()
            thread.join(3)
        if errors:
            raise errors[0]
        self.assertFalse(thread.is_alive())

    def test_object_transfer_frames_and_digest_source(self) -> None:
        host, target = socket.socketpair()
        data = b"manifest-data" * 150
        errors: list[BaseException] = []

        def simulate() -> None:
            try:
                received = bytearray()
                expected = 0
                while len(received) < len(data):
                    header = read_exact(target, bp.PACKAGE_FRAME.size)
                    magic, object_id, sequence, length, crc = bp.PACKAGE_FRAME.unpack(header)
                    block = read_exact(target, length)
                    self.assertEqual((magic, object_id, sequence),
                                     (bp.PACKAGE_FRAME_MAGIC, bp.OBJECT_MANIFEST, expected))
                    self.assertEqual(zlib.crc32(block) & 0xFFFFFFFF, crc)
                    received.extend(block)
                    expected += 1
                    if len(received) == len(data):
                        # Completion is a valid implicit final ACK if the explicit ACK is lost.
                        target.sendall(b"PMOSPKG OBJECT-VERIFIED 00000002\n")
                    else:
                        target.sendall(f"PMOSPKG ACK {object_id:08x} {sequence:08x}\n".encode())
                self.assertEqual(bytes(received), data)
            except BaseException as exc:
                errors.append(exc)
            finally:
                target.close()

        thread = threading.Thread(target=simulate)
        thread.start()
        try:
            bp.send_object(bp.SerialLink(host.fileno(), echo=False), None, data,
                           bp.OBJECT_MANIFEST, 128, 1, 1.0)
        finally:
            host.close()
            thread.join(3)
        if errors:
            raise errors[0]
        self.assertFalse(thread.is_alive())

    def test_bundle_and_payload_family_gate(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = BundleFixture(root)
            info = bp.validate_bundle(bundle.image, bundle.manifest, "MS42P", force=False)
            self.assertEqual(info.family, "jaguar1")
            payload = bundle.payload_luton
            descriptor = bp.inspect_payload(payload)
            self.assertEqual(descriptor.family, "luton26")
            self.assertNotEqual(descriptor.family, info.family)

    def test_bundle_rejects_unknown_flags_layout_and_untested_without_force(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = BundleFixture(root, status="untested")
            with self.assertRaisesRegex(bp.ProtocolError, "force operation"):
                bp.validate_bundle(bundle.image, bundle.manifest, "MS42P", force=False)
            self.assertEqual(bp.validate_bundle(bundle.image, bundle.manifest, "MS42P", force=True).model_status,
                             "untested")
            manifest = json.loads(bundle.manifest.read_text())
            manifest["artifact"]["boot_chain"] = "unknown"
            bundle.manifest.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(bp.ProtocolError, "boot_chain"):
                bp.validate_bundle(bundle.image, bundle.manifest, "MS42P", force=True)


    def test_payload_digest_and_geometry_are_bound_to_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = BundleFixture(root)
            info = bp.validate_bundle(bundle.image, bundle.manifest, "MS42P", force=False)
            descriptor = bp.inspect_payload(bundle.payload_jaguar)
            bp.validate_recovery_payload(bundle.payload_jaguar, descriptor, info)
            bundle.payload_jaguar.write_bytes(bundle.payload_jaguar.read_bytes() + b"tamper")
            tampered = bp.inspect_payload(bundle.payload_jaguar)
            with self.assertRaisesRegex(bp.ProtocolError, "size|SHA-256"):
                bp.validate_recovery_payload(bundle.payload_jaguar, tampered, info)
            manifest = json.loads(bundle.manifest.read_text())
            manifest["recovery"]["uart_firmware"]["flash_geometry"]["page_bytes"] = 512
            bundle.manifest.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(bp.ProtocolError, "flash geometry"):
                bp.validate_bundle(bundle.image, bundle.manifest, "MS42P", force=False)

    def _exercise_embedded_bootlog(self, family: str, family_id: int) -> mock.Mock:
        link = mock.Mock()
        link.wait_for.side_effect = [
            "PMOSBOOT MENU-PROBE TIMEOUT_MS=00000bb8",
            "PMOSBOOT PASS-MENU-TRIGGER: BYTE: 0x0000000D",
            "PMOSBOOT MENU 1=UART-RAMLOADER 2=FW-RECOVERY",
            "PMOSBOOT MENU-READY TIMEOUT_MS=00001388",
            "PMOSBOOT PASS-MENU-CHOICE: SELECTED: 0x00000002",
            f"PMOSBOOT INFO-RECOVERY: SOURCE: MENU-OPTION-2 | SOC: {family}",
            "PMOSBOOT PASS-RECOVERY-SIZE: MAX: 0x00400000 | GOT: 0x000037D8",
            "PMOSBOOT PASS-RECOVERY-COPY: LOAD: 0x81000000 | SIZE: 0x000037D8",
            "PMOSBOOT PASS-RECOVERY-EXEC: ENTRY: 0x81000000",
            f"PMOSREC READY 2 SOC={family} FAMILY={family_id:08x}",
        ]
        selected = br.enter_recovery(
            link, "embedded", family, 30.0, None, 0x81000000, 0x81000000,
            1024, 3, 5.0,
        )
        self.assertEqual(selected, "embedded")
        self.assertEqual(link.write_all.call_args_list, [mock.call(b"\r"), mock.call(b"2")])
        return link

    def test_embedded_recovery_matches_hardware_jaguar1_bootlog(self) -> None:
        self._exercise_embedded_bootlog("jaguar1", 2)

    def test_embedded_recovery_accepts_luton26_bootlog(self) -> None:
        self._exercise_embedded_bootlog("luton26", 1)

    def test_embedded_recovery_rejects_reported_soc_mismatch(self) -> None:
        link = mock.Mock()
        link.wait_for.side_effect = [
            "PMOSBOOT MENU-PROBE TIMEOUT_MS=00000bb8",
            "PMOSBOOT PASS-MENU-TRIGGER: BYTE: 0x0000000D",
            "PMOSBOOT MENU 1=UART-RAMLOADER 2=FW-RECOVERY",
            "PMOSBOOT MENU-READY TIMEOUT_MS=00001388",
            "PMOSBOOT PASS-MENU-CHOICE: SELECTED: 0x00000002",
            "PMOSBOOT INFO-RECOVERY: SOURCE: MENU-OPTION-2 | SOC: luton26",
        ]
        with self.assertRaisesRegex(bp.ProtocolError, "reports luton26"):
            br.enter_recovery(
                link, "embedded", "jaguar1", 30.0, None,
                0x81000000, 0x81000000, 1024, 3, 5.0,
            )

    def test_spim_crc_tamper_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            bundle = BundleFixture(Path(temp))
            with bundle.image.open("r+b") as stream:
                stream.seek(bp.KERNEL_OFFSET + bp.SPIM_HEADER.size)
                original = stream.read(1)
                stream.seek(bp.KERNEL_OFFSET + bp.SPIM_HEADER.size)
                stream.write(bytes([original[0] ^ 1]))
            manifest = json.loads(bundle.manifest.read_text())
            manifest["artifact"]["sha256"] = bp.sha256_file(bundle.image).hex()
            bundle.manifest.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(bp.ProtocolError, "SPIM kernel CRC mismatch"):
                bp.validate_bundle(bundle.image, bundle.manifest, "MS42P", force=False)

    def test_verify_operation_never_opens_serial(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            bundle = BundleFixture(root)
            result = subprocess.run(
                [sys.executable, str(TOOL_DIR / "bootloader-ramload.py"),
                 "--operation", "verify", "--recovery-path", "embedded",
                 "--firmware", str(bundle.image), "--manifest", str(bundle.manifest),
                 "--target-model", "MS42P"],
                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("without opening the serial port", result.stdout)


if __name__ == "__main__":
    unittest.main()
