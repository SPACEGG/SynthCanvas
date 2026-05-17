#ifndef SYNTH_CANVAS_UTILS_BASE64_H
#define SYNTH_CANVAS_UTILS_BASE64_H

#include <cstdint>
#include <string>
#include <vector>


namespace synth_canvas::utils {

auto base64Encode(const std::vector<uint8_t>& data) -> std::string;
auto base64Decode(const std::string& base64_str) -> std::vector<uint8_t>;

}  // namespace synth_canvas::utils

#endif  // SYNTH_CANVAS_UTILS_BASE64_H
