#!/usr/bin/env python3
"""Prepare the explicitly enabled, optional IR database before compilation.

This reads one unconditional literal declaration in the main sketch, not C
preprocessor expressions or included headers. Keep the PowerShell port aligned.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

MACRO = 'CHIPINTELLI_IR_DATABASE'
DATABASE_NAME = '[50000]ir_data_2024_08_16.bin'
DATABASE_SIZE = 70716
DATABASE_SHA256 = 'F7E3680B45F9ABE56C6D3E16D1BBFFD7336E286D0B0DABED20E1D2B4854B97D0'
MANIFEST_NAME = '.chipintelli-ir-database.json'
METADATA = dict(schemaVersion=1, name=DATABASE_NAME, size=DATABASE_SIZE,
                sha256=DATABASE_SHA256)
GUIDANCE = ('Declare #define CHIPINTELLI_IR_DATABASE 0 or 1 exactly once, '
            'unconditionally at the top level of the main sketch; '
            'conditional definitions, aliases and #undef are not supported.')


def mask_comments_and_literals(text):
    # C translation phase 2 precedes comments, including continued // comments.
    text = re.sub(r'\\\r?\n', '', text)
    pattern = re.compile(
        r'//[^\n]*|/\*[\s\S]*?\*/|'
        r'R"(?P<delimiter>[^\s()\\]{0,16})\([\s\S]*?\)(?P=delimiter)"|'
        r'"(?:\\[\s\S]|[^"\\\r\n])*"|\'(?:\\[\s\S]|[^\'\\\r\n])*\'')
    def mask(match):
        token = match.group()
        # Literals must remain non-whitespace tokens: `1 "extra"` is not the
        # supported literal 1 declaration even though its quoted text is opaque.
        marker = ' ' if token.startswith(('//', '/*')) else '@'
        return re.sub(r'[^\r\n]', marker, token)
    return pattern.sub(mask, text)


def database_enabled(source):
    text = mask_comments_and_literals(Path(source).read_text(encoding='utf-8-sig'))
    depth = 0
    value = None
    for line in text.splitlines():
        directive = re.match(r'^\s*#\s*([A-Za-z_][A-Za-z_0-9]*)(.*)$', line)
        if not directive:
            continue
        keyword, rest = directive.groups()
        if keyword in ('if', 'ifdef', 'ifndef'):
            depth += 1
        elif keyword == 'endif':
            depth = max(0, depth - 1)
        elif keyword in ('define', 'undef') and re.match(r'\s+' + MACRO + r'\b', rest):
            declaration = re.fullmatch(r'\s+' + MACRO + r'\s+([01])\s*', rest)
            if keyword != 'define' or depth or value is not None or not declaration:
                raise ValueError(GUIDANCE)
            value = declaration.group(1) == '1'
    return value is True


def resolve_source(value):
    source = Path(value)
    candidates = [source]
    if not source.suffix:
        candidates.extend([source.with_suffix('.ino'), source.with_suffix('.cpp')])
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise ValueError('Arduino source file not found. Tried: ' + ', '.join(map(str, candidates)))


def sha256_file(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def valid_database(path):
    return path.is_file() and path.stat().st_size == DATABASE_SIZE and sha256_file(path) == DATABASE_SHA256


def assert_contained_path(path, root):
    # Fixed destinations must not escape through a symlink/junction. Never use
    # manifest-provided paths to decide which file can be overwritten/deleted.
    path.relative_to(root)
    current = path
    while current != root:
        attributes = getattr(current.lstat(), 'st_file_attributes', 0) if current.exists() else 0
        if current.is_symlink() or attributes & 0x400:  # FILE_ATTRIBUTE_REPARSE_POINT
            raise ValueError(f'IR resource path must not be a link: {current}')
        current = current.parent
    try:
        path.resolve().relative_to(root.resolve())
    except ValueError:
        raise ValueError(f'IR resource path escapes its project: {path}')


def destination_roots(source):
    sketch = source.parent
    roots = [(sketch, sketch / 'recursos' / 'user_file_entries')]
    if sketch.name == 'sketch' and sketch.parent.name == '.temp':
        project = sketch.parent.parent
        package_path = project / 'package.json'
        if package_path.is_file() and (project / 'project.abi').is_file():
            try:
                package = json.loads(package_path.read_text(encoding='utf-8-sig'))
            except (ValueError, OSError):
                package = None
            if isinstance(package, dict) and isinstance(package.get('name'), str) and package['name'].strip():
                roots.insert(0, (project, project / 'src' / 'recursos' / 'user_file_entries'))
    return roots


def read_managed_manifest(path):
    if not path.exists():
        return False
    if not path.is_file():
        raise ValueError(f'IR managed manifest is not a file: {path}')
    try:
        metadata = json.loads(path.read_text(encoding='utf-8-sig'))
    except (ValueError, OSError) as error:
        raise ValueError(f'Invalid IR managed manifest; preserving files: {path}') from error
    if metadata != METADATA:
        raise ValueError(f'Unexpected IR managed manifest; preserving files: {path}')
    return True


def publish_database(database, target, manifest):
    temporary_paths = []
    try:
        with tempfile.NamedTemporaryFile(prefix='.chipintelli-ir-', suffix='.tmp',
                                         dir=str(target.parent), delete=False) as output:
            temporary = Path(output.name)
            temporary_paths.append(temporary)
            with database.open('rb') as input_file:
                shutil.copyfileobj(input_file, output)
            output.flush()
            os.fsync(output.fileno())
        if not valid_database(temporary):
            raise ValueError(f'Official IR database changed while copying: {database}')
        if not manifest.exists():
            with tempfile.NamedTemporaryFile(prefix='.chipintelli-ir-', suffix='.tmp',
                                             dir=str(target.parent), delete=False) as output:
                receipt = Path(output.name)
                temporary_paths.append(receipt)
                output.write((json.dumps(METADATA, indent=2) + '\n').encode('utf-8'))
                output.flush()
                os.fsync(output.fileno())
            os.replace(receipt, manifest)
        # Publish the receipt first: an interruption can leave a managed missing
        # file, which the next build repairs, never a partial/untracked database.
        if target.exists():
            raise ValueError(f'IR destination appeared during preparation; preserving it: {target}')
        os.replace(temporary, target)
    finally:
        for temporary in temporary_paths:
            if temporary.exists():
                temporary.unlink()


def prepare(source, platform_path, chip):
    source = resolve_source(source)
    enabled = database_enabled(source)
    if enabled and chip.lower() == 'ci1302':
        raise ValueError('CHIPINTELLI_IR_DATABASE=1 requires CI1303/CI1306 (4 MB); '
                         'the CI1302 standard resource layout has insufficient capacity. '
                         'Remove the macro or use 0 for Raw/NEC.')
    database = Path(platform_path) / 'libraries' / 'ChipIntelliIR' / 'examples' / 'AirConditioner' / 'recursos' / 'user_file_entries' / DATABASE_NAME
    if enabled and not valid_database(database):
        raise ValueError(f'Official IR database is missing or fails the 70716-byte/SHA-256 check: {database}')

    plans = []
    for root, entries in destination_roots(source):
        target = entries / DATABASE_NAME
        manifest = entries / MANIFEST_NAME
        if not enabled and not manifest.exists():
            continue
        assert_contained_path(entries, root)
        assert_contained_path(target, root)
        assert_contained_path(manifest, root)
        if entries.exists() and not entries.is_dir():
            raise ValueError(f'IR entries path is not a directory: {entries}')
        managed = read_managed_manifest(manifest)
        matches = []
        if enabled and entries.is_dir():
            for entry in entries.iterdir():
                match = re.match(r'^\[(\d+)\]', entry.name)
                if match and int(match.group(1)) == 50000:
                    assert_contained_path(entry, root)
                    if entry.suffix.lower() != '.bin' or not valid_database(entry):
                        raise ValueError(f'IR resource ID 50000 conflicts with the official database; preserving user file: {entry}')
                    matches.append(entry)
            if len(matches) > 1:
                raise ValueError(f'Duplicate IR resource ID 50000 in {entries}; keep one official .bin file.')
        plans.append((target, manifest, managed, bool(matches)))

    # Validate both aily destinations before changing either one.
    for target, manifest, managed, has_database in plans:
        if enabled:
            if has_database:
                print(f'CI13XX IR database already present: {target.parent}')
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            publish_database(database, target, manifest)
            print(f'CI13XX optional IR database prepared: {target}')
        elif managed:
            if valid_database(target):
                target.unlink()
                print(f'CI13XX disabled managed IR database removed: {target}')
            elif target.exists():
                print(f'CI13XX modified IR database preserved as user-owned: {target}')
            manifest.unlink()
    return enabled


def main():
    parser = argparse.ArgumentParser(description='Prepare an explicitly enabled CI13XX IR database')
    parser.add_argument('--source', required=True, help='Main sketch source file, not a directory')
    parser.add_argument('--platform-path', required=True)
    parser.add_argument('--chip', required=True)
    args = parser.parse_args()
    try:
        prepare(args.source, args.platform_path, args.chip)
    except (ValueError, OSError) as error:
        print(f'CI13XX IR database preparation failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
