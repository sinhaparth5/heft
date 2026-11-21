#include "pkgtop.hpp"

#include <cstring>
#include <elf.h>

namespace {

uint64_t uvarint(std::string_view& s) {
    uint64_t v = 0;
    for (int shift = 0; !s.empty() && shift < 64; shift += 7) {
        auto b = uint8_t(s.front());
        s.remove_prefix(1);
        v |= uint64_t(b & 0x7f) << shift;
        if (!(b & 0x80))
            return v;
    }
    return 0;
}

// Section contents by name, from a 64-bit little-endian ELF image. Empty if absent.
std::string_view elf_section(std::string_view img, std::string_view want) {
    Elf64_Ehdr eh;
    if (img.size() < sizeof eh || std::memcmp(img.data(), ELFMAG, SELFMAG) != 0 ||
        img[EI_CLASS] != ELFCLASS64 || img[EI_DATA] != ELFDATA2LSB)
        return {}; // ponytail: 64-bit LE only (x86_64, arm64); add 32-bit/BE if anyone asks
    std::memcpy(&eh, img.data(), sizeof eh);
    auto shdr = [&](size_t i) {
        Elf64_Shdr sh{};
        size_t off = eh.e_shoff + i * sizeof sh;
        if (off + sizeof sh <= img.size())
            std::memcpy(&sh, img.data() + off, sizeof sh);
        return sh;
    };
    if (eh.e_shentsize != sizeof(Elf64_Shdr) || eh.e_shstrndx >= eh.e_shnum)
        return {};
    auto strtab = shdr(eh.e_shstrndx);
    for (size_t i = 0; i < eh.e_shnum; ++i) {
        auto sh = shdr(i);
        size_t name = strtab.sh_offset + sh.sh_name;
        if (name >= img.size() || sh.sh_offset + sh.sh_size > img.size())
            continue;
        if (std::string_view{img.data() + name} == want) // NUL-terminated in the string table
            return img.substr(sh.sh_offset, sh.sh_size);
    }
    return {};
}

} // namespace

// .go.buildinfo (Go 1.18+): "\xff Go buildinf:" + ptrsize + flags, padded to 32 bytes, then
// varint-prefixed Go version and module info. Module info lines: "path\t...",
// "mod\t<path>\t<ver>\t<sum>".
std::optional<GoBuildInfo> parse_go_buildinfo(std::string_view elf) {
    auto s = elf_section(elf, ".go.buildinfo");
    if (s.size() < 32 || !s.starts_with("\xff Go buildinf:") || !(s[15] & 2))
        return std::nullopt; // ponytail: pre-1.18 pointer-based layout not supported
    s.remove_prefix(32);
    GoBuildInfo bi;
    auto take = [&] {
        auto n = uvarint(s);
        auto v = s.substr(0, std::min<uint64_t>(n, s.size()));
        s.remove_prefix(v.size());
        return v;
    };
    bi.go_version = take();
    for (auto mod = take(); !mod.empty();) {
        auto nl = mod.find('\n');
        auto line = mod.substr(0, nl);
        mod = nl == mod.npos ? std::string_view{} : mod.substr(nl + 1);
        if (line.starts_with("mod\t")) {
            line.remove_prefix(4);
            auto tab = line.find('\t');
            bi.module = line.substr(0, tab);
            if (tab != line.npos)
                bi.version = line.substr(tab + 1, line.find('\t', tab + 1) - tab - 1);
            break;
        }
    }
    return bi;
}

// Go binaries in GOBIN: one row per binary, versioned by its main module.
ScanResult scan_go(const std::string& bin_dir) {
    std::vector<Package> out;
    for (auto& name : list_dir(bin_dir)) {
        auto f = map_file((bin_dir + "/" + name).c_str());
        if (!f)
            continue; // a directory or unreadable: not ours to judge
        auto bi = parse_go_buildinfo(f->text);
        if (!bi)
            continue; // not a Go binary (script, C binary, pre-1.18 Go)
        Package p;
        p.ecosystem = "go";
        p.name = name;
        p.version = bi->version.empty() ? "(devel)" : bi->version;
        p.root = bin_dir;
        p.bins = {name};
        p.module = bi->module;
        out.push_back(std::move(p));
    }
    return out;
}
