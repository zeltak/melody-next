// SPDX-License-Identifier: GPL-3.0-only
#include "bench/output_icons.hpp"

#include <QFile>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

#include <algorithm>

namespace trackknife::bench {

const std::vector<OutputIconChoice>& outputIconChoices() {
    static const std::vector<OutputIconChoice> choices{
        {QStringLiteral("speaker"), QObject::tr("Speaker")},
        {QStringLiteral("headphones"), QObject::tr("Headphones")},
        {QStringLiteral("tv"), QObject::tr("TV")},
        {QStringLiteral("computer"), QObject::tr("Computer")},
        {QStringLiteral("laptop"), QObject::tr("Laptop")},
        {QStringLiteral("phone"), QObject::tr("Phone")},
        {QStringLiteral("car"), QObject::tr("Car")},
        {QStringLiteral("radio"), QObject::tr("Radio")},
        {QStringLiteral("living-room"), QObject::tr("Living room")},
        {QStringLiteral("kitchen"), QObject::tr("Kitchen")},
        {QStringLiteral("bath"), QObject::tr("Bathroom")},
        {QStringLiteral("bedroom"), QObject::tr("Bedroom")},
        {QStringLiteral("office"), QObject::tr("Office")},
        {QStringLiteral("garden"), QObject::tr("Garden")},
        {QStringLiteral("music"), QObject::tr("Music")},
    };
    return choices;
}

QString outputIconName(const std::string_view chosen, const bool local) {
    const auto name = QString::fromUtf8(chosen.data(), static_cast<qsizetype>(chosen.size()));
    const auto& choices = outputIconChoices();
    if (std::ranges::any_of(choices, [&name](const auto& choice) { return choice.name == name; })) {
        return name;
    }
    return local ? QStringLiteral("computer") : QStringLiteral("speaker");
}

QIcon outputIcon(const QString& name, const QPalette& palette) {
    QFile file{QStringLiteral(":/melody/outputs/%1.svg").arg(name)};
    if (name.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    // Lucide draws in currentColor, which Qt's renderer does not resolve.
    auto svg = file.readAll();
    svg.replace("currentColor", palette.color(QPalette::WindowText).name().toLatin1());
    QSvgRenderer renderer{svg};
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64}) {
        QPixmap pixmap{size, size};
        pixmap.fill(Qt::transparent);
        QPainter painter{&pixmap};
        renderer.render(&painter);
        painter.end();
        icon.addPixmap(pixmap);
    }
    return icon;
}

} // namespace trackknife::bench
