#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace proton_inject {

struct Mapping {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    std::string perms;
    std::uintptr_t offset = 0;
    std::string path;

    [[nodiscard]] std::size_t size() const { return end > start ? end - start : 0; }

    [[nodiscard]] bool writable() const { return perms.size() > 1 && perms[1] == 'w'; }

    [[nodiscard]] bool executable() const { return perms.size() > 2 && perms[2] == 'x'; }

    [[nodiscard]] bool contains(std::uintptr_t addr) const { return addr >= start && addr < end; }
};

class ProcMem {
public:
    ProcMem() = default;
    ProcMem(const ProcMem&) = delete;
    ProcMem& operator=(const ProcMem&) = delete;
    ProcMem(ProcMem&& other) noexcept;
    ProcMem& operator=(ProcMem&& other) noexcept;
    ~ProcMem();

    [[nodiscard]] static std::expected<ProcMem, std::string> open(pid_t pid);

    [[nodiscard]] std::expected<void, std::string> read(std::uintptr_t addr,
                                                        std::span<std::byte> out) const;
    [[nodiscard]] std::expected<void, std::string> write(std::uintptr_t addr,
                                                         std::span<const std::byte> in) const;

    template<typename T>
    [[nodiscard]] std::expected<T, std::string> read_value(std::uintptr_t addr) const {
        T value{};
        auto bytes = std::as_writable_bytes(std::span<T, 1>(&value, 1));
        if (auto err = read(addr, bytes); !err) {
            return std::unexpected(err.error());
        }
        return value;
    }

    template<typename T>
    [[nodiscard]] std::expected<void, std::string> write_value(std::uintptr_t addr,
                                                               const T& value) const {
        return write(addr, std::as_bytes(std::span<const T, 1>(&value, 1)));
    }

private:
    int fd_ = -1;
    pid_t pid_ = -1;
};

[[nodiscard]] std::string hex_addr(std::uintptr_t addr);
[[nodiscard]] std::expected<std::vector<Mapping>, std::string> parse_maps(pid_t pid);
[[nodiscard]] bool path_ends_with_ignore_case(std::string_view path, std::string_view suffix);

[[nodiscard]] const Mapping* find_mapping(const std::vector<Mapping>& maps, std::uintptr_t addr);
[[nodiscard]] const Mapping* find_module_base(const std::vector<Mapping>& maps,
                                              std::string_view module_name);
[[nodiscard]] bool maps_contain_module(const std::vector<Mapping>& maps,
                                       std::string_view module_name);

}  // namespace proton_inject
