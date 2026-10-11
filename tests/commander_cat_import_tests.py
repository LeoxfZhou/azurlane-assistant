#!/usr/bin/env python3
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).parents[1] / "tools/prepare_commander_cat_import.py"
INVENTORY = b"instance_id,cat_id,nickname,initial_talent_ids\nCAT0001,bella,test,special_wind + engine_small_1\n"
RULE = "embryo-2026-10-06-v2"

def manifest(**overrides):
    value = dict(format_version=2, rule_version=RULE,
                 inventory_sha256=hashlib.sha256(INVENTORY).hexdigest())
    value.update(overrides)
    return json.dumps(value).encode()

class ImportPreparationTests(unittest.TestCase):
    def run_script(self, *inputs):
        output = inputs[0].parent / "out"
        result = subprocess.run([sys.executable, str(SCRIPT), "--output", str(output),
                                 *map(str, inputs)], text=True, capture_output=True)
        return result, json.loads(result.stdout), output

    def test_direct_and_zip(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            csv = root / "commander_cat_inventory.csv"
            metadata = root / "commander_cat_manifest.json"
            csv.write_bytes(INVENTORY)
            metadata.write_bytes(manifest())
            result, payload, output = self.run_script(csv, metadata)
            self.assertEqual(result.returncode, 0, payload)
            self.assertEqual((output / csv.name).read_bytes(), INVENTORY)
            archive = root / "export.zip"
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr(csv.name, INVENTORY)
                z.writestr(metadata.name, manifest())
            result, payload, _ = self.run_script(archive)
            self.assertEqual(result.returncode, 0, payload)

    def test_legacy_and_versions_rejected_without_output_mutation(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            csv = root / "commander_cat_inventory.csv"
            csv.write_bytes(INVENTORY)
            output = root / "out"
            output.mkdir()
            sentinel = output / "keep.txt"
            sentinel.write_text("preserve")
            result, _, _ = self.run_script(csv)
            self.assertNotEqual(result.returncode, 0)
            metadata = root / "commander_cat_manifest.json"
            for data in (manifest(format_version=1), manifest(rule_version="old"),
                         manifest(inventory_sha256="bad"), b"[]", b"broken"):
                metadata.write_bytes(data)
                result, _, _ = self.run_script(csv, metadata)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(sentinel.read_text(), "preserve")

    def test_unsafe_settings_and_extra_files_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            archive = Path(folder) / "bad.zip"
            for name in ("../commander_cat_inventory.csv", "commander_cat_settings.csv", "notes.txt"):
                with zipfile.ZipFile(archive, "w") as z:
                    z.writestr(name, INVENTORY)
                result, _, _ = self.run_script(archive)
                self.assertNotEqual(result.returncode, 0)

    def test_rule_version_matches_cpp(self):
        header = (SCRIPT.parents[1] / "src/commander_cat_embryo.hpp").read_text()
        self.assertIn('"' + RULE + '"', header)

if __name__ == "__main__":
    unittest.main()
