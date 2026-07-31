#!/usr/bin/env python3
"""
Compile CI13XX SDK sources.
Port of compile_sdk_sources.ps1
"""

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HEADER_SIZE = 2
ENTRY_SIZE = 10
ALIGNMENT = 16


def convert_to_response_argument(value: str) -> str:
    """Convert value to GCC response file format."""
    # For defines with embedded quotes, we need to escape properly
    if '"' in value:
        # Already has quotes, just wrap
        return '"' + value.replace('"', '\\"') + '"'
    return f'"{value}"'


def get_source_fingerprint(files: list, arguments: list) -> str:
    """Compute SHA256 fingerprint of source files and arguments."""
    lines = []
    for arg in arguments:
        lines.append(f"arg:{arg}")
    for file_path in sorted(files):
        file_path = Path(file_path)
        stat = file_path.stat()
        lines.append(f"file:{file_path}:{stat.st_size}:{stat.st_mtime_ns}")

    data = '\n'.join(lines).encode('utf-8')
    return hashlib.sha256(data).hexdigest().upper()


def start_compiler_batch(compiler_path: str, working_directory: Path, response_file: Path) -> subprocess.Popen:
    """Start a compiler batch process."""
    args = [compiler_path, f'@{response_file}']
    return subprocess.Popen(
        args,
        cwd=working_directory,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True
    )


def main():
    parser = argparse.ArgumentParser(description='Compile CI13XX SDK sources')
    parser.add_argument('--platform-path', required=True, help='Platform path')
    parser.add_argument('--build-path', required=True, help='Build path')
    parser.add_argument('--compiler', required=True, help='Compiler path')
    parser.add_argument('--variant-path', required=True, help='Variant path')
    parser.add_argument('--build-extra-flags', default='', help='Extra build flags')
    args = parser.parse_args()

    platform_root = Path(args.platform_path).resolve()
    build_root = Path(args.build_path).resolve()
    variant_root = Path(args.variant_path).resolve()
    compiler_candidate = args.compiler

    # Find compiler (handle .exe suffix on Windows)
    compiler_path = Path(compiler_candidate)
    if not compiler_path.is_file() and sys.platform == 'win32':
        exe_path = Path(compiler_candidate + '.exe')
        if exe_path.is_file():
            compiler_path = exe_path

    if not compiler_path.is_file():
        raise ValueError(f"Compiler not found: {compiler_path}")

    sdk_root = platform_root / 'tools' / 'sdk'
    source_root = sdk_root / 'src'
    include_root = sdk_root / 'include'
    manifest = sdk_root / 'source_file.prj'
    object_root = build_root / 'ci13xx_sdk_objects'
    link_response = build_root / 'ci13xx_sdk_objects.rsp'
    stamp_file = object_root / 'build.fingerprint'

    for required_file in [compiler_path, manifest]:
        if not required_file.is_file():
            raise ValueError(f"Required SDK source-build file is missing: {required_file}")

    for required_dir in [source_root, include_root, variant_root]:
        if not required_dir.is_dir():
            raise ValueError(f"Required SDK source-build directory is missing: {required_dir}")

    # Parse source_file.prj
    source_relative_paths = []
    include_relative_paths = []

    with open(manifest, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line.startswith('source-file:'):
                source_relative_paths.append(line[len('source-file:'):].strip())
            elif line.startswith('include-path:'):
                include_relative_paths.append(line[len('include-path:'):].strip())

    if len(source_relative_paths) != 138:
        raise ValueError(f"Expected 138 compiled SDK sources in {manifest}, found {len(source_relative_paths)}")
    if len(include_relative_paths) != 88:
        raise ValueError(f"Expected 88 SDK include entries in {manifest}, found {len(include_relative_paths)}")

    # Resolve all source file paths
    sources = []
    for rel_path in source_relative_paths:
        path = source_root / rel_path
        if not path.is_file():
            raise ValueError(f"Packaged SDK source is missing: {path}")
        sources.append(str(path.resolve()))

    # Check for duplicate basenames
    basenames = {}
    for s in sources:
        name = Path(s).stem
        if name in basenames:
            raise ValueError(f"SDK source basenames must be unique: {name}")
        basenames[name] = True

    # Build common arguments
    common_args = [
        '-march=rv32imafc',
        '-mabi=ilp32f',
        '-mcmodel=medlow',
        '-msmall-data-limit=8',
        '-msave-restore',
        '-mfdiv',
        '-Os',
        '-fsigned-char',
        '-ffunction-sections',
        '-fdata-sections',
        '-fno-common',
        '-fno-delete-null-pointer-checks',
        '-fno-unroll-loops',
        '-fshort-enums',
        '-w',
        '-g',
        '-MMD',
        '-MP',
        '-DASR_CODE_VERSION=2',
        '-DCORE_ID=0',
        '-DCI_ARDUINO_CORE=1',
        '-DCI_CONFIG_FILE="user_config.h"',
    ]

    if args.build_extra_flags.strip():
        common_args.extend(args.build_extra_flags.strip().split())

    preserved_include_root = include_root / 'sdk'
    common_args.extend(['-I', str(variant_root)])
    common_args.extend(['-I', str(preserved_include_root)])

    for rel_path in include_relative_paths:
        preserved_path = preserved_include_root / rel_path
        common_args.extend(['-I', str(preserved_path)])

    # Collect all files for fingerprint
    fingerprint_files = list(sources)

    for h_file in include_root.rglob('*.h'):
        fingerprint_files.append(str(h_file))
    for src_file in source_root.rglob('*'):
        if src_file.is_file():
            fingerprint_files.append(str(src_file))
    for var_file in variant_root.glob('*'):
        if var_file.is_file():
            fingerprint_files.append(str(var_file))

    fingerprint = get_source_fingerprint(sorted(set(fingerprint_files)), common_args)

    # Check cache validity
    expected_object_names = [Path(s).stem + '.o' for s in sources]
    cache_valid = False

    if stamp_file.is_file():
        with open(stamp_file, 'r', encoding='utf-8') as f:
            stored_fingerprint = f.read().strip()
        if stored_fingerprint == fingerprint:
            # Verify all object files exist
            cache_valid = True
            for obj_name in expected_object_names:
                if not (object_root / obj_name).is_file():
                    cache_valid = False
                    break

    if not cache_valid:
        # Clean and rebuild
        full_object_root = object_root.resolve()
        build_prefix = str(build_root.resolve()) + os.sep

        if not str(full_object_root).startswith(build_prefix):
            raise ValueError(f"Refusing to clean SDK object directory outside Arduino build path: {full_object_root}")

        if full_object_root.is_dir():
            import shutil
            shutil.rmtree(full_object_root)
        full_object_root.mkdir(parents=True, exist_ok=True)

        # Separate C and assembly sources
        c_sources = [s for s in sources if s.lower().endswith('.c')]
        assembly_sources = [s for s in sources if s.lower().endswith('.s')]

        if len(c_sources) != 136 or len(assembly_sources) != 2:
            raise ValueError(f"Expected 136 C and 2 assembly SDK sources; found {len(c_sources)} C and {len(assembly_sources)} assembly")

        # Create batches
        import multiprocessing
        batch_count = min(max(1, multiprocessing.cpu_count()), 12)
        batches = []

        for i in range(batch_count):
            batch = c_sources[i::batch_count]
            if batch:
                batches.append((batch, False))
        batches.append((assembly_sources, True))

        print(f"Compiling {len(sources)} CI13XX SDK sources in {len(batches)} parallel batches ...")

        processes = []
        response_files = []

        for batch_index, (batch, is_assembly) in enumerate(batches):
            batch_args = list(common_args)
            if is_assembly:
                batch_args.extend(['-x', 'assembler-with-cpp'])
            else:
                batch_args.append('-std=gnu11')
            batch_args.append('-c')
            batch_args.extend(batch)

            response_file = object_root / f"compile-{batch_index:02d}.rsp"
            response_arguments = [convert_to_response_argument(a) for a in batch_args]

            with open(response_file, 'w', encoding='utf-8') as f:
                f.write('\n'.join(response_arguments))

            response_files.append(response_file)
            proc = start_compiler_batch(str(compiler_path), object_root, response_file)
            processes.append(proc)

        # Wait for all processes and check results
        failed = False
        for proc, rsp_file in zip(processes, response_files):
            stdout, _ = proc.communicate()
            if stdout:
                print(stdout, end='')
            if proc.returncode != 0:
                print(f"SDK compiler batch failed ({rsp_file}) with exit code {proc.returncode}", file=sys.stderr)
                failed = True

        if failed:
            raise ValueError('One or more CI13XX SDK source compiler batches failed.')

        # Verify all object files were created
        actual_objects = list(object_root.glob('*.o'))
        if len(actual_objects) != 138:
            raise ValueError(f"Expected 138 compiled SDK objects, found {len(actual_objects)} in {object_root}")

        # Write fingerprint stamp
        with open(stamp_file, 'w', encoding='utf-8') as f:
            f.write(fingerprint)
    else:
        print('Using cached CI13XX SDK source objects.')

    # Write link response file
    object_paths = [str(object_root / name) for name in expected_object_names]
    link_arguments = [convert_to_response_argument(p) for p in object_paths]

    with open(link_response, 'w', encoding='utf-8') as f:
        f.write('\n'.join(link_arguments))

    print(f"CI13XX SDK source objects ready: {len(object_paths)}")


if __name__ == '__main__':
    main()
