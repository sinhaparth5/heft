#include "heft.hpp"

namespace {

// Next "..." string starting at or after pos; returns contents and advances pos past it.
std::string_view next_string(std::string_view s, size_t& pos) {
    auto a = s.find('"', pos);
    if (a == s.npos)
        return pos = s.npos, std::string_view{};
    auto b =
        s.find('"', a + 1); // crate names, versions, sources and bin names never contain quotes
    if (b == s.npos)
        return pos = s.npos, std::string_view{};
    pos = b + 1;
    return s.substr(a + 1, b - a - 1);
}

} // namespace

// [v1]
// "ripgrep 14.1.1 (registry+https://github.com/rust-lang/crates.io-index)" = ["rg"]
std::vector<Package> parse_crates_toml(std::string_view text, std::string_view cargo_home) {
    std::vector<Package> out;
    while (!text.empty()) {
        auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == text.npos ? std::string_view{} : text.substr(nl + 1);

        if (!line.starts_with('"'))
            continue; // [v1] header, blanks
        size_t pos = 0;
        auto key = next_string(line, pos); // "name version (source)"
        auto eq = line.find('=', pos);
        if (pos == line.npos || eq == line.npos)
            continue;

        auto sp1 = key.find(' ');
        if (sp1 == key.npos)
            continue;
        auto sp2 = key.find(' ', sp1 + 1);

        Package p;
        p.ecosystem = "cargo";
        p.name = key.substr(0, sp1);
        p.version = key.substr(sp1 + 1, sp2 == key.npos ? key.npos : sp2 - sp1 - 1);
        p.root = std::string{cargo_home} + "/bin";
        for (pos = eq; pos != line.npos;)
            if (auto bin = next_string(line, pos); pos != line.npos)
                p.bins.emplace_back(bin);
        out.push_back(std::move(p));
    }
    return out;
}

ScanResult scan_cargo(const std::string& cargo_home) {
    auto f = map_file((cargo_home + "/.crates.toml").c_str());
    if (!f)
        return std::unexpected(f.error());
    return parse_crates_toml(f->text, cargo_home);
}
