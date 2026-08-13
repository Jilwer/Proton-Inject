#include "native/proc_mem.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <ios>
#include <sstream>
#include <unistd.h>

namespace proton_inject {

namespace {

std::string errno_string() {
    return std::strerror(errno);
}

std::string hex_addr(std::uintptr_t addr) {
    std::ostringstream out;
    out << "0x" << std::hex << addr;
    return out.str();
}

std::string to_lower_copy(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

ProcMem::ProcMem(ProcMem&& other) noexcept : fd_(other.fd_), pid_(other.pid_) {
    other.fd_ = -1;
    other.pid_ = -1;
}

ProcMem& ProcMem::operator=(ProcMem&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) {
            close(fd_);
        }
        fd_ = other.fd_;
        pid_ = other.pid_;
        other.fd_ = -1;
        other.pid_ = -1;
    }
    return *this;
}

ProcMem::~ProcMem() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

std::expected<ProcMem, std::string> ProcMem::open(pid_t pid) {
    const std::string path = "/proc/" + std::to_string(pid) + "/mem";
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) {
        std::string error = "Cannot open " + path + ": " + errno_string();
        if (errno == EPERM || errno == EACCES) {
            std::ifstream scope("/proc/sys/kernel/yama/ptrace_scope");
            int yama = 0;
            if (scope && (scope >> yama) && yama != 0) {
                error += " (kernel.yama.ptrace_scope is " + std::to_string(yama) +
                         "; Linux IAT methods need classic same-uid access. Use crt instead)";
            } else {
                error +=
                    " (Linux IAT methods need same-uid access to the game process; use crt "
                    "instead)";
            }
        }
        return std::unexpected(error);
    }

    ProcMem mem;
    mem.fd_ = fd;
    mem.pid_ = pid;
    return mem;
}

std::expected<void, std::string> ProcMem::read(std::uintptr_t addr,
                                               std::span<std::byte> out) const {
    if (fd_ < 0) {
        return std::unexpected("Process memory is not open");
    }
    std::size_t done = 0;
    while (done < out.size()) {
        const ssize_t n =
            pread(fd_, out.data() + done, out.size() - done, static_cast<off_t>(addr + done));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected("Failed to read /proc/" + std::to_string(pid_) + "/mem at " +
                                   hex_addr(addr) + ": " + errno_string());
        }
        if (n == 0) {
            return std::unexpected("Short read from /proc/" + std::to_string(pid_) + "/mem");
        }
        done += static_cast<std::size_t>(n);
    }
    return {};
}

std::expected<void, std::string> ProcMem::write(std::uintptr_t addr,
                                                std::span<const std::byte> in) const {
    if (fd_ < 0) {
        return std::unexpected("Process memory is not open");
    }
    std::size_t done = 0;
    while (done < in.size()) {
        const ssize_t n =
            pwrite(fd_, in.data() + done, in.size() - done, static_cast<off_t>(addr + done));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return std::unexpected("Failed to write /proc/" + std::to_string(pid_) + "/mem at " +
                                   hex_addr(addr) + ": " + errno_string());
        }
        if (n == 0) {
            return std::unexpected("Short write to /proc/" + std::to_string(pid_) + "/mem");
        }
        done += static_cast<std::size_t>(n);
    }
    return {};
}

bool path_ends_with_ignore_case(std::string_view path, std::string_view suffix) {
    if (path.size() < suffix.size()) {
        return false;
    }
    return to_lower_copy(path.substr(path.size() - suffix.size())) == to_lower_copy(suffix);
}

std::expected<std::vector<Mapping>, std::string> parse_maps(pid_t pid) {
    const std::string path = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream input(path);
    if (!input) {
        return std::unexpected("Cannot read " + path);
    }

    std::vector<Mapping> maps;
    for (std::string line; std::getline(input, line);) {
        Mapping mapping;
        char perms[8]{};
        unsigned long long start = 0;
        unsigned long long end = 0;
        unsigned long long offset = 0;
        int consumed = 0;
        if (std::sscanf(line.c_str(), "%llx-%llx %7s %llx %*s %*s %n", &start, &end, perms, &offset,
                        &consumed) < 4) {
            continue;
        }
        mapping.start = static_cast<std::uintptr_t>(start);
        mapping.end = static_cast<std::uintptr_t>(end);
        mapping.perms = perms;
        mapping.offset = static_cast<std::uintptr_t>(offset);
        if (consumed > 0 && static_cast<std::size_t>(consumed) < line.size()) {
            std::string rest = line.substr(static_cast<std::size_t>(consumed));
            const auto first = rest.find_first_not_of(' ');
            if (first != std::string::npos) {
                mapping.path = rest.substr(first);
            }
        }
        maps.push_back(std::move(mapping));
    }
    return maps;
}

const Mapping* find_mapping(const std::vector<Mapping>& maps, std::uintptr_t addr) {
    for (const auto& mapping : maps) {
        if (mapping.contains(addr)) {
            return &mapping;
        }
    }
    return nullptr;
}

const Mapping* find_module_base(const std::vector<Mapping>& maps, std::string_view module_name) {
    const Mapping* best = nullptr;
    for (const auto& mapping : maps) {
        if (mapping.offset != 0) {
            continue;
        }
        if (!path_ends_with_ignore_case(mapping.path, module_name)) {
            continue;
        }
        if (best == nullptr || mapping.start < best->start) {
            best = &mapping;
        }
    }
    return best;
}

bool maps_contain_module(const std::vector<Mapping>& maps, std::string_view module_name) {
    return find_module_base(maps, module_name) != nullptr;
}

}  // namespace proton_inject
