#pragma once

#include <functional>
#include <string>
#include <sstream>

namespace synth_canvas::host {

using LogCallback = std::function<void(const std::string&)>;

// Sets the callback function that will handle log messages.
void set_log_callback(LogCallback callback);

// Internal function to dispatch the formatted message to the callback.
void log_msg(const std::string& message);

/**
 * Variadic template function to log messages.
 * This replaces direct calls to Godot's UtilityFunctions::print within the host code.
 */
template<typename... Args>
void log(Args&&... args) {
    std::stringstream ss;
    // Fold expression to append all arguments to the stringstream
    (ss << ... << args);
    log_msg(ss.str());
}

} // namespace synth_canvas::host
