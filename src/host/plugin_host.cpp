#include "plugin_host.h"

#include <clap/helpers/host.hxx>
#include <clap/helpers/plugin-proxy.hxx>
#include <clap/helpers/reducing-param-queue.hxx>

#include "logger.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <thread>
#include <utility>

namespace synth_canvas::host {

enum class ThreadType {
    kUnknown,
    kMainThread,
    kAudioThread,
};

thread_local ThreadType g_thread_type = ThreadType::kUnknown;

void logMessage(clap_log_severity severity, const char* msg) {
    std::string prefix;
    switch (severity) {
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
    : BaseHost("SynthCanvas Host", "DURUMI", "0.1.0", "https://github.com/SPACEGG/SynthCanvas") {
    g_thread_type = ThreadType::kMainThread;
}

PluginHost::~PluginHost() {
    checkForMainThread();
    unload();
}

void PluginHost::checkForMainThread() {
    if (g_thread_type != ThreadType::kMainThread) [[unlikely]] {
        logMessage(CLAP_LOG_FATAL, "Requires Main Thread!");
        std::terminate();
    }
}

void PluginHost::checkForAudioThread() {
    if (g_thread_type != ThreadType::kAudioThread) {
        logMessage(CLAP_LOG_FATAL, "Requires Audio Thread!");
        std::terminate();
    }
}

auto PluginHost::threadCheckIsMainThread() const noexcept -> bool {
    return g_thread_type == ThreadType::kMainThread;
}

auto PluginHost::threadCheckIsAudioThread() const noexcept -> bool {
    return g_thread_type == ThreadType::kAudioThread;
}

void PluginHost::requestRestart() noexcept {
    _schedule_restart = true;
    logMessage(CLAP_LOG_INFO, "Plugin requested restart.");
}

void PluginHost::requestProcess() noexcept {
    _schedule_processing.store(true, std::memory_order_release);
    logMessage(CLAP_LOG_INFO, "Plugin requested process.");
}

void PluginHost::requestCallback() noexcept {
    _schedule_main_thread_callback = true;
    logMessage(CLAP_LOG_INFO, "Plugin requested main thread callback.");
}

void PluginHost::logLog(clap_log_severity severity, const char* message) const noexcept {
    logMessage(severity, message);
}

void PluginHost::paramsRescan(clap_param_rescan_flags flags) noexcept {
    logMessage(CLAP_LOG_INFO,
               ("Plugin requested parameter rescan with flags: " + std::to_string(flags)).c_str());
}

void PluginHost::paramsClear(clap_id param_id, clap_param_clear_flags flags) noexcept {
    logMessage(CLAP_LOG_INFO,
               ("Plugin requested parameter clear for ID: " + std::to_string(param_id)).c_str());
}

void PluginHost::paramsRequestFlush() noexcept {
    logMessage(CLAP_LOG_INFO, "Plugin requested parameter flush.");
}

void PluginHost::stateMarkDirty() noexcept {
    _state_is_dirty = true;
    logMessage(CLAP_LOG_INFO, "Plugin marked state as dirty.");
}

auto PluginHost::load(const std::string& path, int plugin_index) -> bool {
    unload();

    logMessage(CLAP_LOG_INFO, ("Attempting to load plugin: " + path).c_str());

#if defined(_WIN32)
    _library_handle = LoadLibraryA(path.c_str());
    if (!_library_handle) {
        log_message(CLAP_LOG_ERROR,
                    ("Failed to load plugin library: " + std::to_string(GetLastError())).c_str());
        return false;
    }
    _plugin_entry = reinterpret_cast<const struct clap_plugin_entry*>(
        GetProcAddress((HMODULE)_library_handle, "clap_entry"));
#else
    _library_handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!_library_handle) {
        logMessage(CLAP_LOG_ERROR,
                   ("Failed to load plugin library: " + std::string(dlerror())).c_str());
        return false;
    }
    _plugin_entry =
        reinterpret_cast<const struct clap_plugin_entry*>(dlsym(_library_handle, "clap_entry"));
#endif

    if (!_plugin_entry) {
#if defined(_WIN32)
        log_message(CLAP_LOG_ERROR, "Unable to resolve entry point 'clap_entry'");
        FreeLibrary((HMODULE)_library_handle);
#else
        logMessage(
            CLAP_LOG_ERROR,
            ("Unable to resolve entry point 'clap_entry': " + std::string(dlerror())).c_str());
        dlclose(_library_handle);
#endif
        _library_handle = nullptr;
        return false;
    }

    _plugin_entry->init(path.c_str());

    _plugin_factory =
        static_cast<const clap_plugin_factory*>(_plugin_entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!_plugin_factory) {
        logMessage(CLAP_LOG_ERROR, "Failed to get CLAP plugin factory.");
        _plugin_entry->deinit();
#if defined(_WIN32)
        FreeLibrary((HMODULE)_library_handle);
#else
        dlclose(_library_handle);
#endif
        _library_handle = nullptr;
        return false;
    }

    auto count = _plugin_factory->get_plugin_count(_plugin_factory);
    if (std::cmp_greater_equal(plugin_index, count)) {
        logMessage(CLAP_LOG_ERROR, ("Plugin index " + std::to_string(plugin_index) +
                                    " is invalid, expected at most " + std::to_string(count - 1))
                                       .c_str());
        _plugin_entry->deinit();
#if defined(_WIN32)
        FreeLibrary((HMODULE)_library_handle);
#else
        dlclose(_library_handle);
#endif
        _library_handle = nullptr;
        return false;
    }

    auto desc = _plugin_factory->get_plugin_descriptor(_plugin_factory, plugin_index);
    if (!desc) {
        logMessage(CLAP_LOG_ERROR, "No plugin descriptor found.");
        _plugin_entry->deinit();
#if defined(_WIN32)
        FreeLibrary((HMODULE)_library_handle);
#else
        dlclose(_library_handle);
#endif
        _library_handle = nullptr;
        return false;
    }

    if (!clap_version_is_compatible(desc->clap_version)) {
        logMessage(CLAP_LOG_ERROR, "Incompatible CLAP version.");
        _plugin_entry->deinit();
#if defined(_WIN32)
        FreeLibrary((HMODULE)_library_handle);
#else
        dlclose(_library_handle);
#endif
        _library_handle = nullptr;
        return false;
    }

    logMessage(CLAP_LOG_INFO, ("Loading plugin with id: " + std::string(desc->id) +
                               ", index: " + std::to_string(plugin_index))
                                  .c_str());

    const auto kRawPlugin = _plugin_factory->create_plugin(_plugin_factory, clapHost(), desc->id);
    if (!kRawPlugin) {
        logMessage(CLAP_LOG_ERROR,
                   ("Could not create the plugin with id: " + std::string(desc->id)).c_str());
        _plugin_entry->deinit();
#if defined(_WIN32)
        FreeLibrary((HMODULE)_library_handle);
#else
        dlclose(_library_handle);
#endif
        _library_handle = nullptr;
        return false;
    }

    _plugin = std::make_unique<PluginProxy>(*kRawPlugin, *this);

    if (!_plugin->init()) {
        logMessage(CLAP_LOG_ERROR,
                   ("Could not init the plugin with id: " + std::string(desc->id)).c_str());
        _plugin.reset();
        _plugin_entry->deinit();
#if defined(_WIN32)
        FreeLibrary((HMODULE)_library_handle);
#else
        dlclose(_library_handle);
#endif
        _library_handle = nullptr;
        return false;
    }

    auto audio_ports_ext = static_cast<const clap_plugin_audio_ports_t*>(
        kRawPlugin->get_extension(kRawPlugin, CLAP_EXT_AUDIO_PORTS));

    if (audio_ports_ext) {
        _audio_input_ports_count = audio_ports_ext->count(kRawPlugin, true);
        _audio_output_ports_count = audio_ports_ext->count(kRawPlugin, false);
    } else {
        _audio_input_ports_count = 1;
        _audio_output_ports_count = 1;
    }

    auto note_ports_ext = static_cast<const clap_plugin_note_ports_t*>(
        kRawPlugin->get_extension(kRawPlugin, CLAP_EXT_NOTE_PORTS));

    if (note_ports_ext) {
        uint32_t note_in_count = note_ports_ext->count(kRawPlugin, true);
        _has_note_input = (note_in_count > 0);
    } else {
        _has_note_input = false;
    }

    logMessage(CLAP_LOG_INFO,
               ("Port Config - Audio In: " + std::to_string(_audio_input_ports_count) +
                ", Audio Out: " + std::to_string(_audio_output_ports_count) +
                ", Note In: " + (_has_note_input ? "Yes" : "No"))
                   .c_str());

    setPluginState(kInactive);
    return true;
}

void PluginHost::unload() {
    if (!_library_handle) {
        return;
    }

    deactivate();

    if (_plugin) {
        _plugin.reset();
    }

    if (_plugin_entry) {
        _plugin_entry->deinit();
        _plugin_entry = nullptr;
    }

    if (_library_handle) {
#if defined(_WIN32)
        FreeLibrary((HMODULE)_library_handle);
#else
        dlclose(_library_handle);
#endif
        _library_handle = nullptr;
    }
    setPluginState(kInactive);
    logMessage(CLAP_LOG_INFO, "Plugin unloaded.");
}

auto PluginHost::canActivate() const -> bool { return _plugin != nullptr && !isPluginActive(); }

void PluginHost::activate(int32_t sample_rate, int32_t block_size) {
    if (!_plugin) {
        return;
    }

    if (!_plugin->activate(sample_rate, 1, block_size)) {
        setPluginState(kInactiveWithError);
        logMessage(CLAP_LOG_ERROR, "Failed to activate plugin.");
        return;
    }

    _schedule_processing.store(true, std::memory_order_release);

    setPluginState(kActiveAndSleeping);
    logMessage(CLAP_LOG_INFO, "Plugin activated.");
}

void PluginHost::deactivate() {
    if (!isPluginActive()) {
        return;
    }

    _schedule_processing.store(false, std::memory_order_release);

    auto start_time = std::chrono::steady_clock::now();
    while (_is_processing_active.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() - start_time >
            std::chrono::milliseconds(constants::kPluginDeactivateTimeoutMs)) {
            logMessage(CLAP_LOG_WARNING,
                       "Timeout waiting for audio thread to stop processing. Force deactivating.");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(constants::kPluginDeactivateSleepMs));
    }

    if (_plugin) {
        _plugin->deactivate();
    }
    setPluginState(kInactive);
    logMessage(CLAP_LOG_INFO, "Plugin deactivated.");
}

void PluginHost::setProcessingEnabled(bool enabled) {
    _schedule_processing.store(enabled, std::memory_order_release);
}

void PluginHost::setParameterValue(clap_id param_id, double value) {
    PluginEvent ev;
    ev.event.header.size = sizeof(clap_event_param_value);
    ev.event.header.time = 0;
    ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.event.header.type = CLAP_EVENT_PARAM_VALUE;
    ev.event.header.flags = 0;

    ev.event.param_value.param_id = param_id;
    ev.event.param_value.value = value;
    ev.event.param_value.cookie = nullptr;
    ev.event.param_value.note_id = constants::kClapInvalidId;
    ev.event.param_value.port_index = constants::kClapInvalidId;
    ev.event.param_value.key = constants::kClapInvalidId;
    ev.event.param_value.channel = constants::kClapInvalidId;

    _input_events.try_enqueue(ev);
}

void PluginHost::pollMainThread() {
    checkForMainThread();

    PluginEvent ev;
    while (_output_events_to_main.try_dequeue(ev)) {
        if (ev.event.header.type == CLAP_EVENT_PARAM_VALUE) {
            if (on_parameter_changed) {
                on_parameter_changed(ev.event.param_value.param_id, ev.event.param_value.value);
            }
        }
    }
}

void PluginHost::setPorts(int num_inputs, float** inputs, int num_outputs, float** outputs) {
    _audio_in.channel_count = num_inputs;
    _audio_in.data32 = inputs;
    _audio_in.data64 = nullptr;
    _audio_in.constant_mask = 0;
    _audio_in.latency = 0;

    _audio_out.channel_count = num_outputs;
    _audio_out.data32 = outputs;
    _audio_out.data64 = nullptr;
    _audio_out.constant_mask = 0;
    _audio_out.latency = 0;
}

void PluginHost::processBegin(int nframes) {
    g_thread_type = ThreadType::kAudioThread;
    _process.frames_count = nframes;
}

void PluginHost::processEnd(int nframes) {
    g_thread_type = ThreadType::kUnknown;
    _process.frames_count = nframes;
}

void PluginHost::processNoteOn(int sample_offset, int channel, int key, double velocity,
                               int32_t note_id) {
    if (!_plugin || !_has_note_input) return;

    PluginEvent ev;
    ev.event.header.size = sizeof(clap_event_note);
    ev.event.header.time = sample_offset;
    ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.event.header.type = CLAP_EVENT_NOTE_ON;
    ev.event.header.flags = 0;

    ev.event.note.port_index = constants::kDefaultEventPortIndex;
    ev.event.note.key = key;
    ev.event.note.channel = channel;
    ev.event.note.note_id = note_id;
    ev.event.note.velocity = velocity;

    _input_events.try_enqueue(ev);
}

void PluginHost::processNoteOff(int sample_offset, int channel, int key, double velocity,
                                int32_t note_id) {
    if (!_plugin || !_has_note_input) return;

    PluginEvent ev;
    ev.event.header.size = sizeof(clap_event_note);
    ev.event.header.time = sample_offset;
    ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.event.header.type = CLAP_EVENT_NOTE_OFF;
    ev.event.header.flags = 0;

    ev.event.note.port_index = constants::kDefaultEventPortIndex;
    ev.event.note.key = key;
    ev.event.note.channel = channel;
    ev.event.note.note_id = note_id;
    ev.event.note.velocity = velocity;

    _input_events.try_enqueue(ev);
}

void PluginHost::process() {
    checkForAudioThread();

    if (!_plugin) return;

    if (!isPluginActive()) return;

    bool should_process = _schedule_processing.load(std::memory_order_acquire);
    bool is_currently_processing = _is_processing_active.load(std::memory_order_relaxed);

    if (should_process && !is_currently_processing) {
        if (_plugin->startProcessing()) {
            _is_processing_active.store(true, std::memory_order_release);
            setPluginState(kActiveAndProcessing);
            is_currently_processing = true;
        } else {
            setPluginState(kActiveWithError);
            return;
        }
    } else if (!should_process && is_currently_processing) {
        _plugin->stopProcessing();
        _is_processing_active.store(false, std::memory_order_release);
        setPluginState(kActiveAndSleeping);
        is_currently_processing = false;
    }

    if (_state == kActiveWithError) return;

    if (!is_currently_processing) {
        return;
    }

    _process.transport = nullptr;

    _process.in_events = _ev_in.clapInputEvents();
    _process.out_events = _ev_out.clapOutputEvents();

    if (_audio_input_ports_count == 0) {
        _process.audio_inputs = nullptr;
        _process.audio_inputs_count = 0;
    } else {
        _process.audio_inputs = &_audio_in;
        _process.audio_inputs_count = 1;
    }

    if (_audio_output_ports_count == 0) {
        _process.audio_outputs = nullptr;
        _process.audio_outputs_count = 0;
    } else {
        _process.audio_outputs = &_audio_out;
        _process.audio_outputs_count = 1;
    }

    _ev_out.clear();
    generatePluginInputEvents();

    _plugin->process(&_process);

    handlePluginOutputEvents();

    _ev_out.clear();
    _ev_in.clear();

    g_thread_type = ThreadType::kUnknown;
}

void PluginHost::generatePluginInputEvents() {
    PluginEvent ev;
    while (_input_events.try_dequeue(ev)) {
        _ev_in.push(&ev.event.header);
    }
}

void PluginHost::handlePluginOutputEvents() {
    for (uint32_t i = 0; i < _ev_out.size(); ++i) {
        const auto* ev = _ev_out.get(i);
        if (!ev || ev->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;

        switch (ev->type) {
            case CLAP_EVENT_PARAM_VALUE: {
                auto vev = reinterpret_cast<const clap_event_param_value*>(ev);

                PluginEvent out_ev;
                out_ev.event.param_value = *vev;
                _output_events_to_main.try_enqueue(out_ev);
                _output_events_to_audio.try_enqueue(out_ev);
                break;
            }
            case CLAP_EVENT_NOTE_ON:
            case CLAP_EVENT_NOTE_OFF:
            case CLAP_EVENT_NOTE_CHOKE:
            case CLAP_EVENT_NOTE_EXPRESSION: {
                auto nev = reinterpret_cast<const clap_event_note*>(ev);

                PluginEvent out_ev;
                out_ev.event.note = *nev;
                _output_events_to_audio.try_enqueue(out_ev);
                break;
            }
        }
    }
}

void PluginHost::setPluginState(PluginState state) { _state = state; }

auto PluginHost::isPluginActive() const -> bool {
    return _state != kInactive && _state != kInactiveWithError;
}

auto PluginHost::isPluginProcessing() const -> bool { return _state == kActiveAndProcessing; }

auto PluginHost::isPluginSleeping() const -> bool { return _state == kActiveAndSleeping; }

}  // namespace synth_canvas::host

template class clap::helpers::Host<kPluginHostMh, kPluginHostCl>;
template class clap::helpers::PluginProxy<kPluginHostMh, kPluginHostCl>;