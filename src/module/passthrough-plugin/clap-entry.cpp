#include <clap/entry.h>
#include <clap/factory/plugin-factory.h>

#include <cstring>

#include "passthrough_plugin.h"

static const clap_plugin_descriptor_t* s_plugin_descriptor = nullptr;

static auto clap_get_plugin_count(const struct clap_plugin_factory* factory) -> uint32_t {
    return 1;
}

static auto clap_get_plugin_descriptor(const struct clap_plugin_factory* factory, uint32_t index)
    -> const clap_plugin_descriptor_t* {
    if (index == 0) {
        return s_plugin_descriptor;
    }
    return nullptr;
}

static auto clap_create_plugin(const struct clap_plugin_factory* factory, const clap_host_t* host,
                               const char* plugin_id) -> const clap_plugin_t* {
    if (strcmp(plugin_id, s_plugin_descriptor->id) == 0) {
        // Using `plugin_id` as the path/identifier for the constructor if needed, or pass
        // empty/host
        auto* plugin = new synth_canvas::passthrough_plugin::PassthroughPlugin(plugin_id, host);
        return plugin->clapPlugin();
    }
    return nullptr;
}

static const struct clap_plugin_factory g_clap_plugin_factory = {
    .get_plugin_count = clap_get_plugin_count,
    .get_plugin_descriptor = clap_get_plugin_descriptor,
    .create_plugin = clap_create_plugin,
};

static bool clap_init(const char* plugin_path) {
    s_plugin_descriptor = synth_canvas::passthrough_plugin::PassthroughPlugin::descriptor();
    return true;
}

static void clap_deinit(void) { s_plugin_descriptor = nullptr; }

static const void* clap_get_factory(const char* factory_id) {
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &g_clap_plugin_factory;
    }
    return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = clap_init,
    .deinit = clap_deinit,
    .get_factory = clap_get_factory,
};