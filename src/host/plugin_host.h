#ifndef SYNTH_CANVAS_HOST_PLUGIN_HOST_H
#define SYNTH_CANVAS_HOST_PLUGIN_HOST_H

#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <vector>
#include <atomic>     
#include <functional> 

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/note-ports.h>
#include <clap/helpers/event-list.hh>
#include <clap/helpers/host.hh>
#include <clap/helpers/plugin-proxy.hh>

#include "readerwriterqueue.h"
#include "constants.h"

namespace synth_canvas::host
{
    class AudioEngine;
}

constexpr auto PluginHost_MH = clap::helpers::MisbehaviourHandler::Terminate;
constexpr auto PluginHost_CL = clap::helpers::CheckingLevel::Maximal;

using BaseHost = clap::helpers::Host<PluginHost_MH, PluginHost_CL>;
extern template class clap::helpers::Host<PluginHost_MH, PluginHost_CL>;

using PluginProxy = clap::helpers::PluginProxy<PluginHost_MH, PluginHost_CL>;
extern template class clap::helpers::PluginProxy<PluginHost_MH, PluginHost_CL>;

namespace synth_canvas::host
{
    class PluginHost final : public BaseHost
    {
    public:
        enum PluginState
        {
            Inactive,
            InactiveWithError,
            ActiveAndSleeping,
            ActiveAndProcessing,
            ActiveWithError,
            ActiveAndReadyToDeactivate,
        };

        PluginHost();
        ~PluginHost();

        bool load(const std::string &path, int pluginIndex);
        void unload();

        bool canActivate() const;
        void activate(int32_t sample_rate, int32_t blockSize);
        void deactivate();
        void set_processing_enabled(bool enabled);

        void setParameterValue(clap_id param_id, double value);
        void pollMainThread();

        void setPorts(int numInputs, float **inputs, int numOutputs, float **outputs);

        void processBegin(int nframes);
        void processNoteOn(int sampleOffset, int channel, int key, int velocity);
        void processNoteOff(int sampleOffset, int channel, int key, int velocity);
        void processCC(int sampleOffset, int channel, int cc, int value);
        void process();
        void processEnd(int nframes);

        std::function<void(clap_id, double)> on_parameter_changed;

        bool isPluginActive() const;
        bool isPluginProcessing() const;
        bool isPluginSleeping() const;
        void setPluginState(PluginState state);

        void setInstanceId(uint32_t id) { _instance_id = id; }
        uint32_t getInstanceId() const { return _instance_id; }

    protected:
        void requestRestart() noexcept override;
        void requestProcess() noexcept override;
        void requestCallback() noexcept override;
        bool implementsGui() const noexcept override { return false; }

        bool implementsLog() const noexcept override { return true; }
        void logLog(clap_log_severity severity, const char *message) const noexcept override;
        bool implementsParams() const noexcept override { return true; }
        void paramsRescan(clap_param_rescan_flags flags) noexcept override;
        void paramsClear(clap_id paramId, clap_param_clear_flags flags) noexcept override;
        void paramsRequestFlush() noexcept override;
        bool implementsPosixFdSupport() const noexcept override { return false; }
        bool posixFdSupportRegisterFd(int fd, clap_posix_fd_flags_t flags) noexcept override { return false; }
        bool posixFdSupportModifyFd(int fd, clap_posix_fd_flags_t flags) noexcept override { return false; }
        bool posixFdSupportUnregisterFd(int fd) noexcept override { return false; }
        bool implementsRemoteControls() const noexcept override { return false; }
        void remoteControlsChanged() noexcept override {}
        void remoteControlsSuggestPage(clap_id pageId) noexcept override {}
        bool implementsState() const noexcept override { return false; }
        void stateMarkDirty() noexcept override;
        bool implementsTimerSupport() const noexcept override { return false; }
        bool timerSupportRegisterTimer(uint32_t periodMs, clap_id *timerId) noexcept override { return false; }
        bool timerSupportUnregisterTimer(clap_id timerId) noexcept override { return false; }
        bool threadCheckIsMainThread() const noexcept override;
        bool threadCheckIsAudioThread() const noexcept override;
        bool implementsThreadPool() const noexcept override { return false; }
        bool threadPoolRequestExec(uint32_t numTasks) noexcept override { return false; }

    private:
        void checkForMainThread();
        void checkForAudioThread();

        void generatePluginInputEvents();
        void handlePluginOutputEvents();

        void *_libraryHandle = nullptr;
        const clap_plugin_entry *_pluginEntry = nullptr;
        const clap_plugin_factory *_pluginFactory = nullptr;
        std::unique_ptr<PluginProxy> _plugin;
        clap_audio_buffer _audioIn = {};
        clap_audio_buffer _audioOut = {};
        clap::helpers::EventList _evIn;
        clap::helpers::EventList _evOut;
        clap_process _process;
        
        std::unordered_map<clap_id, bool> _isAdjustingParameter;
        PluginState _state = Inactive;
        bool _stateIsDirty = false;
        bool _scheduleRestart = false;
        bool _scheduleParamFlush = false;
        bool _scheduleMainThreadCallback = false;

        uint32_t _audio_input_ports_count = constants::DEFAULT_AUDIO_PORT_COUNT;
        uint32_t _audio_output_ports_count = constants::DEFAULT_AUDIO_PORT_COUNT;
        bool _has_note_input = true;

        std::atomic<bool> _schedule_processing{false};  
        std::atomic<bool> _is_processing_active{false}; 

        struct PluginEvent
        {
            union {
                clap_event_header_t         header;
                clap_event_note_t           note;
                clap_event_midi_t           midi;
                clap_event_param_value_t    param_value;
            } event;
        };
        
        moodycamel::ReaderWriterQueue<PluginEvent> _to_plugin_event_queue{constants::EVENT_QUEUE_SIZE};
        moodycamel::ReaderWriterQueue<PluginEvent> _from_plugin_event_queue{constants::EVENT_QUEUE_SIZE};

        uint32_t _instance_id = 0;
    };

} // namespace synth_canvas::host

#endif // SYNTH_CANVAS_HOST_PLUGIN_HOST_H