#!/usr/bin/env python3
"""
Post-build processing for CI13XX.
Port of postbuild.ps1
"""

import argparse
import os
import struct
import subprocess
import sys
import shutil
import tempfile
from pathlib import Path


def run_command(args, description="Command"):
    """Run a command and check for errors."""
    print(f"Running: {' '.join(args)}")
    result = subprocess.run(args, capture_output=True, text=True)
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    if result.returncode != 0:
        raise RuntimeError(f"{description} failed with exit code {result.returncode}")
    return result


def align_up(value, alignment):
    """Round value up to the next alignment boundary."""
    return (value + alignment - 1) // alignment * alignment


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
    parser.add_argument('--output', required=True, help='User code output path')
    parser.add_argument('--firmware-output', required=True, help='Complete firmware output path')
    parser.add_argument('--objcopy', required=True, help='objcopy path')
    parser.add_argument('--platform-path', required=True, help='Platform path')
    parser.add_argument('--citool-cli', required=True, help='citool-cli path')
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

    # Handle user_file_entries
    effective_user_file = resource_files['UserFile']
    user_file_entries = project_resources_root / 'user_file_entries'

    if user_file_entries.exists():
        if not user_file_entries.is_dir():
            raise ValueError(f"Project user_file_entries path is not a directory: {user_file_entries}")
        effective_user_file = staging_root / 'user_file.bin'
        run_command([
            sys.executable, str(merge_user_file_entries),
            '--base-user-file', str(resource_files['UserFile']),
            '--entries-directory', str(user_file_entries),
            '--output', str(effective_user_file)
        ], "merge_user_file_entries")

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
