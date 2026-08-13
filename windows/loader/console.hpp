#pragma once

#include <string>
#include <string_view>
#include <windows.h>

namespace loader::console {

enum class Mode {
    Alloc,   // allocate a console for the game process
    Attach,  // write into a console the process already has, or the parent's
    None,    // no console at all; lines only reach an attached debugger
};

[[nodiscard]] Mode parse_mode(std::string_view value, Mode fallback);

// picks the output sink the mode asks for. Call once, before the first write_line.
void init(Mode mode);

void write_line(const std::string& line);

}  // namespace loader::console
