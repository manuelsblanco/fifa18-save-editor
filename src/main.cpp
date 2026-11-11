#include "career.h"
#include <QtConcurrent>
#include <QtWidgets>
#include <memory>

class Window : public QMainWindow {
    std::unique_ptr<Career> career;
    QMap<int, QMap<QString, int>> patches;
    QComboBox *players = new QComboBox;
    QLineEdit *search = new QLineEdit;
    QLabel *status = new QLabel("Open a decrypted career save to get started.");
    QLabel *identity = new QLabel;
    QTableWidget *changes = new QTableWidget(0, 4);
    QMap<QString, QSpinBox *> values;
    QMap<QString, QSlider *> sliders;
    QMap<QString, QLabel *> originals;
    QPushButton *open = new QPushButton("Open decrypted ZIP"),
                *exportZip = new QPushButton("Export edited ZIP"),
                *reset = new QPushButton("Reset player"),
                *proposal = new QPushButton("Export changes JSON"),
                *importProposal = new QPushButton("Import changes JSON");
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
        QMessageBox::warning(this, "Unable to complete action", QString::fromUtf8(e.what()));
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
        status->setText(career ? QString("%1 changes · 3 databases verified · Table and career checksums "
                                         "will be updated on export")
                                     .arg(n)
                               : "Open a decrypted career save to get started.");
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
        identity->setText(QString("%1 · %2 · ID %3 · Original overall %4 · Potential %5")
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
        auto path = QFileDialog::getOpenFileName(this, "Open decrypted save", {},
                                                 "ZIP save (*.zip)");
        if (path.isEmpty())
            return;
        setEnabled(false);
        status->setText("Reading save and verifying checksums…");
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
                QMessageBox::warning(this, "Error", "Unable to read the file.");
                updateSummary();
            }
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([path]() { return Career::load(path); }));
    }
    void save() {
        if (!career)
            return;
        auto path = QFileDialog::getSaveFileName(this, "Export decrypted copy",
                                                 career->name + "-edited.zip", "ZIP (*.zip)");
        if (path.isEmpty())
            return;
        if (!path.endsWith(".zip", Qt::CaseInsensitive))
            path += ".zip";
        try {
            auto edits = career->save(path, patches);
            QMessageBox::information(
                this, "ZIP exported",
                QString("%1 attributes changed.\nTable and career checksums "
                        "updated.\n\nThis ZIP is decrypted. Encrypt it as a PS4 save "
                        "before copying it to your console. Overall rating is not recalculated.")
                    .arg(edits.size()));
        } catch (const std::exception &e) {
            error(e);
        }
    }
    void saveProposal() {
        if (!career)
            return;
        auto path = QFileDialog::getSaveFileName(this, "Save changes", "fifa18-proposal.json",
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
            status->setText("Changes saved as JSON.");
        else
            QMessageBox::warning(this, "Error", "Unable to save changes.");
    }
    void readProposal() {
        if (!career)
            return;
        auto path = QFileDialog::getOpenFileName(this, "Import changes", {}, "JSON (*.json)");
        if (path.isEmpty())
            return;
        try {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly) || f.size() > 1024 * 1024)
                throw std::runtime_error("Cannot read JSON or file is too large");
            QJsonParseError e;
            auto doc = QJsonDocument::fromJson(f.readAll(), &e);
            auto o = doc.object();
            auto hash = QString::fromLatin1(
                QCryptographicHash::hash(career->data, QCryptographicHash::Sha256).toHex());
            if (e.error != QJsonParseError::NoError ||
                o["schema"] != "fifa18-desktop-proposal-v1" || o["sourceSha256"] != hash ||
                !o["patches"].isObject())
                throw std::runtime_error("Invalid changes file or changes belong to another save");
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
                    throw std::runtime_error("Invalid player ID");
                auto a = pi.value().toObject();
                for (auto it = a.begin(); it != a.end(); ++it) {
                    double v = it.value().toDouble(-1);
                    if (!Career::labels.contains(it.key()) || !it.value().isDouble() ||
                        v != int(v) || v < 1 || v > 99)
                        throw std::runtime_error("Invalid attribute");
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
        setWindowTitle(QString("FIFA 18 Save Editor · %1").arg(APP_VERSION));
        resize(1120, 850);
        auto host = new QWidget;
        setCentralWidget(host);
        auto layout = new QVBoxLayout(host);
        layout->setContentsMargins(24, 20, 24, 20);
        layout->setSpacing(12);
        auto title = new QLabel("FIFA 18 · Career editor");
        title->setStyleSheet("font-size:26px;font-weight:700");
        layout->addWidget(title);
        auto info = new QLabel("Open a decrypted PS4 ZIP. Your original files are "
                               "preserved. Export and encrypt the edited save to play on PS4 or PS5.");
        info->setWordWrap(true);
        layout->addWidget(info);
        auto toolbar = new QHBoxLayout;
        for (auto b : {open, importProposal, proposal, reset, exportZip})
            toolbar->addWidget(b);
        layout->addLayout(toolbar);
        search->setPlaceholderText("Search by edited name, player ID or team (standard player "
                                   "names may appear as IDs)");
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
        changes->setHorizontalHeaderLabels({"Player", "Attribute", "Original", "New"});
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
        setStyleSheet(R"(
            QMainWindow { background: #edf2f7; }
            QLabel { color: #172b42; padding: 3px; }
            QPushButton { background: #ffffff; color: #172b42; border: 1px solid #728398;
                          border-radius: 6px; padding: 9px 12px; font-weight: 600; }
            QPushButton:hover { background: #e3edf9; border-color: #175cad; }
            QPushButton:pressed { background: #cbdff5; }
            QPushButton:disabled { background: #e4e9ef; color: #526174; border-color: #acb7c4; }
            QPushButton:focus, QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
                border: 2px solid #175cad;
            }
            QLineEdit, QComboBox, QSpinBox { background: #ffffff; color: #172b42;
                border: 1px solid #728398; border-radius: 5px; padding: 7px; }
            QLineEdit { placeholder-text-color: #526174; }
            QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled {
                background: #e4e9ef; color: #526174;
            }
            QComboBox QAbstractItemView { background: #ffffff; color: #172b42;
                selection-background-color: #175cad; selection-color: #ffffff; }
            QScrollArea { background: #ffffff; border: 1px solid #acb7c4; border-radius: 6px; }
            QScrollArea > QWidget > QWidget { background: #ffffff; }
            QTableWidget { background: #ffffff; alternate-background-color: #eef3f8;
                color: #172b42; gridline-color: #c5ced9; border: 1px solid #acb7c4;
                selection-background-color: #175cad; selection-color: #ffffff; }
            QHeaderView::section { background: #dde7f2; color: #172b42; padding: 8px;
                border: none; border-bottom: 1px solid #acb7c4; font-weight: 600; }
            QSlider::groove:horizontal { height: 6px; background: #c5ced9; border-radius: 3px; }
            QSlider::sub-page:horizontal { background: #175cad; border-radius: 3px; }
            QSlider::handle:horizontal { background: #175cad; border: 2px solid #ffffff;
                width: 16px; margin: -7px 0; border-radius: 9px; }
            QSlider::handle:horizontal:focus { border-color: #172b42; }
        )");
    }

  protected:
    void closeEvent(QCloseEvent *event) override {
        if (!patches.isEmpty() &&
            QMessageBox::question(this, "Close editor",
                                  "Your changes have not been applied to a save. Export them before closing to keep them. Close anyway?") !=
                QMessageBox::Yes)
            event->ignore();
        else
            event->accept();
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    auto font = app.font();
    font.setPointSize(10);
    app.setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#edf2f7"));
    palette.setColor(QPalette::WindowText, QColor("#172b42"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#eef3f8"));
    palette.setColor(QPalette::Text, QColor("#172b42"));
    palette.setColor(QPalette::Button, Qt::white);
    palette.setColor(QPalette::ButtonText, QColor("#172b42"));
    palette.setColor(QPalette::Highlight, QColor("#175cad"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor("#526174"));
    palette.setColor(QPalette::ToolTipBase, Qt::white);
    palette.setColor(QPalette::ToolTipText, QColor("#172b42"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#526174"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#526174"));
    app.setPalette(palette);
    app.setWindowIcon(QIcon(":/assets/fifa18-save-editor.png"));
    app.setApplicationVersion(APP_VERSION);
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
