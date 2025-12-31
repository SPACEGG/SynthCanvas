#include "plugin_host.h"
#include "logger.h"

#include <clap/helpers/host.hxx>
#include <clap/helpers/plugin-proxy.hxx>
#include <clap/helpers/reducing-param-queue.hxx>

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <thread>
#include <mutex>
#include <condition_variable>

namespace synth_canvas::host
{

    enum class ThreadType
    {
        Unknown,
        MainThread,
        AudioThread,
    };

    thread_local ThreadType g_thread_type = ThreadType::Unknown;

    void log_message(clap_log_severity severity, const char *msg)
    {
        std::string prefix;
        switch (severity)
        {
        case CLAP_LOG_DEBUG:
            prefix = "[DEBUG]";
            break;
        case CLAP_LOG_INFO:
            prefix = "[INFO]";
            break;
        case CLAP_LOG_WARNING:
            prefix = "[WARNING]";
            break;
        case CLAP_LOG_ERROR:
            prefix = "[ERROR]";
            break;
        case CLAP_LOG_FATAL:
            prefix = "[FATAL]";
            break;
        case CLAP_LOG_HOST_MISBEHAVING:
            prefix = "[HOST MISBEHAVING]";
            break;
        default:
            prefix = "[UNKNOWN]";
            break;
        }
        log(prefix, " ", msg);
    }

    PluginHost::PluginHost()
        : BaseHost("SynthCanvas Host",
                   "DURUMI",
                   "0.1.0",
                   "https://github.com/SPACEGG/SynthCanvas"
          )
    {
        g_thread_type = ThreadType::MainThread;
    }

    PluginHost::~PluginHost()
    {
        checkForMainThread();
        unload();
    }

    void PluginHost::checkForMainThread()
    {
        if (g_thread_type != ThreadType::MainThread) [[unlikely]]
        {
            log_message(CLAP_LOG_FATAL, "Requires Main Thread!");
            std::terminate();
        }
    }

    void PluginHost::checkForAudioThread()
    {
        if (g_thread_type != ThreadType::AudioThread)
        {
            log_message(CLAP_LOG_FATAL, "Requires Audio Thread!");
            std::terminate();
        }
    }

    bool PluginHost::threadCheckIsMainThread() const noexcept
    {
        return g_thread_type == ThreadType::MainThread;
    }

    bool PluginHost::threadCheckIsAudioThread() const noexcept
    {
        return g_thread_type == ThreadType::AudioThread;
    }

    void PluginHost::requestRestart() noexcept
    {
        _scheduleRestart = true;
        log_message(CLAP_LOG_INFO, "Plugin requested restart.");
    }

    void PluginHost::requestProcess() noexcept
    {
        _schedule_processing.store(true, std::memory_order_release);
        log_message(CLAP_LOG_INFO, "Plugin requested process.");
    }

    void PluginHost::requestCallback() noexcept
    {
        _scheduleMainThreadCallback = true;
        log_message(CLAP_LOG_INFO, "Plugin requested main thread callback.");
    }

    void PluginHost::logLog(clap_log_severity severity, const char *message) const noexcept
    {
        log_message(severity, message);
    }

    void PluginHost::paramsRescan(clap_param_rescan_flags flags) noexcept
    {
        log_message(CLAP_LOG_INFO, ("Plugin requested parameter rescan with flags: " + std::to_string(flags)).c_str());
    }

    void PluginHost::paramsClear(clap_id paramId, clap_param_clear_flags flags) noexcept
    {
        log_message(CLAP_LOG_INFO, ("Plugin requested parameter clear for ID: " + std::to_string(paramId)).c_str());
    }

    void PluginHost::paramsRequestFlush() noexcept
    {
        log_message(CLAP_LOG_INFO, "Plugin requested parameter flush.");
    }

    void PluginHost::stateMarkDirty() noexcept
    {
        _stateIsDirty = true;
        log_message(CLAP_LOG_INFO, "Plugin marked state as dirty.");
    }

    bool PluginHost::load(const std::string &path, int pluginIndex)
    {
        unload();

        log_message(CLAP_LOG_INFO, ("Attempting to load plugin: " + path).c_str());

#if defined(_WIN32)
        _libraryHandle = LoadLibraryA(path.c_str());
        if (!_libraryHandle)
        {
            log_message(CLAP_LOG_ERROR, ("Failed to load plugin library: " + std::to_string(GetLastError())).c_str());
            return false;
        }
        _pluginEntry = reinterpret_cast<const struct clap_plugin_entry *>(GetProcAddress((HMODULE)_libraryHandle, "clap_entry"));
#else
        _libraryHandle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!_libraryHandle)
        {
            log_message(CLAP_LOG_ERROR, ("Failed to load plugin library: " + std::string(dlerror())).c_str());
            return false;
        }
        _pluginEntry = reinterpret_cast<const struct clap_plugin_entry *>(dlsym(_libraryHandle, "clap_entry"));
#endif

        if (!_pluginEntry)
        {
#if defined(_WIN32)
            log_message(CLAP_LOG_ERROR, "Unable to resolve entry point 'clap_entry'");
            FreeLibrary((HMODULE)_libraryHandle);
#else
            log_message(CLAP_LOG_ERROR, ("Unable to resolve entry point 'clap_entry': " + std::string(dlerror())).c_str());
            dlclose(_libraryHandle);
#endif
            _libraryHandle = nullptr;
            return false;
        }

        _pluginEntry->init(path.c_str());

        _pluginFactory = static_cast<const clap_plugin_factory *>(_pluginEntry->get_factory(CLAP_PLUGIN_FACTORY_ID));
        if (!_pluginFactory)
        {
            log_message(CLAP_LOG_ERROR, "Failed to get CLAP plugin factory.");
            _pluginEntry->deinit();
#if defined(_WIN32)
            FreeLibrary((HMODULE)_libraryHandle);
#else
            dlclose(_libraryHandle);
#endif
            _libraryHandle = nullptr;
            return false;
        }

        auto count = _pluginFactory->get_plugin_count(_pluginFactory);
        if (pluginIndex >= count)
        {
            log_message(CLAP_LOG_ERROR, ("Plugin index " + std::to_string(pluginIndex) + " is invalid, expected at most " + std::to_string(count - 1)).c_str());
            _pluginEntry->deinit();
#if defined(_WIN32)
            FreeLibrary((HMODULE)_libraryHandle);
#else
            dlclose(_libraryHandle);
#endif
            _libraryHandle = nullptr;
            return false;
        }

        auto desc = _pluginFactory->get_plugin_descriptor(_pluginFactory, pluginIndex);
        if (!desc)
        {
            log_message(CLAP_LOG_ERROR, "No plugin descriptor found.");
            _pluginEntry->deinit();
#if defined(_WIN32)
            FreeLibrary((HMODULE)_libraryHandle);
#else
            dlclose(_libraryHandle);
#endif
            _libraryHandle = nullptr;
            return false;
        }

        if (!clap_version_is_compatible(desc->clap_version))
        {
            log_message(CLAP_LOG_ERROR, "Incompatible CLAP version.");
            _pluginEntry->deinit();
#if defined(_WIN32)
            FreeLibrary((HMODULE)_libraryHandle);
#else
            dlclose(_libraryHandle);
#endif
            _libraryHandle = nullptr;
            return false;
        }

        log_message(CLAP_LOG_INFO, ("Loading plugin with id: " + std::string(desc->id) + ", index: " + std::to_string(pluginIndex)).c_str());

        const auto raw_plugin = _pluginFactory->create_plugin(_pluginFactory, clapHost(), desc->id);
        if (!raw_plugin)
        {
            log_message(CLAP_LOG_ERROR, ("Could not create the plugin with id: " + std::string(desc->id)).c_str());
            _pluginEntry->deinit();
#if defined(_WIN32)
            FreeLibrary((HMODULE)_libraryHandle);
#else
            dlclose(_libraryHandle);
#endif
            _libraryHandle = nullptr;
            return false;
        }

        _plugin = std::make_unique<PluginProxy>(*raw_plugin, *this);

        if (!_plugin->init())
        {
            log_message(CLAP_LOG_ERROR, ("Could not init the plugin with id: " + std::string(desc->id)).c_str());
            _plugin.reset();
            _pluginEntry->deinit();
#if defined(_WIN32)
            FreeLibrary((HMODULE)_libraryHandle);
#else
            dlclose(_libraryHandle);
#endif
            _libraryHandle = nullptr;
            return false;
        }

        auto audio_ports_ext = static_cast<const clap_plugin_audio_ports_t *>(
            raw_plugin->get_extension(raw_plugin, CLAP_EXT_AUDIO_PORTS));

        if (audio_ports_ext)
        {
            _audio_input_ports_count = audio_ports_ext->count(raw_plugin, true);
            _audio_output_ports_count = audio_ports_ext->count(raw_plugin, false);
        }
        else
        {
            _audio_input_ports_count = 1;
            _audio_output_ports_count = 1;
        }

        auto note_ports_ext = static_cast<const clap_plugin_note_ports_t *>(
            raw_plugin->get_extension(raw_plugin, CLAP_EXT_NOTE_PORTS));

        if (note_ports_ext)
        {
            uint32_t note_in_count = note_ports_ext->count(raw_plugin, true);
            _has_note_input = (note_in_count > 0);
        }
        else
        {
            _has_note_input = false;
        }

        log_message(CLAP_LOG_INFO, ("Port Config - Audio In: " + std::to_string(_audio_input_ports_count) +
                                    ", Audio Out: " + std::to_string(_audio_output_ports_count) +
                                    ", Note In: " + (_has_note_input ? "Yes" : "No"))
                                       .c_str());

        setPluginState(Inactive);
        return true;
    }

    void PluginHost::unload()
    {
        if (!_libraryHandle)
        {
            return;
        }

        deactivate();

        if (_plugin)
        {
            _plugin.reset();
        }

        if (_pluginEntry)
        {
            _pluginEntry->deinit();
            _pluginEntry = nullptr;
        }

        if (_libraryHandle)
        {
#if defined(_WIN32)
            FreeLibrary((HMODULE)_libraryHandle);
#else
            dlclose(_libraryHandle);
#endif
            _libraryHandle = nullptr;
        }
        setPluginState(Inactive);
        log_message(CLAP_LOG_INFO, "Plugin unloaded.");
    }

    bool PluginHost::canActivate() const
    {
        return _plugin != nullptr && !isPluginActive();
    }

    void PluginHost::activate(int32_t sample_rate, int32_t blockSize)
    {
        if (!_plugin)
        {
            return;
        }

        if (!_plugin->activate(sample_rate, 1, blockSize))
        {
            setPluginState(InactiveWithError);
            log_message(CLAP_LOG_ERROR, "Failed to activate plugin.");
            return;
        }

        _schedule_processing.store(true, std::memory_order_release);

        setPluginState(ActiveAndSleeping);
        log_message(CLAP_LOG_INFO, "Plugin activated.");
    }

    void PluginHost::deactivate()
    {
        if (!isPluginActive())
        {
            return;
        }

        _schedule_processing.store(false, std::memory_order_release);

        int retry_count = 0;
        while (_is_processing_active.load(std::memory_order_acquire))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(constants::PLUGIN_DEACTIVATE_SLEEP_MS));
            if (++retry_count > constants::PLUGIN_DEACTIVATE_RETRY_COUNT)
            {
                log_message(CLAP_LOG_WARNING, "Timeout waiting for audio thread to stop processing. Force deactivating.");
                break;
            }
        }

        if (_plugin)
        {
            _plugin->deactivate();
        }
        setPluginState(Inactive);
        log_message(CLAP_LOG_INFO, "Plugin deactivated.");
    }

    void PluginHost::set_processing_enabled(bool enabled)
    {
        _schedule_processing.store(enabled, std::memory_order_release);
    }

    void PluginHost::setParameterValue(clap_id param_id, double value)
    {
        PluginEvent ev;
        ev.event.header.size = sizeof(clap_event_param_value);
        ev.event.header.time = 0;
        ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.event.header.type = CLAP_EVENT_PARAM_VALUE;
        ev.event.header.flags = 0;

        ev.event.param_value.param_id = param_id;
        ev.event.param_value.value = value;
        ev.event.param_value.cookie = nullptr;
        ev.event.param_value.note_id = constants::CLAP_INVALID_ID;
        ev.event.param_value.port_index = constants::CLAP_INVALID_ID;
        ev.event.param_value.key = constants::CLAP_INVALID_ID;
        ev.event.param_value.channel = constants::CLAP_INVALID_ID;

        _to_plugin_event_queue.try_enqueue(ev);
    }

    void PluginHost::pollMainThread()
    {
        checkForMainThread();

        PluginEvent ev;
        while (_from_plugin_event_queue.try_dequeue(ev))
        {
            if (ev.event.header.type == CLAP_EVENT_PARAM_VALUE)
            {
                if (on_parameter_changed)
                {
                    on_parameter_changed(ev.event.param_value.param_id, ev.event.param_value.value);
                }
            }
        }
    }

    void PluginHost::setPorts(int numInputs, float **inputs, int numOutputs, float **outputs)
    {
        _audioIn.channel_count = numInputs;
        _audioIn.data32 = inputs;
        _audioIn.data64 = nullptr;
        _audioIn.constant_mask = 0;
        _audioIn.latency = 0;

        _audioOut.channel_count = numOutputs;
        _audioOut.data32 = outputs;
        _audioOut.data64 = nullptr;
        _audioOut.constant_mask = 0;
        _audioOut.latency = 0;
    }

    void PluginHost::processBegin(int nframes)
    {
        g_thread_type = ThreadType::AudioThread;
        _process.frames_count = nframes;
    }

    void PluginHost::processEnd(int nframes)
    {
        g_thread_type = ThreadType::Unknown;
        _process.frames_count = nframes;
    }

    void PluginHost::processNoteOn(int sampleOffset, int channel, int key, int velocity)
    {
        if (!_plugin || !_has_note_input)
            return;

        PluginEvent ev;
        ev.event.header.size = sizeof(clap_event_note);
        ev.event.header.time = sampleOffset;
        ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.event.header.type = CLAP_EVENT_NOTE_ON;
        ev.event.header.flags = 0;

        ev.event.note.port_index = constants::DEFAULT_EVENT_PORT_INDEX;
        ev.event.note.key = key;
        ev.event.note.channel = channel;
        ev.event.note.note_id = constants::CLAP_INVALID_ID;
        ev.event.note.velocity = velocity / constants::MIDI_MAX_VELOCITY;

        _to_plugin_event_queue.try_enqueue(ev);
    }

    void PluginHost::processNoteOff(int sampleOffset, int channel, int key, int velocity)
    {
        if (!_plugin || !_has_note_input)
            return;

        PluginEvent ev;
        ev.event.header.size = sizeof(clap_event_note);
        ev.event.header.time = sampleOffset;
        ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.event.header.type = CLAP_EVENT_NOTE_OFF;
        ev.event.header.flags = 0;

        ev.event.note.port_index = constants::DEFAULT_EVENT_PORT_INDEX;
        ev.event.note.key = key;
        ev.event.note.channel = channel;
        ev.event.note.note_id = constants::CLAP_INVALID_ID;
        ev.event.note.velocity = velocity / constants::MIDI_MAX_VELOCITY;

        _to_plugin_event_queue.try_enqueue(ev);
    }

    void PluginHost::processCC(int sampleOffset, int channel, int cc, int value)
    {
        if (!_plugin || !_has_note_input)
            return;

        PluginEvent ev;
        ev.event.header.size = sizeof(clap_event_midi);
        ev.event.header.time = sampleOffset;
        ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.event.header.type = CLAP_EVENT_MIDI;
        ev.event.header.flags = 0;

        ev.event.midi.port_index = constants::DEFAULT_EVENT_PORT_INDEX;
        ev.event.midi.data[0] = constants::MIDI_CC_STATUS_BYTE | channel;
        ev.event.midi.data[1] = cc;
        ev.event.midi.data[2] = value;

        _to_plugin_event_queue.try_enqueue(ev);
    }

    void PluginHost::process()
    {
        checkForAudioThread();

        if (!_plugin)
            return;

        if (!isPluginActive())
            return;

        bool should_process = _schedule_processing.load(std::memory_order_acquire);
        bool is_currently_processing = _is_processing_active.load(std::memory_order_relaxed);

        if (should_process && !is_currently_processing)
        {
            if (_plugin->startProcessing())
            {
                _is_processing_active.store(true, std::memory_order_release);
                setPluginState(ActiveAndProcessing);
                is_currently_processing = true;
            }
            else
            {
                setPluginState(ActiveWithError);
                return;
            }
        }
        else if (!should_process && is_currently_processing)
        {
            _plugin->stopProcessing();
            _is_processing_active.store(false, std::memory_order_release);
            setPluginState(ActiveAndSleeping);
            is_currently_processing = false;
        }

        if (_state == ActiveWithError)
            return;

        if (!is_currently_processing)
        {
            return;
        }

        _process.transport = nullptr;

        _process.in_events = _evIn.clapInputEvents();
        _process.out_events = _evOut.clapOutputEvents();

        if (_audio_input_ports_count == 0)
        {
            _process.audio_inputs = nullptr;
            _process.audio_inputs_count = 0;
        }
        else
        {
            _process.audio_inputs = &_audioIn;
            _process.audio_inputs_count = 1;
        }

        if (_audio_output_ports_count == 0)
        {
            _process.audio_outputs = nullptr;
            _process.audio_outputs_count = 0;
        }
        else
        {
            _process.audio_outputs = &_audioOut;
            _process.audio_outputs_count = 1;
        }

        _evOut.clear();
        generatePluginInputEvents();

        _plugin->process(&_process);

        handlePluginOutputEvents();

        _evOut.clear();
        _evIn.clear();

        g_thread_type = ThreadType::Unknown;
    }

    void PluginHost::generatePluginInputEvents()
    {
        PluginEvent ev;
        while (_to_plugin_event_queue.try_dequeue(ev))
        {
            _evIn.push(&ev.event.header);
        }
    }

    void PluginHost::handlePluginOutputEvents()
    {
        for (uint32_t i = 0; i < _evOut.size(); ++i)
        {
            const auto *ev = _evOut.get(i);
            if (!ev || ev->space_id != CLAP_CORE_EVENT_SPACE_ID)
                continue;

            switch (ev->type)
            {
            case CLAP_EVENT_PARAM_VALUE:
            {
                auto vev = reinterpret_cast<const clap_event_param_value *>(ev);

                PluginEvent out_ev;
                out_ev.event.param_value = *vev;
                _from_plugin_event_queue.try_enqueue(out_ev);
                break;
            }
            }
        }
    }

    void PluginHost::setPluginState(PluginState state)
    {
        _state = state;
    }

    bool PluginHost::isPluginActive() const
    {
        return _state != Inactive && _state != InactiveWithError;
    }

    bool PluginHost::isPluginProcessing() const
    {
        return _state == ActiveAndProcessing;
    }

    bool PluginHost::isPluginSleeping() const
    {
        return _state == ActiveAndSleeping;
    }

} // namespace synth_canvas::host

template class clap::helpers::Host<PluginHost_MH, PluginHost_CL>;
template class clap::helpers::PluginProxy<PluginHost_MH, PluginHost_CL>;