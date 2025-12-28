#include "logger.h"
#include <iostream>

namespace synth_canvas::host {

// Static storage for the log callback.
static LogCallback g_log_callback = nullptr;

void set_log_callback(LogCallback callback) {
    g_log_callback = callback;
}

void log_msg(const std::string& message) {
    if (g_log_callback) {
        g_log_callback(message);
    } else {
        // Fallback to standard output (useful for tests or standalone builds)
        std::cout << message << std::endl;
    }
}

} // namespace synth_canvas::host
