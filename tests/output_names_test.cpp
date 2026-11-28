// SPDX-License-Identifier: GPL-3.0-only

// ADR-0361: an output named as the user names it, and one hidden from the
// outputs clients list -- unless it is the one playing. Kept by the engine
// across a restart. No audio is needed: agents not yet connected are
// listed, offline, as after a restart.

#include "trackknife/core/stable_id.hpp"
#include "trackknife/engine/outputs.hpp"
#include "trackknife/engine/player.hpp"
#include "trackknife/engine/workspace.hpp"
#include "trackknife/protocol/dispatch.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace engine = trackknife::engine;
namespace protocol = trackknife::protocol;
using protocol::Json;

int failures = 0;

void check(const bool condition, const char* expression, const int line) {
    if (!condition) {
        std::cerr << "line " << line << ": " << expression << '\n';
        ++failures;
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

[[nodiscard]] protocol::Response call(const protocol::Dispatcher& dispatcher,
                                      const std::string& method, Json params = Json::object()) {
    return dispatcher.dispatch(
        protocol::Request{.id = 1, .method = method, .params = std::move(params)});
}

// The outputs a client is given, by id.
[[nodiscard]] std::map<std::string, Json> listed(const protocol::Dispatcher& dispatcher,
                                                 const bool all = false) {
    std::map<std::string, Json> found;
    const auto answer =
        call(dispatcher, "outputs.list", all ? Json{{"all", true}} : Json::object());
    if (answer.result) {
        for (const auto& output : answer.result->value("outputs", Json::array())) {
            found[output.value("id", std::string{})] = output;
        }
    }
    return found;
}

// Their ids, in the order a client is given them.
[[nodiscard]] std::vector<std::string> ordered(const protocol::Dispatcher& dispatcher,
                                               const bool all = false) {
    std::vector<std::string> ids;
    const auto answer =
        call(dispatcher, "outputs.list", all ? Json{{"all", true}} : Json::object());
    if (answer.result) {
        for (const auto& output : answer.result->value("outputs", Json::array())) {
            ids.push_back(output.value("id", std::string{}));
        }
    }
    return ids;
}

void theRulesAlone() {
    using Listed = engine::Outputs::Listed;
    const std::vector<Listed> listed{
        {.id = "upnp:a", .name = "LS50 Wireless II-c4e3", .selected = false},
        {.id = "upnp:b", .name = "Samsung TV", .selected = false},
        {.id = "agent:porch", .name = "porch", .selected = true},
        {.id = "local", .name = "melody-mini", .selected = false},
    };
    const std::map<std::string, engine::Outputs::Custom> custom{
        {"upnp:a", {.name = "Living room", .hidden = false}},
        {"upnp:b", {.name = "", .hidden = true}},
        {"agent:porch", {.name = "Porch", .hidden = true}},
    };
    const auto shown = engine::Outputs::shown(listed, custom, {}, false);
    CHECK(shown.size() == 3U);
    CHECK(shown[0].name == "Living room" && shown[0].reported_name == "LS50 Wireless II-c4e3");
    // Hidden, but playing: still shown, or no one could see what plays.
    CHECK(shown[1].id == "agent:porch" && shown[1].hidden && shown[1].name == "Porch");
    CHECK(shown[2].name == "melody-mini" && shown[2].reported_name == "melody-mini" &&
          !shown[2].hidden);
    const auto every = engine::Outputs::shown(listed, custom, {}, true);
    CHECK(every.size() == 4U);
    CHECK(every[1].hidden && every[1].name == "Samsung TV");

    // In the user's order; those not in it after, as they were -- among
    // them an id no longer found, which is passed over.
    const auto arranged =
        engine::Outputs::shown(listed, {}, {"local", "upnp:gone", "upnp:b"}, true);
    CHECK(arranged.size() == 4U);
    CHECK(arranged[0].id == "local" && arranged[1].id == "upnp:b" && arranged[2].id == "upnp:a" &&
          arranged[3].id == "agent:porch");
    // An icon chosen is given; none chosen, none.
    const auto pictured = engine::Outputs::shown(
        listed, {{"upnp:a", {.name = "", .hidden = false, .icon = "living-room"}}}, {}, true);
    CHECK(pictured[0].icon == "living-room" && pictured[1].icon.empty());
}

} // namespace

int main() {
    theRulesAlone();

    const auto directory =
        std::filesystem::temp_directory_path() /
        ("trackknife-output-names-" + trackknife::core::StableId::random().to_string());
    std::filesystem::create_directories(directory);
    {
        auto workspace = engine::Workspace::open(directory / "lists.sqlite");
        CHECK(workspace.has_value());
        if (!workspace) {
            return EXIT_FAILURE;
        }
        auto player = engine::Player::create_without_audio();
        std::vector<Json> announced;
        const auto sink = [&announced](const protocol::Event& event) {
            if (event.name == "outputs.changed") {
                announced.push_back(event.data);
            }
        };
        protocol::Dispatcher dispatcher;
        {
            engine::Outputs outputs{*player, {}, &*workspace, sink};
            engine::register_output_methods(dispatcher, outputs);
            // Two agents known by name, as after a restart; the porch plays.
            for (const auto* chosen : {"agent:kitchen", "agent:porch"}) {
                CHECK(workspace->save_engine_state(engine::Outputs::selection_key, chosen, 1)
                          .has_value());
                outputs.restore();
            }
            auto before = listed(dispatcher);
            CHECK(before.size() == 2U);
            CHECK(before["agent:kitchen"].value("name", "") == "kitchen");
            CHECK(before["agent:kitchen"].value("reported_name", "") == "kitchen");
            CHECK(!before["agent:kitchen"].value("hidden", true));

            // Named: every client is given the new name, and the old beside it.
            CHECK(!call(dispatcher, "outputs.configure",
                        Json{{"id", "agent:kitchen"}, {"name", "  Kitchen radio "}})
                       .error);
            auto named = listed(dispatcher);
            CHECK(named["agent:kitchen"].value("name", "") == "Kitchen radio");
            CHECK(named["agent:kitchen"].value("reported_name", "") == "kitchen");
            CHECK(!announced.empty());

            // Hidden: left out of the list and of the news, but there for all.
            CHECK(!call(dispatcher, "outputs.configure",
                        Json{{"id", "agent:kitchen"}, {"hidden", true}})
                       .error);
            CHECK(listed(dispatcher).count("agent:kitchen") == 0U);
            auto every = listed(dispatcher, true);
            CHECK(every.count("agent:kitchen") == 1U &&
                  every["agent:kitchen"].value("hidden", false));
            CHECK(every["agent:kitchen"].value("name", "") == "Kitchen radio");
            bool in_news = false;
            for (const auto& output : announced.back().value("outputs", Json::array())) {
                in_news = in_news || output.value("id", std::string{}) == "agent:kitchen";
            }
            CHECK(!in_news);

            // The one playing, hidden, is still shown.
            CHECK(!call(dispatcher, "outputs.configure",
                        Json{{"id", "agent:porch"}, {"hidden", true}})
                       .error);
            CHECK(listed(dispatcher).count("agent:porch") == 1U);
            CHECK(listed(dispatcher)["agent:porch"].value("hidden", false));

            // ADR-0361: put in the user's order, and given an icon.
            CHECK(ordered(dispatcher, true) ==
                  (std::vector<std::string>{"agent:kitchen", "agent:porch"}));
            CHECK(!call(dispatcher, "outputs.arrange",
                        Json{{"ids", {"agent:porch", "agent:kitchen"}}})
                       .error);
            CHECK(ordered(dispatcher, true) ==
                  (std::vector<std::string>{"agent:porch", "agent:kitchen"}));
            CHECK(announced.back().value("outputs", Json::array()).front().value("id", "") ==
                  "agent:porch");
            CHECK(!call(dispatcher, "outputs.configure",
                        Json{{"id", "agent:porch"}, {"icon", "bath"}})
                       .error);
            CHECK(listed(dispatcher)["agent:porch"].value("icon", "") == "bath");
            CHECK(call(dispatcher, "outputs.configure",
                       Json{{"id", "agent:porch"}, {"icon", "spaceship"}})
                      .error.has_value());
            CHECK(call(dispatcher, "outputs.arrange", Json{{"ids", "agent:porch"}})
                      .error.has_value());
            CHECK(call(dispatcher, "outputs.arrange", Json{{"ids", {1, 2}}}).error.has_value());

            // Refused: no such output, a name too long, nothing to change.
            CHECK(call(dispatcher, "outputs.configure",
                       Json{{"id", "upnp:nothing"}, {"hidden", true}})
                      .error.has_value());
            CHECK(call(dispatcher, "outputs.configure",
                       Json{{"id", "agent:porch"}, {"name", std::string(101, 'x')}})
                      .error.has_value());
            CHECK(call(dispatcher, "outputs.configure", Json{{"id", "agent:porch"}})
                      .error.has_value());
            CHECK(call(dispatcher, "outputs.configure",
                       Json{{"id", "agent:porch"}, {"hidden", "yes"}})
                      .error.has_value());
        }

        // Kept across a restart of the engine.
        protocol::Dispatcher again;
        engine::Outputs restarted{*player, {}, &*workspace, sink};
        engine::register_output_methods(again, restarted);
        CHECK(workspace->save_engine_state(engine::Outputs::selection_key, "agent:kitchen", 2)
                  .has_value());
        restarted.restore();
        CHECK(workspace->save_engine_state(engine::Outputs::selection_key, "agent:porch", 3)
                  .has_value());
        restarted.restore();
        auto kept = listed(again, true);
        CHECK(kept["agent:kitchen"].value("name", "") == "Kitchen radio");
        CHECK(kept["agent:kitchen"].value("hidden", false));
        CHECK(kept["agent:porch"].value("icon", "") == "bath");
        CHECK(ordered(again, true) == (std::vector<std::string>{"agent:porch", "agent:kitchen"}));
        // An empty icon leaves the choice to each client again.
        CHECK(!call(again, "outputs.configure", Json{{"id", "agent:porch"}, {"icon", ""}}).error);
        CHECK(listed(again, true)["agent:porch"].value("icon", "x").empty());

        // An empty name gives the output its own back; shown again, it is
        // as it was, and nothing is kept for it.
        CHECK(!call(again, "outputs.configure",
                    Json{{"id", "agent:kitchen"}, {"name", nullptr}, {"hidden", false}})
                   .error);
        auto plain = listed(again);
        CHECK(plain["agent:kitchen"].value("name", "") == "kitchen");
        CHECK(!plain["agent:kitchen"].value("hidden", true));
        const auto stored = workspace->load_engine_state(engine::Outputs::custom_key);
        CHECK(stored && *stored && (*stored)->find("agent:kitchen") == std::string::npos);
    }
    std::error_code removed;
    std::filesystem::remove_all(directory, removed);
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
