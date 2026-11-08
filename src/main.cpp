#include "career.h"
#include <QtConcurrent>
#include <QtWidgets>
#include <memory>

class Window : public QMainWindow {
    std::unique_ptr<Career> career;
    QMap<int, QMap<QString, int>> patches;
    QComboBox *players = new QComboBox;
    QLineEdit *search = new QLineEdit;
    QLabel *status = new QLabel("Abre una carrera descifrada para comenzar.");
    QLabel *identity = new QLabel;
    QTableWidget *changes = new QTableWidget(0, 4);
    QMap<QString, QSpinBox *> values;
    QMap<QString, QSlider *> sliders;
    QMap<QString, QLabel *> originals;
    QPushButton *open = new QPushButton("Abrir ZIP descifrado"),
                *exportZip = new QPushButton("Exportar ZIP editado"),
                *reset = new QPushButton("Restaurar jugador"),
                *proposal = new QPushButton("Exportar propuesta JSON"),
                *importProposal = new QPushButton("Importar propuesta JSON");
    int current = -1;
    bool loading = false;
    const Player *selected() const {
        if (!career)
            return nullptr;
        for (const auto &p : career->players)
            if (p.id == current)
                return &p;
        return nullptr;
    }
    void error(const std::exception &e) {
        QMessageBox::warning(this, "No se pudo completar", QString::fromUtf8(e.what()));
    }
    void updateSummary() {
        changes->setRowCount(0);
        int n = 0;
        if (career)
            for (auto pi = patches.begin(); pi != patches.end(); ++pi) {
                const Player *p = nullptr;
                for (const auto &v : career->players)
                    if (v.id == pi.key()) {
                        p = &v;
                        break;
                    }
                if (!p)
                    continue;
                for (auto it = pi.value().begin(); it != pi.value().end(); ++it) {
                    if (it.value() == p->attributes.value(it.key()))
                        continue;
                    changes->insertRow(n);
                    QStringList cells = {p->name, Career::labels.value(it.key()),
                                         QString::number(p->attributes.value(it.key())),
                                         QString::number(it.value())};
                    for (int k = 0; k < 4; k++)
                        changes->setItem(n, k, new QTableWidgetItem(cells[k]));
                    n++;
                }
            }
        status->setText(career ? QString("%1 cambios · 3 bases verificadas · CRC de tabla y global "
                                         "se actualizarán al exportar")
                                     .arg(n)
                               : "Abre una carrera descifrada para comenzar.");
        exportZip->setEnabled(career != nullptr);
        proposal->setEnabled(career != nullptr);
        importProposal->setEnabled(career != nullptr);
    }
    void selectPlayer() {
        if (!career || players->currentIndex() < 0)
            return;
        current = players->currentData().toInt();
        auto p = selected();
        if (!p)
            return;
        loading = true;
        identity->setText(QString("%1 · %2 · ID %3 · Media original %4 · Potencial %5")
                              .arg(p->name, p->team)
                              .arg(p->id)
                              .arg(p->overall)
                              .arg(p->potential));
        for (auto it = values.begin(); it != values.end(); ++it) {
            int v = patches.value(current).value(it.key(), p->attributes.value(it.key()));
            it.value()->setValue(v);
            sliders[it.key()]->setValue(v);
            originals[it.key()]->setText(
                QString("Original: %1").arg(p->attributes.value(it.key())));
        }
        loading = false;
        reset->setEnabled(true);
    }
    void filter() {
        if (!career)
            return;
        int previous = current;
        players->blockSignals(true);
        players->clear();
        auto needle = search->text();
        for (const auto &p : career->players) {
            auto label = QString("%1 · #%2 · %3").arg(p.name).arg(p.id).arg(p.team);
            if (needle.isEmpty() || label.contains(needle, Qt::CaseInsensitive))
                players->addItem(label, p.id);
        }
        int idx = players->findData(previous);
        if (idx < 0)
            idx = players->findData(career->primary);
        players->setCurrentIndex(idx < 0 ? 0 : idx);
        players->blockSignals(false);
        selectPlayer();
    }
    void load() {
        auto path = QFileDialog::getOpenFileName(this, "Abrir guardado descifrado", {},
                                                 "Guardado ZIP (*.zip)");
        if (path.isEmpty())
            return;
        setEnabled(false);
        status->setText("Leyendo y verificando los CRC…");
        auto watcher = new QFutureWatcher<Career>(this);
        connect(watcher, &QFutureWatcher<Career>::finished, this, [this, watcher]() {
            setEnabled(true);
            try {
                auto result = watcher->result();
                career = std::make_unique<Career>(std::move(result));
                patches.clear();
                search->clear();
                current = career->primary;
                filter();
                updateSummary();
            } catch (const std::exception &e) {
                error(e);
                updateSummary();
            } catch (...) {
                QMessageBox::warning(this, "Error", "No se pudo leer el archivo.");
                updateSummary();
            }
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([path]() { return Career::load(path); }));
    }
    void save() {
        if (!career)
            return;
        auto path = QFileDialog::getSaveFileName(this, "Exportar copia descifrada",
                                                 career->name + "-edited.zip", "ZIP (*.zip)");
        if (path.isEmpty())
            return;
        if (!path.endsWith(".zip", Qt::CaseInsensitive))
            path += ".zip";
        try {
            auto edits = career->save(path, patches);
            QMessageBox::information(
                this, "ZIP exportado",
                QString("%1 atributos modificados.\nCRC de tabla y CRC global "
                        "actualizados.\n\nEste ZIP está descifrado. Cífralo como guardado PS4 "
                        "antes de copiarlo a la consola. La media no se recalcula.")
                    .arg(edits.size()));
        } catch (const std::exception &e) {
            error(e);
        }
    }
    void saveProposal() {
        if (!career)
            return;
        auto path = QFileDialog::getSaveFileName(this, "Guardar propuesta", "fifa18-proposal.json",
                                                 "JSON (*.json)");
        if (path.isEmpty())
            return;
        QJsonObject edits;
        for (auto pi = patches.begin(); pi != patches.end(); ++pi) {
            QJsonObject attrs;
            for (auto it = pi.value().begin(); it != pi.value().end(); ++it)
                attrs[it.key()] = it.value();
            edits[QString::number(pi.key())] = attrs;
        }
        QJsonObject obj{
            {"schema", "fifa18-desktop-proposal-v1"},
            {"sourceSha256",
             QString::fromLatin1(
                 QCryptographicHash::hash(career->data, QCryptographicHash::Sha256).toHex())},
            {"patches", edits}};
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(obj).toJson()) > 0 &&
            file.commit())
            status->setText("Propuesta JSON guardada.");
        else
            QMessageBox::warning(this, "Error", "No se pudo guardar la propuesta.");
    }
    void readProposal() {
        if (!career)
            return;
        auto path = QFileDialog::getOpenFileName(this, "Importar propuesta", {}, "JSON (*.json)");
        if (path.isEmpty())
            return;
        try {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
                throw std::runtime_error("JSON inaccesible o demasiado grande");
            QJsonParseError e;
            auto doc = QJsonDocument::fromJson(f.readAll(), &e);
            auto o = doc.object();
            auto hash = QString::fromLatin1(
                QCryptographicHash::hash(career->data, QCryptographicHash::Sha256).toHex());
            if (e.error != QJsonParseError::NoError ||
                o["schema"] != "fifa18-desktop-proposal-v1" || o["sourceSha256"] != hash ||
                !o["patches"].isObject())
                throw std::runtime_error("Propuesta inválida o de otro guardado");
            QMap<int, QMap<QString, int>> pending;
            auto edits = o["patches"].toObject();
            for (auto pi = edits.begin(); pi != edits.end(); ++pi) {
                bool ok = false;
                int id = pi.key().toInt(&ok);
                bool exists = false;
                for (const auto &p : career->players)
                    if (p.id == id)
                        exists = true;
                if (!ok || !exists || !pi.value().isObject())
                    throw std::runtime_error("ID de jugador inválido");
                auto a = pi.value().toObject();
                for (auto it = a.begin(); it != a.end(); ++it) {
                    double v = it.value().toDouble(-1);
                    if (!Career::labels.contains(it.key()) || !it.value().isDouble() ||
                        v != int(v) || v < 1 || v > 99)
                        throw std::runtime_error("Atributo inválido");
                    pending[id][it.key()] = int(v);
                }
            }
            patches = pending;
            selectPlayer();
            updateSummary();
        } catch (const std::exception &e) {
            error(e);
        }
    }

  public:
    Window() {
        setWindowTitle("FIFA 18 Save Editor · Desktop");
        resize(1120, 850);
        auto host = new QWidget;
        setCentralWidget(host);
        auto layout = new QVBoxLayout(host);
        auto title = new QLabel("FIFA 18 · Editor de carrera");
        title->setStyleSheet("font-size:26px;font-weight:700");
        layout->addWidget(title);
        auto info = new QLabel("Abre un ZIP descifrado de PS4. Los archivos originales se "
                               "conservan. Exporta y vuelve a cifrar para jugar en PS4 o PS5.");
        info->setWordWrap(true);
        layout->addWidget(info);
        auto toolbar = new QHBoxLayout;
        for (auto b : {open, importProposal, proposal, reset, exportZip})
            toolbar->addWidget(b);
        layout->addLayout(toolbar);
        search->setPlaceholderText("Buscar por nombre editado, ID o equipo (los nombres de "
                                   "jugadores estándar pueden aparecer como ID)");
        layout->addWidget(search);
        layout->addWidget(players);
        layout->addWidget(identity);
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        auto panel = new QWidget;
        auto grid = new QGridLayout(panel);
        int row = 0;
        for (auto it = Career::labels.begin(); it != Career::labels.end(); ++it) {
            QString key = it.key();
            auto label = new QLabel(it.value());
            auto slider = new QSlider(Qt::Horizontal);
            slider->setRange(1, 99);
            auto number = new QSpinBox;
            number->setRange(1, 99);
            auto baseline = new QLabel("Original: —");
            values[key] = number;
            sliders[key] = slider;
            originals[key] = baseline;
            grid->addWidget(label, row, 0);
            grid->addWidget(slider, row, 1);
            grid->addWidget(number, row, 2);
            grid->addWidget(baseline, row, 3);
            grid->setColumnStretch(1, 1);
            connect(slider, &QSlider::valueChanged, number, &QSpinBox::setValue);
            connect(number, &QSpinBox::valueChanged, slider, &QSlider::setValue);
            connect(number, &QSpinBox::valueChanged, this, [this, key](int v) {
                if (loading || !selected())
                    return;
                auto p = selected();
                if (v == p->attributes[key]) {
                    patches[current].remove(key);
                    if (patches[current].isEmpty())
                        patches.remove(current);
                } else
                    patches[current][key] = v;
                updateSummary();
            });
            row++;
        }
        scroll->setWidget(panel);
        layout->addWidget(scroll, 3);
        changes->setHorizontalHeaderLabels({"Jugador", "Atributo", "Original", "Nuevo"});
        changes->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        changes->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(changes, 1);
        layout->addWidget(status);
        reset->setEnabled(false);
        updateSummary();
        connect(open, &QPushButton::clicked, this, [this]() { load(); });
        connect(exportZip, &QPushButton::clicked, this, [this]() { save(); });
        connect(proposal, &QPushButton::clicked, this, [this]() { saveProposal(); });
        connect(importProposal, &QPushButton::clicked, this, [this]() { readProposal(); });
        connect(reset, &QPushButton::clicked, this, [this]() {
            patches.remove(current);
            selectPlayer();
            updateSummary();
        });
        connect(players, &QComboBox::currentIndexChanged, this, [this]() { selectPlayer(); });
        connect(search, &QLineEdit::textChanged, this, [this]() { filter(); });
        setStyleSheet("QMainWindow{background:#eef2f6}QPushButton{padding:8px}QLabel{padding:3px}"
                      "QTableWidget{background:white}");
    }

  protected:
    void closeEvent(QCloseEvent *event) override {
        if (!patches.isEmpty() &&
            QMessageBox::question(this, "Cerrar",
                                  "Las propuestas solo se conservan si las exportas. ¿Cerrar?") !=
                QMessageBox::Yes)
            event->ignore();
        else
            event->accept();
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("FIFA18SaveEditor");
    app.setOrganizationName("FIFA18SaveEditor");
    auto args = app.arguments();
    if (args.size() >= 3 && args[1] == "--verify") {
        try {
            auto c = Career::load(args[2]);
            QTextStream(stdout) << "OK: " << c.players.size() << " players, primary " << c.primary
                                << Qt::endl;
            if (args.size() == 4)
                c.save(args[3], {});
            return 0;
        } catch (const std::exception &e) {
            QTextStream(stderr) << e.what() << Qt::endl;
            return 1;
        }
    }
    if (args.size() == 5 && args[1] == "--apply") {
        try {
            auto c = Career::load(args[2]);
            QFile f(args[3]);
            if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
                throw std::runtime_error("Cannot read proposal");
            QJsonParseError error;
            auto o = QJsonDocument::fromJson(f.readAll(), &error).object();
            if (error.error != QJsonParseError::NoError ||
                o["schema"] != "fifa18-desktop-proposal-v1" ||
                o["sourceSha256"].toString() !=
                    QString::fromLatin1(
                        QCryptographicHash::hash(c.data, QCryptographicHash::Sha256).toHex()) ||
                !o["patches"].isObject())
                throw std::runtime_error("Invalid proposal");
            QMap<int, QMap<QString, int>> patches;
            auto edits = o["patches"].toObject();
            for (auto pi = edits.begin(); pi != edits.end(); ++pi) {
                bool ok = false;
                int id = pi.key().toInt(&ok);
                if (!ok || !pi.value().isObject())
                    throw std::runtime_error("Invalid player");
                auto attrs = pi.value().toObject();
                for (auto it = attrs.begin(); it != attrs.end(); ++it) {
                    double v = it.value().toDouble(-1);
                    if (!it.value().isDouble() || v < 1 || v > 99 || v != int(v))
                        throw std::runtime_error("Invalid attribute");
                    patches[id][it.key()] = int(v);
                }
            }
            auto changes = c.save(args[4], patches);
            QTextStream(stdout) << changes.size() << " attributes changed" << Qt::endl;
            return 0;
        } catch (const std::exception &e) {
            QTextStream(stderr) << e.what() << Qt::endl;
            return 1;
        }
    }
    Window window;
    window.show();
    return app.exec();
}
