"""Synthetic fixtures only: never commit personal game saves."""
import json
from io import BytesIO
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from core import Career, SaveError, SCHEMA, EDITABLE, database_crc, make_zip, read_zip


def synthetic_table(short, fields, rows):
    record_size = max((bit + depth + 7) // 8 for _, _, bit, depth in fields)
    header = bytearray(36)
    struct.pack_into('<I', header, 4, record_size)
    struct.pack_into('<HH', header, 16, len(rows), len(rows))
    header[24] = len(fields)
    descriptors = b''.join(struct.pack('<II4sI', typ, bit, name.encode(), depth) for name, typ, bit, depth in fields)
    records = bytearray()
    for values in rows:
        number = 0
        for (name, typ, bit, depth), value in zip(fields, values):
            number |= value << bit
        records += number.to_bytes(record_size, 'little')
    body = descriptors + records
    struct.pack_into('<I', header, 32, database_crc(header[:32]))
    return bytes(header) + body + struct.pack('<I', database_crc(body))


def synthetic_database(tables):
    header = bytearray(24)
    header[:8] = b'DB\0\x08\0\0\0\0'
    struct.pack_into('<I', header, 16, len(tables))
    directory, bodies = bytearray(), bytearray()
    for name, table in tables:
        directory += name.encode() + struct.pack('<I', len(bodies))
        bodies += table
    struct.pack_into('<I', header, 8, 28 + len(directory) + len(bodies))
    struct.pack_into('<I', header, 20, database_crc(header[:20]))
    return bytes(header) + directory + struct.pack('<I', database_crc(directory)) + bodies


def synthetic_sfo():
    values = {'ACCOUNT_ID': (123456789).to_bytes(8, 'little'),
              'TITLE_ID': b'CUSA00000\0', 'SAVEDATA_DIRECTORY': b'Career20000101000000\0'}
    entries, keys, content = bytearray(), bytearray(), bytearray()
    for name, value in values.items():
        entries += struct.pack('<HHIII', len(keys), 0x204, len(value), len(value), len(content))
        keys += name.encode() + b'\0'
        content += value
    kt = 20 + len(entries)
    return struct.pack('<5I', 0x46535000, 0x101, kt, kt + len(keys), len(values)) + entries + keys + content


def fixture():
    schema = next(v for v in SCHEMA.values() if v['name'] == 'players')
    id_short = next(k for k, v in schema['fields'].items() if v['name'] == 'playerid')
    fields = [(id_short, 3, 0, 19)]
    values = [1001]
    bit = 19
    for name in sorted(EDITABLE):
        short, mapping = next((k, v) for k, v in schema['fields'].items() if v['name'] == name)
        fields.append((short, 3, bit, 7))
        values.append(50 - mapping['low'])
        bit += 7
    table = synthetic_table('CZUM', fields, [values])
    empty = synthetic_table('xxxx', [('test', 3, 0, 8)], [[0]])
    data = bytearray(102)
    data[62:78] = b'SaveType_Career\0'
    data += synthetic_database([('xxxx', empty)])
    data += synthetic_database([('CZUM', table)])
    data += synthetic_database([('xxxx', empty)])
    struct.pack_into('<I', data, 78, zlib.crc32(data[102:]))
    return make_zip({'DATA': bytes(data), 'INDEX': b'synthetic-index', 'NAME': b'Career20000101000000\0',
                     'sce_sys/param.sfo': synthetic_sfo(), 'sce_sys/keystone': bytes(96)})


class CoreTests(unittest.TestCase):
    def test_round_trip_no_changes(self):
        blob = fixture()
        edited, changes = Career(blob).export({})
        self.assertEqual(changes, [])
        self.assertEqual(read_zip(blob), read_zip(edited))

    def test_patch_cross_byte_field_and_crcs(self):
        original = Career(fixture())
        edited, changes = original.export({'1001': {'finishing': 99, 'stamina': 73}})
        result = Career(edited)
        self.assertEqual(result.players[0]['finishing'], 99)
        self.assertEqual(result.players[0]['stamina'], 73)
        self.assertEqual(result.account_id, original.account_id)
        for key in EDITABLE - {'finishing', 'stamina'}:
            self.assertEqual(result.players[0][key], 50)
        self.assertEqual(len(changes), 2)

    def test_reject_invalid_values_or_fields(self):
        for patch in [{'finishing': 100}, {'finishing': True}, {'finishing': 1.5}, {'playerid': 12}]:
            with self.assertRaises(SaveError):
                Career(fixture()).export({'1001': patch})

    def test_reject_stale_global_crc(self):
        files = read_zip(fixture())
        b = bytearray(files['DATA'])
        b[-1] ^= 1
        files['DATA'] = b
        with self.assertRaisesRegex(SaveError, 'CRC global'):
            Career(make_zip(files))

    def test_reject_stale_table_crc_with_valid_global(self):
        c = Career(fixture())
        files = dict(c.files)
        b = bytearray(c.data)
        b[c.players_table['record_start']] ^= 1
        struct.pack_into('<I', b, 78, zlib.crc32(b[102:]))
        files['DATA'] = b
        with self.assertRaisesRegex(SaveError, 'CRC de registros'):
            Career(make_zip(files))

    def test_zip_traversal(self):
        for path in ('../bad', 'C:/bad', '/bad', 'x\\bad'):
            # ZipInfo normalizes backslashes on Windows at construction time.
            # Set the stored name afterwards to test an actual unsafe ZIP entry.
            entry = zipfile.ZipInfo('placeholder')
            entry.filename = path
            entry.orig_filename = path
            stream = BytesIO()
            with zipfile.ZipFile(stream, 'w') as archive:
                archive.writestr(entry, b'x')
            with self.subTest(path=path), self.assertRaises(SaveError):
                read_zip(stream.getvalue())


if __name__ == '__main__':
    unittest.main()
