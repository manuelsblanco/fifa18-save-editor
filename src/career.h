#pragma once
#include <QtCore>
#include <stdexcept>

struct Field {
    int type;
    int bit;
    int depth;
    int low;
};
struct Player {
    int id;
    QString name, team;
    int overall, potential;
    QMap<QString, int> attributes;
    qsizetype offset;
};
struct Edit {
    int player;
    QString field;
    int before, after;
};

class Career {
  public:
    static const QMap<QString, QString> labels;
    QMap<QString, QByteArray> files;
    QList<Player> players;
    QMap<QString, Field> fields;
    QString name, title, account, sourcePath;
    int primary = -1;
    QByteArray data;
    qsizetype checksum = 0, checksumStart = 0, playerTable = 0, playerEnd = 0;
    int recordSize = 0;
    static Career load(const QString &path);
    QList<Edit> save(const QString &path, const QMap<int, QMap<QString, int>> &patches) const;
    static QMap<QString, QByteArray> unzip(const QString &path);
    static void zip(const QString &path, const QMap<QString, QByteArray> &files);
    static quint32 crc(const QByteArray &data, qsizetype start, qsizetype end,
                       bool reflected = false);

  private:
    void parse();
};
