"""FIFA 18 PS4 decrypted save inspection and fixed-size attribute patches.

Independent implementation. See FORMAT.md for layout and research credits.
"""
from io import BytesIO
from pathlib import Path, PurePosixPath
import hashlib
import json
import struct
import zipfile
import zlib

SCHEMA = json.loads(Path(__file__).with_name('schema.json').read_text())
MAGIC = b'DB\0\x08\0\0\0\0'
ATTRIBUTES = {
    'Ritmo': {'acceleration': 'Aceleración', 'sprintspeed': 'Velocidad'},
    'Tiro': {'finishing': 'Definición', 'shotpower': 'Potencia de tiro',
             'longshots': 'Tiros lejanos', 'volleys': 'Voleas', 'penalties': 'Penaltis',
             'positioning': 'Posicionamiento'},
    'Pase': {'vision': 'Visión', 'crossing': 'Centros', 'freekickaccuracy': 'Faltas',
             'shortpassing': 'Pase corto', 'longpassing': 'Pase largo', 'curve': 'Efecto'},
    'Regate': {'agility': 'Agilidad', 'balance': 'Equilibrio', 'reactions': 'Reacciones',
               'ballcontrol': 'Control de balón', 'dribbling': 'Regate', 'composure': 'Compostura'},
    'Defensa': {'interceptions': 'Intercepciones', 'headingaccuracy': 'Cabeceo',
                'marking': 'Marcaje', 'standingtackle': 'Entrada', 'slidingtackle': 'Entrada deslizante'},
    'Físico': {'jumping': 'Salto', 'stamina': 'Resistencia', 'strength': 'Fuerza', 'aggression': 'Agresividad'},
    'Portero': {'gkdiving': 'Estirada', 'gkhandling': 'Paradas', 'gkkicking': 'Saque',
                'gkreflexes': 'Reflejos', 'gkpositioning': 'Colocación'},
}
EDITABLE = frozenset(k for group in ATTRIBUTES.values() for k in group)
MAX_ZIP = 64 * 1024 * 1024
MAX_EXPANDED = 96 * 1024 * 1024


class SaveError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise SaveError(message)


def u16(data, offset):
    require(0 <= offset <= len(data) - 2, 'Lectura fuera de límites.')
    return struct.unpack_from('<H', data, offset)[0]


def u32(data, offset):
    require(0 <= offset <= len(data) - 4, 'Lectura fuera de límites.')
    return struct.unpack_from('<I', data, offset)[0]


CRC_TABLE = []
for _i in range(256):
    _value = _i << 24
    for _ in range(8):
        _value = ((_value << 1) ^ (0x04c11db7 if _value & 0x80000000 else 0)) & 0xffffffff
    CRC_TABLE.append(_value)


def database_crc(data):
    crc = 0xffffffff
    for byte in data:
        crc = ((crc << 8) ^ CRC_TABLE[((crc >> 24) ^ byte) & 255]) & 0xffffffff
    return crc


def read_zip(blob):
    require(len(blob) <= MAX_ZIP, 'ZIP demasiado grande (máximo 64 MiB).')
    result = {}
    try:
        with zipfile.ZipFile(BytesIO(blob)) as archive:
            entries = archive.infolist()
            require(len(entries) <= 256, 'Demasiados archivos en el ZIP.')
            require(sum(i.file_size for i in entries) <= MAX_EXPANDED, 'ZIP expandido demasiado grande.')
            for entry in entries:
                # ZipInfo.filename normalizes backslashes on Windows; validate
                # the original archive name before that platform conversion.
                original_name = entry.orig_filename
                path = PurePosixPath(original_name)
                require(not path.is_absolute() and '..' not in path.parts and '\\' not in original_name and ':' not in original_name,
                        'Ruta insegura en el ZIP.')
                require(not entry.flag_bits & 1, 'ZIP protegido con contraseña no compatible.')
                if entry.is_dir():
                    continue
                require(entry.filename not in result, 'Archivos duplicados en el ZIP.')
                result[entry.filename] = archive.read(entry)
    except (zipfile.BadZipFile, RuntimeError, NotImplementedError) as error:
        raise SaveError('ZIP inválido o método de compresión no compatible.') from error
    return result


def make_zip(files):
    stream = BytesIO()
    with zipfile.ZipFile(stream, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name, content in files.items():
            archive.writestr(name, content)
    return stream.getvalue()


def sfo_fields(data):
    require(data[:4] == b'\0PSF', 'param.sfo no válido.')
    keys, values, count = u32(data, 8), u32(data, 12), u32(data, 16)
    require(count <= 128 and 20 + count * 16 <= len(data), 'Directorio SFO inválido.')
    result = {}
    for i in range(count):
        pos = 20 + 16 * i
        key_offset, length, value_offset = keys + u16(data, pos), u32(data, pos + 4), values + u32(data, pos + 12)
        require(key_offset < len(data) and value_offset + length <= len(data), 'SFO fuera de límites.')
        key = data[key_offset:].split(b'\0', 1)[0].decode('ascii')
        result[key] = data[value_offset:value_offset + length]
    return result


class Career:
    def __init__(self, blob):
        files = read_zip(blob)
        matches = [name for name in files if name == 'DATA' or name.endswith('/DATA')]
        require(len(matches) == 1, 'Importa un ZIP descifrado con DATA, INDEX, NAME y sce_sys/param.sfo.')
        prefix = matches[0][:-4]
        require(all(name.startswith(prefix) for name in files), 'El ZIP contiene archivos fuera del guardado.')
        self.files = {name[len(prefix):]: value for name, value in files.items()}
        require(all(name in self.files for name in ('DATA', 'INDEX', 'NAME', 'sce_sys/param.sfo', 'sce_sys/keystone')),
                'Faltan archivos del guardado descifrado.')
        sfo = sfo_fields(self.files['sce_sys/param.sfo'])
        self.title_id = sfo.get('TITLE_ID', b'').rstrip(b'\0').decode('ascii')
        require(self.title_id.startswith('CUSA'), 'Este editor admite guardados de PS4, no guardados nativos de PS5.')
        account = sfo.get('ACCOUNT_ID', b'')
        require(len(account) == 8, 'ACCOUNT_ID ausente o de tamaño inesperado.')
        self.account_id = f'{int.from_bytes(account, "little"):016x}'
        self.save_name = sfo.get('SAVEDATA_DIRECTORY', b'').rstrip(b'\0').decode('ascii')
        require(self.save_name.startswith('Career') and all(c.isalnum() or c in '_-' for c in self.save_name), 'No es una carrera compatible.')
        self.data = self.files['DATA']
        self.sha256 = hashlib.sha256(self.data).hexdigest()
        marker = self.data.find(b'SaveType_Career\0', 0, 512)
        require(marker >= 0, 'No se encontró SaveType_Career.')
        self.checksum_offset = marker + len(b'SaveType_Career\0')
        self.checksum_start = self.checksum_offset + 24
        require(u32(self.data, self.checksum_offset) == zlib.crc32(self.data[self.checksum_start:]),
                'El CRC global del original no coincide. No se editará este formato.')
        self.tables = []
        self.databases = []
        self._parse()
        players = [table for table in self.tables if table['name'] == 'players']
        require(len(players) == 1, 'Se esperaba una única tabla players.')
        self.players_table = players[0]
        require(EDITABLE <= self.players_table['fields'].keys(), 'Esquema de atributos incompatible.')
        self.players = self.players_table['records']
        require(len({p.get('playerid') for p in self.players}) == len(self.players), 'IDs de jugadores duplicados.')
        self.names = {}
        self.primary_player = None
        self.teams = {}
        self.links = {}
        for table in self.tables:
            if table['name'] == 'editedplayernames':
                for p in table['records']:
                    self.names[p['playerid']] = p.get('commonname') or ' '.join(filter(None, (p.get('firstname'), p.get('surname'))))
            elif table['name'] == 'career_playasplayer' and table['records']:
                self.primary_player = table['records'][0].get('playerid')
            elif table['name'] == 'teams':
                self.teams.update({p['teamid']: p.get('teamname', '') for p in table['records']})
            elif table['name'] == 'teamplayerlinks':
                for p in table['records']:
                    self.links.setdefault(p.get('playerid'), p.get('teamid'))

    def _parse(self):
        data = self.data
        cursor = 0
        while True:
            base = data.find(MAGIC, cursor)
            if base < 0:
                break
            require(len(self.databases) < 8, 'Demasiadas bases de datos.')
            size, count = u32(data, base + 8), u32(data, base + 16)
            end, start = base + size, base + 28 + count * 8
            require(0 < count <= 512 and start < end <= len(data), 'Cabecera de base de datos inválida.')
            require(u32(data, base + 20) == database_crc(data[base:base + 20]), 'CRC de cabecera incorrecto.')
            require(u32(data, start - 4) == database_crc(data[base + 24:start - 4]), 'CRC de directorio incorrecto.')
            entries = []
            for i in range(count):
                entry = base + 24 + i * 8
                short = data[entry:entry + 4].decode('ascii')
                entries.append((start + u32(data, entry + 4), short))
            entries.sort()
            require(len({p for p, _ in entries}) == count, 'Tablas superpuestas.')
            for i, (pos, short) in enumerate(entries):
                table_end = entries[i + 1][0] if i + 1 < count else end
                require(start <= pos < table_end - 40, 'Tabla fuera de límites.')
                require(u32(data, pos + 32) == database_crc(data[pos:pos + 32]), 'CRC de tabla incorrecto.')
                require(u32(data, table_end - 4) == database_crc(data[pos + 36:table_end - 4]), 'CRC de registros incorrecto.')
                if short not in SCHEMA:
                    continue
                schema = SCHEMA[short]
                record_size, records_count, fields_count = u32(data, pos + 4), u16(data, pos + 18), data[pos + 24]
                record_start = pos + 36 + fields_count * 16
                require(record_size > 0 and record_start + records_count * record_size <= table_end - 4, 'Registros fuera de límites.')
                fields = {}
                for j in range(fields_count):
                    fp = pos + 36 + j * 16
                    typ, bit, depth = u32(data, fp), u32(data, fp + 4), u32(data, fp + 12)
                    short_field = data[fp + 8:fp + 12].decode('ascii')
                    require(0 < depth <= record_size * 8 and bit + depth <= record_size * 8, 'Campo fuera de límites.')
                    if short_field in schema['fields']:
                        f = schema['fields'][short_field]
                        fields[f['name']] = {'type': typ, 'bit': bit, 'depth': depth, 'low': f['low']}
                records = []
                for j in range(records_count):
                    rp = record_start + j * record_size
                    raw = data[rp:rp + record_size]
                    number = int.from_bytes(raw, 'little')
                    record = {}
                    for name, field in fields.items():
                        bit, depth, typ = field['bit'], field['depth'], field['type']
                        if typ == 3:
                            record[name] = ((number >> bit) & ((1 << depth) - 1)) + field['low']
                        elif typ == 0:
                            record[name] = raw[bit // 8:(bit + depth) // 8].split(b'\0', 1)[0].decode('utf-8', 'replace')
                    records.append(record)
                self.tables.append({'name': schema['name'], 'fields': fields, 'records': records,
                                    'start': pos, 'end': table_end, 'record_start': record_start, 'record_size': record_size})
            self.databases.append((base, end))
            cursor = end
        require(len(self.databases) == 3, 'Se esperaban tres bases de datos de FIFA 18.')

    def summary(self):
        records = []
        for p in self.players:
            pid = p['playerid']
            records.append({'id': pid, 'name': self.names.get(pid, f'Jugador #{pid}'),
                            'team': self.teams.get(self.links.get(pid), ''),
                            'overall': p.get('overallrating'), 'potential': p.get('potential'),
                            'attributes': {key: p[key] for key in EDITABLE}})
        return {'save': self.save_name, 'titleId': self.title_id, 'sha256': self.sha256,
                'primaryPlayer': self.primary_player, 'players': records, 'groups': ATTRIBUTES,
                'databaseCount': len(self.databases)}

    def export(self, patches):
        require(isinstance(patches, dict) and len(patches) <= len(self.players), 'Propuesta inválida.')
        data = bytearray(self.data)
        table = self.players_table
        index = {p['playerid']: i for i, p in enumerate(self.players)}
        changes = []
        allowed_bits = set()
        for player_id, attrs in patches.items():
            require(isinstance(player_id, str) and player_id.isdecimal(), 'ID de jugador inválido.')
            pid = int(player_id)
            require(pid in index and isinstance(attrs, dict) and attrs.keys() <= EDITABLE, 'Jugador o atributo no editable.')
            original = self.players[index[pid]]
            pos = table['record_start'] + index[pid] * table['record_size']
            for name, value in attrs.items():
                require(type(value) is int and 1 <= value <= 99, 'Los atributos deben ser enteros entre 1 y 99.')
                if value == original[name]:
                    continue
                f = table['fields'][name]
                require(f['type'] == 3 and 0 <= value - f['low'] < (1 << f['depth']), 'Codificación incompatible.')
                for k in range(f['depth']):
                    bit = pos * 8 + f['bit'] + k
                    mask = 1 << (bit % 8)
                    data[bit // 8] = (data[bit // 8] & ~mask) | (((value - f['low']) >> k & 1) << (bit % 8))
                    allowed_bits.add(bit)
                changes.append({'playerId': pid, 'field': name, 'original': original[name], 'edited': value})
        if changes:
            struct.pack_into('<I', data, table['end'] - 4, database_crc(data[table['start'] + 36:table['end'] - 4]))
            struct.pack_into('<I', data, self.checksum_offset, zlib.crc32(data[self.checksum_start:]))
        allowed_bytes = {bit // 8 for bit in allowed_bits} | set(range(table['end'] - 4, table['end'])) | set(range(self.checksum_offset, self.checksum_offset + 4))
        require(all(i in allowed_bytes for i, (a, b) in enumerate(zip(self.data, data)) if a != b), 'Cambios fuera de los campos permitidos.')
        files = dict(self.files)
        files['DATA'] = bytes(data)
        blob = make_zip(files)
        check = Career(blob)
        require(check.account_id == self.account_id, 'El identificador de cuenta cambió.')
        for change in changes:
            player = check.players[index[change['playerId']]]
            require(player[change['field']] == change['edited'], 'Error al releer los cambios.')
        return blob, changes
