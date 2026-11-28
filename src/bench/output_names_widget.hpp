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
// in Melody or not, named and given an icon as the user chooses, in the
// order the user drags them into. A change is the engine's at once.
class OutputNamesWidget final : public QWidget {
    Q_OBJECT
  public:
    enum Column : int { shown = 0, name = 1, icon = 2, found_as = 3 };

    explicit OutputNamesWidget(OutputNamesSession& session, QWidget* parent = nullptr);

    [[nodiscard]] QTreeWidget* view() const { return view_; }

  protected:
    void changeEvent(QEvent* event) override;

  private:
    void sync();
    void edited(QTreeWidgetItem* item, int column);
    // The rows' order, given to the engine.
    void arrange();
    void move(int by);

    OutputNamesSession& session_;
    QTreeWidget* view_;
    QPushButton* up_;
    QPushButton* down_;
    QPushButton* refresh_;
    QLabel* note_;
    QLabel* status_;
    // While the rows are being filled: what changes is not the user's.
    bool filling_{false};
    // A drag moved rows: their order goes to the engine once it is done.
    bool dragged_{false};
};

} // namespace trackknife::bench
