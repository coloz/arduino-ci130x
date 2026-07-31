#!/usr/bin/env python3
"""
Merge user_file entries.
Port of merge_user_file_entries.ps1
"""

import argparse
import os
import re
import struct
import shutil
from pathlib import Path

HEADER_SIZE = 2
ENTRY_SIZE = 10
ALIGNMENT = 16


def get_aligned_offset(value: int) -> int:
    return ((value + ALIGNMENT - 1) // ALIGNMENT) * ALIGNMENT


def get_user_file_entries(buffer: bytes) -> list:
    if len(buffer) < HEADER_SIZE:
        raise ValueError('Base user_file.bin is too small.')

    count = struct.unpack_from('<H', buffer, 0)[0]
    table_end = HEADER_SIZE + count * ENTRY_SIZE

    if table_end > len(buffer):
        raise ValueError('Base user_file.bin has a truncated entry table.')

    entries = []
    seen_ids = set()

    for index in range(count):
        entry_offset = HEADER_SIZE + index * ENTRY_SIZE
        entry_id, data_offset, data_size = struct.unpack_from('<HII', buffer, entry_offset)

        id_key = str(entry_id)
        if id_key in seen_ids:
            raise ValueError(f"Base user_file.bin contains duplicate ID {entry_id}.")
        seen_ids.add(id_key)

        data_end = data_offset + data_size
        if data_offset < table_end or data_end > len(buffer):
            raise ValueError(f"Base user_file.bin entry {entry_id} points outside the container.")

        data = buffer[data_offset:data_offset + data_size] if data_size > 0 else b''
        entries.append({
            'id': entry_id,
            'data': data,
            'source': 'base user_file.bin'
        })

    return entries


def main():
    parser = argparse.ArgumentParser(description='Merge CI13XX user_file entries')
    parser.add_argument('--base-user-file', required=True, help='Base user_file.bin path')
    parser.add_argument('--entries-directory', required=True, help='Entries directory')
    parser.add_argument('--output', required=True, help='Output file path')
    args = parser.parse_args()

    base_path = Path(args.base_user_file).resolve()
    entries_path = Path(args.entries_directory).resolve()
    output_path = Path(args.output).resolve()

    if not base_path.is_file():
        raise ValueError(f"Base user-file path is not a file: {base_path}")
    if not entries_path.is_dir():
        raise ValueError(f"User-file entries path is not a directory: {entries_path}")

    if str(base_path).lower() == str(output_path).lower():
        raise ValueError('Output must not overwrite the base user_file.bin.')

    # Read base file and parse entries
    base_bytes = base_path.read_bytes()
    entries = get_user_file_entries(base_bytes)

    # Create index by ID
    base_index_by_id = {str(e['id']): i for i, e in enumerate(entries)}

    # Read overlay entries
    overlay_ids = {}
    overlays = []

    entry_files = sorted(entries_path.glob('*.bin'))
    for file in entry_files:
        match = re.match(r'^\[(?P<id>[0-9]+)\].*\.bin$', file.name)
        if not match:
            raise ValueError(f"User-file entry name must start with a numeric [id]: {file.name}")

        parsed_id = int(match.group('id'))
        if parsed_id > 65535:
            raise ValueError(f"User-file entry ID must be between 0 and 65535: {file.name}")

        id_key = str(parsed_id)
        if id_key in overlay_ids:
            raise ValueError(f"Duplicate user-file overlay ID {parsed_id} in '{overlay_ids[id_key]}' and '{file.name}'.")

        data = file.read_bytes()
        if len(data) == 0:
            raise ValueError(f"User-file overlay entry is empty: {file.name}")

        overlays.append({
            'id': parsed_id,
            'data': data,
            'source': str(file)
        })
        overlay_ids[id_key] = file.name

    # Apply overlays
    replaced = 0
    added = 0

    for overlay in sorted(overlays, key=lambda x: (x['id'], x['source'])):
        id_key = str(overlay['id'])
        if id_key in base_index_by_id:
            entries[base_index_by_id[id_key]] = overlay
            replaced += 1
        else:
            if len(entries) >= 65535:
                raise ValueError('The merged user-file entry count exceeds 65535.')
            base_index_by_id[id_key] = len(entries)
            entries.append(overlay)
            added += 1

    # Sort by ID (canonical layout)
    entries = sorted(entries, key=lambda x: x['id'])

    # Compute layout
    table_end = HEADER_SIZE + len(entries) * ENTRY_SIZE
    next_data_offset = get_aligned_offset(table_end)
    layout = []

    for entry in entries:
        data_end = next_data_offset + len(entry['data'])
        if next_data_offset > 0xFFFFFFFF or data_end > 0xFFFFFFFF:
            raise ValueError(f"Merged user-file entry {entry['id']} exceeds the 32-bit container format.")

        layout.append({
            'id': entry['id'],
            'data': entry['data'],
            'offset': next_data_offset
        })
        next_data_offset = get_aligned_offset(data_end)

    # Compute output size
    if not layout:
        output_size = next_data_offset
    else:
        last_entry = layout[-1]
        output_size = last_entry['offset'] + len(last_entry['data'])

    if output_size > 0x7FFFFFFF:
        raise ValueError('Merged user_file.bin is too large to materialize.')

    # Build output bytes
    output_bytes = bytearray([0xFF] * output_size)

    # Write header
    struct.pack_into('<H', output_bytes, 0, len(layout))

    # Write entries
    for index, entry in enumerate(layout):
        entry_offset = HEADER_SIZE + index * ENTRY_SIZE
        struct.pack_into('<HII', output_bytes, entry_offset,
                         entry['id'], entry['offset'], len(entry['data']))
        if entry['data']:
            output_bytes[entry['offset']:entry['offset'] + len(entry['data'])] = entry['data']

    # Write output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(output_bytes)

    print(f"CI13XX user-file entries: base={len(entries) - added}, replaced={replaced}, added={added}, output={output_path}")


if __name__ == '__main__':
    main()
