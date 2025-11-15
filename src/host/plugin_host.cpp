#include <godot_cpp/variant/utility_functions.hpp>

#include "plugin_host.h"

#include <clap/helpers/host.hxx>
#include <clap/helpers/plugin-proxy.hxx>
#include <clap/helpers/reducing-param-queue.hxx>

// #include "audio_engine.h" // No longer needed for GUI callbacks

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

// For thread management
#include <thread>
#include <mutex>
#include <condition_variable>

namespace synth_canvas::host
{

    // Global thread type for checking, similar to clap-host's approach
    enum class ThreadType
    {
        Unknown,
        MainThread,
        AudioThread,
        // AudioThreadPool, // Not implementing thread pool initially
    };

    thread_local ThreadType g_thread_type = ThreadType::Unknown;

    // Helper for logging
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
        godot::UtilityFunctions::print(godot::String(prefix.c_str()) + " " + godot::String(msg));
        if (severity >= CLAP_LOG_ERROR)
        {
            std::cerr << prefix << " " << msg << std::endl;
        }
    }

    PluginHost::PluginHost()
        : BaseHost("SynthCanvas Host",                      // name
                   "DURUMI",                                // vendor
                   "0.1.0",                                 // version
                   "https://github.com/SPACEGG/SynthCanvas" // url
          )
    {
        g_thread_type = ThreadType::MainThread;
    }

    PluginHost::~PluginHost()
    {
        // checkForMainThread(); // Re-enable if thread checking is fully implemented
        unload();
    }

    // --- Thread Checking --- (Simplified for now)
    void PluginHost::checkForMainThread()
    {
        // if (g_thread_type != ThreadType::MainThread) [[unlikely]] {
        //     log_message(CLAP_LOG_FATAL, "Requires Main Thread!");
        //     std::terminate();
        // }
    }

    void PluginHost::checkForAudioThread()
    {
        // if (g_thread_type != ThreadType::AudioThread) {
        //     log_message(CLAP_LOG_FATAL, "Requires Audio Thread!");
        //     std::terminate();
        // }
    }

    bool PluginHost::threadCheckIsMainThread() const noexcept
    {
        return g_thread_type == ThreadType::MainThread;
    }

    bool PluginHost::threadCheckIsAudioThread() const noexcept
    {
        return g_thread_type == ThreadType::AudioThread;
    }

    // --- CLAP Host Callbacks (Simplified/Adapted) ---
    void PluginHost::requestRestart() noexcept
    {
        _scheduleRestart = true;
        // In a real scenario, you'd signal the main thread to handle this.
        log_message(CLAP_LOG_INFO, "Plugin requested restart.");
    }

    void PluginHost::requestProcess() noexcept
    {
        _scheduleProcess = true;
        // In a real scenario, you'd signal the audio thread to process.
        log_message(CLAP_LOG_INFO, "Plugin requested process.");
    }

    void PluginHost::requestCallback() noexcept
    {
        _scheduleMainThreadCallback = true;
        // In a real scenario, you'd signal the main thread to handle this.
        log_message(CLAP_LOG_INFO, "Plugin requested main thread callback.");
    }

    void PluginHost::logLog(clap_log_severity severity, const char *message) const noexcept
    {
        log_message(severity, message);
    }

    void PluginHost::paramsRescan(clap_param_rescan_flags flags) noexcept
    {
        log_message(CLAP_LOG_INFO, ("Plugin requested parameter rescan with flags: " + std::to_string(flags)).c_str());
        // TODO: Implement parameter scanning logic
    }

    void PluginHost::paramsClear(clap_id paramId, clap_param_clear_flags flags) noexcept
    {
        log_message(CLAP_LOG_INFO, ("Plugin requested parameter clear for ID: " + std::to_string(paramId)).c_str());
        // TODO: Implement parameter clear logic
    }

    void PluginHost::paramsRequestFlush() noexcept
    {
        log_message(CLAP_LOG_INFO, "Plugin requested parameter flush.");
        // TODO: Implement parameter flush logic
    }

    void PluginHost::stateMarkDirty() noexcept
    {
        _stateIsDirty = true;
        log_message(CLAP_LOG_INFO, "Plugin marked state as dirty.");
    }

    // --- PluginHost Core Logic ---
    bool PluginHost::load(const std::string &path, int pluginIndex)
    {
        unload(); // Ensure any previous plugin is unloaded

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

        // Create a copy of the plugin structure to ensure its lifetime
        _plugin_instance = std::make_unique<clap_plugin_t>(*raw_plugin);

        // Initialize the proxy with our copy of the plugin instance
        _plugin = std::make_unique<PluginProxy>(*_plugin_instance, *this);

        if (!_plugin->init())
        {
            log_message(CLAP_LOG_ERROR, ("Could not init the plugin with id: " + std::string(desc->id)).c_str());
            _plugin.reset();
            _plugin_instance.reset();
            _pluginEntry->deinit();
#if defined(_WIN32)
            FreeLibrary((HMODULE)_libraryHandle);
#else
            dlclose(_libraryHandle);
#endif
            _libraryHandle = nullptr;
            return false;
        }

        setPluginState(Inactive);
        return true;
    }

    void PluginHost::unload()
    {
        if (!_libraryHandle)
        {
            return; // No plugin loaded
        }

        deactivate();

        if (_plugin)
        {
            // The unique_ptr's destructor will call the PluginProxy's destructor,
            // which in turn calls plugin->destroy().
            _plugin.reset();
        }
        if (_plugin_instance)
        {
            _plugin_instance.reset();
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
        // Simplified: assume always true if plugin is loaded and not active
        return _plugin != nullptr && !isPluginActive();
    }

    void PluginHost::activate(int32_t sample_rate, int32_t blockSize)
    {
        if (!_plugin)
        {
            return;
        }

        if (!_plugin->activate(sample_rate, blockSize, blockSize))
        {
            setPluginState(InactiveWithError);
            log_message(CLAP_LOG_ERROR, "Failed to activate plugin.");
            return;
        }

        _scheduleProcess = true;
        setPluginState(ActiveAndSleeping);
        log_message(CLAP_LOG_INFO, "Plugin activated.");
    }

    void PluginHost::deactivate()
    {
        if (!isPluginActive())
        {
            return;
        }

        // In a real scenario, you'd wait for audio thread to finish processing
        // For now, just deactivate directly.
        if (_plugin)
        {
            if (isPluginProcessing())
            {
                _plugin->stopProcessing();
            }
            _plugin->deactivate();
        }
        setPluginState(Inactive);
        log_message(CLAP_LOG_INFO, "Plugin deactivated.");
    }

    void PluginHost::setParameterValue(clap_id param_id, double value)
    {
        checkForMainThread();
        _appToEngineValueQueue.set(param_id, {nullptr, value});
    }

    void PluginHost::pollMainThread()
    {
        checkForMainThread();
        _engineToAppValueQueue.consume([this](const clap_id &param_id, const EngineToAppParamQueueValue &v)
                                       {
            if (v.has_value && on_parameter_changed)
            {
                on_parameter_changed(param_id, v.value);
            } });
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
        // _process.steady_time = _audioEngine._steadyTime; // AudioEngine needs _steadyTime
    }

    void PluginHost::processEnd(int nframes)
    {
        g_thread_type = ThreadType::Unknown;
        _process.frames_count = nframes;
        // _process.steady_time = _audioEngine._steadyTime; // AudioEngine needs _steadyTime
    }

    void PluginHost::processNoteOn(int sampleOffset, int channel, int key, int velocity)
    {
        // checkForAudioThread(); // Re-enable if thread checking is fully implemented

        if (!_plugin)
            return;

        clap_event_note ev;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_NOTE_ON;
        ev.header.time = sampleOffset;
        ev.header.flags = 0;
        ev.header.size = sizeof(ev);
        ev.port_index = 0;
        ev.key = key;
        ev.channel = channel;
        ev.note_id = -1;
        ev.velocity = velocity / 127.0;

        _evIn.push(&ev.header);
    }

    void PluginHost::processNoteOff(int sampleOffset, int channel, int key, int velocity)
    {
        // checkForAudioThread(); // Re-enable if thread checking is fully implemented

        if (!_plugin)
            return;

        clap_event_note ev;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_NOTE_OFF;
        ev.header.time = sampleOffset;
        ev.header.flags = 0;
        ev.header.size = sizeof(ev);
        ev.port_index = 0;
        ev.key = key;
        ev.channel = channel;
        ev.note_id = -1;
        ev.velocity = velocity / 127.0;

        _evIn.push(&ev.header);
    }

    void PluginHost::processCC(int sampleOffset, int channel, int cc, int value)
    {
        // checkForAudioThread(); // Re-enable if thread checking is fully implemented

        if (!_plugin)
            return;

        clap_event_midi ev;
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.type = CLAP_EVENT_MIDI;
        ev.header.time = sampleOffset;
        ev.header.flags = 0;
        ev.header.size = sizeof(ev);
        ev.port_index = 0;
        ev.data[0] = 0xB0 | channel;
        ev.data[1] = cc;
        ev.data[2] = value;

        _evIn.push(&ev.header);
    }

    void PluginHost::process()
    {
        // checkForAudioThread(); // Re-enable if thread checking is fully implemented

        if (!_plugin)
            return;

        // Can't process a plugin that is not active
        if (!isPluginActive())
            return;

        if (_plugin_instance && _plugin_instance->process == nullptr)
        {
            std::stringstream ss;
            ss << "[DEBUG] FATAL in PluginHost::process() on instance " << this << ". process ptr is NULL!";
            log_message(CLAP_LOG_FATAL, ss.str().c_str());
            return; // Avoid crash
        }

        // Do we want to deactivate the plugin?
        if (_scheduleDeactivate)
        {
            _scheduleDeactivate = false;
            if (_state == ActiveAndProcessing)
                _plugin->stopProcessing();
            setPluginState(ActiveAndReadyToDeactivate);
            return;
        }

        // We can't process a plugin which failed to start processing
        if (_state == ActiveWithError)
            return;

        _process.transport = nullptr; // TODO: Implement transport if needed

        _process.in_events = _evIn.clapInputEvents();
        _process.out_events = _evOut.clapOutputEvents();

        _process.audio_inputs = &_audioIn;
        _process.audio_inputs_count = 1;
        _process.audio_outputs = &_audioOut;
        _process.audio_outputs_count = 1;

        _evOut.clear();
        generatePluginInputEvents(); // Handle parameter changes from host

        if (isPluginSleeping())
        {
            if (!_scheduleProcess && _evIn.empty())
                return;

            _scheduleProcess = false;
            if (!_plugin->startProcessing())
            {
                setPluginState(ActiveWithError);
                return;
            }
            setPluginState(ActiveAndProcessing);
        }

        // int32_t status = CLAP_PROCESS_SLEEP; // Original clap-host had this
        if (isPluginProcessing())
        {
            _plugin->process(&_process);
        }

        handlePluginOutputEvents(); // Handle parameter changes from plugin

        _evOut.clear();
        _evIn.clear();

        _engineToAppValueQueue.producerDone();

        // TODO: send plugin to sleep if possible
        // g_thread_type = ThreadType::Unknown; // Reset thread type after processing
    }

    void PluginHost::generatePluginInputEvents()
    {
        _appToEngineValueQueue.consume([this](const clap_id &param_id, const AppToEngineParamQueueValue &v)
                                       {
            clap_event_param_value ev;
            ev.header.size = sizeof(ev);
            ev.header.time = 0; // Process immediately
            ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            ev.header.type = CLAP_EVENT_PARAM_VALUE;
            ev.header.flags = 0;
            ev.param_id = param_id;
            ev.cookie = v.cookie;
            ev.value = v.value;
            ev.note_id = -1;
            ev.port_index = -1;
            ev.key = -1;
            ev.channel = -1;

            _evIn.push(&ev.header); });
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
                EngineToAppParamQueueValue v;
                v.has_value = true;
                v.value = vev->value;
                _engineToAppValueQueue.set(vev->param_id, v);
                break;
            }
            }
        }
    }

    // --- Plugin State Management ---
    void PluginHost::setPluginState(PluginState state)
    {
        // Basic state transitions, can be made more robust if needed
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

// Explicit template instantiation for clap::helpers::Host and PluginProxy
template class clap::helpers::Host<PluginHost_MH, PluginHost_CL>;
template class clap::helpers::PluginProxy<PluginHost_MH, PluginHost_CL>;
