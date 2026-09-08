#!/usr/bin/env python3
"""Check public/profile overlay merging and Python/PowerShell parity.

Run with: python tools/tests/test_user_file_overlays.py
PowerShell comparisons run when powershell or pwsh is available. No SDK build
or attached hardware is required; every generated file lives in a temp folder.
"""

import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True
TOOLS = Path(__file__).resolve().parents[1]
ROOT = TOOLS.parent
POWERSHELL = shutil.which('powershell') or shutil.which('pwsh')
SPEC = importlib.util.spec_from_file_location('ci13xx_postbuild', TOOLS / 'postbuild.py')
POSTBUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(POSTBUILD)


def read_container(data):
    count = struct.unpack_from('<H', data)[0]
    entries = {}
    for index in range(count):
        entry_id, offset, size = struct.unpack_from('<HII', data, 2 + index * 10)
        if entry_id in entries or offset % 16 or offset + size > len(data):
            raise AssertionError('Invalid merged user-file table')
        entries[entry_id] = data[offset:offset + size]
    return entries


class UserFileOverlayTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ci13xx overlays ')
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.base = self.work / 'base.bin'
        self.base.write_bytes(struct.pack('<HHII', 1, 7, 16, 4) + b'\xff' * 4 + b'base')
        self.public = self.work / 'recursos' / 'user_file_entries'
        self.profile = self.work / 'recursos' / 'cwsl' / 'user_file_entries'
        self.public.mkdir(parents=True)
        self.profile.mkdir(parents=True)

    def put(self, directory, name, data):
        (directory / name).write_bytes(data)

    def merge_all(self, primary=None, additional=None, error=None):
        primary = primary or self.public
        commands = [('python', [sys.executable, str(TOOLS / 'merge_user_file_entries.py'),
                                '--base-user-file', str(self.base),
                                '--entries-directory', str(primary)])]
        if additional is not None:
            commands[0][1].extend(['--additional-entries-directory', str(additional)])
        if POWERSHELL:
            command = [POWERSHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                       str(TOOLS / 'merge_user_file_entries.ps1'),
                       '-BaseUserFile', str(self.base), '-EntriesDirectory', str(primary)]
            if additional is not None:
                command.extend(['-AdditionalEntriesDirectory', str(additional)])
            commands.append(('powershell', command))
        outputs = []
        original_base = self.base.read_bytes()
        for language, command in commands:
            with self.subTest(language=language):
                output = self.work / (language + '-merged.bin')
                command.extend(['--output' if language == 'python' else '-Output', str(output)])
                result = subprocess.run(command, capture_output=True, text=True,
                                        encoding='utf-8', errors='replace', timeout=30)
                detail = result.stdout + result.stderr
                if error is not None:
                    self.assertNotEqual(result.returncode, 0, detail)
                    self.assertIn(error, detail)
                    self.assertFalse(output.exists(), 'A rejected merge must not create output')
                else:
                    self.assertEqual(result.returncode, 0, detail)
                    outputs.append(output.read_bytes())
                self.assertEqual(self.base.read_bytes(), original_base)
        if outputs:
            self.assertTrue(all(data == outputs[0] for data in outputs),
                            'Python and PowerShell must produce identical container bytes')
            return read_container(outputs[0])

    def test_legacy_single_directory_still_overrides_base(self):
        self.put(self.public, '[7]replacement.bin', b'replacement')
        self.put(self.public, '[9]new.BIN', b'new')
        self.assertEqual(self.merge_all(), {7: b'replacement', 9: b'new'})

    def test_public_and_profile_are_merged_once_with_identical_ids_deduplicated(self):
        self.put(self.public, '[7]public.bin', b'replacement')
        self.put(self.profile, '[7]legacy.bin', b'replacement')
        self.put(self.profile, '[19]profile.bin', b'profile')
        database = (ROOT / 'libraries/ChipIntelliIR/examples/AirConditioner/recursos'
                    '/user_file_entries/[50000]ir_data_2024_08_16.bin').read_bytes()
        self.put(self.public, '[50000]ir.bin', database)
        self.put(self.profile, '[50000]legacy_ir.bin', database)
        self.assertEqual(self.merge_all(additional=self.profile),
                         {7: b'replacement', 19: b'profile', 50000: database})

    def test_same_directory_is_not_scanned_twice(self):
        self.put(self.public, '[8]one.bin', b'one')
        self.assertEqual(self.merge_all(additional=str(self.public) + '/.'),
                         {7: b'base', 8: b'one'})

    def test_cross_directory_conflict_fails_before_overriding_base(self):
        self.put(self.public, '[7]public.bin', b'public')
        self.put(self.profile, '[7]profile.bin', b'profile')
        self.merge_all(additional=self.profile, error='Conflicting user-file overlay ID 7')

    def test_duplicate_ids_within_one_directory_remain_an_error(self):
        self.put(self.public, '[8]first.bin', b'same')
        self.put(self.public, '[8]second.bin', b'same')
        self.merge_all(additional=self.profile, error='Duplicate user-file overlay ID 8')

    def test_empty_directories_and_empty_base_are_valid(self):
        self.base.write_bytes(b'\x00\x00')
        self.assertEqual(self.merge_all(additional=self.profile), {})

    def test_corrupt_reserved_database_is_rejected(self):
        self.put(self.public, '[50000]invalid.bin', b'invalid database')
        self.merge_all(additional=self.profile, error='User-file ID 50000 is reserved')

    def test_postbuild_collects_public_and_profile_for_ci1302_check(self):
        resources = self.public.parent
        directories = POSTBUILD.user_file_overlay_directories(resources, self.profile.parent)
        self.assertEqual(directories, [self.public.resolve(), self.profile.resolve()])
        self.assertEqual(POSTBUILD.user_file_overlay_directories(resources, resources),
                         [self.public.resolve()])
        self.put(self.public, '[50000]public.bin', b'identified by reserved ID')
        with self.assertRaisesRegex(ValueError, 'CI1302 cannot package'):
            for directory in directories:
                POSTBUILD.reject_ci1302_ir_database('ci1302', self.base, directory)
        if not POWERSHELL:
            return
        driver = self.work / 'check-postbuild.ps1'
        driver.write_text(r'''
param([string]$Postbuild, [string]$Resources, [string]$Profile, [string]$BaseFile)
$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($Postbuild, [ref]$tokens, [ref]$errors)
if ($errors) { throw $errors[0] }
$names = @('Get-UserFileOverlayDirectories', 'Find-UserFileOverlay', 'Test-UserFileContainsEntry', 'Assert-Ci1302IrDatabaseLayout')
foreach ($function in $ast.FindAll({param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst]}, $false)) {
    if ($names -contains $function.Name) { Invoke-Expression $function.Extent.Text }
}
$directories = @(Get-UserFileOverlayDirectories -ResourcesBase $Resources -ProfileResources $Profile)
ConvertTo-Json -InputObject $directories -Compress
foreach ($directory in $directories) {
    Assert-Ci1302IrDatabaseLayout -SelectedChip 'ci1302' -BaseUserFile $BaseFile -EntriesDirectory $directory
}
''', encoding='utf-8')
        result = subprocess.run([POWERSHELL, '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                 '-File', str(driver), '-Postbuild', str(TOOLS / 'postbuild.ps1'),
                                 '-Resources', str(resources), '-Profile', str(self.profile.parent),
                                 '-BaseFile', str(self.base)], capture_output=True, text=True,
                                encoding='utf-8', errors='replace', timeout=30)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('CI1302 cannot package', result.stderr)
        self.assertEqual([Path(value) for value in json.loads(result.stdout.strip())], directories)


if __name__ == '__main__':
    print('PowerShell parity: ' + (POWERSHELL or 'unavailable; Python checks only'))
    unittest.main()
