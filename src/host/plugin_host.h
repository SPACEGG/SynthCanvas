#ifndef SYNTH_CANVAS_HOST_PLUGIN_HOST_H
#define SYNTH_CANVAS_HOST_PLUGIN_HOST_H

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>

#include <atomic>
#include <clap/helpers/event-list.hh>
#include <clap/helpers/host.hh>
#include <clap/helpers/plugin-proxy.hh>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "constants.h"
#include "readerwriterqueue.h"

namespace synth_canvas::host {
class AudioEngine;
}

constexpr auto kPluginHostMh = clap::helpers::MisbehaviourHandler::Terminate;
constexpr auto kPluginHostCl = clap::helpers::CheckingLevel::Maximal;

using BaseHost = clap::helpers::Host<kPluginHostMh, kPluginHostCl>;
extern template class clap::helpers::Host<kPluginHostMh, kPluginHostCl>;

using PluginProxy = clap::helpers::PluginProxy<kPluginHostMh, kPluginHostCl>;
extern template class clap::helpers::PluginProxy<kPluginHostMh, kPluginHostCl>;

namespace synth_canvas::host {
class PluginHost final : public BaseHost {
   public:
    enum PluginState {
        kInactive,
        kInactiveWithError,
        kActiveAndSleeping,
        kActiveAndProcessing,
        kActiveWithError,
        kActiveAndReadyToDeactivate,
    };

    PluginHost();
    ~PluginHost() override;

    auto load(const std::string& path, int plugin_index) -> bool;
    void unload();

    auto canActivate() const -> bool;
    void activate(int32_t sample_rate, int32_t block_size);
    void deactivate();
    void setProcessingEnabled(bool enabled);

    void setParameterValue(clap_id param_id, double value);
    void pollMainThread();

    void setPorts(int num_inputs, float** inputs, int num_outputs, float** outputs);

    void processBegin(int nframes);
    void processNoteOn(int sample_offset, int channel, int key, double velocity,
                       int32_t note_id = constants::kClapInvalidId);
    void processNoteOff(int sample_offset, int channel, int key, double velocity,
                        int32_t note_id = constants::kClapInvalidId);
    void process();
    void processEnd(int nframes);

    std::function<void(clap_id, double)> on_parameter_changed;

    auto isPluginActive() const -> bool;
    auto isPluginProcessing() const -> bool;
    auto isPluginSleeping() const -> bool;
    void setPluginState(PluginState state);

    void setInstanceId(uint32_t id) { _instance_id = id; }
    auto getInstanceId() const -> uint32_t { return _instance_id; }

   protected:
    void requestRestart() noexcept override;
    void requestProcess() noexcept override;
    void requestCallback() noexcept override;
    auto implementsGui() const noexcept -> bool override { return false; }

    auto implementsLog() const noexcept -> bool override { return true; }
    void logLog(clap_log_severity severity, const char* message) const noexcept override;
    auto implementsParams() const noexcept -> bool override { return true; }
    void paramsRescan(clap_param_rescan_flags flags) noexcept override;
    void paramsClear(clap_id param_id, clap_param_clear_flags flags) noexcept override;
    void paramsRequestFlush() noexcept override;
    auto implementsPosixFdSupport() const noexcept -> bool override { return false; }
    auto posixFdSupportRegisterFd(int fd, clap_posix_fd_flags_t flags) noexcept -> bool override {
        return false;
    }
    auto posixFdSupportModifyFd(int fd, clap_posix_fd_flags_t flags) noexcept -> bool override {
        return false;
    }
    auto posixFdSupportUnregisterFd(int fd) noexcept -> bool override { return false; }
    auto implementsRemoteControls() const noexcept -> bool override { return false; }
    void remoteControlsChanged() noexcept override {}
    void remoteControlsSuggestPage(clap_id page_id) noexcept override {}
    auto implementsState() const noexcept -> bool override { return false; }
    void stateMarkDirty() noexcept override;
    auto implementsTimerSupport() const noexcept -> bool override { return false; }
    auto timerSupportRegisterTimer(uint32_t period_ms, clap_id* timer_id) noexcept
        -> bool override {
        return false;
    }
    auto timerSupportUnregisterTimer(clap_id timer_id) noexcept -> bool override { return false; }
    auto threadCheckIsMainThread() const noexcept -> bool override;
    auto threadCheckIsAudioThread() const noexcept -> bool override;
    auto implementsThreadPool() const noexcept -> bool override { return false; }
    auto threadPoolRequestExec(uint32_t num_tasks) noexcept -> bool override { return false; }

   private:
    void checkForMainThread();
    void checkForAudioThread();

    void generatePluginInputEvents();
    void handlePluginOutputEvents();

    void* _library_handle = nullptr;
    const clap_plugin_entry* _plugin_entry = nullptr;
    const clap_plugin_factory* _plugin_factory = nullptr;
    std::unique_ptr<PluginProxy> _plugin;
    clap_audio_buffer _audio_in = {};
    clap_audio_buffer _audio_out = {};
    clap::helpers::EventList _ev_in;
    clap::helpers::EventList _ev_out;
    clap_process _process;

    std::unordered_map<clap_id, bool> _is_adjusting_parameter;
    PluginState _state = kInactive;
    bool _state_is_dirty = false;
    bool _schedule_restart = false;
    bool _schedule_param_flush = false;
    bool _schedule_main_thread_callback = false;

    uint32_t _audio_input_ports_count = constants::kDefaultAudioPortCount;
    uint32_t _audio_output_ports_count = constants::kDefaultAudioPortCount;
    bool _has_note_input = true;

    std::atomic<bool> _schedule_processing{false};
    std::atomic<bool> _is_processing_active{false};

    struct PluginEvent {
        union {
            clap_event_header_t header;
            clap_event_note_t note;
            clap_event_midi_t midi;
            clap_event_param_value_t param_value;
        } event;
    };

    moodycamel::ReaderWriterQueue<PluginEvent> _to_plugin_event_queue{constants::kEventQueueSize};
    moodycamel::ReaderWriterQueue<PluginEvent> _from_plugin_event_queue{constants::kEventQueueSize};

    uint32_t _instance_id = 0;
};

}  // namespace synth_canvas::host

#endif  // SYNTH_CANVAS_HOST_PLUGIN_HOST_H