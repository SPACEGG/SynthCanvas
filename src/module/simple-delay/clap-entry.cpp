#include <clap/entry.h>
#include <clap/factory/plugin-factory.h>

#include <cstring>

#include "simple_delay_plugin.h"

static const clap_plugin_descriptor_t* s_plugin_descriptor = nullptr;

static auto clapGetPluginCount(const struct clap_plugin_factory* factory) -> uint32_t { return 1; }

static auto clapGetPluginDescriptor(const struct clap_plugin_factory* factory, uint32_t index)
    -> const clap_plugin_descriptor_t* {
    if (index == 0) {
        return s_plugin_descriptor;
    }
    return nullptr;
}

static auto clapCreatePlugin(const struct clap_plugin_factory* factory, const clap_host_t* host,
                             const char* plugin_id) -> const clap_plugin_t* {
    if (strcmp(plugin_id, s_plugin_descriptor->id) == 0) {
        // Using `plugin_id` as the path/identifier for the constructor if needed, or pass
        // empty/host
        auto* plugin = new synth_canvas::simple_delay_plugin::SimpleDelayPlugin(plugin_id, host);
        return plugin->clapPlugin();
    }
    return nullptr;
}

static const struct clap_plugin_factory kGClapPluginFactory = {
    .get_plugin_count = clapGetPluginCount,
    .get_plugin_descriptor = clapGetPluginDescriptor,
    .create_plugin = clapCreatePlugin,
};

static auto clapInit(const char* plugin_path) -> bool {
    s_plugin_descriptor = synth_canvas::simple_delay_plugin::SimpleDelayPlugin::descriptor();
    return true;
}

static void clapDeinit() { s_plugin_descriptor = nullptr; }

static auto clapGetFactory(const char* factory_id) -> const void* {
    if (strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &kGClapPluginFactory;
    }
    return nullptr;
}

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = clapInit,
    .deinit = clapDeinit,
    .get_factory = clapGetFactory,
};