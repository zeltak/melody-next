// SPDX-License-Identifier: GPL-3.0-only
#include "bench/output_names_widget.hpp"

#include "bench/output_icons.hpp"
#include "workspace/output_names_session.hpp"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace trackknife::bench {

OutputNamesWidget::OutputNamesWidget(OutputNamesSession& session, QWidget* parent)
    : QWidget(parent), session_(session), view_(new QTreeWidget(this)),
      up_(new QPushButton(tr("Move up"), this)), down_(new QPushButton(tr("Move down"), this)),
      refresh_(new QPushButton(tr("Refresh"), this)), note_(new QLabel(this)),
      status_(new QLabel(this)) {
    setObjectName(QStringLiteral("bench-output-names"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    view_->setObjectName(QStringLiteral("bench-output-names-view"));
    view_->setAccessibleName(tr("Outputs"));
    view_->setRootIsDecorated(false);
    view_->setUniformRowHeights(true);
    view_->setHeaderLabels({tr("Show"), tr("Name"), tr("Icon"), tr("Found as")});
    view_->header()->setStretchLastSection(true);
    view_->header()->setSectionResizeMode(shown, QHeaderView::ResizeToContents);
    view_->setColumnWidth(name, 200);
    view_->setColumnWidth(icon, 150);
    // Only the name is edited, whichever column is clicked.
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // ADR-0361: dragged into the order Melody lists them in.
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setDragDropMode(QAbstractItemView::InternalMove);
    view_->setDefaultDropAction(Qt::MoveAction);
    view_->setDragEnabled(true);
    view_->setAcceptDrops(true);
    view_->setDropIndicatorShown(true);
    view_->setMaximumHeight(200);
    layout->addWidget(view_);

    note_->setObjectName(QStringLiteral("bench-output-names-note"));
    note_->setWordWrap(true);
    note_->setForegroundRole(QPalette::PlaceholderText);
    note_->setText(tr("Drag outputs into the order Melody lists them in, on every computer and "
                      "phone. Untick one to keep it out of the list; the one playing is always "
                      "shown. Double-click a name to change it; an empty name gives the output "
                      "its own back."));
    layout->addWidget(note_);

    auto* row = new QHBoxLayout;
    status_->setObjectName(QStringLiteral("bench-output-names-status"));
    status_->setWordWrap(true);
    up_->setObjectName(QStringLiteral("bench-output-names-up"));
    down_->setObjectName(QStringLiteral("bench-output-names-down"));
    refresh_->setObjectName(QStringLiteral("bench-output-names-refresh"));
    refresh_->setToolTip(tr("Ask the engine again: a speaker found since shows"));
    row->addWidget(up_);
    row->addWidget(down_);
    row->addWidget(status_, 1);
    row->addWidget(refresh_);
    layout->addLayout(row);

    connect(view_, &QTreeWidget::itemChanged, this, &OutputNamesWidget::edited);
    connect(view_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) { view_->editItem(item, name); });
    // A drag takes the row out and puts it back where it is dropped: once
    // that is over, the order is the engine's.
    connect(view_->model(), &QAbstractItemModel::rowsInserted, this, [this] {
        if (filling_ || dragged_) {
            return;
        }
        dragged_ = true;
        QTimer::singleShot(0, this, [this] {
            dragged_ = false;
            arrange();
        });
    });
    connect(view_, &QTreeWidget::currentItemChanged, this, [this] {
        const auto row = view_->currentIndex().row();
        up_->setEnabled(view_->isEnabled() && row > 0);
        down_->setEnabled(view_->isEnabled() && row >= 0 && row + 1 < view_->topLevelItemCount());
    });
    connect(up_, &QPushButton::clicked, this, [this] { move(-1); });
    connect(down_, &QPushButton::clicked, this, [this] { move(1); });
    connect(refresh_, &QPushButton::clicked, &session_, &OutputNamesSession::load);
    connect(&session_, &OutputNamesSession::changed, this, &OutputNamesWidget::sync);
    sync();
    if (!session_.known()) {
        session_.load();
    }
}

void OutputNamesWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    // The icons are drawn in the palette's colours: a new palette, new icons.
    if (event->type() == QEvent::PaletteChange) {
        sync();
    }
}

void OutputNamesWidget::sync() {
    const auto current = view_->currentItem() != nullptr
                             ? view_->currentItem()->data(shown, Qt::UserRole).toString()
                             : QString{};
    filling_ = true;
    view_->clear();
    for (const auto& output : session_.outputs()) {
        auto* item = new QTreeWidgetItem(view_);
        item->setData(shown, Qt::UserRole, output.id);
        // Dragged, never dropped onto: outputs hold no others.
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable |
                       Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        item->setCheckState(shown, output.hidden ? Qt::Unchecked : Qt::Checked);
        item->setText(name, output.name);
        item->setIcon(name, outputIcon(outputIconName(output.icon, output.local), palette()));
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
        // ADR-0361: an icon of its own, or Melody's for what it is.
        auto* icons = new QComboBox(view_);
        icons->setObjectName(QStringLiteral("bench-output-names-icon"));
        icons->setAccessibleName(tr("Icon of %1").arg(output.name));
        icons->addItem(outputIcon(outputIconName({}, output.local), palette()), tr("Automatic"),
                       QString{});
        for (const auto& choice : outputIconChoices()) {
            icons->addItem(outputIcon(choice.name, palette()), choice.label, choice.name);
        }
        icons->setCurrentIndex(std::max(0, icons->findData(QString::fromStdString(output.icon))));
        connect(icons, &QComboBox::activated, this, [this, icons, id = output.id](const int) {
            session_.setIcon(id, icons->currentData().toString());
        });
        view_->setItemWidget(item, icon, icons);
        if (output.id == current) {
            view_->setCurrentItem(item);
        }
    }
    const bool usable = session_.known() && !session_.busy();
    view_->setEnabled(usable);
    const auto row = view_->currentIndex().row();
    up_->setEnabled(usable && row > 0);
    down_->setEnabled(usable && row >= 0 && row + 1 < view_->topLevelItemCount());
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

void OutputNamesWidget::arrange() {
    std::vector<QString> ids;
    for (int index = 0; index < view_->topLevelItemCount(); ++index) {
        ids.push_back(view_->topLevelItem(index)->data(shown, Qt::UserRole).toString());
    }
    std::vector<QString> before;
    for (const auto& output : session_.outputs()) {
        before.push_back(output.id);
    }
    if (ids != before) {
        session_.arrange(ids);
    }
}

void OutputNamesWidget::move(const int by) {
    const auto row = view_->currentIndex().row();
    const auto to = row + by;
    if (row < 0 || to < 0 || to >= view_->topLevelItemCount()) {
        return;
    }
    filling_ = true;
    auto* item = view_->takeTopLevelItem(row);
    view_->insertTopLevelItem(to, item);
    view_->setCurrentItem(item);
    filling_ = false;
    arrange();
}

} // namespace trackknife::bench
