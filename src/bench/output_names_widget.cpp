// SPDX-License-Identifier: GPL-3.0-only
#include "bench/output_names_widget.hpp"

#include "workspace/output_names_session.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace trackknife::bench {

OutputNamesWidget::OutputNamesWidget(OutputNamesSession& session, QWidget* parent)
    : QWidget(parent), session_(session), view_(new QTreeWidget(this)),
      refresh_(new QPushButton(tr("Refresh"), this)), note_(new QLabel(this)),
      status_(new QLabel(this)) {
    setObjectName(QStringLiteral("bench-output-names"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    view_->setObjectName(QStringLiteral("bench-output-names-view"));
    view_->setAccessibleName(tr("Outputs"));
    view_->setRootIsDecorated(false);
    view_->setUniformRowHeights(true);
    view_->setHeaderLabels({tr("Show"), tr("Name"), tr("Found as")});
    view_->header()->setStretchLastSection(true);
    view_->header()->setSectionResizeMode(shown, QHeaderView::ResizeToContents);
    view_->setColumnWidth(name, 220);
    // Only the name is edited, whichever column is clicked.
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setMaximumHeight(170);
    layout->addWidget(view_);

    note_->setObjectName(QStringLiteral("bench-output-names-note"));
    note_->setWordWrap(true);
    note_->setForegroundRole(QPalette::PlaceholderText);
    note_->setText(tr("Untick an output to keep it out of Melody's outputs, on every computer and "
                      "phone; the one playing is always shown. Double-click a name to change "
                      "it; an empty name gives the output its own back."));
    layout->addWidget(note_);

    auto* row = new QHBoxLayout;
    status_->setObjectName(QStringLiteral("bench-output-names-status"));
    status_->setWordWrap(true);
    refresh_->setObjectName(QStringLiteral("bench-output-names-refresh"));
    refresh_->setToolTip(tr("Ask the engine again: a speaker found since shows"));
    row->addWidget(status_, 1);
    row->addWidget(refresh_);
    layout->addLayout(row);

    connect(view_, &QTreeWidget::itemChanged, this, &OutputNamesWidget::edited);
    connect(view_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) { view_->editItem(item, name); });
    connect(refresh_, &QPushButton::clicked, &session_, &OutputNamesSession::load);
    connect(&session_, &OutputNamesSession::changed, this, &OutputNamesWidget::sync);
    sync();
    if (!session_.known()) {
        session_.load();
    }
}

void OutputNamesWidget::sync() {
    filling_ = true;
    view_->clear();
    for (const auto& output : session_.outputs()) {
        auto* item = new QTreeWidgetItem(view_);
        item->setData(shown, Qt::UserRole, output.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
                       Qt::ItemIsEditable);
        item->setCheckState(shown, output.hidden ? Qt::Unchecked : Qt::Checked);
        item->setText(name, output.name);
        auto found = output.kind + QStringLiteral(" · ") + output.reported;
        if (!output.online) {
            found += tr(" · offline");
        }
        if (output.selected) {
            found += tr(" · playing");
        }
        item->setText(found_as, found);
        item->setToolTip(name, output.renamed ? tr("Its own name: %1").arg(output.reported)
                                              : tr("Double-click to give it a name of your own"));
    }
    const bool usable = session_.known() && !session_.busy();
    view_->setEnabled(usable);
    refresh_->setEnabled(!session_.busy());
    note_->setVisible(session_.known());
    status_->setText(session_.status());
    status_->setVisible(!session_.status().isEmpty());
    filling_ = false;
}

void OutputNamesWidget::edited(QTreeWidgetItem* item, const int column) {
    if (filling_ || item == nullptr) {
        return;
    }
    const auto id = item->data(shown, Qt::UserRole).toString();
    for (const auto& output : session_.outputs()) {
        if (output.id != id) {
            continue;
        }
        if (column == shown) {
            const bool on = item->checkState(shown) == Qt::Checked;
            if (on == output.hidden) {
                session_.setShown(id, on);
            }
        } else if (column == name) {
            const auto typed = item->text(name).trimmed();
            // Its own name typed back is no name of the user's.
            const auto given = typed == output.reported ? QString{} : typed;
            if (typed != output.name) {
                session_.rename(id, given);
            }
        }
        return;
    }
}

} // namespace trackknife::bench
