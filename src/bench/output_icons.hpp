// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QIcon>
#include <QString>

#include <string_view>
#include <vector>

class QPalette;

namespace trackknife::bench {

// ADR-0361: the icons an output can be given, as the protocol names them,
// with what each is called in Melody. Drawn from Lucide icons built into
// the app, the same on Linux and the Mac.
struct OutputIconChoice final {
    QString name;
    QString label;
};
[[nodiscard]] const std::vector<OutputIconChoice>& outputIconChoices();

// The icon an output shows: the one chosen for it, or else a computer for
// an engine's own audio and a speaker for the rest.
[[nodiscard]] QString outputIconName(std::string_view chosen, bool local);

// Drawn in the palette's text colour. Empty for a name Melody does not know.
[[nodiscard]] QIcon outputIcon(const QString& name, const QPalette& palette);

} // namespace trackknife::bench
