#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct Package {
    std::string_view ecosystem; // static literal, e.g. "apt"
    std::string name;
    std::string version;
    std::string root;              // install root; empty when files are scattered (apt)
    std::vector<std::string> bins; // exposed executables
    bool essential = false;        // never offer uninstall
};

// Read-only mmap of a whole file. Missing or empty file = empty text, not an error.
struct MappedFile {
    std::string_view text;
    MappedFile() = default;
    MappedFile(MappedFile&& o) noexcept : text(std::exchange(o.text, {})) {}
    MappedFile& operator=(MappedFile&&) = delete;
    ~MappedFile();
};
std::expected<MappedFile, std::string> map_file(const char* path);

using ScanResult = std::expected<std::vector<Package>, std::string>;

// dpkg: /var/lib/dpkg/status. Missing file = not a Debian system = empty, not an error.
std::vector<Package> parse_dpkg_status(std::string_view text);
ScanResult scan_dpkg(const char* path = "/var/lib/dpkg/status");

// cargo: $CARGO_HOME/.crates.toml (cargo writes it alongside .crates2.json; one line per install).
std::vector<Package> parse_crates_toml(std::string_view text, std::string_view cargo_home);
ScanResult scan_cargo(const std::string& cargo_home);
