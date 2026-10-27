"""Regression: fvecs payloads are floats, not numeric int32 values."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest


class FvecsConversionTest(unittest.TestCase):
    def test_preserves_float_payload_bits_across_records(self):
        records = [(1.0, -2.5, 0.125), (-0.0, 3.25, -7.0)]
        converter = Path(__file__).resolve().parents[1] / "scripts/fvecs_to_f32.py"
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = Path(tmp) / "input.fvecs", Path(tmp) / "output.f32"
            src.write_bytes(b"".join(struct.pack("<i3f", 3, *row) for row in records))
            subprocess.run([sys.executable, str(converter), str(src), str(dst), "3"],
                           check=True, capture_output=True, text=True)
            self.assertEqual(dst.read_bytes(),
                             b"".join(struct.pack("<3f", *row) for row in records))


if __name__ == "__main__":
    unittest.main()
