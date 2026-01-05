#include <clap/clap.h>
#include <clap/events.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define _USE_MATH_DEFINES
#include <math.h>
#endif

// Forward declarations
static uint32_t audio_ports_count(const clap_plugin_t *plugin, bool is_input);
static bool audio_ports_get(const clap_plugin_t *plugin, uint32_t index, bool is_input,
                            clap_audio_port_info_t *info);
static uint32_t note_ports_count(const clap_plugin_t *plugin, bool is_input);
static bool note_ports_get(const clap_plugin_t *plugin, uint32_t index, bool is_input,
                           clap_note_port_info_t *info);
static const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id);
static bool plugin_init_instance(const clap_plugin_t *plugin);
static void plugin_destroy(const clap_plugin_t *plugin);
static bool plugin_activate(const clap_plugin_t *plugin, double sample_rate,
                            uint32_t min_frames_count, uint32_t max_frames_count);
static void plugin_deactivate(const clap_plugin_t *plugin);
static bool plugin_start_processing(const clap_plugin_t *plugin);
static void plugin_stop_processing(const clap_plugin_t *plugin);
static clap_process_status plugin_process(const clap_plugin_t *plugin,
                                          const clap_process_t *process);
static uint32_t get_plugin_count(const clap_plugin_factory_t *factory);
static const clap_plugin_descriptor_t *get_plugin_descriptor(const clap_plugin_factory_t *factory,
                                                             uint32_t index);
static const clap_plugin_t *create_plugin(const clap_plugin_factory_t *factory,
                                          const clap_host_t *host, const char *plugin_id);
static bool plugin_init(const char *plugin_path);
static void plugin_deinit(void);
static const void *plugin_get_factory(const char *factory_id);

// Simple sine wave synthesizer state
typedef struct {
    clap_plugin_t plugin;
    const clap_host_t *host;
    const clap_host_log_t *host_log;

    double sample_rate;
    float phase;
    bool note_is_active;
    int32_t note_key;
    double note_freq;
    double note_velocity;
} tutorial_plugin_t;

//////////////////
// Extensions
//////////////////

// Audio Ports Extension
static const clap_audio_port_info_t g_audio_outputs[] = {
    {
        0,                        // id
        "Main",                   // name
        CLAP_AUDIO_PORT_IS_MAIN,  // flags
        2,                        // channel_count
        CLAP_PORT_STEREO,         // port_type
        CLAP_INVALID_ID,          // in_place_pair
    },
};

static const clap_plugin_audio_ports_t g_audio_ports_extension = {
    audio_ports_count,
    audio_ports_get,
};

static uint32_t audio_ports_count(const clap_plugin_t *plugin, bool is_input) {
    return is_input ? 0 : sizeof(g_audio_outputs) / sizeof(g_audio_outputs[0]);
}

static bool audio_ports_get(const clap_plugin_t *plugin, uint32_t index, bool is_input,
                            clap_audio_port_info_t *info) {
    if (is_input || index != 0) return false;
    *info = g_audio_outputs[index];
    return true;
}

// Note Ports Extension
static const clap_note_port_info_t g_note_inputs[] = {
    {
        0,                                                // id
        CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI,  // supported_dialects
        CLAP_NOTE_DIALECT_CLAP,                           // preferred_dialect
        "Main",                                           // name
    },
};

static const clap_plugin_note_ports_t g_note_ports_extension = {
    note_ports_count,
    note_ports_get,
};

static uint32_t note_ports_count(const clap_plugin_t *plugin, bool is_input) {
    return is_input ? sizeof(g_note_inputs) / sizeof(g_note_inputs[0]) : 0;
}

static bool note_ports_get(const clap_plugin_t *plugin, uint32_t index, bool is_input,
                           clap_note_port_info_t *info) {
    if (!is_input || index != 0) return false;
    *info = g_note_inputs[index];
    return true;
}

// Get Extension
static const void *plugin_get_extension(const clap_plugin_t *plugin, const char *id) {
    if (strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &g_audio_ports_extension;
    if (strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &g_note_ports_extension;
    return NULL;
}

//////////////////
// Plugin
//////////////////

static bool plugin_init_instance(const clap_plugin_t *plugin) { return true; }
static void plugin_destroy(const clap_plugin_t *plugin) {
    tutorial_plugin_t *p = (tutorial_plugin_t *)plugin->plugin_data;
    free(p);
}

static bool plugin_activate(const clap_plugin_t *plugin, double sample_rate,
                            uint32_t min_frames_count, uint32_t max_frames_count) {
    tutorial_plugin_t *p = (tutorial_plugin_t *)plugin->plugin_data;
    p->sample_rate = sample_rate;
    return true;
}

static void plugin_deactivate(const clap_plugin_t *plugin) {}

static bool plugin_start_processing(const clap_plugin_t *plugin) { return true; }
static void plugin_stop_processing(const clap_plugin_t *plugin) {}

static clap_process_status plugin_process(const clap_plugin_t *plugin,
                                          const clap_process_t *process) {
    tutorial_plugin_t *p = (tutorial_plugin_t *)plugin->plugin_data;
    const uint32_t nframes = process->frames_count;
    const uint32_t nev = process->in_events->size(process->in_events);
    uint32_t ev_index = 0;

    for (uint32_t i = 0; i < nframes;) {
        while (ev_index < nev) {
            const clap_event_header_t *hdr = process->in_events->get(process->in_events, ev_index);
            if (hdr->time > i) break;

            if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID && hdr->type == CLAP_EVENT_NOTE_ON) {
                const clap_event_note_t *ev = (const clap_event_note_t *)hdr;
                p->note_key = ev->key;
                p->note_freq = 440.0 * pow(2.0, (p->note_key - 69.0) / 12.0);
                p->note_velocity = ev->velocity;
                p->note_is_active = true;
            } else if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID &&
                       hdr->type == CLAP_EVENT_NOTE_OFF) {
                const clap_event_note_t *ev = (const clap_event_note_t *)hdr;
                if (ev->key == p->note_key) {
                    p->note_is_active = false;
                }
            }
            ++ev_index;
        }

        float *out_l = process->audio_outputs[0].data32[0];
        float *out_r = process->audio_outputs[0].data32[1];

        if (p->note_is_active) {
            out_l[i] = sinf(p->phase * 2 * M_PI) * (float)(p->note_velocity * 0.2);
            p->phase += p->note_freq / p->sample_rate;
            if (p->phase > 1) p->phase -= 1;
        } else {
            out_l[i] = 0;
        }
        out_r[i] = out_l[i];
        ++i;
    }

    return CLAP_PROCESS_CONTINUE;
}

//////////////////
// Factory
//////////////////

static const char *const g_plugin_features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, "synthesizer",
                                                nullptr};

static const clap_plugin_descriptor_t g_plugin_descriptor = {
    CLAP_VERSION_INIT,                                  // clap_version
    "com.synthcanvas.test-plugin",                      // id
    "Tutorial Synth",                                   // name
    "SynthCanvas",                                      // vendor
    "https://github.com/SPACEGG/SynthCanvas",           // url
    "",                                                 // manual_url
    "",                                                 // support_url
    "1.0.0",                                            // version
    "A simple sine wave synthesizer from a tutorial.",  // description
    g_plugin_features,                                  // features
};

static const clap_plugin_t *create_plugin(const clap_plugin_factory_t *factory,
                                          const clap_host_t *host, const char *plugin_id) {
    if (strcmp(plugin_id, g_plugin_descriptor.id)) {
        return NULL;
    }

    tutorial_plugin_t *p = (tutorial_plugin_t *)calloc(1, sizeof(tutorial_plugin_t));
    p->host = host;
    if (host) {
        p->host_log = (const clap_host_log_t *)host->get_extension(host, CLAP_EXT_LOG);
    }
    p->note_velocity = 0.5;  // Default velocity

    p->plugin.desc = &g_plugin_descriptor;
    p->plugin.plugin_data = p;
    p->plugin.init = plugin_init_instance;
    p->plugin.destroy = plugin_destroy;
    p->plugin.activate = plugin_activate;
    p->plugin.deactivate = plugin_deactivate;
    p->plugin.start_processing = plugin_start_processing;
    p->plugin.stop_processing = plugin_stop_processing;
    p->plugin.process = plugin_process;
    p->plugin.get_extension = plugin_get_extension;

    return &p->plugin;
}

static uint32_t get_plugin_count(const clap_plugin_factory_t *factory) { return 1; }

static const clap_plugin_descriptor_t *get_plugin_descriptor(const clap_plugin_factory_t *factory,
                                                             uint32_t index) {
    return index == 0 ? &g_plugin_descriptor : NULL;
}

static const clap_plugin_factory_t g_plugin_factory = {
    get_plugin_count,
    get_plugin_descriptor,
    create_plugin,
};

//////////////////
// Entry
//////////////////

static bool plugin_init(const char *plugin_path) { return true; }
static void plugin_deinit(void) {}

static const void *plugin_get_factory(const char *factory_id) {
    return strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &g_plugin_factory : NULL;
}

extern "C" const clap_plugin_entry_t clap_entry = {
    CLAP_VERSION_INIT,
    plugin_init,
    plugin_deinit,
    plugin_get_factory,
};
