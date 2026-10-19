#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reject OELFs whose initialized/runtime sections would not be loaded on PS4."""
import struct
import sys
from pathlib import Path

path = Path(sys.argv[1])
blob = path.read_bytes()
if not __debug__:
    raise RuntimeError('Run validation without Python optimization flags')
header = struct.unpack_from('<16sHHIQQQIHHHHHH', blob)
assert header[0][:6] == b'\x7fELF\x02\x01', 'expected little-endian ELF64'
assert header[2] == 62, 'expected x86-64'
assert header[9] == 56 and header[11] == 64, 'unexpected table entry sizes'
programs = [struct.unpack_from('<IIQQQQQQ', blob, header[5] + i * header[9])
            for i in range(header[10])]
loads = [entry for entry in programs if entry[0] in (1, 0x61000010)]  # PT_LOAD, PT_SCE_RELRO
dynamic = next(entry for entry in programs if entry[0] == 2)
dynlib = next(entry for entry in programs if entry[0] == 0x61000000)
assert dynlib[2] <= dynamic[2] and dynamic[2] + dynamic[5] <= dynlib[2] + dynlib[5], \
    'dynamic table is outside PS4 loader metadata'
assert sum(entry[1] == 6 for entry in loads) == 1, 'expected one RW PT_LOAD'
for entry in loads:
    assert entry[5] <= entry[6], 'file image exceeds memory segment'
    assert entry[2] + entry[5] <= len(blob), 'truncated segment'
    assert entry[7] >= 0x4000, 'PS4 load segment alignment is below 16 KiB'
    assert entry[2] % 0x4000 == entry[3] % 0x4000, 'misaligned load segment'
assert any(entry[1] & 1 and entry[3] <= header[4] < entry[3] + entry[6]
           for entry in loads), 'entry point is outside executable memory'

sections = [struct.unpack_from('<IIQQQQIIQQ', blob, header[6] + i * header[11])
            for i in range(header[12])]
names_section = sections[header[13]]
names = blob[names_section[4]:names_section[4] + names_section[5]]
checked = 0
for section in sections:
    # create-fself moves SHT_DYNAMIC into file-only PT_SCE_DYNLIBDATA metadata.
    if not section[2] & 2 or not section[5] or section[1] == 6:
        continue
    name = names[section[0]:].split(b'\0')[0].decode(errors='replace')
    segment = next((entry for entry in loads if entry[3] <= section[3]
                    and section[3] + section[5] <= entry[3] + entry[6]), None)
    assert segment is not None, f'{name} is outside PT_LOAD memory'
    if section[1] != 8:  # SHT_NOBITS has no initialized file image.
        offset = segment[2] + section[3] - segment[3]
        assert offset + section[5] <= segment[2] + segment[5], f'{name} has no loadable file image'
        assert blob[offset:offset + section[5]] == blob[section[4]:section[4] + section[5]], \
            f'{name} file image differs from its mapped contents'
    checked += 1
print(f'check-oelf: {path.name}: {checked} allocated sections and entry point covered; PASS')
