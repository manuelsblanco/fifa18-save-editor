"""Optional GarlicSaves API client. Requires Python 3.10+, no third-party modules.

Uploads the edited decrypted save, encrypts it, then decrypts the result again to
verify account identity and DATA before producing a console-ready ZIP.
"""
import argparse
import getpass
import hashlib
import json
from pathlib import Path
import time
import urllib.error
import urllib.request
import uuid

from core import Career, SaveError, make_zip, read_zip

BASE = 'https://www.garlicsaves.com/api/v1'


class Api:
    def __init__(self, key):
        self.key = key
        self.last_job = None

    def request(self, path, data=None, content_type=None):
        headers = {'X-Worker-Key': self.key, 'Accept': 'application/json',
                   'User-Agent': 'FIFA18SaveEditor/0.1'}
        if content_type:
            headers['Content-Type'] = content_type
        req = urllib.request.Request(BASE + path, data=data, headers=headers)
        with urllib.request.urlopen(req, timeout=90) as response:
            result = response.read(100 * 1024 * 1024 + 1)
        if len(result) > 100 * 1024 * 1024:
            raise SaveError('API response too large.')
        return result

    def submit(self, operation, blob, fields):
        boundary = '----FIFA18' + uuid.uuid4().hex
        chunks = []
        for key, value in {'operation': operation, **fields}.items():
            chunks.append(f'--{boundary}\r\nContent-Disposition: form-data; name="{key}"\r\n\r\n{value}\r\n'.encode())
        chunks.append(f'--{boundary}\r\nContent-Disposition: form-data; name="files"; filename="save.zip"\r\nContent-Type: application/zip\r\n\r\n'.encode() + blob + b'\r\n')
        chunks.append(f'--{boundary}--\r\n'.encode())
        response = json.loads(self.request('/jobs', b''.join(chunks), 'multipart/form-data; boundary=' + boundary))
        job = str(uuid.UUID(response['job_id']))
        self.last_job = job
        print(f'{operation}: {job}', flush=True)
        return job

    def wait(self, job):
        deadline = time.monotonic() + 1800
        previous = None
        while time.monotonic() < deadline:
            response = json.loads(self.request('/jobs/' + job))
            status = response.get('status')
            if status != previous:
                print('Status:', status, flush=True)
                previous = status
            if status == 'done':
                return self.request('/jobs/' + job + '/result')
            if status not in ('queued', 'running'):
                raise SaveError('Remote job failed. Inspect the job in GarlicSaves.')
            time.sleep(10)
        raise SaveError('Timed out waiting for GarlicSaves. The remote job may still finish; check the site before resubmitting.')


def encrypt_and_verify(api, blob, encoding='raw'):
    source = Career(blob)
    # Displayed numeric ID versus raw little-endian SFO bytes: see FORMAT.md.
    account = bytes.fromhex(source.account_id)[::-1].hex() if encoding == 'raw' else source.account_id
    result = api.wait(api.submit('encrypt', blob, {'account_id': account, 'platform': 'ps4'}))
    encrypted = read_zip(result)
    flat = {}
    for name, content in encrypted.items():
        leaf = name.rsplit('/', 1)[-1]
        if leaf in flat:
            raise SaveError('Duplicate encrypted save files.')
        flat[leaf] = content
    if set(flat) != {source.save_name, source.save_name + '.bin'} or len(flat[source.save_name + '.bin']) != 96 or len(flat[source.save_name]) < 4096:
        raise SaveError('Unexpected encrypted result.')
    check = api.wait(api.submit('decrypt', make_zip(flat), {'include_sce_sys': 'true'}))
    verified = Career(check)
    if source.account_id != verified.account_id:
        raise SaveError('Encrypted result belongs to another account. Nothing was exported. Try the other --account-encoding mode.')
    for name in ('DATA', 'INDEX', 'NAME', 'sce_sys/keystone'):
        if source.files[name] != verified.files[name]:
            raise SaveError('Encrypted result does not preserve ' + name + '. Nothing was exported.')
    if verified.title_id != source.title_id or verified.save_name != source.save_name:
        raise SaveError('Result title or slot mismatch.')
    prefix = f'PS4/SAVEDATA/{source.account_id}/{source.title_id}/'
    return make_zip({prefix + name: content for name, content in flat.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path, help='Edited decrypted FIFA 18 career ZIP')
    parser.add_argument('output', type=Path, help='New encrypted ZIP; must not already exist')
    parser.add_argument('--key-file', type=Path, help='Local file with API key (never commit it)')
    parser.add_argument('--account-encoding', choices=['raw', 'numeric'], default='raw')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output already exists; choose a new path.')
    key = args.key_file.read_text().strip() if args.key_file else getpass.getpass('GarlicSaves API key: ').strip()
    if not key or '\n' in key or '\r' in key:
        parser.error('Key must be one non-empty line.')
    api = Api(key)
    try:
        result = encrypt_and_verify(api, args.input.read_bytes(), args.account_encoding)
        with args.output.open('xb') as target:
            target.write(result)
        print('Account and game files verified. Saved:', args.output)
    except urllib.error.HTTPError as error:
        print('API HTTP error:', error.code, '(key not displayed).')
        if api.last_job:
            print('Last job:', api.last_job, '— check its status on GarlicSaves before retrying.')
        raise SystemExit(1)
    except (OSError, ValueError, KeyError, SaveError) as error:
        # Remote error bodies and credentials are deliberately not printed.
        print('Could not finish:', type(error).__name__)
        if isinstance(error, SaveError):
            print(str(error).replace(key, '[hidden]'))
        if api.last_job:
            print('Last job:', api.last_job)
        raise SystemExit(1)


if __name__ == '__main__':
    main()
