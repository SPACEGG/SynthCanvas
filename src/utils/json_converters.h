#ifndef SYNTH_CANVAS_UTILS_JSON_CONVERTERS_H
#define SYNTH_CANVAS_UTILS_JSON_CONVERTERS_H

#include "api/types.h"
#include "nlohmann/json.hpp"

namespace synth_canvas {

using json = nlohmann::json;

// --- TransportState ---
// NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(json& j, const TransportState& t) {
    j = json{{"tempo", t.tempo},
             {"song_pos_beats", t.song_pos_beats},
             {"is_playing", t.is_playing},
             {"ts_num", t.ts_num},
             {"ts_denom", t.ts_denom}};
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void from_json(const json& j, TransportState& t) {
    j.at("tempo").get_to(t.tempo);
    j.at("song_pos_beats").get_to(t.song_pos_beats);
    j.at("is_playing").get_to(t.is_playing);
    j.at("ts_num").get_to(t.ts_num);
    j.at("ts_denom").get_to(t.ts_denom);
}

// --- InternalPluginConfig ---
// NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(json& j, const InternalPluginConfig& p) {
    j = json{{"alias", p.alias}, {"plugin_path", p.plugin_path}};
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void from_json(const json& j, InternalPluginConfig& p) {
    j.at("alias").get_to(p.alias);
    j.at("plugin_path").get_to(p.plugin_path);
}

// --- ConnectionType ---
NLOHMANN_JSON_SERIALIZE_ENUM(ConnectionType, {
                                                 {ConnectionType::kAudio, "audio"},
                                                 {ConnectionType::kEvent, "event"},
                                                 {ConnectionType::kModulation, "modulation"},
                                             })

// --- InternalRoutingConfig ---
// NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(json& j, const InternalRoutingConfig& r) {
    j = json{{"from_node", r.from_node}, {"from_port", r.from_port}, {"to_node", r.to_node},
             {"to_port", r.to_port},     {"type", r.type},           {"scale", r.scale},
             {"bypass", r.bypass}};
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void from_json(const json& j, InternalRoutingConfig& r) {
    j.at("from_node").get_to(r.from_node);
    j.at("from_port").get_to(r.from_port);
    j.at("to_node").get_to(r.to_node);
    j.at("to_port").get_to(r.to_port);
    j.at("type").get_to(r.type);
    j.at("scale").get_to(r.scale);
    j.at("bypass").get_to(r.bypass);
}

// --- ParameterMappingConfig ---
// NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(json& j, const ParameterMappingConfig& m) {
    j = json{{"param_id", m.param_id},
             {"target_node", m.target_node},
             {"target_param_index", m.target_param_index}};
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void from_json(const json& j, ParameterMappingConfig& m) {
    j.at("param_id").get_to(m.param_id);
    j.at("target_node").get_to(m.target_node);
    j.at("target_param_index").get_to(m.target_param_index);
}

// --- PortProxyConfig ---
// NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(json& j, const PortProxyConfig& p) {
    j = json{{"external_port_index", p.external_port_index},
             {"internal_node", p.internal_node},
             {"internal_port_index", p.internal_port_index},
             {"type", p.type},
             {"target_param_id", p.target_param_id}};
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void from_json(const json& j, PortProxyConfig& p) {
    j.at("external_port_index").get_to(p.external_port_index);
    j.at("internal_node").get_to(p.internal_node);
    j.at("internal_port_index").get_to(p.internal_port_index);
    j.at("type").get_to(p.type);
    j.at("target_param_id").get_to(p.target_param_id);
}

// --- CompositeConfig ---
// NOLINTNEXTLINE(readability-identifier-naming)
inline void to_json(json& j, const CompositeConfig& c) {
    j = json{{"plugins", c.plugins},
             {"routings", c.routings},
             {"parameter_mappings", c.parameter_mappings},
             {"input_proxies", c.input_proxies},
             {"output_proxies", c.output_proxies},
             {"display", c.display}};
}

// NOLINTNEXTLINE(readability-identifier-naming)
inline void from_json(const json& j, CompositeConfig& c) {
    j.at("plugins").get_to(c.plugins);
    j.at("routings").get_to(c.routings);
    j.at("parameter_mappings").get_to(c.parameter_mappings);
    j.at("input_proxies").get_to(c.input_proxies);
    j.at("output_proxies").get_to(c.output_proxies);
    c.display = j.contains("display") ? j.at("display").get<std::string>() : "";
}

}  // namespace synth_canvas

#endif  // SYNTH_CANVAS_UTILS_JSON_CONVERTERS_H
