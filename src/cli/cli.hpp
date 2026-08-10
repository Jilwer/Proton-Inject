#pragma once

#include <expected>
#include <string>
#include <vector>

namespace proton_inject {

[[nodiscard]] std::expected<void, std::string> run_cli(int argc, char** argv);

}  // namespace proton_inject
