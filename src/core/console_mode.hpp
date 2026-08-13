#pragma once

#include <string>
#include <string_view>

namespace proton_inject {

// what the injected loader does about a console. "attach" is for games that already have
// one -- BepInEx and other mod loaders allocate their own, and a second window on top of
// it is both redundant and where nothing ends up looking.
inline constexpr std::string_view kConsoleAlloc = "alloc";
inline constexpr std::string_view kConsoleAttach = "attach";
inline constexpr std::string_view kConsoleNone = "none";

[[nodiscard]] std::string normalize_console_mode(std::string_view mode);
[[nodiscard]] bool valid_console_mode(std::string_view mode);

}  // namespace proton_inject
