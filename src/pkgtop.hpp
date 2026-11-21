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

// Package size, picking the right method for its ecosystem: manifest file lists (apt, pacman),
// just the binaries (cargo, go: root is a shared bin dir), or the whole root tree (the rest).
DiskUsage package_size(const Package& p);
DiskUsage dpkg_package_size(const Package& p, const char* info_dir = "/var/lib/dpkg/info");
DiskUsage pacman_package_size(const Package& p,
                              const std::string& local_dir = "/var/lib/pacman/local");
DiskUsage bins_usage(const Package& p); // root/<bin> for each bin

// Read-only mmap of a whole file. Missing or empty file = empty text, not an error.
struct MappedFile {
    std::string_view text;
    MappedFile() = default;
    MappedFile(MappedFile&& o) noexcept : text(std::exchange(o.text, {})) {}
    MappedFile& operator=(MappedFile&& o) noexcept { return std::swap(text, o.text), *this; }
    ~MappedFile();
};
std::expected<MappedFile, std::string> map_file(const char* path);

// Entry names in dir, sorted; skips dotfiles, and symlinks unless asked. Missing dir = empty.
std::vector<std::string> list_dir(const std::string& dir, bool symlinks = false);

// Minimal JSON (json.cpp): one level of an object/array as (key, raw value); strings unquoted,
// escapes kept. Array items have empty keys. json_get = value of one key, or "".
std::vector<std::pair<std::string_view, std::string_view>> json_items(std::string_view json);
std::string_view json_get(std::string_view object, std::string_view key);

using ScanResult = std::expected<std::vector<Package>, std::string>;

// dpkg: /var/lib/dpkg/status. Missing file = not a Debian system = empty, not an error.
std::vector<Package> parse_dpkg_status(std::string_view text);
ScanResult scan_dpkg(const char* path = "/var/lib/dpkg/status");

// cargo: $CARGO_HOME/.crates.toml (cargo writes it alongside .crates2.json; one line per install).
std::vector<Package> parse_crates_toml(std::string_view text, std::string_view cargo_home);
ScanResult scan_cargo(const std::string& cargo_home);

// Every provider below: missing directory = tool not installed = empty, not an error.

// pacman: <local_dir>/<name>-<ver>/desc (default /var/lib/pacman/local).
Package parse_pacman_desc(std::string_view text);
ScanResult scan_pacman(const std::string& local_dir);

// Homebrew: <prefix>/Cellar/<name>/<version>/INSTALL_RECEIPT.json.
ScanResult scan_brew(const std::string& prefix);

// uv tools: <tools_dir>/<name>/uv-receipt.toml (default ~/.local/share/uv/tools).
std::vector<std::string> parse_uv_receipt(std::string_view text); // entrypoint names
ScanResult scan_uv_tools(const std::string& tools_dir);

// pipx: <venvs_dir>/<name>/pipx_metadata.json.
ScanResult scan_pipx(const std::string& venvs_dir);

// npm globals: <prefix>/lib/node_modules. pnpm globals: <PNPM_HOME>/global.
Package read_node_package(const std::string& dir, std::string_view ecosystem);
ScanResult scan_npm(const std::string& node_modules);
ScanResult scan_pnpm(const std::string& global_dir);

// Go: binaries in GOBIN, versioned from their .go.buildinfo ELF section.
struct GoBuildInfo {
    std::string go_version, module, version;
};
std::optional<GoBuildInfo> parse_go_buildinfo(std::string_view elf_image);
ScanResult scan_go(const std::string& bin_dir);

// $PATH graph (path.cpp): one entry per executable in each $PATH dir, in lookup order.
// Directories that resolve to the same real dir (/bin -> /usr/bin) are listed once.
struct PathEntry {
    std::string name;
    std::string dir;        // the $PATH entry as written
    std::string target;     // fully resolved file; empty when the symlink chain is broken
    int rank = 0;           // 1-based position of dir in the deduplicated $PATH
    int pkg = -1;           // index into the packages passed to scan_path, -1 = none
    std::string_view owner; // package ecosystem, version manager, "system" or "other"
    bool shadowed = false;  // an earlier dir has a different file with the same name
    bool shim = false;      // version-manager shim (pyenv/rbenv/asdf/mise shims, volta)
    bool dangling = false;  // symlink whose chain ends nowhere; reported, never deleted
};
std::vector<PathEntry> scan_path(std::string_view path_env, const std::vector<Package>& pkgs);

// tui.cpp: full-screen UI. Sizes are computed in the background; returns the exit code.
int run_tui(std::vector<Package> pkgs, std::vector<Cache> caches, std::vector<PathEntry> path,
            std::vector<std::string> errors);
