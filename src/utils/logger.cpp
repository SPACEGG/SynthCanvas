#include "utils/logger.h"

#include <iostream>

namespace synth_canvas::host {

// Static storage for the log callback.
static LogCallback g_log_callback = nullptr;

void setLogCallback(LogCallback callback) { g_log_callback = callback; }

void logMsg(const std::string& message) {
    if (g_log_callback) {
        g_log_callback(message);
    } else {
        // Fallback to standard output (useful for tests or standalone builds)
        std::cout << message << std::endl;
    }
}

}  // namespace synth_canvas::host
