#pragma once

#include <windows.h>

namespace loader {

void attach(HINSTANCE instance);
void detach();

}  // namespace loader
