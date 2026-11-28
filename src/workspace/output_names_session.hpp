// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "trackknife/protocol/client.hpp"

#include <QObject>
#include <QString>

#include <optional>
#include <string>
#include <vector>

namespace trackknife::bench {

// ADR-0361: one engine's outputs -- its own audio, its agents, the UPnP
// speakers it found -- each with a name of the user's own and whether it
// is shown in Melody. Asked of the engine, and changed there at once: every
// client of that engine sees the change.
class OutputNamesSession final : public QObject {
    Q_OBJECT

  public:
    struct Output final {
        QString id;
        // As shown: the user's name, or else the output's own.
        QString name;
        // As the output calls itself.
        QString reported;
        bool renamed{false};
        bool hidden{false};
        // The icon chosen for it, or empty for Melody's own choice.
        std::string icon;
        bool local{false};
        bool online{false};
        bool selected{false};
        // What it is, in a word or two: "This engine", "Agent", "UPnP".
        QString kind;
    };

    explicit OutputNamesSession(protocol::Endpoint endpoint, QObject* parent = nullptr);

    [[nodiscard]] bool known() const { return known_; }
    [[nodiscard]] bool busy() const { return busy_; }
    [[nodiscard]] const std::vector<Output>& outputs() const { return outputs_; }
    // What to say below them: asking, why it cannot be changed.
    [[nodiscard]] QString status() const { return status_; }

    void load();
    // An empty name gives the output its own back.
    void rename(const QString& id, const QString& name);
    void setShown(const QString& id, bool shown);
    // One of the protocol's icon names; empty for Melody's own choice.
    void setIcon(const QString& id, const QString& icon);
    // ADR-0361: the outputs in this order, first to last.
    void arrange(const std::vector<QString>& ids);

  signals:
    void changed();

  private:
    // Sends a change to the engine, then asks it again.
    void send(const char* method, protocol::Json params);

    protocol::Endpoint endpoint_;
    bool known_{false};
    bool busy_{false};
    std::vector<Output> outputs_;
    QString status_;
};

// The outputs an outputs.list answer gives, as the session keeps them.
[[nodiscard]] std::vector<OutputNamesSession::Output> outputsFrom(const protocol::Json& answer);

} // namespace trackknife::bench
