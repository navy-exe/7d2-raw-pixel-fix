"""Verify the published snapshot's file hashes without requiring camera data."""
import hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parent.parent
manifest=json.loads((root/'MANIFEST.json').read_text())
for item in manifest['files']:
    data=(root/item['path']).read_bytes()
    assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'],item['path']
print('PASS:',len(manifest['files']),'published file hashes')
