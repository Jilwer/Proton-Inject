#pragma once

#include "core/inject_options.hpp"

#include <expected>
#include <string>
#include <vector>

namespace proton_inject {

class Injector {
public:
    [[nodiscard]] std::expected<void, std::string> inject_with(const InjectOptions& options) const;

private:
    [[nodiscard]] std::expected<void, std::string> run_steam(
        const InjectOptions& options, const std::string& stage_dir,
        const std::string& local_injector, const std::vector<std::string>& injector_args) const;
    [[nodiscard]] std::expected<void, std::string> run_umu(
        const InjectOptions& options, const std::string& stage_dir,
        const std::string& local_injector, const std::vector<std::string>& injector_args) const;
    [[nodiscard]] bool wait_for_process(const std::string& process_name, int timeout_seconds) const;
    [[nodiscard]] bool is_process_running(const std::string& process_name) const;
};

[[nodiscard]] bool launches_target(const InjectOptions& options);
[[nodiscard]] std::vector<std::string> build_injector_args(const InjectOptions& options,
                                                           const std::string& target_dll,
                                                           bool staged_basenames = false);
[[nodiscard]] std::expected<std::string, std::string> resolve_launch_target(
    const std::string& app_id, const std::string& target);

}  // namespace proton_inject
