import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("symbols", Path(__file__).parents[1] / "folderstorm_symbols.py")
symbols = importlib.util.module_from_spec(spec)
spec.loader.exec_module(symbols)


class Symbols(unittest.TestCase):
    def test_exact_symbols_only(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = [Path(directory) / name for name in ("viewer.pdb", "host.pdb")]
            for path in paths:
                path.write_bytes(b"Microsoft C/C++ MSF 7.00\r\n\x1aDS" + b"x" * 64)
            result = symbols.prepare("7.2.5.1", "a" * 40, paths)
            self.assertEqual(2, len(result["files"]))
            self.assertEqual(64, len(result["files"][0]["sha256"]))
            with self.assertRaises(ValueError):
                symbols.prepare("7.2.5.1", "a" * 40, paths[:1])
            with self.assertRaises(ValueError):
                symbols.prepare("7.2.5.1", "a" * 40, [paths[0], paths[0]])
            paths[0].write_bytes(b"not symbols")
            with self.assertRaises(ValueError):
                symbols.prepare("7.2.5.1", "a" * 40, paths)

    def test_build_identity(self):
        for version, commit in (("7.2.5", "a" * 40), ("7.2.5.1", "main")):
            with self.assertRaises(ValueError):
                symbols.prepare(version, commit, [])


if __name__ == "__main__":
    unittest.main()
