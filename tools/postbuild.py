#!/usr/bin/env python3
"""
Post-build processing for CI13XX.
Port of postbuild.ps1
"""

import argparse
import os
import re
import struct
import subprocess
import sys
import shutil
import tempfile
from pathlib import Path

IR_DATABASE_USER_FILE_ID = 50000
USER_FILE_HEADER_SIZE = 2
USER_FILE_ENTRY_SIZE = 10


def run_command(args, description="Command"):
    """Run a command and check for errors."""
    print(f"Running: {' '.join(args)}", flush=True)
    # Inherit the post-build streams so long-running tools (notably
    # citool-cli generate) can report progress to Aily Builder in real time.
    result = subprocess.run(args)
    if result.returncode != 0:
        raise RuntimeError(f"{description} failed with exit code {result.returncode}")
    return result


def align_up(value, alignment):
    """Round value up to the next alignment boundary."""
    return (value + alignment - 1) // alignment * alignment


def resolve_arduino_source(value):
    """Resolve Arduino's primary source with or without the .ino suffix."""
    source = Path(value)
    candidates = [source]
    if not source.suffix:
        candidates.append(source.with_suffix('.ino'))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    tried = ', '.join(str(candidate) for candidate in candidates)
    raise ValueError(f"Arduino source file not found. Tried: {tried}")


def arduino_asset_root(source_path):
    """Return the Aily project root or the regular Arduino sketch directory."""
    source_directory = source_path.parent
    temporary_directory = source_directory.parent
    if (source_directory.name.lower() == 'sketch'
            and temporary_directory.name.lower() == '.temp'):
        return temporary_directory.parent
    return source_directory


def find_user_file_overlay(entries_directory, entry_id):
    """Return an overlay whose bracketed decimal ID equals entry_id."""
    if not entries_directory.is_dir():
        return None
    entry_pattern = re.compile(
        r'^\[(?P<id>[0-9]+)\].*\.bin$',
        re.IGNORECASE,
    )
    for path in entries_directory.iterdir():
        if not path.is_file():
            continue
        match = entry_pattern.match(path.name)
        if match and int(match.group('id'), 10) == entry_id:
            return path
    return None


def user_file_overlay_directories(resources_base, profile_resources):
    """Collect public and legacy profile overlays without visiting a path twice."""
    directories = []
    for root in (resources_base, profile_resources):
        directory = (root / 'user_file_entries').resolve()
        if directory in directories:
            continue
        if directory.exists() and not directory.is_dir():
            raise ValueError(f"Project user_file_entries path is not a directory: {directory}")
        directories.append(directory)
    return directories


def user_file_contains_entry(user_file_path, entry_id):
    """Inspect a valid-looking CI13XX user-file table for a numeric ID."""
    buffer = user_file_path.read_bytes()
    if len(buffer) < USER_FILE_HEADER_SIZE:
        return False

    count = struct.unpack_from('<H', buffer, 0)[0]
    table_end = USER_FILE_HEADER_SIZE + count * USER_FILE_ENTRY_SIZE
    if table_end > len(buffer):
        return False

    for index in range(count):
        entry_offset = USER_FILE_HEADER_SIZE + index * USER_FILE_ENTRY_SIZE
        current_id = struct.unpack_from('<H', buffer, entry_offset)[0]
        if current_id == entry_id:
            return True
    return False


def reject_ci1302_ir_database(chip, base_user_file, entries_directory):
    """Reject the vendor IR database before CI1302 firmware composition."""
    if chip != 'ci1302':
        return

    overlay = find_user_file_overlay(
        entries_directory,
        IR_DATABASE_USER_FILE_ID,
    )
    if (overlay is None
            and not user_file_contains_entry(
                base_user_file,
                IR_DATABASE_USER_FILE_ID,
            )):
        return

    raise ValueError(
        "CI1302 cannot package the reserved [50000] ChipIntelliIR "
        "V2.7.14 air-conditioner database because the standard CI1302 "
        "resource layout cannot accommodate it. Use CI1303 or CI1306 "
        "for air-conditioner database mode. CI1302 raw/NEC infrared "
        "mode remains available when user-file ID 50000 is absent."
    )


def variable_number_playback_is_linked(elf_path, objcopy_path):
    """Detect localized ChipIntelliAudio number playback in the final ELF."""
    nm_name = re.sub(
        r'objcopy(?P<suffix>\.exe)?$',
        r'nm\g<suffix>',
        objcopy_path.name,
        flags=re.IGNORECASE,
    )
    if nm_name == objcopy_path.name:
        raise ValueError(f"Cannot derive nm path from objcopy path: {objcopy_path}")
    nm_path = objcopy_path.with_name(nm_name)
    if not nm_path.is_file():
        raise ValueError(f"CI13XX toolchain nm executable not found next to objcopy: {nm_path}")

    result = subprocess.run(
        [str(nm_path), str(elf_path)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        detail = result.stderr.strip()
        if detail:
            detail = f": {detail}"
        raise RuntimeError(
            "nm failed while inspecting variable-number playback usage "
            f"(exit code {result.returncode}){detail}"
        )
    # playVoice(String) is an inline language-selector wrapper. The linked
    # implementation name remains visible in the raw C++ symbol regardless of
    # which language enum value the wrapper passed.
    return 'playLocalizedNumber' in result.stdout


def build_user_code_container(host_code, algorithm_code):
    """Build the two-image container emitted by ci-tool-kit merge user-file.

    The vendor format begins with a 16-bit little-endian image count followed
    by one packed 10-byte record per image: 16-bit file ID, payload offset and
    unpadded payload size.  The record table and payload gaps are padded with
    0xff to 16 bytes.  CI13XX uses file IDs 0 and 1 for the host and algorithm
    images respectively.
    """
    alignment = 16
    record_format = '<HII'
    images = (host_code, algorithm_code)
    header_size = align_up(
        struct.calcsize('<H') + len(images) * struct.calcsize(record_format),
        alignment,
    )

    records = []
    next_offset = header_size
    for file_id, image in enumerate(images):
        if len(image) > 0xffffffff:
            raise ValueError('CI13XX user-code image exceeds the container format limit.')
        records.append((file_id, next_offset, len(image)))
        next_offset = align_up(next_offset + len(image), alignment)

    header = bytearray(struct.pack('<H', len(images)))
    for record in records:
        header.extend(struct.pack(record_format, *record))
    header.extend(b'\xff' * (header_size - len(header)))

    container = header
    for image, (_, offset, _) in zip(images, records):
        container.extend(b'\xff' * (offset - len(container)))
        container.extend(image)
    return bytes(container)


def main():
    parser = argparse.ArgumentParser(description='CI13XX post-build packaging')
    parser.add_argument('--elf', required=True, help='ELF file path')
    parser.add_argument('--source', required=True, help='Primary Arduino source file path')
    parser.add_argument('--output', required=True, help='User code output path')
    parser.add_argument('--firmware-output', required=True, help='Complete firmware output path')
    parser.add_argument('--objcopy', required=True, help='objcopy path')
    parser.add_argument('--platform-path', required=True, help='Platform path')
    parser.add_argument('--citool-cli', required=True, help='citool-cli path')
    parser.add_argument('--service-url', required=True, help='ci-service base URL')
    parser.add_argument('--project-resources', required=True, help='Project resources path')
    parser.add_argument('--chip', choices=['ci1302', 'ci1303', 'ci1306'], required=True, help='Chip model')
    parser.add_argument(
        '--algorithm',
        choices=['aec', 'null', 'cwsl_aec', 'cwsl'],
        required=True,
        help='Algorithm profile',
    )
    parser.add_argument('--max-user-code-size', type=int, required=True, help='Maximum user code size')
    args = parser.parse_args()

    elf_path = Path(args.elf).resolve()
    source_path = resolve_arduino_source(args.source)
    asset_root = arduino_asset_root(source_path)
    firmware_output_path = Path(args.firmware_output).resolve()

    # Find objcopy
    objcopy_path = Path(args.objcopy)
    if not objcopy_path.is_file() and sys.platform == 'win32':
        exe_path = Path(args.objcopy + '.exe')
        if exe_path.is_file():
            objcopy_path = exe_path

    # Find citool-cli
    citool_path = Path(args.citool_cli)
    if not citool_path.is_file() and sys.platform == 'win32':
        exe_path = Path(args.citool_cli + '.exe')
        if exe_path.is_file():
            citool_path = exe_path

    if not citool_path.is_file():
        raise ValueError(f"citool-cli not found: {citool_path}")
    if not objcopy_path.is_file():
        raise ValueError(f"objcopy not found: {objcopy_path}")

    platform_root = Path(args.platform_path).resolve()
    tool_kit = platform_root / 'tools' / 'sdk' / 'bin' / 'ci-tool-kit.exe'
    second_core = platform_root / 'tools' / 'sdk' / 'bin' / f'libbnpu_core_alg_pro_{args.algorithm}.a'
    merge_user_file_entries = platform_root / 'tools' / 'merge_user_file_entries.py'
    project_resources_base = Path(args.project_resources).resolve()

    if args.algorithm in ('cwsl_aec', 'cwsl'):
        project_resources_root = project_resources_base / 'cwsl'
    else:
        project_resources_root = project_resources_base

    # Check required files
    for required in [second_core, merge_user_file_entries]:
        if not required.is_file():
            raise ValueError(f"Missing post-build packaging asset: {required}")

    # Check resource files
    resource_files = {
        'ASR': project_resources_root / 'asr.bin',
        'DNN': project_resources_root / 'dnn.bin',
        'Voice': project_resources_root / 'voice.bin',
        'UserFile': project_resources_root / 'user_file.bin',
    }

    for name, path in resource_files.items():
        if not path.is_file():
            raise ValueError(f"Missing project {args.algorithm} profile {name} resource: {path}")

    user_file_entry_directories = user_file_overlay_directories(
        project_resources_base,
        project_resources_root,
    )
    for entries_directory in user_file_entry_directories:
        reject_ci1302_ir_database(
            args.chip,
            resource_files['UserFile'],
            entries_directory,
        )

    output_full_path = Path(args.output).resolve()

    if str(output_full_path).lower() == str(firmware_output_path).lower():
        raise ValueError('User-code and complete-firmware output paths must be different.')

    # Create output directories
    output_full_path.parent.mkdir(parents=True, exist_ok=True)
    firmware_output_path.parent.mkdir(parents=True, exist_ok=True)

    # Create staging directory
    staging_root = output_full_path.parent / (output_full_path.stem + '.ci13xx')
    if staging_root.exists():
        shutil.rmtree(staging_root)
    staging = staging_root / 'user_code'
    staging.mkdir(parents=True)

    source_text = source_path.read_text(encoding='utf-8-sig')
    resource_macro_pattern = re.compile(
        r'^\s*#define\s+(?:VOICEMP3|VOICE|WAKEWORD|COMMAND)[0-9]+\s+',
        re.MULTILINE,
    )
    has_resource_macros = bool(resource_macro_pattern.search(source_text))
    uses_variable_number_voices = variable_number_playback_is_linked(
        elf_path,
        objcopy_path,
    )
    if has_resource_macros or uses_variable_number_voices:
        generated_resources = staging_root / 'generated_resources'
        generated_resources.mkdir()
        for resource_path in resource_files.values():
            shutil.copy2(resource_path, generated_resources / resource_path.name)

        generation_reasons = []
        if has_resource_macros:
            generation_reasons.append('source resource macros')
        if uses_variable_number_voices:
            generation_reasons.append('variable-number playback')
        print(
            f"CI13XX {' and '.join(generation_reasons)} found; "
            "generating resources through ci-service."
        )
        generate_command = [
            str(citool_path), 'generate',
            '--source', str(source_path),
            '--asset-root', str(asset_root),
            '--service-url', args.service_url,
            '--chip', args.chip,
            '--output', str(generated_resources),
        ]
        run_command(generate_command, "citool-cli generate")

        resource_files = {
            'ASR': generated_resources / 'asr.bin',
            'DNN': generated_resources / 'dnn.bin',
            'Voice': generated_resources / 'voice.bin',
            'UserFile': generated_resources / 'user_file.bin',
        }
        for name, resource_path in resource_files.items():
            if not resource_path.is_file():
                raise ValueError(
                    f"citool-cli generate did not create the expected {name} "
                    f"resource: {resource_path}"
                )
        for entries_directory in user_file_entry_directories:
            reject_ci1302_ir_database(
                args.chip,
                resource_files['UserFile'],
                entries_directory,
            )
    else:
        print('CI13XX generated resources are not required; using the sketch resource set.')

    # Handle user_file_entries
    effective_user_file = resource_files['UserFile']

    existing_entry_directories = [directory for directory in user_file_entry_directories
                                  if directory.is_dir()]
    if existing_entry_directories:
        effective_user_file = staging_root / 'user_file.bin'
        merge_arguments = [
            sys.executable, str(merge_user_file_entries),
            '--base-user-file', str(resource_files['UserFile']),
            '--entries-directory', str(existing_entry_directories[0]),
            '--output', str(effective_user_file)
        ]
        if len(existing_entry_directories) > 1:
            merge_arguments.extend([
                '--additional-entries-directory', str(existing_entry_directories[1]),
            ])
        run_command(merge_arguments, "merge_user_file_entries")

    # Host image - extract from ELF
    host_image = staging / '[0]code.bin'
    run_command([
        str(objcopy_path), '-O', 'binary',
        str(elf_path), str(host_image)
    ], "objcopy ELF to binary")

    host_image_size = host_image.stat().st_size
    print(f"CI13XX host image [0]code.bin: {host_image_size} bytes")

    # Algorithm image - copy the archive
    algorithm_image = staging / '[1]code.bin'
    shutil.copy2(second_core, algorithm_image)

    # Check if ci-tool-kit.exe is available (Windows only)
    if not tool_kit.is_file():
        raise ValueError(f"ci-tool-kit.exe not found: {tool_kit}")

    # Try to run ci-tool-kit - it's a Windows exe, so this may fail on non-Windows
    # We've already extracted libbnpu_core_alg_pro as a pre-merged archive
    # So we need to create the user_code.bin format

    # Try to run using wine if available and not on Windows
    merge_cmd = None
    if sys.platform == 'win32':
        merge_cmd = [str(tool_kit), 'merge', 'user-file', '-i', str(staging)]
    else:
        # Check for wine
        wine_path = shutil.which('wine')
        if wine_path:
            merge_cmd = [wine_path, str(tool_kit), 'merge', 'user-file', '-i', str(staging)]
        else:
            # Check for wine64
            wine64_path = shutil.which('wine64')
            if wine64_path:
                merge_cmd = [wine64_path, str(tool_kit), 'merge', 'user-file', '-i', str(staging)]

    if merge_cmd:
        try:
            run_command(merge_cmd, "ci-tool-kit merge user-file")
        except Exception as e:
            print(f"ci-tool-kit merge failed: {e}", file=sys.stderr)
            print("Attempting to use simplified merging...", file=sys.stderr)
            merge_cmd = None

    # If ci-tool-kit is not available (or failed), we need to manually create user_code.bin
    if not merge_cmd:
        print("Using manual user_code.bin construction")
        merged_image = staging / 'user_code.bin'
        code0 = host_image.read_bytes()
        code1 = algorithm_image.read_bytes()
        merged_image.write_bytes(build_user_code_container(code0, code1))
    else:
        # ci-tool-kit ran, check for output
        merged_image = staging / 'user_code.bin'

    if not merged_image.is_file():
        raise ValueError(f"Merge did not create the expected image: {merged_image}")

    expected_container = build_user_code_container(
        host_image.read_bytes(),
        algorithm_image.read_bytes(),
    )
    if merged_image.read_bytes() != expected_container:
        raise ValueError(
            'Merged user_code.bin does not match the CI13XX dual-core '
            'container format.'
        )

    merged_image_size = merged_image.stat().st_size
    if merged_image_size > args.max_user_code_size:
        raise ValueError(f"Merged user_code.bin is {merged_image_size} bytes, exceeding the vendor user-code/SRAM limit ({args.max_user_code_size} bytes).")

    # Copy to final output
    shutil.copy2(merged_image, output_full_path)
    print(f"CI13XX user-code image: {output_full_path} ({merged_image_size} / {args.max_user_code_size} bytes)")

    # Use citool-cli to compose the complete firmware
    run_command([
        str(citool_path), 'compose',
        '--chip', args.chip,
        '--user-code', str(output_full_path),
        '--user-code-capacity', str(args.max_user_code_size),
        '--asr', str(resource_files['ASR']),
        '--dnn', str(resource_files['DNN']),
        '--voice', str(resource_files['Voice']),
        '--user-file', str(effective_user_file),
        '--output', str(firmware_output_path),
        '--force'
    ], "citool-cli compose")

    # Inspect the firmware
    run_command([
        str(citool_path), 'inspect', str(firmware_output_path)
    ], "citool-cli inspect")

    print(f"CI13XX complete firmware: {firmware_output_path}")


if __name__ == '__main__':
    main()
