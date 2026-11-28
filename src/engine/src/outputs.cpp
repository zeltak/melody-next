// SPDX-License-Identifier: GPL-3.0-only

#include "trackknife/engine/outputs.hpp"

#include "trackknife/protocol/client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>

#include <chrono>
#include <functional>
#include <iostream>
#include <utility>

namespace trackknife::engine {
namespace {

using protocol::Json;

constexpr std::string_view agent_prefix{"agent:"};

[[nodiscard]] std::string_view replay_gain_text(const audio::ReplayGainMode mode) {
    switch (mode) {
    case audio::ReplayGainMode::track:
        return "track";
    case audio::ReplayGainMode::album:
        return "album";
    default:
        return "off";
    }
}

[[nodiscard]] std::optional<audio::ReplayGainMode> replay_gain_of(const std::string& text) {
    if (text == "track") {
        return audio::ReplayGainMode::track;
    }
    if (text == "album") {
        return audio::ReplayGainMode::album;
    }
    if (text == "off") {
        return audio::ReplayGainMode::off;
    }
    return std::nullopt;
}

[[nodiscard]] std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

[[nodiscard]] core::Error not_found(const std::string& id) {
    return core::Error{.code = core::ErrorCode::not_found,
                       .message = "no such output",
                       .context = {{.key = "output", .value = id}}};
}

// The engine's own address on a connection, as text: an IPv4 address that
// came in over an IPv6 socket is given as IPv4. Empty for a unix socket.
[[nodiscard]] std::string local_address(const int descriptor) {
    sockaddr_storage address{};
    socklen_t length = sizeof(address);
    if (::getsockname(descriptor, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        return {};
    }
    std::array<char, INET6_ADDRSTRLEN> text{};
    if (address.ss_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
        return ::inet_ntop(AF_INET, &ipv4->sin_addr, text.data(), text.size()) != nullptr
                   ? std::string{text.data()}
                   : std::string{};
    }
    if (address.ss_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(&address);
        if (IN6_IS_ADDR_V4MAPPED(&ipv6->sin6_addr)) {
            return ::inet_ntop(AF_INET, &ipv6->sin6_addr.s6_addr[12], text.data(), text.size()) !=
                           nullptr
                       ? std::string{text.data()}
                       : std::string{};
        }
        return ::inet_ntop(AF_INET6, &ipv6->sin6_addr, text.data(), text.size()) != nullptr
                   ? std::string{text.data()}
                   : std::string{};
    }
    return {};
}

} // namespace

Outputs::Outputs(Player& player, output::AgentPaths paths, Workspace* workspace, EventSink sink,
                 std::string own_name)
    : player_(&player), own_name_(std::move(own_name)), paths_(std::move(paths)),
      workspace_(workspace), sink_(std::move(sink)) {
    // ADR-0325: what each output was last set to.
    if (workspace_ != nullptr) {
        if (auto stored = workspace_->load_engine_state(settings_key); stored && *stored) {
            const auto parsed = Json::parse(**stored, nullptr, false);
            if (parsed.is_object()) {
                for (const auto& [id, kept] : parsed.items()) {
                    if (!kept.is_object()) {
                        continue;
                    }
                    Remembered remembered;
                    if (const auto volume = kept.find("volume");
                        volume != kept.end() && volume->is_number_integer() &&
                        volume->get<int>() >= 0 && volume->get<int>() <= 100) {
                        remembered.volume = volume->get<int>();
                    }
                    if (const auto gain = kept.find("replay_gain");
                        gain != kept.end() && gain->is_string()) {
                        remembered.replay_gain = replay_gain_of(gain->get<std::string>());
                    }
                    remembered_[id] = remembered;
                }
            }
        }
    }
    // ADR-0361: names given and outputs hidden.
    if (workspace_ != nullptr) {
        if (auto stored = workspace_->load_engine_state(custom_key); stored && *stored) {
            const auto parsed = Json::parse(**stored, nullptr, false);
            if (parsed.is_object()) {
                for (const auto& [id, kept] : parsed.items()) {
                    if (!kept.is_object()) {
                        continue;
                    }
                    Custom custom{.name = kept.value("name", std::string{}),
                                  .hidden = kept.value("hidden", false),
                                  .icon = kept.value("icon", std::string{})};
                    if (!custom.name.empty() || custom.hidden || !custom.icon.empty()) {
                        custom_[id] = std::move(custom);
                    }
                }
            }
        }
        if (auto stored = workspace_->load_engine_state(order_key); stored && *stored) {
            const auto parsed = Json::parse(**stored, nullptr, false);
            if (parsed.is_array()) {
                for (const auto& id : parsed) {
                    if (id.is_string()) {
                        order_.push_back(id.get<std::string>());
                    }
                }
            }
        }
    }
    player_->on_output_settings([this] { remember(); });
}

std::vector<Outputs::Listed> Outputs::shown(std::vector<Listed> listed,
                                            const std::map<std::string, Custom>& custom,
                                            const std::vector<std::string>& order, const bool all) {
    // The user's order first; those not in it after, as they were.
    const auto place = [&order](const Listed& output) {
        const auto found = std::ranges::find(order, output.id);
        return static_cast<std::size_t>(found - order.begin());
    };
    std::ranges::stable_sort(listed, std::less{}, place);
    std::vector<Listed> visible;
    for (auto& output : listed) {
        output.reported_name = output.name;
        if (const auto found = custom.find(output.id); found != custom.end()) {
            if (!found->second.name.empty()) {
                output.name = found->second.name;
            }
            output.hidden = found->second.hidden;
            output.icon = found->second.icon;
        }
        if (all || !output.hidden || output.selected) {
            visible.push_back(std::move(output));
        }
    }
    return visible;
}

core::Result<void> Outputs::configure(const std::string& id, Change change) {
    auto& name = change.name;
    if (change.icon && !change.icon->empty() &&
        std::ranges::find(icons, std::string_view{*change.icon}) == icons.end()) {
        return std::unexpected(core::Error{.code = core::ErrorCode::invalid_argument,
                                           .message = "no such output icon",
                                           .context = {{.key = "icon", .value = *change.icon}}});
    }
    if (name) {
        // Spaces around it are not part of it; inside, any text.
        const auto first = name->find_first_not_of(" \t\r\n");
        const auto last = name->find_last_not_of(" \t\r\n");
        *name = first == std::string::npos ? std::string{} : name->substr(first, last - first + 1);
        if (name->size() > longest_name) {
            return std::unexpected(core::Error{.code = core::ErrorCode::invalid_argument,
                                               .message = "an output's name is at most 100 bytes",
                                               .context = {{.key = "param", .value = "name"}}});
        }
    }
    const auto known =
        std::ranges::any_of(list(), [&id](const Listed& output) { return output.id == id; });
    {
        const std::lock_guard guard{mutex_};
        if (!known && !custom_.contains(id)) {
            return std::unexpected(not_found(id));
        }
        auto& custom = custom_[id];
        if (name) {
            custom.name = std::move(*name);
        }
        if (change.hidden) {
            custom.hidden = *change.hidden;
        }
        if (change.icon) {
            custom.icon = std::move(*change.icon);
        }
        if (custom.name.empty() && !custom.hidden && custom.icon.empty()) {
            custom_.erase(id);
        }
    }
    keep_custom();
    announce();
    return {};
}

core::Result<void> Outputs::arrange(std::vector<std::string> ids) {
    std::vector<std::string> order;
    for (auto& id : ids) {
        if (std::ranges::find(order, id) == order.end()) {
            order.push_back(std::move(id));
        }
    }
    {
        const std::lock_guard guard{mutex_};
        // Outputs not named keep their place after those that are, among
        // them any not found since: a renderer off now stays where it was.
        for (const auto& id : order_) {
            if (std::ranges::find(order, id) == order.end()) {
                order.push_back(id);
            }
        }
        order_ = std::move(order);
    }
    keep_order();
    announce();
    return {};
}

void Outputs::keep_order() {
    Json stored;
    {
        const std::lock_guard guard{mutex_};
        stored = order_;
    }
    if (workspace_ != nullptr) {
        static_cast<void>(workspace_->save_engine_state(order_key, stored.dump(), now_ms()));
    }
}

void Outputs::keep_custom() {
    auto stored = Json::object();
    {
        const std::lock_guard guard{mutex_};
        for (const auto& [id, custom] : custom_) {
            auto entry = Json::object();
            if (!custom.name.empty()) {
                entry["name"] = custom.name;
            }
            if (custom.hidden) {
                entry["hidden"] = true;
            }
            if (!custom.icon.empty()) {
                entry["icon"] = custom.icon;
            }
            stored[id] = std::move(entry);
        }
    }
    if (workspace_ != nullptr) {
        static_cast<void>(workspace_->save_engine_state(custom_key, stored.dump(), now_ms()));
    }
}

void Outputs::remember() {
    if (recalling_) {
        return;
    }
    std::string id;
    {
        const std::lock_guard guard{mutex_};
        id = selected_;
    }
    if (id.empty()) {
        return;
    }
    const auto state = player_->state();
    auto stored = Json::object();
    {
        const std::lock_guard guard{mutex_};
        if (id != selected_) {
            return;
        }
        remembered_[id] =
            Remembered{.volume = state.volume_percent, .replay_gain = state.replay_gain_mode};
        for (const auto& [output, kept] : remembered_) {
            auto entry = Json::object();
            if (kept.volume) {
                entry["volume"] = *kept.volume;
            }
            if (kept.replay_gain) {
                entry["replay_gain"] = replay_gain_text(*kept.replay_gain);
            }
            stored[output] = std::move(entry);
        }
    }
    if (workspace_ != nullptr) {
        static_cast<void>(workspace_->save_engine_state(settings_key, stored.dump(), now_ms()));
    }
}

void Outputs::recall(const std::string& id) {
    Remembered kept;
    {
        const std::lock_guard guard{mutex_};
        const auto found = remembered_.find(id);
        if (found == remembered_.end()) {
            return;
        }
        kept = found->second;
    }
    recalling_ = true;
    if (kept.replay_gain) {
        static_cast<void>(player_->set_replay_gain_mode(*kept.replay_gain));
    }
    if (kept.volume) {
        // An agent away takes it when it is back (admit).
        static_cast<void>(player_->set_volume_percent(*kept.volume));
    }
    recalling_ = false;
}

Outputs::~Outputs() {
    player_->on_output_settings({});
    // The player must not be left pointing at an agent about to go.
    static_cast<void>(player_->set_output(player_->local_output()));
}

void Outputs::admit(const Json& params, const int descriptor) {
    const auto name = params.value("name", std::string{});
    if (name.empty()) {
        // Registered without a name: nothing to call it, nothing to keep.
        ::close(descriptor);
        return;
    }
    const bool files = params.value("files", true);
    output::AgentAudition* agent = nullptr;
    bool selected = false;
    bool adopt_first = false;
    std::optional<output::AgentAudition::LastHeard> heard;
    {
        const std::lock_guard guard{mutex_};
        auto& slot = agents_[name];
        if (!slot) {
            slot = make_agent(name);
        }
        agent = slot.get();
        // An engine with nothing to play on -- no audio of its own, no
        // agent chosen yet -- plays on the first agent there is, rather than
        // into silence until someone thinks to choose it.
        adopt_first = selected_.empty();
        selected = selected_ == std::string{agent_prefix} + name;
        // Before the new connection wipes it: where it was when it dropped.
        heard = agent->last_heard();
    }
    // Asked before the descriptor is handed over: where this agent reached
    // the engine, which is where it can fetch streams.
    auto reached = local_address(descriptor);
    output::AgentAudition::Returning returning{.instance = params.value("instance", std::string{}),
                                               .report = std::nullopt};
    if (const auto report = params.find("report"); report != params.end() && report->is_object()) {
        returning.report = *report;
    }
    const bool kept = agent->attach(protocol::Client::adopt(descriptor), files, std::move(reached),
                                    output::StreamWishes::from_json(params), std::move(returning));
    if (adopt_first) {
        static_cast<void>(select(std::string{agent_prefix} + name));
        return;
    }
    // ADR-0325: an agent started again starts at its own volume; the one
    // playing is given back what it was set to.
    if (selected) {
        recall(std::string{agent_prefix} + name);
    }
    // ADR-0274: back having played on from its buffer, and still on what the
    // engine plays: taken as it is, not wound back to where it dropped.
    if (selected && kept && player_->keep_output(agent->snapshot().raw_path)) {
        announce();
        return;
    }
    if (selected) {
        // The music was here when the agent went away; it takes up there.
        static_cast<void>(
            player_->resume_output(heard ? std::optional{heard->position_ms} : std::nullopt,
                                   heard ? std::optional{heard->playing} : std::nullopt));
    }
    announce();
}

std::unique_ptr<output::AgentAudition> Outputs::make_agent(const std::string& name) {
    auto agent = std::make_unique<output::AgentAudition>(name, paths_);
    // Gone is worth telling every client: it is listed offline from now.
    agent->on_offline([this] { announce(); });
    return agent;
}

std::vector<Outputs::Listed> Outputs::list() const {
    const std::lock_guard guard{mutex_};
    std::vector<Listed> listed;
    if (player_->local_output() != nullptr) {
        listed.push_back(Listed{.id = local_id,
                                .name = own_name_,
                                .local = true,
                                .online = true,
                                .selected = selected_ == local_id,
                                .files = true});
    }
    for (const auto& [name, agent] : agents_) {
        const auto id = std::string{agent_prefix} + name;
        listed.push_back(Listed{.id = id,
                                .name = name,
                                .local = false,
                                .online = agent->online(),
                                .selected = selected_ == id,
                                .files = agent->files()});
    }
#if MELODY_ENABLE_UPNP
    for (const auto& [udn, renderer] : renderers_) {
        const auto id = "upnp:" + udn;
        listed.push_back(Listed{.id = id,
                                .name = renderer->name(),
                                .local = false,
                                .online = renderer->online(),
                                .selected = selected_ == id,
                                .files = false,
                                .replay_gain = false});
    }
#endif
    return listed;
}

core::Result<void> Outputs::select(const std::string& id) {
    audio::Audition* chosen = nullptr;
    {
        const std::lock_guard guard{mutex_};
        if (id == local_id) {
            chosen = player_->local_output();
            if (chosen == nullptr) {
                return std::unexpected(core::Error{.code = core::ErrorCode::unsupported,
                                                   .message = "this machine has no audio output",
                                                   .context = {}});
            }
#if MELODY_ENABLE_UPNP
        } else if (id.starts_with("upnp:")) {
            auto found = renderers_.find(id.substr(5));
            if (found == renderers_.end()) {
                return std::unexpected(not_found(id));
            }
            chosen = found->second.get();
#endif
        } else if (id.starts_with(agent_prefix)) {
            const auto found = agents_.find(id.substr(agent_prefix.size()));
            if (found == agents_.end()) {
                return std::unexpected(not_found(id));
            }
            chosen = found->second.get();
        } else {
            return std::unexpected(not_found(id));
        }
        selected_ = id;
    }
    if (workspace_ != nullptr) {
        static_cast<void>(workspace_->save_engine_state(selection_key, id, now_ms()));
    }
    // An agent chosen while it is away is where playback waits; switching
    // reports that it could not take the music up yet, which is not a
    // refusal of the choice.
    if (auto moved = player_->set_output(chosen); !moved) {
        std::cerr << "melodyd: selected output " << id
                  << " but could not move playback yet: " << moved.error().message << '\n';
    }
    // ADR-0325: as loud, and with the gain, it was last set to.
    recall(id);
    announce();
    return {};
}

void Outputs::restore() {
    std::string id;
    if (workspace_ != nullptr) {
        if (auto stored = workspace_->load_engine_state(selection_key); stored && *stored) {
            id = **stored;
        }
    }
    if (id.empty()) {
        id = player_->local_output() != nullptr ? std::string{local_id} : std::string{};
    }
    if (id.starts_with(agent_prefix)) {
        // Not connected yet after a restart: known by name, so the choice
        // holds until it registers.
        const std::lock_guard guard{mutex_};
        auto& slot = agents_[id.substr(agent_prefix.size())];
        if (!slot) {
            slot = make_agent(id.substr(agent_prefix.size()));
        }
    }
#if MELODY_ENABLE_UPNP
    if (id.starts_with("upnp:")) {
        const std::lock_guard guard{mutex_};
        auto& slot = renderers_[id.substr(5)];
        if (!slot) {
            slot =
                std::make_unique<output::UpnpAudition>(id.substr(5), upnp_control_, upnp_prepare_);
        }
    }
#endif
    if (!id.empty()) {
        static_cast<void>(select(id));
    }
}

#if MELODY_ENABLE_UPNP
void Outputs::configure_upnp(std::shared_ptr<discovery::UpnpControl> control,
                             output::UpnpAudition::Prepare prepare) {
    const std::lock_guard guard{mutex_};
    upnp_control_ = std::move(control);
    upnp_prepare_ = std::move(prepare);
}

void Outputs::renderer_changed(const discovery::UpnpRenderer& renderer) {
    output::UpnpAudition* audition = nullptr;
    bool selected = false;
    bool reconnect = false;
    {
        const std::lock_guard guard{mutex_};
        auto& slot = renderers_[renderer.udn];
        if (!slot) {
            slot =
                std::make_unique<output::UpnpAudition>(renderer.udn, upnp_control_, upnp_prepare_);
        }
        audition = slot.get();
        reconnect = !audition->online() && renderer.online;
        selected = selected_ == "upnp:" + renderer.udn;
    }
    const auto before = audition->snapshot();
    audition->update(renderer);
    if (selected && reconnect) {
        if (before.format && before.format->sample_rate > 0) {
            static_cast<void>(
                player_->resume_output(before.position_sample * 1000 / before.format->sample_rate,
                                       audition->wants_playing()));
        } else {
            static_cast<void>(player_->resume_output(std::nullopt, std::nullopt));
        }
    }
    announce();
}
#endif

namespace {

[[nodiscard]] Json described(const std::vector<Outputs::Listed>& listed) {
    auto outputs = Json::array();
    for (const auto& output : listed) {
        outputs.push_back(Json{{"id", output.id},
                               {"name", output.name},
                               {"reported_name", output.reported_name},
                               {"hidden", output.hidden},
                               {"icon", output.icon},
                               {"local", output.local},
                               {"online", output.online},
                               {"selected", output.selected},
                               {"files", output.files},
                               {"replay_gain", output.replay_gain}});
    }
    return outputs;
}

} // namespace

std::vector<Outputs::Listed> Outputs::visible(const bool all) const {
    auto listed = list();
    const std::lock_guard guard{mutex_};
    return shown(std::move(listed), custom_, order_, all);
}

void Outputs::announce() {
    if (!sink_) {
        return;
    }
    // ADR-0361: as clients are shown them -- hidden ones left out.
    sink_(protocol::Event{.name = "outputs.changed",
                          .data = Json{{"outputs", described(visible(false))}}});
}

void register_output_methods(protocol::Dispatcher& dispatcher, Outputs& outputs) {
    // ADR-0361: `all` lists hidden outputs too, for whoever names and hides
    // them; otherwise they are left out, unless one is playing.
    dispatcher.on("outputs.list", [&outputs](const Json& params) -> core::Result<Json> {
        const auto all = params.is_object() && params.value("all", false);
        return Json{{"outputs", described(outputs.visible(all))}};
    });
    dispatcher.on("outputs.configure", [&outputs](const Json& params) -> core::Result<Json> {
        const auto id = params.find("id");
        if (id == params.end() || !id->is_string()) {
            return std::unexpected(core::Error{.code = core::ErrorCode::invalid_argument,
                                               .message = "an output id is required",
                                               .context = {{.key = "param", .value = "id"}}});
        }
        std::optional<std::string> name;
        if (const auto given = params.find("name"); given != params.end()) {
            if (given->is_null()) {
                name = std::string{};
            } else if (given->is_string()) {
                name = given->get<std::string>();
            } else {
                return std::unexpected(core::Error{.code = core::ErrorCode::invalid_argument,
                                                   .message = "an output's name is text",
                                                   .context = {{.key = "param", .value = "name"}}});
            }
        }
        std::optional<bool> hidden;
        if (const auto given = params.find("hidden"); given != params.end()) {
            if (!given->is_boolean()) {
                return std::unexpected(
                    core::Error{.code = core::ErrorCode::invalid_argument,
                                .message = "hidden is true or false",
                                .context = {{.key = "param", .value = "hidden"}}});
            }
            hidden = given->get<bool>();
        }
        std::optional<std::string> icon;
        if (const auto given = params.find("icon"); given != params.end()) {
            if (given->is_null()) {
                icon = std::string{};
            } else if (given->is_string()) {
                icon = given->get<std::string>();
            } else {
                return std::unexpected(core::Error{.code = core::ErrorCode::invalid_argument,
                                                   .message = "an output's icon is a name",
                                                   .context = {{.key = "param", .value = "icon"}}});
            }
        }
        if (!name && !hidden && !icon) {
            return std::unexpected(core::Error{.code = core::ErrorCode::invalid_argument,
                                               .message = "give a name, hidden or an icon",
                                               .context = {}});
        }
        auto configured = outputs.configure(
            id->get<std::string>(),
            Outputs::Change{.name = std::move(name), .hidden = hidden, .icon = std::move(icon)});
        if (!configured) {
            return std::unexpected(std::move(configured.error()));
        }
        return Json::object();
    });
    // ADR-0361: the outputs in the user's order, first to last.
    dispatcher.on("outputs.arrange", [&outputs](const Json& params) -> core::Result<Json> {
        const auto ids = params.find("ids");
        if (ids == params.end() || !ids->is_array() || ids->size() > 1'000U ||
            !std::ranges::all_of(*ids, [](const Json& id) { return id.is_string(); })) {
            return std::unexpected(core::Error{.code = core::ErrorCode::invalid_argument,
                                               .message = "ids is a list of output ids",
                                               .context = {{.key = "param", .value = "ids"}}});
        }
        auto arranged = outputs.arrange(ids->get<std::vector<std::string>>());
        if (!arranged) {
            return std::unexpected(std::move(arranged.error()));
        }
        return Json::object();
    });
    dispatcher.on("outputs.select", [&outputs](const Json& params) -> core::Result<Json> {
        const auto id = params.find("id");
        if (id == params.end() || !id->is_string()) {
            return std::unexpected(core::Error{.code = core::ErrorCode::invalid_argument,
                                               .message = "an output id is required",
                                               .context = {{.key = "param", .value = "id"}}});
        }
        auto selected = outputs.select(id->get<std::string>());
        if (!selected) {
            return std::unexpected(std::move(selected.error()));
        }
        return Json::object();
    });
}

} // namespace trackknife::engine
