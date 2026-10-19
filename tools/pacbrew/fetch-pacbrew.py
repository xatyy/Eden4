"""Download pinned public PacBrew packages and verify index SHA-256 hashes."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import json
import urllib.request

root = Path(__file__).resolve().parent / 'downloads'
packages = json.loads((root / 'pacbrew-manifest.json').read_text())

def fetch(package):
    path = root / package['FILENAME']
    if not path.exists():
        with urllib.request.urlopen('https://pacman.mydedibox.fr/pacbrew/packages/' + path.name, timeout=180) as response:
            path.write_bytes(response.read())
    actual = hashlib.sha256(path.read_bytes()).hexdigest()
    if actual != package['SHA256SUM']:
        raise RuntimeError(f'Checksum mismatch: {path.name}')
    return f'{path.name}: SHA256 OK'

with ThreadPoolExecutor(max_workers=4) as pool:
    for result in pool.map(fetch, packages):
        print(result, flush=True)
