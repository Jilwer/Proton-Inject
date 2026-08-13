#include "console.hpp"

#include <algorithm>
#include <cctype>

namespace loader::console {

namespace {

HANDLE g_output = nullptr;

// CONOUT$ addresses the console itself rather than whatever std handle the game was
// started with. That matters when another mod loader allocated the console and never
// rebound the process handles: opening it doubles as the test for "is there a console".
HANDLE open_console_output() {
    const HANDLE handle = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, 0, nullptr);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}

HANDLE std_output() {
    const HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}

}  // namespace

Mode parse_mode(const std::string_view value, const Mode fallback) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized == "alloc" || normalized == "allocate" || normalized == "new") {
        return Mode::Alloc;
    }
    if (normalized == "attach" || normalized == "existing" || normalized == "reuse") {
        return Mode::Attach;
    }
    if (normalized == "none" || normalized == "off" || normalized == "disabled") {
        return Mode::None;
    }
    return fallback;
}

void init(const Mode mode) {
    if (mode == Mode::None) {
        return;
    }

    if (mode == Mode::Alloc) {
        // fails when the process already owns a console, which leaves us on that console
        // anyway -- the same place the write would have gone.
        AllocConsole();
    } else {
        g_output = open_console_output();
        if (g_output == nullptr) {
            AttachConsole(ATTACH_PARENT_PROCESS);
        }
    }

    if (g_output == nullptr) {
        g_output = open_console_output();
    }
    if (g_output == nullptr) {
        g_output = std_output();
    }
}

void write_line(const std::string& line) {
    OutputDebugStringA((line + "\n").c_str());
    if (g_output == nullptr) {
        return;
    }
    const std::string message = line + "\r\n";
    DWORD written = 0;
    WriteFile(g_output, message.c_str(), static_cast<DWORD>(message.size()), &written, nullptr);
}

}  // namespace loader::console
