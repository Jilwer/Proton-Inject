#pragma once

namespace proton_inject {

// installs the icon into the user's icon theme and points a desktop entry at this binary,
// which is what gives a portable build a taskbar icon.
void setup_app_icon();

}  // namespace proton_inject
