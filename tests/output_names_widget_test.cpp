// SPDX-License-Identifier: GPL-3.0-only

// ADR-0361: Settings' outputs, against a real engine with two outputs --
// moved up and down, dragged into order, given an icon -- and the engine
// keeping each as the widget says it.

#include "bench/output_icons.hpp"
#include "bench/output_names_widget.hpp"
#include "trackknife/core/stable_id.hpp"
#include "trackknife/engine/outputs.hpp"
#include "trackknife/engine/player.hpp"
#include "trackknife/engine/server.hpp"
#include "trackknife/engine/workspace.hpp"
#include "workspace/output_names_session.hpp"

#include <QComboBox>
#include <QPalette>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

#include <filesystem>
#include <string>
#include <vector>

namespace trackknife::bench {

class OutputNamesWidgetTest final : public QObject {
    Q_OBJECT

  private slots:
    void outputsAreMovedAndGivenIcons();
    void everyIconIsBuiltIn();
};

namespace {

[[nodiscard]] QStringList rows(const QTreeWidget* view) {
    QStringList ids;
    for (int index = 0; index < view->topLevelItemCount(); ++index) {
        ids << view->topLevelItem(index)->data(OutputNamesWidget::shown, Qt::UserRole).toString();
    }
    return ids;
}

[[nodiscard]] QStringList engineOrder(const engine::Outputs& outputs) {
    QStringList ids;
    for (const auto& output : outputs.visible(true)) {
        ids << QString::fromStdString(output.id);
    }
    return ids;
}

} // namespace

void OutputNamesWidgetTest::everyIconIsBuiltIn() {
    // Every name the engine accepts is drawn, and Melody offers each.
    QCOMPARE(outputIconChoices().size(), engine::Outputs::icons.size());
    for (const auto name : engine::Outputs::icons) {
        const auto icon = outputIcon(
            QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())), QPalette{});
        QVERIFY2(!icon.isNull(), name.data());
        QVERIFY(!icon.pixmap(24).isNull());
    }
    QCOMPARE(outputIconName({}, true), QStringLiteral("computer"));
    QCOMPARE(outputIconName({}, false), QStringLiteral("speaker"));
    QCOMPARE(outputIconName("bath", false), QStringLiteral("bath"));
    QCOMPARE(outputIconName("spaceship", false), QStringLiteral("speaker"));
}

void OutputNamesWidgetTest::outputsAreMovedAndGivenIcons() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto workspace = engine::Workspace::open(std::filesystem::path{directory.path().toStdString()} /
                                             "lists.sqlite");
    QVERIFY(workspace.has_value());
    auto player = engine::Player::create_without_audio();
    protocol::Dispatcher dispatcher;
    auto server = engine::Server::listen_tcp("127.0.0.1", 0, dispatcher, "token");
    QVERIFY(server.has_value());
    engine::Outputs outputs{*player, {}, &*workspace, (*server)->sink()};
    engine::register_output_methods(dispatcher, outputs);
    // Two agents known by name, as after a restart.
    for (const auto* chosen : {"agent:kitchen", "agent:porch"}) {
        QVERIFY(
            workspace->save_engine_state(engine::Outputs::selection_key, chosen, 1).has_value());
        outputs.restore();
    }
    (*server)->start();

    OutputNamesSession session{protocol::Endpoint{
        .socket = {}, .host = "127.0.0.1", .port = (*server)->port(), .token = "token"}};
    OutputNamesWidget widget{session};
    widget.show();
    auto* view = widget.view();
    QTRY_COMPARE_WITH_TIMEOUT(view->topLevelItemCount(), 2, 10'000);
    QTRY_VERIFY(view->isEnabled());
    QCOMPARE(rows(view),
             (QStringList{QStringLiteral("agent:kitchen"), QStringLiteral("agent:porch")}));

    // Moved down: the engine lists it second, and so does the widget.
    view->setCurrentItem(view->topLevelItem(0));
    auto* down = widget.findChild<QPushButton*>(QStringLiteral("bench-output-names-down"));
    auto* up = widget.findChild<QPushButton*>(QStringLiteral("bench-output-names-up"));
    QVERIFY(down->isEnabled() && !up->isEnabled());
    down->click();
    const QStringList porch_first{QStringLiteral("agent:porch"), QStringLiteral("agent:kitchen")};
    QTRY_COMPARE_WITH_TIMEOUT(engineOrder(outputs), porch_first, 10'000);
    QTRY_VERIFY(view->isEnabled());
    QCOMPARE(rows(view), porch_first);
    // The moved one stays chosen, so it can be moved again.
    QCOMPARE(view->currentItem()->data(OutputNamesWidget::shown, Qt::UserRole).toString(),
             QStringLiteral("agent:kitchen"));
    up->click();
    const QStringList kitchen_first{QStringLiteral("agent:kitchen"), QStringLiteral("agent:porch")};
    QTRY_COMPARE_WITH_TIMEOUT(engineOrder(outputs), kitchen_first, 10'000);
    QTRY_VERIFY(view->isEnabled());

    // A drag takes the row out and puts it back where it is dropped.
    auto* dragged = view->takeTopLevelItem(1);
    view->insertTopLevelItem(0, dragged);
    QTRY_COMPARE_WITH_TIMEOUT(engineOrder(outputs), porch_first, 10'000);
    QTRY_VERIFY(view->isEnabled());
    QCOMPARE(rows(view), porch_first);

    // Given an icon from its picker.
    auto* picker =
        qobject_cast<QComboBox*>(view->itemWidget(view->topLevelItem(0), OutputNamesWidget::icon));
    QVERIFY(picker != nullptr);
    const auto bath = picker->findData(QStringLiteral("bath"));
    picker->setCurrentIndex(bath);
    emit picker->activated(bath);
    QTRY_VERIFY_WITH_TIMEOUT(outputs.visible(true).front().icon == "bath", 10'000);
    QTRY_VERIFY(view->isEnabled());
    picker =
        qobject_cast<QComboBox*>(view->itemWidget(view->topLevelItem(0), OutputNamesWidget::icon));
    QCOMPARE(picker->currentData().toString(), QStringLiteral("bath"));
    // Neither was hidden: both are ticked.
    for (int index = 0; index < view->topLevelItemCount(); ++index) {
        QCOMPARE(view->topLevelItem(index)->checkState(OutputNamesWidget::shown), Qt::Checked);
    }
    (*server)->stop();
}

} // namespace trackknife::bench

QTEST_MAIN(trackknife::bench::OutputNamesWidgetTest)
#include "output_names_widget_test.moc"
