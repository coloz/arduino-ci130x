#!/usr/bin/env python3
"""
Prepare resources for CI13XX Arduino build.
Port of prepare_resources.ps1
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
from pathlib import Path

REQUIRED_FILES = ['asr.bin', 'dnn.bin', 'voice.bin', 'user_file.bin']

LEGACY_STANDARD_METADATA = {
    'asr.bin': {'size': 17323, 'sha256': '39D9EFF49C52C5ED15E4B98BE309A358D4F92D98AB33431A3FF2B3D045392951'},
    'dnn.bin': {'size': 278236, 'sha256': 'EF94C3CF7AAC6744641CB6607E42598E027C4C37C392550C73DF606EB73D6AE1'},
    'voice.bin': {'size': 515597, 'sha256': '549ECBA3A4CE30B876F00C270AA68356D9F6071021C6022544EEF82C37DB79B7'},
    'user_file.bin': {'size': 558615, 'sha256': 'C3E48B7415ACA245714F88B73F26153D8ADF95EBE9B987D8EF909B51FB1B9E81'},
}


def sha256_file(file_path: Path) -> str:
    h = hashlib.sha256()
    with open(file_path, 'rb') as f:
        for chunk in iter(lambda: f.read(8192), b''):
            h.update(chunk)
    return h.hexdigest().upper()


def read_manifest(path: Path):
    with open(path, 'r', encoding='utf-8') as f:
        manifest = json.load(f)
    if manifest.get('schemaVersion') != 1 or not manifest.get('resources'):
        raise ValueError(f"Unsupported resource manifest: {path}")
    return manifest


def get_metadata(manifest) -> dict:
    metadata = {}
    resources = manifest['resources']
    for name in REQUIRED_FILES:
        if name not in resources:
            raise ValueError(f"Manifest missing metadata for {name}")
        res = resources[name]
        metadata[name] = {
            'size': int(res['size']),
            'sha256': str(res['sha256']).upper()
        }
    return metadata


def test_resource_set(root: Path, metadata: dict) -> bool:
    for name in REQUIRED_FILES:
        path = root / name
        if not path.is_file():
            return False
        stat = path.stat()
        if stat.st_size != metadata[name]['size']:
            return False
        if sha256_file(path) != metadata[name]['sha256']:
            return False
    return True


def main():
    parser = argparse.ArgumentParser(description='Prepare CI13XX build resources')
    parser.add_argument('--project-path', required=True, help='Sketch project path')
    parser.add_argument('--package-resources', required=True, help='Package resources directory')
    parser.add_argument(
        '--algorithm',
        choices=['aec', 'null', 'cwsl_aec', 'cwsl'],
        required=True,
        help='Algorithm profile',
    )
    args = parser.parse_args()

    project_root = Path(args.project_path).resolve()
    package_root = Path(args.package_resources).resolve()

    uses_cwsl_resources = args.algorithm in ('cwsl_aec', 'cwsl')
    profile_subdir = 'cwsl' if uses_cwsl_resources else ''
    package_profile_resources = package_root / profile_subdir if profile_subdir else package_root
    project_resources_base = project_root / 'recursos'
    project_resources = project_resources_base / profile_subdir if profile_subdir else project_resources_base
    expected_profile = 'cwsl' if uses_cwsl_resources else 'standard'

    # Read package manifest
    package_manifest_path = package_profile_resources / 'manifest.json'
    package_manifest = read_manifest(package_manifest_path)
    if package_manifest.get('profile') != expected_profile:
        raise ValueError(f"Package resource manifest profile '{package_manifest.get('profile')}' does not match {expected_profile}")

    package_metadata = get_metadata(package_manifest)

    # Verify package files exist
    for name in REQUIRED_FILES:
        source = package_profile_resources / name
        if not source.is_file():
            raise ValueError(f"Package is missing the {args.algorithm} profile resource: {source}")

    if not test_resource_set(package_profile_resources, package_metadata):
        raise ValueError(f"Package {args.algorithm} resources do not match their manifest")

    # Create project resources directory
    project_resources.mkdir(parents=True, exist_ok=True)

    managed_manifest_name = '.chipintelli-package-resources.json'
    project_managed_manifest_path = project_resources / managed_manifest_name

    # Check if already up-to-date
    if test_resource_set(project_resources, package_metadata):
        print(f"CI13XX {args.algorithm} profile resources already match the package manifest.")
        if project_managed_manifest_path.is_file():
            package_hash = sha256_file(package_manifest_path)
            managed_hash = sha256_file(project_managed_manifest_path)
            if package_hash != managed_hash:
                shutil.copy2(package_manifest_path, project_managed_manifest_path)
                print(f"CI13XX package-managed resource manifest updated: {project_managed_manifest_path}")
        return

    replace_managed_set = False
    replacement_reason = None

    if project_managed_manifest_path.is_file():
        try:
            managed_manifest = read_manifest(project_managed_manifest_path)
            managed_metadata = get_metadata(managed_manifest)
            if test_resource_set(project_resources, managed_metadata):
                replace_managed_set = True
                replacement_reason = 'managed package resource set'
            else:
                print(f"WARNING: Sketch resources no longer match {project_managed_manifest_path}; preserving them as user-owned files.", file=sys.stderr)
        except Exception as e:
            print(f"WARNING: {e}. Preserving sketch resources as user-owned files.", file=sys.stderr)
    elif not uses_cwsl_resources and test_resource_set(project_resources, LEGACY_STANDARD_METADATA):
        replace_managed_set = True
        replacement_reason = 'legacy v1.0.3 Standard resource set'

    if replace_managed_set:
        for name in REQUIRED_FILES:
            source = package_profile_resources / name
            destination = project_resources / name
            shutil.copy2(source, destination)
            print(f"CI13XX {args.algorithm} profile resource upgraded ({replacement_reason}): {destination}")
        shutil.copy2(package_manifest_path, project_managed_manifest_path)
        return

    # Copy files, preserving user-owned files
    for name in REQUIRED_FILES:
        source = package_profile_resources / name
        destination = project_resources / name
        if destination.is_file():
            print(f"CI13XX user-owned resource kept: {destination}")
            continue
        if destination.exists():
            raise ValueError(f"CI13XX resource path exists but is not a file: {destination}")
        shutil.copy2(source, destination)
        print(f"CI13XX {args.algorithm} profile resource copied: {destination}")

    if test_resource_set(project_resources, package_metadata):
        shutil.copy2(package_manifest_path, project_managed_manifest_path)
        print(f"CI13XX package-managed resource manifest copied: {project_managed_manifest_path}")
    else:
        print("WARNING: Sketch resources are a custom or mixed set; compatibility is the sketch owner's responsibility.", file=sys.stderr)


if __name__ == '__main__':
    main()
