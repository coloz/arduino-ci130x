#!/usr/bin/env python3
"""Run the IR resource preparation contract against Python and PowerShell.

Run from any directory with Python 3. PowerShell cases run when powershell or
pwsh is installed. All sketch fixtures live in a disposable temporary folder.
"""

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


PLATFORM = Path(__file__).resolve().parents[2]
DATABASE_NAME = "[50000]ir_data_2024_08_16.bin"
MANIFEST_NAME = ".chipintelli-ir-database.json"
DATABASE = (PLATFORM / "libraries/ChipIntelliIR/examples/AirConditioner/recursos"
            / "user_file_entries" / DATABASE_NAME).read_bytes()
DATABASE_SHA256 = "f7e3680b45f9abe56c6d3e16d1bbffd7336e286d0b0dabed20e1d2b4854b97d0"
POWERSHELL = shutil.which("powershell") or shutil.which("pwsh")
ENABLED = "#define CHIPINTELLI_IR_DATABASE 1\n#include <ChipIntelliIR.h>\n"
DISABLED = "#define CHIPINTELLI_IR_DATABASE 0\n#include <ChipIntelliIR.h>\n"


class PrepareDatabaseContract:
    implementation = None

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ci13xx ir [fixture] ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / "Arduino sketch [1]" / "sketch.ino"
        self.write_source(ENABLED)

    def write_source(self, text):
        self.source.parent.mkdir(parents=True, exist_ok=True)
        self.source.write_text(text + "\nvoid setup() {}\nvoid loop() {}\n",
                               encoding="utf-8")

    def entries(self, root=None):
        return (root or self.source.parent) / "recursos/user_file_entries"

    def invoke(self, success=True, chip="ci1306", platform=PLATFORM, source=None):
        source = self.source if source is None else source
        if self.implementation == "python":
            command = [sys.executable, str(PLATFORM / "tools/prepare_ir_database.py"),
                       "--source", str(source), "--platform-path", str(platform),
                       "--chip", chip]
        else:
            command = [POWERSHELL, "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                       str(PLATFORM / "tools/prepare_ir_database.ps1"),
                       "-Source", str(source), "-PlatformPath", str(platform),
                       "-Chip", chip]
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=30)
        output = result.stdout + result.stderr
        if success:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0, output)
        return output

    def assert_official_database(self, directory):
        self.assertEqual((directory / DATABASE_NAME).read_bytes(), DATABASE)

    def snapshot(self):
        return {str(path.relative_to(self.root)): (path.read_bytes(), path.stat().st_mtime_ns)
                for path in self.root.rglob("*") if path.is_file()}

    def make_aily(self):
        project = self.root / "Aily project [2]"
        project.mkdir()
        (project / "package.json").write_text(json.dumps({
            "name": "ir-fixture", "board": "CI1306",
            "dependencies": {"@aily-project/board-ci1306": "1.0.0"},
        }), encoding="utf-8")
        (project / "project.abi").write_text("{}", encoding="utf-8")
        self.source = project / ".temp/sketch/sketch.ino"
        self.write_source(ENABLED)
        return project

    def test_arduino_creates_common_overlay_and_is_idempotent(self):
        self.invoke()
        directory = self.entries()
        self.assert_official_database(directory)
        manifest = json.loads((directory / MANIFEST_NAME).read_text(encoding="utf-8-sig"))
        self.assertEqual(manifest["schemaVersion"], 1)
        self.assertEqual(manifest["name"], DATABASE_NAME)
        self.assertEqual(manifest["size"], len(DATABASE))
        self.assertEqual(manifest["sha256"].lower(), DATABASE_SHA256)
        before = self.snapshot()
        self.invoke()
        self.assertEqual(self.snapshot(), before, "Repeated preparation must not rewrite files")
        self.assertFalse((self.source.parent / "src").exists())
        self.assertFalse((self.source.parent / "recursos/cwsl").exists())

    def test_aily_syncs_persistent_and_current_sketch_on_first_build(self):
        project = self.make_aily()
        self.invoke()
        persistent = self.entries(project / "src")
        current = self.entries()
        self.assert_official_database(persistent)
        self.assert_official_database(current)
        self.assertTrue((persistent / MANIFEST_NAME).is_file())
        self.assertTrue((current / MANIFEST_NAME).is_file())
        before = self.snapshot()
        self.invoke()
        self.assertEqual(self.snapshot(), before)

    def test_extensionless_aily_source_resolves_ino_and_syncs_both_paths(self):
        project = self.make_aily()
        self.invoke(source=self.source.with_suffix(""))
        self.assert_official_database(self.entries(project / "src"))
        self.assert_official_database(self.entries())

    def test_extensionless_source_can_resolve_cpp(self):
        self.source = self.root / "C++ sketch [3]" / "sketch.cpp"
        self.write_source(ENABLED)
        self.invoke(source=self.source.with_suffix(""))
        self.assert_official_database(self.entries())

    def test_source_resolution_prefers_exact_file_then_ino(self):
        self.source = self.root / "source priority [4]" / "sketch"
        self.write_source(ENABLED)
        self.source.with_suffix(".ino").write_text(DISABLED, encoding="utf-8")
        self.invoke()
        self.assert_official_database(self.entries())

        self.source = self.root / "ino priority [5]" / "sketch.ino"
        self.write_source(ENABLED)
        self.source.with_suffix(".cpp").write_text(DISABLED, encoding="utf-8")
        self.invoke(source=self.source.with_suffix(""))
        self.assert_official_database(self.entries())

    def test_temp_sketch_without_project_marker_stays_local(self):
        self.source = self.root / ".temp/sketch/sketch.ino"
        self.write_source(ENABLED)
        self.invoke()
        self.assert_official_database(self.entries())
        self.assertFalse((self.root / "src").exists())

    def test_removing_macro_cleans_only_owned_files_from_both_aily_locations(self):
        project = self.make_aily()
        self.invoke()
        directories = [self.entries(project / "src"), self.entries()]
        for directory in directories:
            (directory / "[123]user data.bin").write_bytes(b"keep custom resource")
        self.write_source("#include <ChipIntelliIR.h>")
        self.invoke()
        for directory in directories:
            self.assertFalse((directory / DATABASE_NAME).exists())
            self.assertFalse((directory / MANIFEST_NAME).exists())
            self.assertEqual((directory / "[123]user data.bin").read_bytes(),
                             b"keep custom resource")

    def test_zero_disables_and_modified_managed_file_is_preserved(self):
        self.invoke()
        database = self.entries() / DATABASE_NAME
        database.write_bytes(b"user changed this database")
        self.write_source(DISABLED)
        self.invoke()
        self.assertEqual(database.read_bytes(), b"user changed this database")

    def test_user_owned_official_file_is_never_claimed_or_deleted(self):
        directory = self.entries()
        directory.mkdir(parents=True)
        database = directory / DATABASE_NAME
        database.write_bytes(DATABASE)
        self.invoke()
        self.assertFalse((directory / MANIFEST_NAME).exists())
        self.write_source(DISABLED)
        self.invoke()
        self.assert_official_database(directory)

    def test_user_owned_same_id_with_other_filename_is_reused(self):
        directory = self.entries()
        directory.mkdir(parents=True)
        database = directory / "[050000]my database.bin"
        database.write_bytes(DATABASE)
        self.invoke()
        self.assertEqual(list(directory.iterdir()), [database])
        self.write_source(DISABLED)
        self.invoke()
        self.assertEqual(database.read_bytes(), DATABASE)

    def test_raw_mode_does_not_require_bundled_database(self):
        self.write_source(DISABLED)
        self.invoke(chip="ci1302", platform=self.root / "missing platform")
        self.assertFalse(self.entries().exists())

    def test_disabled_without_manifest_does_not_validate_entries_path(self):
        self.write_source(DISABLED)
        entries_path = self.entries()
        entries_path.parent.mkdir(parents=True)
        entries_path.write_bytes(b"user file, not a resource directory")
        before = self.snapshot()
        self.invoke(chip="ci1302", platform=self.root / "missing platform")
        self.assertEqual(self.snapshot(), before)

    @unittest.skipUnless(POWERSHELL, "PowerShell is not installed")
    def test_other_implementation_can_clean_managed_resource(self):
        self.invoke()
        self.write_source(DISABLED)
        self.implementation = ("powershell" if self.implementation == "python"
                               else "python")
        self.invoke()
        self.assertFalse((self.entries() / DATABASE_NAME).exists())
        self.assertFalse((self.entries() / MANIFEST_NAME).exists())

    def test_managed_receipt_without_database_is_repaired_without_temporary_files(self):
        self.invoke()
        directory = self.entries()
        (directory / DATABASE_NAME).unlink()
        self.assertTrue((directory / MANIFEST_NAME).is_file())
        self.invoke()
        self.assert_official_database(directory)
        self.assertEqual({path.name for path in directory.iterdir()},
                         {DATABASE_NAME, MANIFEST_NAME})

    def test_missing_zero_and_quoted_or_commented_macro_do_not_enable(self):
        cases = {
            "missing": "#include <ChipIntelliIR.h>",
            "zero": DISABLED,
            "line comment": "// #define CHIPINTELLI_IR_DATABASE 1",
            "block comment": "/*\n#define CHIPINTELLI_IR_DATABASE 1\n*/",
            "string": 'const char *s = "#define CHIPINTELLI_IR_DATABASE 1";',
            "raw string": 'const char *s = R"tag(\n#define CHIPINTELLI_IR_DATABASE 1\n)tag";',
            "raw conditional text": 'const char *s = R"tag(\n#endif\n#if 1\n#define CHIPINTELLI_IR_DATABASE 1\n)tag";',
            "continued comment": "// comment \\\n#define CHIPINTELLI_IR_DATABASE 1",
        }
        for index, (name, source) in enumerate(cases.items()):
            with self.subTest(source=name):
                self.source = self.root / ("disabled case %s" % index) / "sketch.ino"
                self.write_source(source)
                self.invoke(chip="ci1302")
                self.assertFalse((self.entries() / DATABASE_NAME).exists())

    def test_supported_whitespace_comments_and_continuation(self):
        self.write_source('const char *s = R"tag(\n#endif\n#if 1\n)tag";\n'
                          "  # define CHIPINTELLI_IR_DATABASE \\\n1 // enable resource\n")
        self.invoke()
        self.assert_official_database(self.entries())

    def test_unsupported_macro_forms_fail_before_copy(self):
        cases = [
            "#define CHIPINTELLI_IR_DATABASE (1)",
            "#define CHIPINTELLI_IR_DATABASE true",
            '#define CHIPINTELLI_IR_DATABASE "1"',
            '#define CHIPINTELLI_IR_DATABASE 1 "extra"',
            "#define CHIPINTELLI_IR_DATABASE 1 'x'",
            "#define CHIPINTELLI_IR_DATABASE 2",
            "#if 0\n#define CHIPINTELLI_IR_DATABASE 1\n#endif",
            "#ifdef SOME_FLAG\n#define CHIPINTELLI_IR_DATABASE 1\n#endif",
            "#undef CHIPINTELLI_IR_DATABASE",
            "#define CHIPINTELLI_IR_DATABASE 1\n#define CHIPINTELLI_IR_DATABASE 1",
        ]
        for index, source in enumerate(cases):
            with self.subTest(source=source):
                self.source = self.root / ("invalid case %s" % index) / "sketch.ino"
                self.write_source(source)
                output = self.invoke(success=False)
                self.assertIn("CHIPINTELLI_IR_DATABASE", output)
                self.assertFalse((self.entries() / DATABASE_NAME).exists())

    def test_ci1302_enabled_is_rejected_before_creating_resources(self):
        output = self.invoke(success=False, chip="ci1302")
        self.assertIn("CI1302", output.upper())
        self.assertFalse(self.entries().exists())

    def test_destination_hash_and_same_id_conflicts_preserve_user_data(self):
        for name in [DATABASE_NAME, "[50000]user.bin", "[050000]user.bin"]:
            with self.subTest(name=name):
                directory = self.entries()
                directory.mkdir(parents=True, exist_ok=True)
                conflict = directory / name
                conflict.write_bytes(b"custom incompatible resource")
                before = self.snapshot()
                self.invoke(success=False)
                self.assertEqual(self.snapshot(), before)
                conflict.unlink()

    def test_aily_checks_both_destinations_before_creating_any_database(self):
        project = self.make_aily()
        current = self.entries()
        current.mkdir(parents=True)
        (current / DATABASE_NAME).write_bytes(b"user incompatible database")
        before = self.snapshot()
        self.invoke(success=False)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse((self.entries(project / "src") / DATABASE_NAME).exists())

    def test_same_id_without_supported_bin_extension_is_rejected(self):
        directory = self.entries()
        directory.mkdir(parents=True)
        (directory / "[50000]database.dat").write_bytes(DATABASE)
        before = self.snapshot()
        self.invoke(success=False)
        self.assertEqual(self.snapshot(), before)

    def test_uppercase_bin_extension_is_reused_like_merger(self):
        directory = self.entries()
        directory.mkdir(parents=True)
        database = directory / "[50000]database.BIN"
        database.write_bytes(DATABASE)
        before = self.snapshot()
        self.invoke()
        self.assertEqual(self.snapshot(), before)
        self.assertFalse((directory / DATABASE_NAME).exists())
        self.assertFalse((directory / MANIFEST_NAME).exists())

    def test_duplicate_valid_same_id_files_are_rejected_before_copy(self):
        directory = self.entries()
        directory.mkdir(parents=True)
        (directory / DATABASE_NAME).write_bytes(DATABASE)
        (directory / "[050000]duplicate.bin").write_bytes(DATABASE)
        before = self.snapshot()
        self.invoke(success=False)
        self.assertEqual(self.snapshot(), before)

    def test_corrupt_package_database_is_rejected(self):
        fake_platform = self.root / "incomplete core"
        source_database = (fake_platform
                           / "libraries/ChipIntelliIR/examples/AirConditioner/recursos"
                           / "user_file_entries" / DATABASE_NAME)
        source_database.parent.mkdir(parents=True)
        source_database.write_bytes(b"invalid official database")
        output = self.invoke(success=False, platform=fake_platform)
        self.assertRegex(output.lower(), "sha.?256|hash|database")
        self.assertFalse((self.entries() / DATABASE_NAME).exists())


class PythonContract(PrepareDatabaseContract, unittest.TestCase):
    implementation = "python"


@unittest.skipUnless(POWERSHELL, "PowerShell is not installed")
class PowerShellContract(PrepareDatabaseContract, unittest.TestCase):
    implementation = "powershell"


if __name__ == "__main__":
    if hashlib.sha256(DATABASE).hexdigest() != DATABASE_SHA256:
        raise SystemExit("The package IR database does not match its documented SDK hash")
    unittest.main(verbosity=2)
