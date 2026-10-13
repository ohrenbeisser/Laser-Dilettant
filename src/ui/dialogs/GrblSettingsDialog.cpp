#include "ui/dialogs/GrblSettingsDialog.h"

#include "device/GrblCodes.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

GrblSettingsDialog::GrblSettingsDialog(const GrblSettings& current, const GrblSettings& backup, QWidget* parent)
    : QDialog(parent)
    , m_table(new QTableWidget(this))
{
    setWindowTitle(tr("Restore GRBL settings"));
    resize(760, 520);

    for (auto it = backup.values().cbegin(); it != backup.values().cend(); ++it) {
        if (!current.has(it.key())) {
            ++m_ignored;
            continue;
        }
        bool okA = false, okB = false;
        const double a = current.raw(it.key()).toDouble(&okA);
        const double b = it.value().toDouble(&okB);
        const bool same = (okA && okB) ? qAbs(a - b) < 1e-6 : current.raw(it.key()) == it.value();
        if (!same)
            m_rows.append({ it.key(), it.value() });
    }

    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({ tr("Setting"), tr("Meaning"), tr("Device now"), tr("Backup") });
    m_table->setRowCount(static_cast<int>(m_rows.size()));
    m_table->verticalHeader()->hide();
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int r = 0; r < m_rows.size(); ++r) {
        const int n = m_rows[r].first;
        auto* item = new QTableWidgetItem(QStringLiteral("$%1").arg(n));
        item->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        item->setCheckState(Qt::Checked);
        m_table->setItem(r, 0, item);
        m_table->setItem(r, 1, new QTableWidgetItem(GrblCodes::hasSetting(n) ? GrblCodes::setting(n).name : tr("unknown")));
        m_table->setItem(r, 2, new QTableWidgetItem(current.raw(n)));
        auto* nv = new QTableWidgetItem(m_rows[r].second);
        QFont f = nv->font();
        f.setBold(true);
        nv->setFont(f);
        m_table->setItem(r, 3, nv);
    }
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->resizeColumnsToContents();

    QString info;
    if (m_rows.isEmpty())
        info = tr("The backup matches the device. Nothing to restore.");
    else
        info = tr("%n setting(s) differ. Checked values are written to the device.", nullptr, int(m_rows.size()));
    if (m_ignored > 0)
        info += QLatin1Char(' ') + tr("%n value(s) from the backup are unknown to this device and are skipped.", nullptr, m_ignored);
    auto* label = new QLabel(info, this);
    label->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Write selected"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_rows.isEmpty());
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* l = new QVBoxLayout(this);
    l->addWidget(label);
    l->addWidget(m_table, 1);
    l->addWidget(buttons);
}

QList<QPair<int, QString>> GrblSettingsDialog::selectedChanges() const
{
    QList<QPair<int, QString>> out;
    for (int r = 0; r < m_rows.size(); ++r)
        if (m_table->item(r, 0)->checkState() == Qt::Checked)
            out.append(m_rows[r]);
    return out;
}
