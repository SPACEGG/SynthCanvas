#include "plugin_host.h"

#include <clap/ext/state.h>

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

#include <charconv>
#include <thread>
#include <utility>

namespace synth_canvas::host {

namespace {
auto clapOStreamWrite(const clap_ostream* stream, const void* buffer, uint64_t size) -> int64_t {
    auto* vec = static_cast<std::vector<uint8_t>*>(stream->ctx);
    const auto* src = static_cast<const uint8_t*>(buffer);
    vec->insert(vec->end(), src, src + size);
    return static_cast<int64_t>(size);
}

struct IStreamContext {
    const std::vector<uint8_t>* data;
    uint64_t offset;
};

auto clapIStreamRead(const clap_istream* stream, void* buffer, uint64_t size) -> int64_t {
    auto* ctx = static_cast<IStreamContext*>(stream->ctx);
    uint64_t available = ctx->data->size() - ctx->offset;
    uint64_t to_read = std::min(size, available);
    if (to_read > 0) {
        std::memcpy(buffer, ctx->data->data() + ctx->offset, to_read);
        ctx->offset += to_read;
    }
    return static_cast<int64_t>(to_read);
}
}  // namespace

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
            prefix = "[PluginHost-DEBUG]";
            break;
        case CLAP_LOG_INFO:
            prefix = "[PluginHost-INFO]";
            break;
        case CLAP_LOG_WARNING:
            prefix = "[PluginHost-WARNING]";
            break;
        case CLAP_LOG_ERROR:
            prefix = "[PluginHost-ERROR]";
            break;
        case CLAP_LOG_FATAL:
            prefix = "[PluginHost-FATAL]";
            break;
        case CLAP_LOG_HOST_MISBEHAVING:
            prefix = "[PluginHost-HOST_MISBEHAVING]";
            break;
        default:
            prefix = "[PluginHost-UNKNOWN]";
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

auto PluginHost::isPluginSleeping() const -> bool { return _state == kActiveAndSleeping; }

void PluginHost::scanParameters() {
    _params.clear();
    _param_id_to_index.clear();

    if (!_plugin) return;

    auto params_ext = static_cast<const clap_plugin_params_t*>(
        _plugin->clapPlugin()->get_extension(_plugin->clapPlugin(), CLAP_EXT_PARAMS));

    if (!params_ext) return;

    uint32_t count = params_ext->count(_plugin->clapPlugin());
    _params.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        auto slot = std::make_unique<ParameterSlot>();
        if (params_ext->get_info(_plugin->clapPlugin(), i, &slot->info)) {
            double val = 0.0;
            if (params_ext->get_value(_plugin->clapPlugin(), slot->info.id, &val)) {
                slot->base_value.store(val);
                slot->current_value.store(val);
            }
            _param_id_to_index[slot->info.id] = _params.size();
            _params.push_back(std::move(slot));
        }
    }

    logMessage(CLAP_LOG_INFO,
               ("Scanned " + std::to_string(_params.size()) + " parameters.").c_str());
}

auto PluginHost::getParameterSlot(clap_id param_id) -> ParameterSlot* {
    auto it = _param_id_to_index.find(param_id);
    if (it != _param_id_to_index.end()) {
        return _params[it->second].get();
    }
    return nullptr;
}

auto PluginHost::getParameterSlot(clap_id param_id) const -> const ParameterSlot* {
    auto it = _param_id_to_index.find(param_id);
    if (it != _param_id_to_index.end()) {
        return _params[it->second].get();
    }
    return nullptr;
}

auto PluginHost::getParameterText(clap_id param_id, double value) const -> std::string {
    if (!_plugin) return std::to_string(value);

    auto params_ext = static_cast<const clap_plugin_params_t*>(
        _plugin->clapPlugin()->get_extension(_plugin->clapPlugin(), CLAP_EXT_PARAMS));

    if (!params_ext) return std::to_string(value);

    // NOLINTNEXTLINE(modernize-avoid-c-arrays)
    char display[CLAP_NAME_SIZE];
    if (params_ext->value_to_text(_plugin->clapPlugin(), param_id, value, display,
                                  sizeof(display))) {
        return {display};
    }

    return std::to_string(value);
}

void PluginHost::paramsRescan(clap_param_rescan_flags flags) noexcept {
    logMessage(CLAP_LOG_INFO,
               ("Plugin requested parameter rescan with flags: " + std::to_string(flags)).c_str());
    scanParameters();

    if (on_params_rescan) {
        on_params_rescan(_instance_id);
    }
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

void PluginHost::scanAudioPorts() {
    _audio_input_ports.clear();
    _audio_output_ports.clear();
    _audio_input_ports_count = 0;
    _audio_output_ports_count = 0;

    if (!_plugin) return;

    auto audio_ports_ext = static_cast<const clap_plugin_audio_ports_t*>(
        _plugin->clapPlugin()->get_extension(_plugin->clapPlugin(), CLAP_EXT_AUDIO_PORTS));

    if (!audio_ports_ext) {
        logMessage(CLAP_LOG_WARNING,
                   "Plugin does not implement CLAP_EXT_AUDIO_PORTS. Assuming no audio ports.");
    } else {
        _audio_input_ports_count = audio_ports_ext->count(_plugin->clapPlugin(), true);
        _audio_output_ports_count = audio_ports_ext->count(_plugin->clapPlugin(), false);

        for (uint32_t i = 0; i < _audio_input_ports_count; ++i) {
            AudioPortInfo info;
            info.index = i;
            info.is_input = true;
            if (audio_ports_ext->get(_plugin->clapPlugin(), i, true, &info.clap_info)) {
                info.is_modulation = false;
                info.target_param_id = -1;
                _audio_input_ports.push_back(info);
            }
        }

        for (uint32_t i = 0; i < _audio_output_ports_count; ++i) {
            AudioPortInfo info;
            info.index = i;
            info.is_input = false;
            if (audio_ports_ext->get(_plugin->clapPlugin(), i, false, &info.clap_info)) {
                info.is_modulation = false;
                info.target_param_id = -1;
                _audio_output_ports.push_back(info);
            }
        }
    }

    // Now, add artificial modulation ports from modulatable parameters
    for (const auto& slot : _params) {
        if (slot->info.flags & CLAP_PARAM_IS_MODULATABLE) {
            AudioPortInfo mod_port;
            mod_port.index = static_cast<uint32_t>(_audio_input_ports.size());
            mod_port.is_input = true;
            mod_port.is_modulation = true;
            mod_port.target_param_id = slot->info.id;

            // Fill clap_info for UI/metadata consistency
            std::strncpy(mod_port.clap_info.name, slot->info.name, CLAP_NAME_SIZE);
            mod_port.clap_info.id = mod_port.index;
            mod_port.clap_info.channel_count = 1;
            mod_port.clap_info.flags = CLAP_AUDIO_PORT_IS_MAIN;
            mod_port.clap_info.port_type = CLAP_PORT_MONO;

            _audio_input_ports.push_back(mod_port);
        }
    }

    // Add Event ports
    auto note_ports_ext = static_cast<const clap_plugin_note_ports_t*>(
        _plugin->clapPlugin()->get_extension(_plugin->clapPlugin(), CLAP_EXT_NOTE_PORTS));

    if (note_ports_ext) {
        uint32_t note_in_count = note_ports_ext->count(_plugin->clapPlugin(), true);
        for (uint32_t i = 0; i < note_in_count; ++i) {
            clap_note_port_info note_info;
            if (note_ports_ext->get(_plugin->clapPlugin(), i, true, &note_info)) {
                AudioPortInfo port;
                port.index = static_cast<uint32_t>(_audio_input_ports.size());
                port.is_input = true;
                port.is_modulation = false;
                port.target_param_id = -1;

                // Map NotePortInfo to AudioPortInfo structure
                // Note: Note ports don't have id_out, we only use id and name.
                port.clap_info.id = note_info.id;
                std::strncpy(port.clap_info.name, note_info.name, sizeof(port.clap_info.name) - 1);
                port.clap_info.channel_count = 16;  // Standard MIDI
                port.clap_info.flags = CLAP_AUDIO_PORT_IS_MAIN;
                port.clap_info.port_type = "event";

                _audio_input_ports.push_back(port);
            }
        }

        uint32_t note_out_count = note_ports_ext->count(_plugin->clapPlugin(), false);
        for (uint32_t i = 0; i < note_out_count; ++i) {
            clap_note_port_info note_info;
            if (note_ports_ext->get(_plugin->clapPlugin(), i, false, &note_info)) {
                AudioPortInfo port;
                port.index = static_cast<uint32_t>(_audio_output_ports.size());
                port.is_input = false;
                port.is_modulation = false;
                port.target_param_id = -1;

                port.clap_info.id = note_info.id;
                std::strncpy(port.clap_info.name, note_info.name, sizeof(port.clap_info.name) - 1);
                port.clap_info.channel_count = 16;
                port.clap_info.flags = CLAP_AUDIO_PORT_IS_MAIN;
                port.clap_info.port_type = "event";

                _audio_output_ports.push_back(port);
            }
        }
    }
}

auto PluginHost::load(const std::string& path, int plugin_index) -> bool {
    unload();

    logMessage(CLAP_LOG_INFO, ("Attempting to load plugin: " + path).c_str());

#if defined(_WIN32)
    _library_handle = LoadLibraryA(path.c_str());
    if (!_library_handle) {
        logMessage(CLAP_LOG_ERROR,
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
        logMessage(CLAP_LOG_ERROR, "Unable to resolve entry point 'clap_entry'");
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

    const auto raw_plugin = _plugin_factory->create_plugin(_plugin_factory, clapHost(), desc->id);
    if (!raw_plugin) {
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

    _plugin = std::make_unique<PluginProxy>(*raw_plugin, *this);

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

    scanParameters();
    scanAudioPorts();

    // Reserve owned output buffers based on scanned port count
    reserveOutputBuffers(_audio_output_ports_count);

    auto note_ports_ext = static_cast<const clap_plugin_note_ports_t*>(
        _plugin->clapPlugin()->get_extension(_plugin->clapPlugin(), CLAP_EXT_NOTE_PORTS));

    if (note_ports_ext) {
        uint32_t note_in_count = note_ports_ext->count(_plugin->clapPlugin(), true);
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

    _params.clear();
    _param_id_to_index.clear();
    _audio_input_ports.clear();
    _audio_output_ports.clear();

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

void PluginHost::activate(int32_t sample_rate, int32_t block_size) {
    if (!_plugin) {
        return;
    }

    for (auto& buf : _output_buffers) {
        buf.resize(constants::kDefaultChannelCount, block_size);
    }

    if (!_plugin->activate(sample_rate, 1, block_size)) {
        setPluginState(kInactiveWithError);
        logMessage(CLAP_LOG_ERROR, "Failed to activate plugin.");
        return;
    }

    _input_events_data.reserve(constants::kEventQueueSize);

    _schedule_processing.store(true, std::memory_order_release);

    setPluginState(kActiveAndSleeping);
    logMessage(CLAP_LOG_INFO, "Plugin activated.");
}

void PluginHost::deactivate() {
    if (!isActive()) {
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

void PluginHost::setTransport(const TransportState* transport) { _transport = transport; }

void PluginHost::setParameterValue(clap_id param_id, double value) {
    if (auto* slot = getParameterSlot(param_id)) {
        slot->base_value.store(value, std::memory_order_relaxed);
        slot->current_value.store(value, std::memory_order_relaxed);
    }

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

void PluginHost::setParameterValue(const std::string& param_id, double value) {
    int32_t id = 0;
    auto [ptr, ec] = std::from_chars(param_id.data(), param_id.data() + param_id.size(), id);

    if (ec == std::errc()) {
        setParameterValue(id, value);
    } else if (ec == std::errc::invalid_argument) {
        logMessage(CLAP_LOG_WARNING, "Param id not found.");
    } else if (ec == std::errc::result_out_of_range) {
        logMessage(CLAP_LOG_WARNING, "Param id out of range.");
    }
}

void PluginHost::applyModulation(clap_id param_id, double value, uint32_t sample_offset) {
    if (auto* slot = getParameterSlot(param_id)) {
        double base = slot->base_value.load(std::memory_order_relaxed);
        slot->modulation_value.store(value, std::memory_order_relaxed);
        slot->current_value.store(base + value, std::memory_order_relaxed);
    }

    PluginEvent ev;
    ev.event.header.size = sizeof(clap_event_param_mod);
    ev.event.header.time = sample_offset;
    ev.event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.event.header.type = CLAP_EVENT_PARAM_MOD;
    ev.event.header.flags = 0;

    ev.event.param_mod.param_id = param_id;
    ev.event.param_mod.amount = value;
    ev.event.param_mod.cookie = nullptr;
    ev.event.param_mod.note_id = constants::kClapInvalidId;
    ev.event.param_mod.port_index = constants::kClapInvalidId;
    ev.event.param_mod.key = constants::kClapInvalidId;
    ev.event.param_mod.channel = constants::kClapInvalidId;

    _input_events.try_enqueue(ev);
}

auto PluginHost::saveState(std::vector<uint8_t>& data) -> bool {
    checkForMainThread();

    if (!_plugin) return false;

    auto* state_ext = static_cast<const clap_plugin_state_t*>(
        _plugin->clapPlugin()->get_extension(_plugin->clapPlugin(), CLAP_EXT_STATE));

    if (!state_ext) return true;  // Not an error if the plugin doesn't have state

    clap_ostream stream;
    stream.ctx = &data;
    stream.write = clapOStreamWrite;

    return state_ext->save(_plugin->clapPlugin(), &stream);
}

auto PluginHost::loadState(const std::vector<uint8_t>& data) -> bool {
    checkForMainThread();

    if (!_plugin) return false;

    auto* state_ext = static_cast<const clap_plugin_state_t*>(
        _plugin->clapPlugin()->get_extension(_plugin->clapPlugin(), CLAP_EXT_STATE));

    if (!state_ext) return true;  // Not an error

    IStreamContext ctx = {.data = &data, .offset = 0};
    clap_istream stream;
    stream.ctx = &ctx;
    stream.read = clapIStreamRead;

    return state_ext->load(_plugin->clapPlugin(), &stream);
}

auto PluginHost::getParameterBaseValue(clap_id param_id) const -> double {
    auto it = _param_id_to_index.find(param_id);
    if (it != _param_id_to_index.end()) {
        return _params[it->second]->base_value.load(std::memory_order_relaxed);
    }
    return 0.0;
}

auto PluginHost::getParameterCurrentValue(clap_id param_id) const -> double {
    auto it = _param_id_to_index.find(param_id);
    if (it != _param_id_to_index.end()) {
        return _params[it->second]->current_value.load(std::memory_order_relaxed);
    }
    return 0.0;
}

auto PluginHost::getParameterModulationOffset(clap_id param_id) const -> double {
    auto it = _param_id_to_index.find(param_id);
    if (it != _param_id_to_index.end()) {
        return _params[it->second]->modulation_value.load(std::memory_order_relaxed);
    }
    return 0.0;
}

void PluginHost::queueEvent(const PluginEvent& event) { _input_events.try_enqueue(event); }

auto PluginHost::popOutputEvent(uint32_t port_index, PluginEvent& out_event) -> bool {
    if (port_index == 0) {
        return _output_events_to_audio.try_dequeue(out_event);
    }
    return false;
}

auto PluginHost::getOutputBuffer(uint32_t port_idx) -> AudioBuffer* {
    if (port_idx < _output_buffers.size()) {
        return &_output_buffers[port_idx];
    }
    return nullptr;
}

void PluginHost::reserveOutputBuffers(uint32_t count) {
    if (count > _output_buffers.size()) {
        _output_buffers.resize(count);
        for (auto& buf : _output_buffers) {
            buf.owns_memory = true;
        }
    }
}

void PluginHost::pollMainThread() {
    checkForMainThread();

    PluginEvent ev;
    while (_output_events_to_main.try_dequeue(ev)) {
        if (ev.event.header.type == CLAP_EVENT_PARAM_VALUE) {
            if (auto* slot = getParameterSlot(ev.event.param_value.param_id)) {
                slot->base_value.store(ev.event.param_value.value, std::memory_order_relaxed);
                slot->current_value.store(ev.event.param_value.value, std::memory_order_relaxed);
            }
        }

        if (on_event_occured) {
            on_event_occured(_instance_id, ev);
        }
    }
}

void PluginHost::setPorts(uint32_t num_inputs, clap_audio_buffer* inputs, uint32_t num_outputs,
                          clap_audio_buffer* outputs) {
    _process.audio_inputs = inputs;
    _process.audio_inputs_count = num_inputs;
    _process.audio_outputs = outputs;
    _process.audio_outputs_count = num_outputs;
}

void PluginHost::processBegin(int nframes) {
    g_thread_type = ThreadType::kAudioThread;
    _process.frames_count = nframes;
}

void PluginHost::processEnd(int nframes) { g_thread_type = ThreadType::kUnknown; }

void PluginHost::process() {
    checkForAudioThread();

    if (!_plugin) return;

    if (!isActive()) return;

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

    clap_event_transport_t clap_transport = {0};
    if (_transport) {
        clap_transport.header.size = sizeof(clap_event_transport_t);
        clap_transport.header.time = 0;
        clap_transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        clap_transport.header.type = CLAP_EVENT_TRANSPORT;
        clap_transport.header.flags = 0;

        clap_transport.flags = 0;
        if (_transport->is_playing) {
            clap_transport.flags |= CLAP_TRANSPORT_IS_PLAYING;
        }

        // CLAP uses 64-bit fixed point with 31-bit fractional part (CLAP_BEATTIME_FACTOR)
        const auto factor = static_cast<double>(1LL << 31);

        clap_transport.song_pos_beats =
            static_cast<int64_t>(std::round(_transport->song_pos_beats * factor));

        // Convert beats to seconds: seconds = (beats * 60) / tempo
        double song_pos_seconds = (_transport->song_pos_beats * 60.0) / _transport->tempo;
        clap_transport.song_pos_seconds =
            static_cast<int64_t>(std::round(song_pos_seconds * factor));

        clap_transport.tempo = _transport->tempo;
        clap_transport.tsig_num = static_cast<uint16_t>(_transport->ts_num);
        clap_transport.tsig_denom = static_cast<uint16_t>(_transport->ts_denom);

        clap_transport.flags |= CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                                CLAP_TRANSPORT_HAS_SECONDS_TIMELINE |
                                CLAP_TRANSPORT_HAS_TIME_SIGNATURE;

        _process.transport = &clap_transport;
    } else {
        _process.transport = nullptr;
    }

    _process.in_events = _ev_in.clapInputEvents();
    _process.out_events = _ev_out.clapOutputEvents();

    _ev_out.clear();
    generatePluginInputEvents();

    _plugin->process(&_process);

    handlePluginOutputEvents();

    _ev_out.clear();
    _ev_in.clear();
}

void PluginHost::generatePluginInputEvents() {
    _input_events_data.clear();
    PluginEvent ev;
    while (_input_events.try_dequeue(ev)) {
        _input_events_data.push_back(ev);
    }

    for (auto& stored_ev : _input_events_data) {
        _ev_in.push(&stored_ev.event.header);
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
                _output_events_to_main.try_enqueue(out_ev);
                _output_events_to_audio.try_enqueue(out_ev);
                break;
            }
        }
    }
}

void PluginHost::setPluginState(PluginState state) { _state = state; }

auto PluginHost::isActive() const -> bool {
    return _state != kInactive && _state != kInactiveWithError;
}

auto PluginHost::isPluginProcessing() const -> bool { return _state == kActiveAndProcessing; }

}  // namespace synth_canvas::host

template class clap::helpers::Host<kPluginHostMh, kPluginHostCl>;
template class clap::helpers::PluginProxy<kPluginHostMh, kPluginHostCl>;
