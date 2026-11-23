#include "heft.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/vfs.h>
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

// pnpm's global bins are cmd-shim sh wrappers, not links. The real file is on a
// "# cmd-shim-target=" line, or in `exec "$basedir/..."`; pn/pnpx exec the pnpm beside them.
std::string wrapper_target(const std::string& file, int hops = 3) {
    auto m = map_file(file.c_str());
    if (!m || !m->text.starts_with("#!/bin/sh"))
        return "";
    auto t = m->text;
    if (auto i = t.find("\n# cmd-shim-target="); i != t.npos) {
        t.remove_prefix(i + 19);
        return std::string{t.substr(0, t.find('\n'))};
    }
    for (std::string_view pre : {"exec \"$basedir/", "exec \"${self%/*}/"})
        if (auto i = t.find(pre); i != t.npos) {
            t.remove_prefix(i + pre.size());
            auto next = file.substr(0, file.rfind('/') + 1) + std::string{t.substr(0, t.find('"'))};
            auto deeper = hops ? wrapper_target(next, hops - 1) : "";
            return deeper.empty() ? next : deeper;
        }
    return "";
}

// Everything but directories, from d_type alone: no per-file syscalls.
std::vector<std::string> plain_files(const std::string& dir) {
    std::vector<std::string> out;
    if (DIR* d = ::opendir(dir.c_str())) {
        while (dirent* e = ::readdir(d))
            if (e->d_name[0] != '.' && e->d_type != DT_DIR)
                out.emplace_back(e->d_name);
        ::closedir(d);
    }
    std::ranges::sort(out);
    return out;
}

} // namespace

void own_system_bins(std::vector<PathEntry>& path, const std::vector<Package>& pkgs) {
    std::unordered_map<std::string, std::string> dirs; // as written -> real (/bin is /usr/bin)
    auto in_real_dir = [&](const std::string& dir, std::string_view name) {
        auto [it, fresh] = dirs.try_emplace(dir);
        if (fresh)
            it->second = real(dir);
        return it->second + "/" + std::string{name};
    };
    std::unordered_map<std::string, int> owner;
    for (int i = 0; i < int(pkgs.size()); ++i)
        if (pkgs[i].ecosystem == "apt" || pkgs[i].ecosystem == "pacman")
            for (auto& f : pkgs[i].bins) {
                auto slash = f.rfind('/');
                owner.emplace(in_real_dir(f.substr(0, slash), f.substr(slash + 1)), i);
            }
    // The manifest lists the file where it sits, or where its link (alternatives, uutils) leads.
    for (auto& e : path) {
        if (e.pkg >= 0 || e.shim)
            continue;
        auto it = owner.find(in_real_dir(e.dir, e.name));
        if (it == owner.end())
            it = owner.find(e.target);
        if (it != owner.end())
            e.pkg = it->second, e.owner = pkgs[e.pkg].ecosystem;
    }
}

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
        // WSL appends the Windows PATH (/mnt/c/...). drvfs is 9P: every stat is a round trip
        // to the host, ~10 ms each, so System32 alone took 50 s. Trust d_type there instead.
        // ponytail: any 9P mount gets this; WSL1 drvfs isn't 9P and still pays full price.
        struct statfs fs;
        bool remote = ::statfs(dir.c_str(), &fs) == 0 && fs.f_type == 0x01021997; // V9FS_MAGIC

        for (auto& name : remote ? plain_files(raw) : list_dir(raw, true)) {
            PathEntry e;
            e.name = name, e.dir = raw, e.rank = rank;
            auto full = raw + "/" + name;
            e.target = dir + "/" + name;
            if (!remote) {
                struct stat st;
                if (::lstat(full.c_str(), &st) != 0)
                    continue;
                if (S_ISLNK(st.st_mode)) {
                    e.target = real(full);
                    e.dangling = e.target.empty();
                }
                if (!e.dangling && (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
                                    ::access(full.c_str(), X_OK) != 0))
                    continue; // not an executable file (data, dirs, helpers)
            }

            auto own = [&](const std::string& file) {
                if (auto it = exact.find(file); it != exact.end())
                    return e.pkg = it->second, void();
                size_t best = 0;
                for (auto& [root, i] : roots)
                    if (root.size() > best && under(file, root))
                        best = root.size(), e.pkg = i;
            };
            own(e.target);
            if (e.pkg < 0 && !e.dangling && !system_dir && mgr.empty() && !remote)
                if (auto w = wrapper_target(full); !w.empty())
                    own(real(w));
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
    own_system_bins(out, pkgs);
    return out;
}
