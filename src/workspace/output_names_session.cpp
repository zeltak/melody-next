// SPDX-License-Identifier: GPL-3.0-only
#include "workspace/output_names_session.hpp"

#include "bench/post_back.hpp"

#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <chrono>
#include <string>
#include <utility>

namespace trackknife::bench {
namespace {

struct Asked {
    std::optional<std::vector<OutputNamesSession::Output>> outputs;
    QString error;
};

[[nodiscard]] QString text(const protocol::Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string()
               ? QString::fromStdString(found->get<std::string>())
               : QString{};
}

[[nodiscard]] Asked ask(const protocol::Endpoint& endpoint) {
    auto client = protocol::Client::connect(endpoint);
    if (!client) {
        return {.outputs = std::nullopt, .error = QString::fromStdString(client.error().message)};
    }
    auto answer =
        (*client)->call("outputs.list", protocol::Json{{"all", true}}, std::chrono::seconds{5});
    (*client)->close();
    if (!answer) {
        return {.outputs = std::nullopt, .error = QString::fromStdString(answer.error().message)};
    }
    return {.outputs = outputsFrom(*answer), .error = {}};
}

} // namespace

std::vector<OutputNamesSession::Output> outputsFrom(const protocol::Json& answer) {
    std::vector<OutputNamesSession::Output> outputs;
    const auto listed = answer.find("outputs");
    if (listed == answer.end() || !listed->is_array()) {
        return outputs;
    }
    for (const auto& output : *listed) {
        if (!output.is_object()) {
            continue;
        }
        OutputNamesSession::Output kept;
        kept.id = text(output, "id");
        kept.name = text(output, "name");
        // An engine from before ADR-0361 says only its own name.
        kept.reported =
            output.contains("reported_name") ? text(output, "reported_name") : kept.name;
        kept.renamed = kept.name != kept.reported;
        kept.hidden = output.value("hidden", false);
        kept.icon = output.value("icon", std::string{});
        kept.local = output.value("local", false);
        kept.online = output.value("online", false);
        kept.selected = output.value("selected", false);
        if (output.value("local", false)) {
            kept.kind = QObject::tr("This engine");
        } else if (kept.id.startsWith(QLatin1String("upnp:"))) {
            kept.kind = QObject::tr("UPnP speaker");
        } else {
            kept.kind = QObject::tr("Agent");
        }
        if (!kept.id.isEmpty()) {
            outputs.push_back(std::move(kept));
        }
    }
    return outputs;
}

OutputNamesSession::OutputNamesSession(protocol::Endpoint endpoint, QObject* parent)
    : QObject(parent), endpoint_(std::move(endpoint)) {}

void OutputNamesSession::load() {
    if (busy_) {
        return;
    }
    busy_ = true;
    status_ = tr("Asking the engine…");
    emit changed();
    const QPointer self{this};
    static_cast<void>(QtConcurrent::run([self, endpoint = endpoint_] {
        auto asked = ask(endpoint);
        postBack(self, [self, asked = std::move(asked)] {
            self->busy_ = false;
            if (!asked.outputs) {
                self->known_ = false;
                self->status_ = tr("Could not ask the engine · %1").arg(asked.error);
            } else {
                self->known_ = true;
                self->outputs_ = *asked.outputs;
                self->status_ =
                    self->outputs_.empty() ? tr("The engine has no outputs yet") : QString{};
            }
            emit self->changed();
        });
    }));
}

void OutputNamesSession::rename(const QString& id, const QString& name) {
    send("outputs.configure",
         protocol::Json{{"id", id.toStdString()}, {"name", name.trimmed().toStdString()}});
}

void OutputNamesSession::setShown(const QString& id, const bool shown) {
    send("outputs.configure", protocol::Json{{"id", id.toStdString()}, {"hidden", !shown}});
}

void OutputNamesSession::setIcon(const QString& id, const QString& icon) {
    send("outputs.configure",
         protocol::Json{{"id", id.toStdString()}, {"icon", icon.toStdString()}});
}

void OutputNamesSession::arrange(const std::vector<QString>& ids) {
    auto listed = protocol::Json::array();
    for (const auto& id : ids) {
        listed.push_back(id.toStdString());
    }
    send("outputs.arrange", protocol::Json{{"ids", std::move(listed)}});
}

void OutputNamesSession::send(const char* method, protocol::Json params) {
    if (busy_ || !known_) {
        return;
    }
    busy_ = true;
    emit changed();
    const QPointer self{this};
    static_cast<void>(QtConcurrent::run([self, endpoint = endpoint_, method = std::string{method},
                                         params = std::move(params)] {
        QString error;
        if (auto client = protocol::Client::connect(endpoint); !client) {
            error = QString::fromStdString(client.error().message);
        } else {
            auto set = (*client)->call(method, params, std::chrono::seconds{5});
            if (!set) {
                error = set.error().code == core::ErrorCode::unsupported
                            ? tr("This engine is too old to name, hide or order outputs; update it")
                            : QString::fromStdString(set.error().message);
            }
            (*client)->close();
        }
        postBack(self, [self, error] {
            self->busy_ = false;
            if (!error.isEmpty()) {
                self->status_ = tr("The engine did not take it · %1").arg(error);
                emit self->changed();
                return;
            }
            // As the engine now has it.
            self->load();
        });
    }));
}

} // namespace trackknife::bench
