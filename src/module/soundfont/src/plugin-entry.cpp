#include <clap/entry.h>
#include <clap/factory/plugin-factory.h>

#include <cstring>

#include "soundfont-plugin.h"

namespace synth_canvas::soundfont_plugin {

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
    if (std::strcmp(plugin_id, s_plugin_descriptor->id) == 0) {
        auto* plugin = new SoundfontPlugin(plugin_id, host);
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
    s_plugin_descriptor = SoundfontPlugin::descriptor();
    return true;
}

static void clapDeinit() { s_plugin_descriptor = nullptr; }

static auto clapGetFactory(const char* factory_id) -> const void* {
    if (std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID) == 0) {
        return &kGClapPluginFactory;
    }
    return nullptr;
}

}  // namespace synth_canvas::soundfont_plugin

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
    .clap_version = CLAP_VERSION_INIT,
    .init = synth_canvas::soundfont_plugin::clapInit,
    .deinit = synth_canvas::soundfont_plugin::clapDeinit,
    .get_factory = synth_canvas::soundfont_plugin::clapGetFactory,
};
