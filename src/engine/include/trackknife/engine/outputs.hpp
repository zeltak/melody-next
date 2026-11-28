// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "trackknife/engine/job_registry.hpp"
#include "trackknife/engine/player.hpp"
#include "trackknife/engine/workspace.hpp"
#include "trackknife/output/agent_audition.hpp"
#if MELODY_ENABLE_UPNP
#include "trackknife/output/upnp_audition.hpp"
#endif
#include "trackknife/protocol/dispatch.hpp"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace trackknife::engine {

// ADR-0228: what the engine can play on -- its own audio, if it has any, and
// every output agent that has registered -- and which one it plays on now.
//
// An agent is kept by name once seen. One that drops is the same output when
// it comes back, and if it was the one playing, the music takes up where it
// stopped there, rather than moving somewhere else.
class Outputs final {
  public:
    static constexpr auto selection_key = "outputs.selected.v1";
    static constexpr auto local_id = "local";
    // ADR-0325: each output's volume and ReplayGain mode, as last set.
    static constexpr auto settings_key = "outputs.settings.v1";
    // ADR-0361: each output's own name, if given one, and whether it is
    // hidden from the outputs clients list.
    static constexpr auto custom_key = "outputs.custom.v1";
    // A name given an output is at most this long, in bytes.
    static constexpr std::size_t longest_name = 100;

    // `own_name` is what the engine's own audio is listed as: the engine's
    // name, so a client shows "gemenon", not "this machine".
    Outputs(Player& player, output::AgentPaths paths, Workspace* workspace, EventSink sink,
            std::string own_name = "this machine");
    Outputs(const Outputs&) = delete;
    Outputs& operator=(const Outputs&) = delete;
    Outputs(Outputs&&) = delete;
    Outputs& operator=(Outputs&&) = delete;
    ~Outputs();

    // A connection that sent agent.register: the server's agent handler.
    void admit(const protocol::Json& params, int descriptor);

    struct Listed final {
        std::string id;
        // As the user named it, or else as the output calls itself.
        std::string name;
        // As the output calls itself: an agent's name, a renderer's
        // friendly name, the engine's own name.
        std::string reported_name;
        // ADR-0361: left out of what clients are shown, unless it plays.
        bool hidden{false};
        bool local{false};
        bool online{false};
        bool selected{false};
        // For an agent: whether it opens files itself rather than streaming.
        bool files{true};
        bool replay_gain{true};
    };
    // Every output, hidden ones too, as each calls itself.
    [[nodiscard]] std::vector<Listed> list() const;
    // ADR-0361: as clients are shown them; `all` keeps the hidden ones.
    [[nodiscard]] std::vector<Listed> visible(bool all) const;
    // ADR-0361: a name of the user's own for an output -- empty gives it
    // back its own -- and whether clients are shown it. Kept by the engine.
    [[nodiscard]] core::Result<void>
    configure(const std::string& id, std::optional<std::string> name, std::optional<bool> hidden);

    struct Custom final {
        std::string name;
        bool hidden{false};
    };
    // The outputs as clients are shown them: each named as the user named
    // it, and those hidden left out -- unless `all`, or it is the one
    // playing, which is never hidden from view.
    [[nodiscard]] static std::vector<Listed>
    shown(std::vector<Listed> listed, const std::map<std::string, Custom>& custom, bool all);
    // "local", or "agent:<name>". Persisted, so a restart plays on the same.
    [[nodiscard]] core::Result<void> select(const std::string& id);
    // What was chosen last time. An agent chosen then and not connected yet
    // is waited for: playback resumes on it once it registers.
    void restore();
#if MELODY_ENABLE_UPNP
    // Configured before discovery starts. A restored renderer can wait offline.
    void configure_upnp(std::shared_ptr<discovery::UpnpControl> control,
                        output::UpnpAudition::Prepare prepare);
    void renderer_changed(const discovery::UpnpRenderer& renderer);
#endif

  private:
    // Called with mutex_ held.
    [[nodiscard]] std::unique_ptr<output::AgentAudition> make_agent(const std::string& name);
    void announce();
    void keep_custom();
    // ADR-0325: the volume and ReplayGain mode just set, kept for the output
    // playing; and those kept for `id`, given back to it.
    void remember();
    void recall(const std::string& id);

    struct Remembered final {
        std::optional<int> volume;
        std::optional<audio::ReplayGainMode> replay_gain;
    };

    Player* player_;
    std::string own_name_;
    output::AgentPaths paths_;
    Workspace* workspace_;
    EventSink sink_;
    mutable std::mutex mutex_;
    // Stable addresses: the player holds a pointer to whichever is playing.
    std::map<std::string, std::unique_ptr<output::AgentAudition>> agents_;
    std::string selected_;
    std::map<std::string, Remembered> remembered_;
    std::map<std::string, Custom> custom_;
    // Set while recall() gives settings back: what it sets is not news.
    std::atomic<bool> recalling_{false};
#if MELODY_ENABLE_UPNP
    std::shared_ptr<discovery::UpnpControl> upnp_control_;
    output::UpnpAudition::Prepare upnp_prepare_;
    std::map<std::string, std::unique_ptr<output::UpnpAudition>> renderers_;
#endif
};

void register_output_methods(protocol::Dispatcher& dispatcher, Outputs& outputs);

} // namespace trackknife::engine
