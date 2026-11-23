#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <unordered_map>
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
// One screen cell's glyph for --ascii: "┌" -> "+", "●" -> "*"; unknown glyphs pass through.
std::string_view ascii_glyph(std::string_view g);

// When a package was last used (usage.cpp), from shell history and binary atimes.
struct Usage {
    int64_t last = 0; // unix seconds: last use, or install time when used == false
    enum Confidence : uint8_t { none, low, medium, high } conf = none; // none = no binaries
    std::string_view source; // what `last` came from, for the Details pane
    bool used = false;       // false = no sign of use since install
};

struct Package {
    std::string_view ecosystem; // static literal, e.g. "apt"
    std::string name;
    std::string version;
    std::string root;                  // install root; empty when files are scattered (apt)
    std::vector<std::string> bins;     // exposed executables; apt/pacman: manifest paths, late
    bool essential = false;            // never offer uninstall
    bool automatic = false;            // apt/pacman: pulled in as a dependency
    std::string arch;                  // apt only: locates name:arch.list for Multi-Arch packages
    std::optional<DiskUsage> size;     // filled in later, never during scan
    std::optional<Usage> usage;        // likewise
    std::string module;                // go only: main module path, for the proxy lookup
    std::vector<std::string> deps;     // apt/pacman: names it depends on (and recommends)
    std::vector<std::string> provides; // apt/pacman: virtual names it satisfies
    std::optional<std::string> latest; // registry's latest version ("" = lookup failed)
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
// Sizes from the last run, so a restart paints at once. Lines are "bytes files skipped key".
using SizeCache = std::unordered_map<std::string, DiskUsage>;
std::string size_key(const Package& p); // changes with version, root or root's mtime
SizeCache load_sizes(const std::string& file);
void save_sizes(const std::string& file, const SizeCache& sizes); // best effort, atomic
// Manifest file lists (absolute paths); nullopt when the manifest can't be read.
std::optional<std::vector<std::string>> dpkg_files(const Package& p,
                                                   const char* info_dir = "/var/lib/dpkg/info");
std::optional<std::vector<std::string>>
pacman_files(const Package& p, const std::string& local_dir = "/var/lib/pacman/local");

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
// index = apt's Packages lists or extended_states (no Status field): keep every entry.
// states: apt's Auto-Installed marks, for Package::automatic.
std::vector<Package> parse_dpkg_status(std::string_view text, bool index = false);
ScanResult scan_dpkg(const char* path = "/var/lib/dpkg/status",
                     const char* states = "/var/lib/apt/extended_states");

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
// Hands entries to the apt/pacman package whose bins (manifest paths from package_bins, filled
// in once read: too slow cold for startup) list them. scan_path runs it too.
void own_system_bins(std::vector<PathEntry>& path, const std::vector<Package>& pkgs);

// Shell history: command name -> last run (unix seconds), 0 = run, but the history has no
// timestamps (bash without HISTTIMEFORMAT, plain zsh). Reads zsh, bash and fish formats.
using History = std::unordered_map<std::string, int64_t>;
std::vector<std::string_view> command_names(std::string_view line); // "sudo a | b" -> a, b
void parse_history(std::string_view text, History& out);
History load_history(const std::string& home); // $HISTFILE, zsh, bash, fish

using Bin = std::pair<std::string, std::string>; // command name, file it runs
// apt/pacman: bin-dir files from the manifest. Others: their $PATH entries (index = pkg).
std::vector<Bin> package_bins(const Package& p, int index, std::span<const PathEntry> path);
// activity: ecosystem evidence (npm_activity), counted only when nothing better is known.
Usage bin_usage(std::span<const Bin> bins, const History& h, int64_t activity = 0);
// npm's cache index (~/.npm/_cacache/index-v5): package name -> last registry fetch.
void parse_npm_index(std::string_view text, History& out);
History load_npm_cache(const std::string& home);
int64_t npm_activity(const Package& p, const History& npm); // npm/pnpm only, else 0
// Libraries (apt/pacman, no binaries, installed as a dependency) take their dependents' latest
// use, transitively. Unused since install when every dependent is known unused, or there are
// none; still unknown when a dependent's use is. Run once usages are in.
void dependents_usage(std::vector<Package>& pkgs, const char* info_dir = "/var/lib/dpkg/info",
                      const std::string& local_dir = "/var/lib/pacman/local");
std::string ago(int64_t ts, int64_t now); // "today", "12d ago", "5mo ago", "3y ago"
// "● 12d ago", "◌ never", "◌ in history": confidence glyph (● high ◐ medium ◌ low) + when.
std::string last_active(const Usage& u, int64_t now);
// Bloat score: bytes x days idle (min 1). -1 until both size and usage are known, or when
// usage can't be told (no binaries).
double bloat(const Package& p, int64_t now);

// Mutation (mutate.cpp): the ecosystem's own command as an argv, never a shell string. Empty
// = nothing to offer (essential package, or the tool has no such command).
std::vector<std::string> uninstall_argv(const Package& p);
std::vector<std::string> purge_argv(const Cache& c);
// Runs argv on the inherited terminal and waits: exit code, 128 + signal, or 127 when it
// couldn't start. Ctrl-C reaches the child; the caller ignores it meanwhile.
int run_foreground(const std::vector<std::string>& argv);

// Docker (docker.cpp): read from the Engine API over its unix socket ($DOCKER_HOST unix://, else
// /var/run/docker.sock), a request rather than a subprocess. No socket = empty, not an error.
struct DockerItem {
    std::string_view kind;         // container, image, volume, network: the ecosystem column
    std::string id, name;          // image name: first tag (+N more) or <none>
    std::string status;            // container: "Up 2 hours"; image: short id; else driver
    std::string state;             // container only: running, exited, ...
    std::string image;             // container: its image; volume: mountpoint
    std::vector<std::string> tags; // image only
    std::optional<DiskUsage> size; // as the daemon reports it; container = writable layer
    int64_t shared = 0;            // image: bytes shared with other images
    int64_t refs = -1;             // image/volume: containers using it, -1 = unknown
};
std::vector<DockerItem> parse_docker(std::string_view df, std::string_view networks);
std::expected<std::string, std::string> docker_get(const std::string& sock, std::string_view path);
std::expected<std::vector<DockerItem>, std::string> scan_docker();
// toggle: docker start/stop (containers). Otherwise remove; empty for a running container or a
// built-in network.
std::vector<std::string> docker_argv(const DockerItem& d, bool toggle);

// Opt-in update checks (updates.cpp, http.cpp). Network only on request, never on startup.
std::string update_url(const Package& p); // "" = no registry for this ecosystem
std::string parse_latest(std::string_view eco, std::string_view body); // "" when not found
// <0, 0, >0 like strcmp, dpkg rules: epoch first, then digit runs numerically, ~ sorts first.
int vercmp(std::string_view a, std::string_view b);
bool outdated(const Package& p);
// apt and pacman answer from their own local indexes (refreshed by apt update / pacman -Sy),
// no network: highest version in the apt lists or pacman sync dbs, "" when none has it.
bool local_update_source(const Package& p);
void local_latest(const std::vector<Package>& pkgs,
                  const std::function<void(size_t, std::string)>& done,
                  const std::string& apt_lists = "/var/lib/apt/lists",
                  const std::string& pacman_sync = "/var/lib/pacman/sync");
bool updates_supported();         // false when built without libcurl: registries are skipped
bool checkable(const Package& p); // has a local index or (with libcurl) a registry
// Local indexes first, then every registry, 6 requests at a time; done(i, latest) runs on
// the worker threads. Returns how many packages it looked up. Stops early when st does.
size_t check_updates(const std::vector<Package>& pkgs, std::stop_token st,
                     std::function<void(size_t, std::string)> done);

// tui.cpp: full-screen UI. Sizes are computed in the background; returns the exit code.
// rescan re-reads every provider's metadata; the TUI keeps only the mutated provider's rows.
int run_tui(std::vector<Package> pkgs, std::vector<Cache> caches, std::vector<PathEntry> path,
            std::vector<std::string> errors, std::function<std::vector<Package>()> rescan,
            bool updates, // updates: look up registry versions in the background
            bool ascii);  // ascii: draw with ASCII only (--ascii)
