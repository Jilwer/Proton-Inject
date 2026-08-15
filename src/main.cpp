#include "cli/cli.hpp"
#include "gui/application.hpp"
#include "utils/utils.hpp"

#include <iostream>
#include <string_view>

namespace {

bool wants_gui(int argc, char** argv) {
    if (argc <= 1) {
        return true;
    }
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--gui") {
            return true;
        }
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    proton_inject::migrate_legacy_state_dir();

    if (wants_gui(argc, argv)) {
        return proton_inject::run_gui(argc, argv);
    }

    const auto result = proton_inject::run_cli(argc, argv);
    if (!result) {
        std::cerr << "error: " << result.error() << '\n';
        return 1;
    }
    return 0;
}
