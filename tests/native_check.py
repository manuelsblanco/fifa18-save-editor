"""Cross-check native C++ exports against an independent Python reader."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile

from test_core import Career, fixture, read_zip

executable = str(Path(sys.argv[1]).resolve())
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    source = root / 'synthetic.zip'
    source.write_bytes(fixture())
    noop = root / 'noop.zip'
    subprocess.run([executable, '--verify', str(source), str(noop)], check=True)
    assert read_zip(noop.read_bytes()) == read_zip(source.read_bytes())
    c = Career(source.read_bytes())
    proposal = root / 'proposal.json'
    proposal.write_text(json.dumps({'schema': 'fifa18-desktop-proposal-v1',
        'sourceSha256': c.sha256, 'patches': {'1001': {'finishing': 99, 'stamina': 73}}}))
    edited = root / 'edited.zip'
    subprocess.run([executable, '--apply', str(source), str(proposal), str(edited)], check=True)
    expected, _ = c.export({'1001': {'finishing': 99, 'stamina': 73}})
    assert read_zip(edited.read_bytes()) == read_zip(expected)
    parsed = Career(edited.read_bytes())
    assert parsed.players[0]['finishing'] == 99
    proposal.write_text(json.dumps({'schema': 'fifa18-desktop-proposal-v1',
        'sourceSha256': c.sha256, 'patches': {'1001': {'finishing': 100}}}))
    rejected = root / 'rejected.zip'
    assert subprocess.run([executable, '--apply', str(source), str(proposal), str(rejected)]).returncode != 0
    assert not rejected.exists()
print('Native reader/writer matches reference: no-op, edits, CRCs, account, invalid input.')
