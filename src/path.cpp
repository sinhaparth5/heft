#include "pkgtop.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace {

std::string real(const std::string& p) {
    char buf[PATH_MAX];
    return ::realpath(p.c_str(), buf) ? buf : "";
}

bool under(std::string_view path, std::string_view root) {
    return path.starts_with(root) && (path.size() == root.size() || path[root.size()] == '/');
}

// Version managers recognised by where their bin dir lives.
std::string_view manager(std::string_view dir) {
    static constexpr std::pair<std::string_view, std::string_view> known[] = {
        {"/.pyenv/", "pyenv"}, {"/.rbenv/", "rbenv"}, {"/.asdf/", "asdf"},
        {"/mise/", "mise"},    {"/.nvm/", "nvm"},     {"/.volta/", "volta"}};
    for (auto [needle, name] : known)
        if (std::string{dir}.append("/").find(needle) != std::string::npos)
            return name;
    return {};
}

} // namespace

std::vector<PathEntry> scan_path(std::string_view path_env, const std::vector<Package>& pkgs) {
    // Ownership: cargo/go roots are a shared bin dir, so match root/<bin> exactly; every
    // other provider owns everything under its root (symlinks are resolved first).
    std::unordered_map<std::string, int> exact;
    std::vector<std::pair<std::string, int>> roots;
    for (int i = 0; i < int(pkgs.size()); ++i) {
        auto& p = pkgs[i];
        if (p.root.empty())
            continue;
        if (p.ecosystem == "cargo" || p.ecosystem == "go")
            for (auto& b : p.bins)
                exact.emplace(p.root + "/" + b, i);
        else
            roots.emplace_back(p.root, i);
    }
    bool system_pkgs = std::ranges::any_of(pkgs, [](auto& p) { return p.root.empty(); });

    std::vector<PathEntry> out;
    std::vector<std::string> seen_dirs;
    std::unordered_map<std::string, size_t> first; // name -> index of the entry that wins
    int rank = 0;
    for (auto rest = path_env; !rest.empty();) {
        auto colon = rest.find(':');
        auto raw = std::string{rest.substr(0, colon)};
        rest = colon == rest.npos ? std::string_view{} : rest.substr(colon + 1);
        // ponytail: empty and relative entries (cwd lookups) are skipped, not flagged.
        if (raw.empty() || raw[0] != '/')
            continue;
        auto dir = real(raw); // /bin -> /usr/bin on merged-usr systems: same dir, not a rival
        if (dir.empty() || std::ranges::contains(seen_dirs, dir))
            continue;
        seen_dirs.push_back(dir);
        ++rank;
        auto mgr = manager(raw);
        bool shim_dir = raw.ends_with("/shims") || mgr == "volta";
        bool system_dir = dir == "/usr/bin" || dir == "/usr/sbin" || dir == "/usr/games";

        for (auto& name : list_dir(raw, true)) {
            PathEntry e;
            e.name = name, e.dir = raw, e.rank = rank;
            auto full = raw + "/" + name;
            struct stat st;
            if (::lstat(full.c_str(), &st) != 0)
                continue;
            if (S_ISLNK(st.st_mode)) {
                e.target = real(full);
                e.dangling = e.target.empty();
            } else {
                e.target = dir + "/" + name;
            }
            if (!e.dangling && (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
                                ::access(full.c_str(), X_OK) != 0))
                continue; // not an executable file (data, dirs, helpers)

            if (auto it = exact.find(e.target); it != exact.end())
                e.pkg = it->second;
            else {
                size_t best = 0;
                for (auto& [root, i] : roots)
                    if (root.size() > best && under(e.target, root))
                        best = root.size(), e.pkg = i;
            }
            // ponytail: pnpm's PNPM_HOME/bin entries are wrapper scripts, not links, so they
            // stay "other"; reading the script for its target is the upgrade path.
            e.owner = e.pkg >= 0                  ? pkgs[e.pkg].ecosystem
                      : !mgr.empty()              ? mgr
                      : system_dir && system_pkgs ? std::string_view{"system"}
                                                  : std::string_view{"other"};
            e.shim = shim_dir;

            auto [it, fresh] = first.try_emplace(name, out.size());
            e.shadowed = !fresh && out[it->second].target != e.target;
            out.push_back(std::move(e));
        }
    }
    return out;
}
