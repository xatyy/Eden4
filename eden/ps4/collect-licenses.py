#!/usr/bin/env python3
"""Bundle dependency notices available in this local PS4 build."""
import json
import shutil
import sys
from pathlib import Path

eden, build_root, destination = map(Path, sys.argv[1:])
destination.mkdir(parents=True, exist_ok=True)
packages = (
    'boost', 'enet', 'ffmpeg', 'fmt', 'frozen', 'httplib', 'lz4', 'nlohmann_json',
    'nx_tzdb', 'openssl', 'opus', 'simpleini', 'sirit', 'spirv-headers',
    'vulkanheaders', 'vulkanmemoryallocator', 'vulkanutilitylibraries', 'xbyak',
    'zbic', 'zlib', 'zstd',
)
manifest = {}
for name in packages:
    for source in (eden / '.cache/cpm' / name).glob('*'):
        if not source.is_dir():
            continue
        notices = set()
        for pattern in ('LICENSE*', 'LICENCE*', 'COPYING*', 'COPYRIGHT*', 'NOTICE*'):
            notices.update(path for path in source.glob(pattern) if path.is_file())
        if name == 'lz4':
            notices.update((source / 'lib').glob('LICENSE*'))
        for path in sorted(notices):
            output = destination / name / source.name / path.relative_to(source)
            output.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, output)
        manifest[name] = source.name

for library in ('libcxx', 'libcxxabi', 'libunwind'):
    for source in (build_root / 'src').glob(f'llvm-project-*.src/{library}/LICENSE.TXT'):
        shutil.copyfile(source, destination / f'{library}-LICENSE.txt')
for source in (build_root / 'src/mesa/docs/license.rst',):
    if source.is_file():
        shutil.copyfile(source, destination / 'Mesa-license.rst')
(destination / 'dependency-versions.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('Bundled dependency notices:', len(manifest), 'packages')
