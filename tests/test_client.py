import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from garlic_client import encrypt_and_verify
from test_core import fixture, Career, make_zip
from core import SaveError


class FakeApi:
    def __init__(self, source, wrong_account=False):
        self.source = source
        self.calls = []
        self.wrong_account = wrong_account

    def submit(self, operation, blob, fields):
        self.calls.append((operation, fields))
        return operation

    def wait(self, job):
        c = Career(self.source)
        if job == 'encrypt':
            return make_zip({c.save_name: bytes(4096), c.save_name + '.bin': bytes(96)})
        if not self.wrong_account:
            return self.source
        files = dict(c.files)
        sfo = bytearray(files['sce_sys/param.sfo'])
        raw = int(c.account_id, 16).to_bytes(8, 'little')
        i = sfo.index(raw)
        sfo[i] ^= 1
        files['sce_sys/param.sfo'] = bytes(sfo)
        return make_zip(files)


class ClientTests(unittest.TestCase):
    def test_verifies_account_and_places_correct_folder(self):
        source = fixture()
        api = FakeApi(source)
        output = encrypt_and_verify(api, source)
        self.assertTrue(output.startswith(b'PK'))
        self.assertEqual([x[0] for x in api.calls], ['encrypt', 'decrypt'])
        self.assertEqual(api.calls[0][1]['account_id'], (123456789).to_bytes(8, 'little').hex())

    def test_rejects_reversed_or_wrong_account(self):
        source = fixture()
        with self.assertRaisesRegex(SaveError, 'another account'):
            encrypt_and_verify(FakeApi(source, True), source)


if __name__ == '__main__':
    unittest.main()
