#include "career.h"
#include <archive.h>
#include <archive_entry.h>
#include <limits>
#include <memory>

namespace {
void check(bool ok, const QString &message) {
    if (!ok)
        throw std::runtime_error(message.toStdString());
}
quint32 u32(const QByteArray &b, qsizetype p) {
    check(p >= 0 && p + 4 <= b.size(), "Read out of bounds");
    return qFromLittleEndian<quint32>(b.constData() + p);
}
quint16 u16(const QByteArray &b, qsizetype p) {
    check(p >= 0 && p + 2 <= b.size(), "Read out of bounds");
    return qFromLittleEndian<quint16>(b.constData() + p);
}
void put32(QByteArray &b, qsizetype p, quint32 v) {
    check(p >= 0 && p + 4 <= b.size(), "Write out of bounds");
    qToLittleEndian(v, b.data() + p);
}
QString text(const QByteArray &b) {
    return QString::fromUtf8(b.left(b.indexOf('\0') < 0 ? b.size() : b.indexOf('\0')));
}
quint64 bits(const QByteArray &b, qsizetype offset, int bit, int depth) {
    check(depth > 0 && depth <= 32 && offset >= 0 && offset + (bit + depth + 7) / 8 <= b.size(),
          "Field out of bounds");
    quint64 v = 0;
    for (int i = 0; i < depth; i++)
        v |= quint64((quint8(b[offset + (bit + i) / 8]) >> ((bit + i) % 8)) & 1) << i;
    return v;
}
QMap<QString, QByteArray> sfo(const QByteArray &b) {
    check(b.left(4) == QByteArray("\0PSF", 4), "Invalid param.sfo");
    quint32 kt = u32(b, 8), dt = u32(b, 12), count = u32(b, 16);
    check(count <= 128, "Invalid SFO");
    QMap<QString, QByteArray> out;
    for (quint32 i = 0; i < count; i++) {
        qsizetype p = 20 + i * 16, k = kt + u16(b, p), v = dt + u32(b, p + 12), len = u32(b, p + 4);
        check(k < b.size() && v + len <= b.size(), "SFO out of bounds");
        out[text(b.mid(k))] = b.mid(v, len);
    }
    return out;
}
} // namespace

const QMap<QString, QString> Career::labels = {{"acceleration", "Acceleration"},
                                               {"sprintspeed", "Sprint speed"},
                                               {"finishing", "Finishing"},
                                               {"shotpower", "Shot power"},
                                               {"longshots", "Long shots"},
                                               {"volleys", "Volleys"},
                                               {"penalties", "Penalties"},
                                               {"positioning", "Positioning"},
                                               {"vision", "Vision"},
                                               {"crossing", "Crossing"},
                                               {"freekickaccuracy", "Free kick accuracy"},
                                               {"shortpassing", "Short passing"},
                                               {"longpassing", "Long passing"},
                                               {"curve", "Curve"},
                                               {"agility", "Agility"},
                                               {"balance", "Balance"},
                                               {"reactions", "Reactions"},
                                               {"ballcontrol", "Ball control"},
                                               {"dribbling", "Dribbling"},
                                               {"composure", "Composure"},
                                               {"interceptions", "Interceptions"},
                                               {"headingaccuracy", "Heading accuracy"},
                                               {"marking", "Marking"},
                                               {"standingtackle", "Standing tackle"},
                                               {"slidingtackle", "Sliding tackle"},
                                               {"jumping", "Jumping"},
                                               {"stamina", "Stamina"},
                                               {"strength", "Strength"},
                                               {"aggression", "Aggression"},
                                               {"gkdiving", "GK diving"},
                                               {"gkhandling", "GK handling"},
                                               {"gkkicking", "GK kicking"},
                                               {"gkreflexes", "GK reflexes"},
                                               {"gkpositioning", "GK positioning"}};

quint32 Career::crc(const QByteArray &b, qsizetype start, qsizetype end, bool reflected) {
    check(0 <= start && start <= end && end <= b.size(), "Invalid checksum range");
    quint32 table[256];
    for (quint32 i = 0; i < 256; i++) {
        quint32 v = reflected ? i : i << 24;
        for (int k = 0; k < 8; k++)
            v = reflected ? ((v >> 1) ^ ((v & 1) ? 0xedb88320 : 0))
                          : ((v << 1) ^ ((v & 0x80000000) ? 0x04c11db7 : 0));
        table[i] = v;
    }
    quint32 c = 0xffffffff;
    for (qsizetype i = start; i < end; i++)
        c = reflected ? ((c >> 8) ^ table[(c ^ quint8(b[i])) & 255])
                      : ((c << 8) ^ table[((c >> 24) ^ quint8(b[i])) & 255]);
    return reflected ? c ^ 0xffffffff : c;
}

QMap<QString, QByteArray> Career::unzip(const QString &path) {
    QFile input(path);
    check(input.open(QIODevice::ReadOnly) && input.size() <= 64 * 1024 * 1024,
          "Cannot read ZIP or file exceeds 64 MiB");
    QByteArray blob = input.readAll();
    std::unique_ptr<archive, decltype(&archive_read_free)> a(archive_read_new(), archive_read_free);
    archive_read_support_format_zip(a.get());
    check(archive_read_open_memory(a.get(), blob.constData(), blob.size()) == ARCHIVE_OK,
          "Invalid ZIP");
    QMap<QString, QByteArray> out;
    archive_entry *entry = nullptr;
    qsizetype total = 0;
    int count = 0, status;
    while ((status = archive_read_next_header(a.get(), &entry)) == ARCHIVE_OK) {
        check(++count <= 256, "Too many files");
        QString n = QString::fromUtf8(archive_entry_pathname(entry));
        check(!n.startsWith('/') && !n.contains('\\') && !n.contains(':') && !n.split('/').contains(".."),
              "Unsafe ZIP path");
        if (archive_entry_filetype(entry) == AE_IFDIR)
            continue;
        check(archive_entry_filetype(entry) == AE_IFREG && !out.contains(n),
              "Unsupported or duplicate ZIP entry");
        qint64 size = archive_entry_size(entry);
        check(size >= 0 && size <= 96 * 1024 * 1024 - total, "Uncompressed ZIP is too large");
        total += size;
        QByteArray content(size, Qt::Uninitialized);
        qsizetype got = 0;
        while (got < size) {
            auto r = archive_read_data(a.get(), content.data() + got, size - got);
            check(r > 0, "Unable to decompress ZIP");
            got += r;
        }
        char extra;
        check(archive_read_data(a.get(), &extra, 1) == 0, "Inconsistent ZIP size");
        out[n] = content;
    }
    check(status == ARCHIVE_EOF, "Corrupt ZIP");
    return out;
}

void Career::zip(const QString &path, const QMap<QString, QByteArray> &files) {
    QSaveFile target(path);
    check(target.open(QIODevice::WriteOnly), "Cannot write ZIP");
    std::unique_ptr<archive, decltype(&archive_write_free)> a(archive_write_new(),
                                                              archive_write_free);
    archive_write_set_format_zip(a.get());
    auto write = [](archive *, void *client, const void *buf, size_t len) -> la_ssize_t {
        return static_cast<QSaveFile *>(client)->write(static_cast<const char *>(buf), len);
    };
    check(archive_write_open(a.get(), &target, nullptr, write, nullptr) == ARCHIVE_OK,
          "Cannot create ZIP");
    for (auto it = files.begin(); it != files.end(); ++it) {
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> e(archive_entry_new(),
                                                                        archive_entry_free);
        auto n = it.key().toUtf8();
        archive_entry_set_pathname(e.get(), n.constData());
        archive_entry_set_size(e.get(), it.value().size());
        archive_entry_set_filetype(e.get(), AE_IFREG);
        archive_entry_set_perm(e.get(), 0777);
        check(archive_write_header(a.get(), e.get()) == ARCHIVE_OK, "ZIP header error");
        check(archive_write_data(a.get(), it.value().constData(), it.value().size()) ==
                  it.value().size(),
              "ZIP write error");
    }
    check(archive_write_close(a.get()) == ARCHIVE_OK, "Cannot close ZIP");
    check(target.commit(), "Cannot save ZIP");
}

Career Career::load(const QString &path) {
    Career c;
    c.sourcePath = QFileInfo(path).absoluteFilePath();
    c.files = unzip(path);
    QString prefix;
    int matches = 0;
    for (auto it = c.files.begin(); it != c.files.end(); ++it)
        if (it.key() == "DATA" || it.key().endsWith("/DATA")) {
            prefix = it.key().left(it.key().size() - 4);
            matches++;
        }
    check(matches == 1, "Open a decrypted ZIP containing DATA, INDEX, NAME and sce_sys");
    QMap<QString, QByteArray> normalized;
    for (auto it = c.files.begin(); it != c.files.end(); ++it) {
        check(it.key().startsWith(prefix), "ZIP contains multiple saves");
        normalized[it.key().mid(prefix.size())] = it.value();
    }
    c.files = normalized;
    for (const auto &n : {"DATA", "INDEX", "NAME", "sce_sys/param.sfo", "sce_sys/keystone"})
        check(c.files.contains(n), QString("Missing %1").arg(n));
    auto metadata = sfo(c.files["sce_sys/param.sfo"]);
    c.title = text(metadata["TITLE_ID"]);
    c.name = text(metadata["SAVEDATA_DIRECTORY"]);
    check(c.title.startsWith("CUSA") && c.name.startsWith("Career"),
          "A FIFA 18 PS4 career save is required");
    auto account = metadata["ACCOUNT_ID"];
    check(account.size() == 8, "Invalid SFO account");
    c.account = QString::number(qFromLittleEndian<quint64>(account.constData()), 16)
                    .rightJustified(16, '0');
    c.data = c.files["DATA"];
    auto marker = c.data.indexOf(QByteArray("SaveType_Career\0", 16));
    check(marker >= 0 && marker < 512, "Unsupported career save");
    c.checksum = marker + 16;
    c.checksumStart = c.checksum + 24;
    check(u32(c.data, c.checksum) == crc(c.data, c.checksumStart, c.data.size(), true),
          "Original career checksum does not match");
    c.parse();
    return c;
}

void Career::parse() {
    QFile schemaFile(":/schema.json");
    check(schemaFile.open(QIODevice::ReadOnly), "Schema not found");
    auto schema = QJsonDocument::fromJson(schemaFile.readAll()).object();
    QMap<QString, QList<QJsonObject>> records;
    QMap<int, QString> names, teams;
    QMap<int, int> links;
    qsizetype cursor = 0;
    int dbs = 0;
    while (true) {
        qsizetype base = data.indexOf(QByteArray("DB\0\x08\0\0\0\0", 8), cursor);
        if (base < 0)
            break;
        check(++dbs <= 8, "Too many databases");
        qsizetype size = u32(data, base + 8), end = base + size;
        quint32 count = u32(data, base + 16);
        qsizetype start = base + 28 + count * 8;
        check(count > 0 && count <= 512 && start < end && end <= data.size(),
              "Database out of bounds");
        check(u32(data, base + 20) == crc(data, base, base + 20), "Invalid header checksum");
        check(u32(data, start - 4) == crc(data, base + 24, start - 4),
              "Invalid directory checksum");
        QList<QPair<qsizetype, QString>> entries;
        for (quint32 i = 0; i < count; i++) {
            auto p = base + 24 + i * 8;
            entries.append({start + u32(data, p + 4), QString::fromLatin1(data.mid(p, 4))});
        }
        std::sort(entries.begin(), entries.end());
        for (qsizetype i = 0; i < entries.size(); i++) {
            auto pos = entries[i].first;
            auto te = i + 1 < entries.size() ? entries[i + 1].first : end;
            check(pos >= start && pos + 40 < te, "Table out of bounds");
            check(u32(data, pos + 32) == crc(data, pos, pos + 32), "Invalid table checksum");
            check(u32(data, te - 4) == crc(data, pos + 36, te - 4), "Invalid record checksum");
            auto sc = schema[entries[i].second].toObject();
            if (sc.isEmpty())
                continue;
            QString name = sc["name"].toString();
            auto mapping = sc["fields"].toObject();
            int rs = u32(data, pos + 4), n = u16(data, pos + 18), nf = quint8(data[pos + 24]);
            auto begin = pos + 36 + nf * 16;
            check(rs > 0 && begin + qsizetype(n) * rs <= te - 4, "Records out of bounds");
            QMap<QString, Field> defs;
            for (int j = 0; j < nf; j++) {
                auto fp = pos + 36 + j * 16;
                auto m = mapping[QString::fromLatin1(data.mid(fp + 8, 4))].toObject();
                int bit = u32(data, fp + 4), depth = u32(data, fp + 12);
                check(bit >= 0 && depth > 0 && qint64(bit) + depth <= qint64(rs) * 8,
                      "Field out of bounds");
                if (!m.isEmpty())
                    defs[m["name"].toString()] = {int(u32(data, fp)), bit, depth, m["low"].toInt()};
            }
            QList<QJsonObject> rows;
            for (int j = 0; j < n; j++) {
                auto rp = begin + qsizetype(j) * rs;
                QJsonObject row;
                for (auto f = defs.begin(); f != defs.end(); ++f) {
                    auto v = f.value();
                    if (v.type == 3)
                        row[f.key()] = qint64(bits(data, rp, v.bit, v.depth)) + v.low;
                    else if (v.type == 0)
                        row[f.key()] = text(data.mid(rp + v.bit / 8, v.depth / 8));
                }
                rows.append(row);
            }
            records[name] = rows;
            if (name == "players") {
                check(fields.isEmpty(), "Multiple player tables");
                fields = defs;
                recordSize = rs;
                playerTable = pos;
                playerEnd = te;
                for (int j = 0; j < n; j++) {
                    auto r = rows[j];
                    Player p;
                    p.id = r["playerid"].toInt();
                    p.overall = r["overallrating"].toInt();
                    p.potential = r["potential"].toInt();
                    p.offset = begin + qsizetype(j) * rs;
                    for (auto it = labels.begin(); it != labels.end(); ++it) {
                        check(defs.contains(it.key()), "Missing attribute");
                        p.attributes[it.key()] = r[it.key()].toInt();
                    }
                    players.append(p);
                }
            }
        }
        cursor = end;
    }
    check(dbs == 3 && !players.isEmpty(), "Expected three FIFA 18 databases");
    for (auto r : records["editedplayernames"])
        names[r["playerid"].toInt()] =
            r["commonname"].toString().isEmpty()
                ? r["firstname"].toString() + " " + r["surname"].toString()
                : r["commonname"].toString();
    for (auto r : records["teams"])
        teams[r["teamid"].toInt()] = r["teamname"].toString();
    for (auto r : records["teamplayerlinks"])
        if (!links.contains(r["playerid"].toInt()))
            links[r["playerid"].toInt()] = r["teamid"].toInt();
    if (!records["career_playasplayer"].isEmpty())
        primary = records["career_playasplayer"].first()["playerid"].toInt();
    QSet<int> ids;
    for (auto &p : players) {
        check(!ids.contains(p.id), "Duplicate player IDs");
        ids.insert(p.id);
        p.name = names.value(p.id, QString("Player #%1").arg(p.id));
        p.team = teams.value(links.value(p.id, -1));
    }
}

QList<Edit> Career::save(const QString &path, const QMap<int, QMap<QString, int>> &patches) const {
    auto destination = QFileInfo(path);
    auto source = QFileInfo(sourcePath);
    check(destination.absoluteFilePath() != source.absoluteFilePath() &&
              (!destination.exists() ||
               destination.canonicalFilePath() != source.canonicalFilePath()),
          "Choose another file: the original cannot be overwritten");
    QByteArray edited = data;
    QList<Edit> changes;
    QSet<qsizetype> allowed;
    QMap<int, const Player *> index;
    for (const auto &p : players)
        index[p.id] = &p;
    for (auto pi = patches.begin(); pi != patches.end(); ++pi) {
        check(index.contains(pi.key()), "Player not found");
        auto p = index[pi.key()];
        for (auto it = pi.value().begin(); it != pi.value().end(); ++it) {
            check(labels.contains(it.key()) && it.value() >= 1 && it.value() <= 99,
                  "Invalid attribute or value");
            auto f = fields[it.key()];
            check(f.type == 3 && it.value() - f.low >= 0 &&
                      quint64(it.value() - f.low) < (quint64(1) << f.depth),
                  "Value out of range");
            if (it.value() == p->attributes[it.key()])
                continue;
            for (int k = 0; k < f.depth; k++) {
                auto byte = p->offset + (f.bit + k) / 8;
                int shift = (f.bit + k) % 8;
                quint8 v = quint8(edited[byte]);
                v = (v & ~(1 << shift)) | (((it.value() - f.low) >> k & 1) << shift);
                edited[byte] = char(v);
                allowed.insert(byte);
            }
            changes.append({p->id, it.key(), p->attributes[it.key()], it.value()});
        }
    }
    if (!changes.isEmpty()) {
        put32(edited, playerEnd - 4, crc(edited, playerTable + 36, playerEnd - 4));
        put32(edited, checksum, crc(edited, checksumStart, edited.size(), true));
    }
    for (qsizetype i = 0; i < 4; i++) {
        allowed.insert(checksum + i);
        allowed.insert(playerEnd - 4 + i);
    }
    for (qsizetype i = 0; i < data.size(); i++)
        check(data[i] == edited[i] || allowed.contains(i), "Change outside allowed fields");
    for (const auto &p : players)
        for (auto it = labels.begin(); it != labels.end(); ++it) {
            auto f = fields[it.key()];
            int expected = patches.value(p.id).value(it.key(), p.attributes[it.key()]);
            check(qint64(bits(edited, p.offset, f.bit, f.depth)) + f.low == expected,
                  "Attribute verification failed");
        }
    auto output = files;
    output["DATA"] = edited;
    zip(path, output);
    return changes;
}
