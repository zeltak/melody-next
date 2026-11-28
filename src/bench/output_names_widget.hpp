// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace trackknife::bench {

class OutputNamesSession;

// ADR-0361: in Settings › Engines, the chosen engine's outputs, each shown
// in Melody or not, and named as the user names it. A change is the
// engine's at once.
class OutputNamesWidget final : public QWidget {
    Q_OBJECT
  public:
    enum Column : int { shown = 0, name = 1, found_as = 2 };

    explicit OutputNamesWidget(OutputNamesSession& session, QWidget* parent = nullptr);

    [[nodiscard]] QTreeWidget* view() const { return view_; }

  private:
    void sync();
    void edited(QTreeWidgetItem* item, int column);

    OutputNamesSession& session_;
    QTreeWidget* view_;
    QPushButton* refresh_;
    QLabel* note_;
    QLabel* status_;
    // While the rows are being filled: what changes is not the user's.
    bool filling_{false};
};

} // namespace trackknife::bench
