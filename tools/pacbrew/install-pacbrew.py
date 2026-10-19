"""Install verified PacBrew files inside the disposable Linux container only."""
from pathlib import Path
import hashlib
import json
import tarfile

root = Path(__file__).resolve().parent / 'downloads'
for package in json.loads((root / 'pacbrew-manifest.json').read_text()):
    path = root / package['FILENAME']
    if hashlib.sha256(path.read_bytes()).hexdigest() != package['SHA256SUM']:
        raise RuntimeError(f'Checksum mismatch: {path}')
    with tarfile.open(path) as archive:
        members = [m for m in archive if m.name.startswith('opt/pacbrew/') or m.name == 'opt']
        archive.extractall('/', members=members, filter='tar')
    print(package['NAME'], flush=True)
