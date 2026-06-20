from __future__ import annotations

from contextlib import redirect_stdout
import hashlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zlib

import pmosrec_v3 as p3
from bootloader_protocol import ProtocolError


class FakeBinaryLink:
    def __init__(self, reads: list[bytes]) -> None:
        self.reads = list(reads)
        self.writes: list[bytes] = []

    def read_exact(self, length: int, timeout: float, *, echo: bool = False) -> bytes:
        if not self.reads:
            raise ProtocolError("timed out reading binary bytes")
        data = self.reads.pop(0)
        if len(data) != length:
            raise AssertionError((len(data), length))
        return data

    def write_all(self, data: bytes, timeout: float = 10.0) -> None:
        self.writes.append(bytes(data))


class FakeLineLink:
    def __init__(self, lines: list[str]) -> None:
        self.lines = list(lines)

    def read_line(self, timeout: float) -> str:
        if not self.lines:
            raise ProtocolError("timed out")
        return self.lines.pop(0)




class FakeProtocolLink:
    def __init__(self, lines: list[str]) -> None:
        self.lines = list(lines)
        self.writes: list[bytes] = []

    def write_all(self, data: bytes, timeout: float = 10.0) -> None:
        self.writes.append(bytes(data))

    def wait_for(self, prefixes: tuple[str, ...], timeout: float, **_kwargs) -> str:
        if not self.lines:
            raise ProtocolError(f"timed out waiting for: {prefixes}")
        line = self.lines.pop(0)
        if not line.startswith(prefixes):
            raise AssertionError((line, prefixes))
        return line


class PMOSRECv3Tests(unittest.TestCase):
    def test_lz4_roundtrips_compressible_random_and_short_blocks(self) -> None:
        samples = [
            b"A" * 4096,
            p3.xorshift_bytes(0x12345678, 4096),
            b"postmerkOS" * 13,
            bytes(range(256)) * 16,
        ]
        for sample in samples:
            encoded = p3.lz4_compress_block(sample)
            self.assertEqual(p3.lz4_decompress_block(encoded, len(sample)), sample)

    def test_lz4_rejects_corrupt_and_truncated_blocks(self) -> None:
        with self.assertRaises(ProtocolError):
            p3.lz4_decompress_block(b"\x10", 4096)
        with self.assertRaises(ProtocolError):
            p3.lz4_decompress_block(b"\x00\x00\x00", 4096)

    def test_sparse_plan_omits_ff_frames_and_reconstructs_exactly(self) -> None:
        data = b"\xff" * 4096 + b"A" * 4096 + b"\xff" * 4096 + b"B" * 123
        plan = p3.make_plan(data, 4096, p3.REP_SPARSE)
        self.assertEqual([frame.offset for frame in plan.frames], [4096, 12288])
        output = bytearray(b"\xff" * len(data))
        for frame in plan.frames:
            decoded = (
                p3.lz4_decompress_block(frame.payload, 4096)
                if frame.flags & p3.FRAME_FLAG_LZ4 else frame.payload
            )
            output[frame.offset : frame.offset + frame.decoded_length] = decoded
        self.assertEqual(bytes(output), data)

    def test_sparse_lz4_is_only_selected_when_combined_mode_qualified(self) -> None:
        selection = p3.TransportSelection(460800, 4096, 8, True, True, False)
        with tempfile.TemporaryDirectory() as temp:
            image = Path(temp) / "image.bin"
            image.write_bytes((b"A" * 4096 + b"\xff" * 4096) * 2048)
            chosen = p3.choose_representation(image, selection)
        self.assertNotEqual(chosen.mode, p3.REP_SPARSE_LZ4)

    def test_frame_header_has_independent_header_wire_and_decoded_crcs(self) -> None:
        decoded = b"frame-data" * 100
        frame = p3.EncodedFrame(7, 0x4000, len(decoded), 0, decoded, zlib.crc32(decoded) & 0xFFFFFFFF)
        wire = frame.wire(p3.OBJECT_IMAGE)
        fields = p3.FRAME_HEADER.unpack(wire[: p3.FRAME_HEADER.size])
        self.assertEqual(fields[0], p3.FRAME_MAGIC)
        self.assertEqual(fields[1], p3.OBJECT_IMAGE)
        self.assertEqual(fields[2], 7)
        self.assertEqual(fields[7], zlib.crc32(decoded) & 0xFFFFFFFF)
        self.assertEqual(fields[8], zlib.crc32(decoded) & 0xFFFFFFFF)
        self.assertEqual(fields[9], zlib.crc32(wire[: p3.FRAME_HEADER.size - 4]) & 0xFFFFFFFF)

    def test_compact_ack_is_crc_checked_and_confirmed(self) -> None:
        body = struct.pack("<6I", p3.ACK_MAGIC, p3.OBJECT_IMAGE, 8, 4, 0, 0)
        good = body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF)
        bad = good[:-1] + bytes([good[-1] ^ 0x80])
        link = FakeBinaryLink([bad, good])
        self.assertEqual(p3._read_ack(link, p3.OBJECT_IMAGE, 8.0), (8, 4, 0, 0))
        self.assertEqual(link.writes, [p3.ACK_CONFIRM_BYTE])

    def test_package_header_is_v3_manifest_first_and_crc_bound(self) -> None:
        class Bundle:
            model = "MS42P"
            family = "jaguar1"
            manifest_bytes = b"{}"
            manifest_crc32 = zlib.crc32(manifest_bytes) & 0xFFFFFFFF
            image_crc32 = 0x12345678
            image_sha256 = hashlib.sha256(b"image").digest()
            manifest_sha256 = hashlib.sha256(manifest_bytes).digest()

        image_plan = p3.RepresentationPlan(p3.REP_RAW, 4096, (
            p3.EncodedFrame(0, 0, 1, 0, b"x", zlib.crc32(b"x") & 0xFFFFFFFF),
        ))
        manifest_plan = p3.make_manifest_plan(Bundle.manifest_bytes, 4096)
        raw = p3.make_package_header(Bundle, image_plan, manifest_plan, 8, dry_run=False, force=True)
        self.assertEqual(len(raw), 144)
        fields = p3.PACKAGE_HEADER.unpack(raw)
        self.assertEqual(fields[0], p3.PACKAGE_MAGIC)
        self.assertEqual(fields[1], 3)
        self.assertEqual(fields[6], 4096)
        self.assertEqual(fields[7], 8)
        self.assertEqual(fields[-1], zlib.crc32(raw[:-4]) & 0xFFFFFFFF)

    def test_flash_phase_progress_and_success_are_rendered(self) -> None:
        link = FakeLineLink([
            "PMOSREC PROGRESS ERASE-BEGIN",
            "PMOSREC PROGRESS ERASE 00100000",
            "PMOSREC PROGRESS ERASE-END",
            "PMOSREC PROGRESS PROGRAM-BEGIN",
            "PMOSREC PROGRESS PROGRAM 00800000",
            "PMOSREC PROGRESS PROGRAM-END",
            "PMOSREC PROGRESS VERIFY-BEGIN",
            "PMOSREC PROGRESS VERIFY 00f00000",
            "PMOSREC PROGRESS VERIFY-END",
            "PMOSREC RESULT SUCCESS",
        ])
        output = io.StringIO()
        with redirect_stdout(output):
            result = p3.wait_for_flash_success(link, 30.0)
        self.assertEqual(result, "PMOSREC RESULT SUCCESS")
        text = output.getvalue()
        self.assertIn("Erasing SPI NOR", text)
        self.assertIn("Programming SPI NOR", text)
        self.assertIn("Readback verification", text)
        self.assertIn("100% complete", text)

    def test_flash_phase_error_is_not_hidden(self) -> None:
        link = FakeLineLink(["PMOSREC RESULT ERROR PROGRAM-TIMEOUT AT=00040000"])
        with self.assertRaisesRegex(ProtocolError, "PROGRAM-TIMEOUT"):
            p3.wait_for_flash_success(link, 1.0)

    def test_baud_refinement_stops_at_two_percent(self) -> None:
        class Controller:
            current_rate = 115200

        link = object()
        results = {
            153600: (True, 153600),
            230400: (True, 230400),
            250000: (True, 250000),
            256000: (True, 256000),
            307200: (True, 307200),
            460800: (True, 460800),
            500000: (False, 500000),
        }

        def candidate(_link, _controller, requested, current, tested):
            if requested in results:
                return results[requested]
            return False, requested

        with mock.patch.object(p3, "STANDARD_BAUDS", tuple(results)), \
             mock.patch.object(p3, "test_baud_candidate", side_effect=candidate) as called:
            selected = p3.negotiate_fastest_baud(link, Controller(), refine_percent=2.0)
        self.assertEqual(selected, 460800)
        # Gap to 500000 is >2%, so at least one midpoint is considered.
        self.assertTrue(any(call.args[2] == 480400 for call in called.call_args_list))

    def _package_fixture(self):
        class Bundle:
            model = "MS42P"
            family = "jaguar1"
            manifest_bytes = b"{}"
            manifest_crc32 = zlib.crc32(manifest_bytes) & 0xFFFFFFFF
            image_crc32 = 0x12345678
            image_sha256 = hashlib.sha256(b"image").digest()
            manifest_sha256 = hashlib.sha256(manifest_bytes).digest()
            image = Path("unused.bin")

        plan = p3.RepresentationPlan(p3.REP_RAW, 4096, (
            p3.EncodedFrame(0, 0, 1, 0, b"x", zlib.crc32(b"x") & 0xFFFFFFFF),
        ))
        selection = p3.TransportSelection(460800, 4096, 8, True, True, True, plan)
        return Bundle, plan, selection

    def test_auto_confirmation_sends_the_complete_live_challenge(self) -> None:
        Bundle, plan, selection = self._package_fixture()
        lines = [
            "PMOS3 PACKAGE-READY",
            "PMOS3 PACKAGE-HEADER-ACK MODEL=MS42P",
            "PMOS3 MANIFEST-OBJECT-VERIFIED",
            "PMOS3 MANIFEST-ACCEPTED",
            "PMOS3 IMAGE-OBJECT-VERIFIED",
            "PMOSPKG VERIFIED MODEL=MS42P",
            "PMOSREC ERASE-CHALLENGE 12620a82",
            "PMOSREC CONFIRMATION-WAIT ENTER=ERASEFLASH+12620a82",
            "PMOSREC CONFIRMATION-ACK",
            *(f"PMOSREC REBOOT {value}" for value in range(5, 0, -1)),
            "PMOSREC REBOOT NOW",
            "LinuxLoader built test",
        ]
        link = FakeProtocolLink(lines)
        with mock.patch.object(p3, "choose_representation", return_value=plan), \
             mock.patch.object(p3, "make_manifest_plan", return_value=plan), \
             mock.patch.object(p3, "send_frames", return_value=0), \
             mock.patch.object(p3, "wait_for_flash_success", return_value="PMOSREC RESULT SUCCESS"):
            result = p3.send_package_v3(
                link, Bundle, selection, dry_run=False, force=True, auto_confirm=True
            )
        self.assertEqual(result, "PMOSREC RESULT SUCCESS")
        self.assertIn(b"ERASEFLASH 12620a82\n", link.writes)

    def test_manual_confirmation_retries_without_cancelling(self) -> None:
        Bundle, plan, selection = self._package_fixture()
        lines = [
            "PMOS3 PACKAGE-READY",
            "PMOS3 PACKAGE-HEADER-ACK MODEL=MS42P",
            "PMOS3 MANIFEST-OBJECT-VERIFIED",
            "PMOS3 MANIFEST-ACCEPTED",
            "PMOS3 IMAGE-OBJECT-VERIFIED",
            "PMOSPKG VERIFIED MODEL=MS42P",
            "PMOSREC ERASE-CHALLENGE deadbeef",
            "PMOSREC CONFIRMATION-WAIT ENTER=ERASEFLASH+deadbeef",
            "PMOSREC CONFIRMATION-REJECTED EXPECTED=ERASEFLASH deadbeef",
            "PMOSREC CONFIRMATION-ACK",
            *(f"PMOSREC REBOOT {value}" for value in range(5, 0, -1)),
            "PMOSREC REBOOT NOW",
            "LinuxLoader built test",
        ]
        link = FakeProtocolLink(lines)
        entries = iter(("ERASEFLASH", "ERASEFLASH deadbeef"))
        with mock.patch.object(p3, "choose_representation", return_value=plan), \
             mock.patch.object(p3, "make_manifest_plan", return_value=plan), \
             mock.patch.object(p3, "send_frames", return_value=0), \
             mock.patch.object(p3, "wait_for_flash_success", return_value="PMOSREC RESULT SUCCESS"):
            p3.send_package_v3(
                link, Bundle, selection, dry_run=False, force=False, auto_confirm=False,
                manual_input=lambda _prompt: next(entries),
            )
        self.assertIn(b"ERASEFLASH\n", link.writes)
        self.assertIn(b"ERASEFLASH deadbeef\n", link.writes)

    def test_initial_eta_scales_with_negotiated_baud(self) -> None:
        slow = p3.ProgressTracker(1024 * 1024, 115200, "slow")
        fast = p3.ProgressTracker(1024 * 1024, 460800, "fast")
        self.assertAlmostEqual(slow.initial_seconds / fast.initial_seconds, 4.0, places=6)


    def test_source_contract_has_sync_commit_and_independent_fallback(self) -> None:
        source = (Path(__file__).resolve().parents[1] / "pmosrec_v3.py").read_text()
        for token in (
            "BAUD-SYNC-ACK", "BAUD-COMMIT", "BAUD-COMMITTED",
            "BAUD-FALLBACK-READY", "refine_percent", "STANDARD_BAUDS",
            "termios2", "sparse-lz4", "ACK_CONFIRM_BYTE",
        ):
            self.assertIn(token, source)


if __name__ == "__main__":
    unittest.main()
