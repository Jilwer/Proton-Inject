#include "native/pe_export.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace proton_inject {

namespace {

constexpr uint16_t kDosMagic = 0x5A4D;         // MZ
constexpr uint32_t kPeSignature = 0x00004550;  // PE\0\0
constexpr uint16_t kPe32PlusMagic = 0x020B;
constexpr uint16_t kMachineAmd64 = 0x8664;
constexpr uint32_t kExportDirectoryIndex = 0;
constexpr uint32_t kImportDirectoryIndex = 1;
constexpr uint32_t kScnMemExecute = 0x20000000;
constexpr uint32_t kScnMemWrite = 0x80000000;
constexpr uint64_t kOrdinalFlag64 = 0x8000000000000000ULL;

struct DosHeader {
    uint16_t e_magic;
    uint8_t pad[58];
    int32_t e_lfanew;
};

struct CoffHeader {
    uint16_t machine;
    uint16_t number_of_sections;
    uint32_t time_date_stamp;
    uint32_t pointer_to_symbol_table;
    uint32_t number_of_symbols;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
};

struct DataDirectory {
    uint32_t virtual_address;
    uint32_t size;
};

struct ExportDirectory {
    uint32_t characteristics;
    uint32_t time_date_stamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t name;
    uint32_t base;
    uint32_t number_of_functions;
    uint32_t number_of_names;
    uint32_t address_of_functions;
    uint32_t address_of_names;
    uint32_t address_of_name_ordinals;
};

struct SectionHeader {
    char name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t pointer_to_linenumbers;
    uint16_t number_of_relocations;
    uint16_t number_of_linenumbers;
    uint32_t characteristics;
};

struct ImportDescriptor {
    uint32_t original_first_thunk;
    uint32_t time_date_stamp;
    uint32_t forwarder_chain;
    uint32_t name;
    uint32_t first_thunk;
};

static_assert(sizeof(DosHeader) == 64);
static_assert(sizeof(CoffHeader) == 20);
static_assert(sizeof(ExportDirectory) == 40);
static_assert(sizeof(SectionHeader) == 40);
static_assert(sizeof(ImportDescriptor) == 20);

constexpr std::size_t kOptionalMagicOffset = 0;
constexpr std::size_t kSectionAlignmentOffset = 32;
constexpr std::size_t kNumberOfRvaAndSizesOffset = 108;
constexpr std::size_t kDataDirectoryOffset = 112;

std::expected<void, std::string> read_exact(const ProcMem& mem, std::uintptr_t addr, void* out,
                                            std::size_t size) {
    return mem.read(addr, std::span(static_cast<std::byte*>(out), size));
}

bool is_zero_run(const ProcMem& mem, std::uintptr_t addr, std::size_t size) {
    std::vector<std::byte> buf(size);
    if (!mem.read(addr, std::span(buf))) {
        return false;
    }
    return std::all_of(buf.begin(), buf.end(), [](std::byte b) { return b == std::byte{0}; });
}

struct PeHeaders {
    std::uintptr_t nt = 0;
    CoffHeader coff{};
    uint32_t number_of_rva_and_sizes = 0;
};

std::expected<PeHeaders, std::string> read_pe_headers(const ProcMem& mem,
                                                      std::uintptr_t image_base) {
    DosHeader dos{};
    if (auto err = read_exact(mem, image_base, &dos, sizeof(dos)); !err) {
        return std::unexpected(err.error());
    }
    if (dos.e_magic != kDosMagic || dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000) {
        return std::unexpected("Remote image is not a PE (bad DOS header)");
    }

    PeHeaders headers;
    headers.nt = image_base + static_cast<std::uintptr_t>(dos.e_lfanew);

    uint32_t signature = 0;
    if (auto err = read_exact(mem, headers.nt, &signature, sizeof(signature)); !err) {
        return std::unexpected(err.error());
    }
    if (signature != kPeSignature) {
        return std::unexpected("Remote image is not a PE (bad NT signature)");
    }

    if (auto err = read_exact(mem, headers.nt + 4, &headers.coff, sizeof(headers.coff)); !err) {
        return std::unexpected(err.error());
    }
    if (headers.coff.machine != kMachineAmd64) {
        return std::unexpected("Remote PE is not x86_64 (Linux IAT methods are 64-bit only)");
    }
    if (headers.coff.size_of_optional_header < kDataDirectoryOffset + sizeof(DataDirectory)) {
        return std::unexpected("Remote PE optional header is too small");
    }

    const std::uintptr_t optional = headers.nt + 4 + sizeof(CoffHeader);
    uint16_t magic = 0;
    if (auto err = read_exact(mem, optional + kOptionalMagicOffset, &magic, sizeof(magic)); !err) {
        return std::unexpected(err.error());
    }
    if (magic != kPe32PlusMagic) {
        return std::unexpected("Remote PE is not PE32+");
    }

    if (auto err =
            read_exact(mem, optional + kNumberOfRvaAndSizesOffset, &headers.number_of_rva_and_sizes,
                       sizeof(headers.number_of_rva_and_sizes));
        !err) {
        return std::unexpected(err.error());
    }
    return headers;
}

std::expected<DataDirectory, std::string> read_data_directory(const ProcMem& mem,
                                                              const PeHeaders& headers,
                                                              uint32_t index) {
    if (headers.number_of_rva_and_sizes <= index) {
        return std::unexpected("Remote PE has no data directory entry " + std::to_string(index));
    }
    DataDirectory entry{};
    const std::uintptr_t addr =
        headers.nt + 4 + sizeof(CoffHeader) + kDataDirectoryOffset + index * sizeof(DataDirectory);
    if (auto err = read_exact(mem, addr, &entry, sizeof(entry)); !err) {
        return std::unexpected(err.error());
    }
    return entry;
}

}  // namespace

std::expected<std::uintptr_t, std::string> find_pe_export(const ProcMem& mem,
                                                          std::uintptr_t image_base,
                                                          std::string_view function_name) {
    const auto headers = read_pe_headers(mem, image_base);
    if (!headers) {
        return std::unexpected(headers.error());
    }

    const auto directory = read_data_directory(mem, *headers, kExportDirectoryIndex);
    if (!directory) {
        return std::unexpected(directory.error());
    }
    const DataDirectory export_dir_entry = *directory;
    if (export_dir_entry.virtual_address == 0 || export_dir_entry.size == 0) {
        return std::unexpected("Remote PE export directory is empty");
    }

    ExportDirectory export_dir{};
    if (auto err = read_exact(mem, image_base + export_dir_entry.virtual_address, &export_dir,
                              sizeof(export_dir));
        !err) {
        return std::unexpected(err.error());
    }
    if (export_dir.number_of_names == 0 || export_dir.number_of_names > 0x10000 ||
        export_dir.number_of_functions == 0 || export_dir.number_of_functions > 0x10000) {
        return std::unexpected("Remote PE export directory looks invalid");
    }

    std::vector<uint32_t> name_rvas(export_dir.number_of_names);
    std::vector<uint16_t> ordinals(export_dir.number_of_names);
    std::vector<uint32_t> function_rvas(export_dir.number_of_functions);
    if (auto err = read_exact(mem, image_base + export_dir.address_of_names, name_rvas.data(),
                              name_rvas.size() * sizeof(uint32_t));
        !err) {
        return std::unexpected(err.error());
    }
    if (auto err = read_exact(mem, image_base + export_dir.address_of_name_ordinals,
                              ordinals.data(), ordinals.size() * sizeof(uint16_t));
        !err) {
        return std::unexpected(err.error());
    }
    if (auto err = read_exact(mem, image_base + export_dir.address_of_functions,
                              function_rvas.data(), function_rvas.size() * sizeof(uint32_t));
        !err) {
        return std::unexpected(err.error());
    }

    const std::size_t name_len = function_name.size() + 1;
    std::vector<char> export_name(name_len, '\0');
    for (uint32_t i = 0; i < export_dir.number_of_names; ++i) {
        if (auto err = read_exact(mem, image_base + name_rvas[i], export_name.data(), name_len);
            !err) {
            continue;
        }
        if (std::string_view(export_name.data()) != function_name) {
            continue;
        }
        if (ordinals[i] >= export_dir.number_of_functions) {
            continue;
        }
        const uint32_t function_rva = function_rvas[ordinals[i]];
        if (function_rva >= export_dir_entry.virtual_address &&
            function_rva < export_dir_entry.virtual_address + export_dir_entry.size) {
            return std::unexpected("Export " + std::string(function_name) + " is forwarded");
        }
        return image_base + function_rva;
    }
    return std::unexpected("Export " + std::string(function_name) + " not found");
}

std::optional<IatSlot> find_iat_slot(const ProcMem& mem, std::uintptr_t image_base,
                                     std::string_view function_name) {
    // probing every mapped image, so a module without a usable import table is an ordinary
    // outcome rather than an error worth a message.
    const auto headers = read_pe_headers(mem, image_base);
    if (!headers) {
        return std::nullopt;
    }
    const auto directory = read_data_directory(mem, *headers, kImportDirectoryIndex);
    if (!directory) {
        return std::nullopt;
    }
    const DataDirectory import_dir = *directory;
    if (import_dir.virtual_address == 0 || import_dir.size < sizeof(ImportDescriptor)) {
        return std::nullopt;
    }

    const std::size_t count = import_dir.size / sizeof(ImportDescriptor);
    for (std::size_t i = 0; i < count && i < 512; ++i) {
        ImportDescriptor desc{};
        const std::uintptr_t desc_addr =
            image_base + import_dir.virtual_address + i * sizeof(ImportDescriptor);
        if (!read_exact(mem, desc_addr, &desc, sizeof(desc))) {
            break;
        }
        if (desc.name == 0 && desc.first_thunk == 0) {
            break;
        }
        const uint32_t int_rva =
            desc.original_first_thunk != 0 ? desc.original_first_thunk : desc.first_thunk;
        if (int_rva == 0 || desc.first_thunk == 0) {
            continue;
        }
        for (std::size_t j = 0; j < 4096; ++j) {
            uint64_t thunk = 0;
            uint64_t iat_value = 0;
            if (!read_exact(mem, image_base + int_rva + j * 8, &thunk, sizeof(thunk)) ||
                !read_exact(mem, image_base + desc.first_thunk + j * 8, &iat_value,
                            sizeof(iat_value))) {
                break;
            }
            if (thunk == 0) {
                break;
            }
            if ((thunk & kOrdinalFlag64) != 0) {
                continue;
            }
            const std::uintptr_t name_addr = image_base + static_cast<std::uintptr_t>(thunk) + 2;
            std::vector<char> imported(function_name.size() + 1, '\0');
            if (!read_exact(mem, name_addr, imported.data(), imported.size())) {
                continue;
            }
            if (std::string_view(imported.data()) != function_name) {
                continue;
            }
            if (iat_value == 0) {
                return std::nullopt;
            }
            return IatSlot{image_base + desc.first_thunk + j * 8, iat_value};
        }
    }
    return std::nullopt;
}

std::expected<std::vector<SectionCave>, std::string> find_pe_caves(const ProcMem& mem,
                                                                   std::uintptr_t image_base) {
    const auto headers = read_pe_headers(mem, image_base);
    if (!headers) {
        return std::unexpected(headers.error());
    }
    const CoffHeader& coff = headers->coff;
    if (coff.number_of_sections == 0 || coff.number_of_sections > 96) {
        return std::unexpected("Remote PE has an invalid section count");
    }

    uint32_t section_alignment = 0x1000;
    if (auto err = read_exact(mem, headers->nt + 4 + sizeof(CoffHeader) + kSectionAlignmentOffset,
                              &section_alignment, sizeof(section_alignment));
        !err) {
        return std::unexpected(err.error());
    }
    if (section_alignment == 0) {
        section_alignment = 0x1000;
    }

    const std::uintptr_t section_table =
        headers->nt + 4 + sizeof(CoffHeader) + coff.size_of_optional_header;
    std::vector<SectionHeader> sections(coff.number_of_sections);
    if (auto err = read_exact(mem, section_table, sections.data(),
                              sections.size() * sizeof(SectionHeader));
        !err) {
        return std::unexpected(err.error());
    }

    std::vector<uint32_t> starts;
    starts.reserve(sections.size());
    for (const auto& section : sections) {
        starts.push_back(section.virtual_address);
    }
    std::sort(starts.begin(), starts.end());

    std::vector<SectionCave> caves;
    for (const auto& section : sections) {
        if (section.virtual_size == 0) {
            continue;
        }
        const uint32_t aligned =
            (section.virtual_size + section_alignment - 1) / section_alignment * section_alignment;
        uint32_t cave_end = section.virtual_address + aligned;
        auto next = std::upper_bound(starts.begin(), starts.end(), section.virtual_address);
        if (next != starts.end() && *next < cave_end) {
            cave_end = *next;
        }
        const uint32_t cave_rva = section.virtual_address + section.virtual_size;
        if (cave_end <= cave_rva) {
            continue;
        }
        SectionCave cave;
        cave.addr = image_base + cave_rva;
        cave.size = cave_end - cave_rva;
        cave.writable = (section.characteristics & kScnMemWrite) != 0;
        cave.executable = (section.characteristics & kScnMemExecute) != 0;
        if (cave.size < 16) {
            continue;
        }
        const std::size_t check = std::min(cave.size, static_cast<std::size_t>(256));
        if (!is_zero_run(mem, cave.addr, check)) {
            continue;
        }
        caves.push_back(cave);
    }
    return caves;
}

}  // namespace proton_inject
