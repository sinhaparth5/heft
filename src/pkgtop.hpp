#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

// Physical disk usage: allocated blocks (st_blocks * 512), hardlinks counted once,
// symlinks never followed, mount points never crossed.
struct DiskUsage {
    uint64_t bytes = 0;
    uint64_t files = 0;
    uint64_t skipped = 0; // entries we could not read (EACCES/EPERM); size is a lower bound
};
DiskUsage disk_usage(const char* path);                    // file or whole directory tree
DiskUsage files_usage(std::span<const std::string> paths); // explicit file list; dirs ignored
std::string human_size(uint64_t bytes);                    // 1536 -> "1.5K"

struct Package {
    std::string_view ecosystem; // static literal, e.g. "apt"
    std::string name;
    std::string version;
    std::string root;              // install root; empty when files are scattered (apt)
    std::vector<std::string> bins; // exposed executables
    bool essential = false;        // never offer uninstall
    std::string arch;              // apt only: locates name:arch.list for Multi-Arch packages
    std::optional<DiskUsage> size; // filled in later, never during scan
};

struct Cache {
    std::string_view ecosystem;
    std::string path;
    std::optional<DiskUsage> size;
};

// Known cache stores that exist on this machine. home = $HOME.
std::vector<Cache> find_caches(const std::string& home);

// Per-ecosystem package size.
DiskUsage dpkg_package_size(const Package& p, const char* info_dir = "/var/lib/dpkg/info");
DiskUsage cargo_package_size(const Package& p);

// Read-only mmap of a whole file. Missing or empty file = empty text, not an error.
struct MappedFile {
    std::string_view text;
    MappedFile() = default;
    MappedFile(MappedFile&& o) noexcept : text(std::exchange(o.text, {})) {}
    MappedFile& operator=(MappedFile&& o) noexcept { return std::swap(text, o.text), *this; }
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

// tui.cpp: full-screen UI. Sizes are computed in the background; returns the exit code.
int run_tui(std::vector<Package> pkgs, std::vector<Cache> caches, std::vector<std::string> errors);
