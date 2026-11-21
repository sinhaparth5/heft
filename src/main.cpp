#include "pkgtop.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <print>
#include <pwd.h>
#include <string_view>
#include <unistd.h>

namespace {

std::string env_or(const char* name, std::string fallback) {
    const char* v = std::getenv(name);
    return v && *v ? v : std::move(fallback);
}

std::string home() {
    if (const char* h = std::getenv("HOME"); h && *h)
        return h;
    const passwd* pw = ::getpwuid(::getuid());
    return pw ? pw->pw_dir : "";
}

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// "1.5M", or "1.5M+" when some entries were unreadable (size is a lower bound).
std::string size_cell(const DiskUsage& u) { return human_size(u.bytes) + (u.skipped ? "+" : ""); }

struct Inventory {
    std::vector<Package> pkgs;
    std::vector<std::string> errors; // "eco: reason", one per failed provider
};

Inventory scan_all() {
    Inventory inv;
    auto add = [&](std::string_view eco, ScanResult r) {
        if (r)
            std::ranges::move(*r, std::back_inserter(inv.pkgs));
        else
            inv.errors.push_back(std::format("{}: {}", eco, r.error()));
    };
    auto h = home();
    auto data = env_or("XDG_DATA_HOME", h + "/.local/share");
    auto gopath = env_or("GOPATH", h + "/go");

    add("apt", scan_dpkg());
    add("pacman", scan_pacman("/var/lib/pacman/local"));
    add("cargo", scan_cargo(env_or("CARGO_HOME", h + "/.cargo")));
    add("go", scan_go(env_or("GOBIN", gopath.substr(0, gopath.find(':')) + "/bin")));
    add("uv", scan_uv_tools(env_or("UV_TOOL_DIR", data + "/uv/tools")));
    if (const char* p = std::getenv("PIPX_HOME"); p && *p)
        add("pipx", scan_pipx(std::string{p} + "/venvs"));
    else // pipx moved its default home in 1.3
        for (auto& dir : {data + "/pipx/venvs", h + "/.local/pipx/venvs"})
            add("pipx", scan_pipx(dir));
    add("pnpm", scan_pnpm(env_or("PNPM_HOME", data + "/pnpm") + "/global"));

    // Homebrew: the first prefix that has a Cellar.
    for (auto& prefix : {env_or("HOMEBREW_PREFIX", "/home/linuxbrew/.linuxbrew"), h + "/.linuxbrew",
                         std::string{"/opt/homebrew"}, std::string{"/usr/local"}})
        if (!list_dir(prefix + "/Cellar").empty()) {
            add("brew", scan_brew(prefix));
            break;
        }

    // npm globals live under the prefix: $NPM_CONFIG_PREFIX, ~/.npmrc prefix=, every nvm
    // node, or /usr/local. ponytail: doesn't read /etc/npmrc or project-level .npmrc.
    std::vector<std::string> prefixes;
    if (const char* p = std::getenv("NPM_CONFIG_PREFIX"); p && *p)
        prefixes.emplace_back(p);
    if (auto rc = map_file((h + "/.npmrc").c_str()); rc)
        for (auto t = rc->text; !t.empty();) {
            auto nl = t.find('\n');
            auto line = t.substr(0, nl);
            t = nl == t.npos ? std::string_view{} : t.substr(nl + 1);
            if (line.starts_with("prefix=")) {
                auto v = std::string{line.substr(7)};
                prefixes.push_back(v.starts_with("~/") ? h + v.substr(1) : v);
            }
        }
    auto nvm = env_or("NVM_DIR", h + "/.nvm") + "/versions/node";
    for (auto& v : list_dir(nvm))
        prefixes.push_back(nvm + "/" + v);
    prefixes.emplace_back("/usr/local");
    std::ranges::sort(prefixes);
    prefixes.erase(std::ranges::unique(prefixes).begin(), prefixes.end());
    for (auto& p : prefixes)
        add("npm", scan_npm(p + "/lib/node_modules"));
    return inv;
}

int list(bool sizes) {
    auto t0 = std::chrono::steady_clock::now();
    auto [all, errs] = scan_all();
    for (auto& e : errs)
        std::println(stderr, "{}", e);
    auto ms = ms_since(t0);

    double size_ms = 0;
    if (sizes) {
        auto t1 = Clock::now();
        for (auto& p : all)
            p.size = package_size(p);
        size_ms = ms_since(t1);
        std::ranges::stable_sort(all, std::greater{}, [](auto& p) { return p.size->bytes; });
    }

    size_t w = 4, vw = 7;
    for (auto& p : all)
        w = std::max(w, p.name.size()), vw = std::max(vw, p.version.size());
    w = std::min<size_t>(w, 40), vw = std::min<size_t>(vw, 32);

    const int sw = sizes ? 7 : 0; // SIZE column, hidden without --sizes
    std::println("{:<{}}  {:<5}  {:>{}}{}{:<{}}  {}", "NAME", w, "ECO", sizes ? "SIZE" : "", sw,
                 sizes ? "  " : "", "VERSION", vw, "BINS");
    for (auto& p : all) {
        std::string bins;
        for (auto& b : p.bins)
            bins += (bins.empty() ? "" : " ") + b;
        if (p.essential)
            bins += bins.empty() ? "[essential]" : " [essential]";
        auto row = std::format("{:<{}}  {:<5}  {:>{}}{}{:<{}}  {}", p.name, w, p.ecosystem,
                               sizes ? size_cell(*p.size) : "", sw, sizes ? "  " : "", p.version,
                               vw, bins);
        row.erase(row.find_last_not_of(' ') + 1);
        std::println("{}", row);
    }
    std::println(stderr, "{} packages in {:.2f} ms", all.size(), ms);
    if (sizes)
        std::println(stderr, "sized in {:.0f} ms", size_ms);
    return errs.empty() ? 0 : 1;
}

int path() {
    auto t0 = Clock::now();
    auto [pkgs, errs] = scan_all();
    auto t1 = Clock::now();
    auto v = scan_path(env_or("PATH", ""), pkgs);
    auto ms = ms_since(t1);
    size_t shadowed = 0, dangling = 0;
    std::println("{:>3}  {:<24}  {:<6}  {:<6}  {}", "#", "NAME", "OWNER", "FLAGS", "DIR");
    for (auto& e : v) {
        shadowed += e.shadowed, dangling += e.dangling;
        auto flags =
            std::format("{}{}{}", e.shadowed ? "S" : "", e.shim ? "M" : "", e.dangling ? "X" : "");
        auto pkg = e.pkg >= 0 ? " (" + pkgs[e.pkg].name + ")" : "";
        std::println("{:>3}  {:<24}  {:<6}  {:<6}  {}{}", e.rank, e.name, e.owner, flags, e.dir,
                     pkg);
    }
    std::println(stderr,
                 "{} binaries, {} shadowed, {} dangling; resolved in {:.1f} ms ({:.1f} ms "
                 "with package scan)",
                 v.size(), shadowed, dangling, ms, ms_since(t0));
    return errs.empty() ? 0 : 1;
}

// Packages by bloat score: big and long unused first. Libraries without binaries are left
// out (no way to tell their use), and so are essential packages.
int stale() {
    auto [pkgs, errs] = scan_all();
    auto path = scan_path(env_or("PATH", ""), pkgs);
    auto t0 = Clock::now();
    auto hist = load_history(home());
    int64_t now = std::time(nullptr);
    std::vector<Package*> rows;
    for (int i = 0; i < int(pkgs.size()); ++i) {
        auto& p = pkgs[i];
        p.usage = bin_usage(package_bins(p, i, path), hist);
        if (p.usage->conf == Usage::none || p.essential)
            continue;
        p.size = package_size(p);
        rows.push_back(&p);
    }
    auto ms = ms_since(t0);
    std::ranges::sort(rows, std::greater{}, [&](auto* p) { return bloat(*p, now); });
    std::println("{:<32}  {:<6}  {:>7}  {:<14}  {}", "NAME", "ECO", "SIZE", "LAST ACTIVE",
                 "SOURCE");
    for (auto* p : rows)
        std::println("{:<32}  {:<6}  {:>7}  {:<14}  {}", p->name, p->ecosystem, size_cell(*p->size),
                     last_active(*p->usage, now), p->usage->source);
    std::println(stderr, "{} packages with binaries, {} history commands; {:.0f} ms", rows.size(),
                 hist.size(), ms);
    return errs.empty() ? 0 : 1;
}

int caches() {
    auto t0 = Clock::now();
    auto cs = find_caches(home());
    for (auto& c : cs)
        c.size = disk_usage(c.path.c_str());
    auto ms = ms_since(t0);
    std::ranges::sort(cs, std::greater{}, [](auto& c) { return c.size->bytes; });

    uint64_t total = 0;
    std::println("{:<5}  {:>7}  {:>8}  {}", "ECO", "SIZE", "FILES", "PATH");
    for (auto& c : cs) {
        total += c.size->bytes;
        std::println("{:<5}  {:>7}  {:>8}  {}", c.ecosystem, size_cell(*c.size), c.size->files,
                     c.path);
    }
    std::println(stderr, "{} caches, {} total, sized in {:.0f} ms", cs.size(), human_size(total),
                 ms);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string_view arg = argc > 1 ? argv[1] : "";
    if (arg == "--version") {
        std::println("pkgtop {}", PKGTOP_VERSION);
        return 0;
    }
    std::string_view arg2 = argc > 2 ? argv[2] : "";
    if (arg == "--list")
        return list(arg2 == "--sizes");
    if (arg == "--caches")
        return caches();
    if (arg == "--path")
        return path();
    if (arg == "--stale")
        return stale();
    if (arg.empty() && ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO)) {
        auto inv = scan_all();
        auto path = scan_path(env_or("PATH", ""), inv.pkgs);
        return run_tui(std::move(inv.pkgs), find_caches(home()), std::move(path),
                       std::move(inv.errors));
    }
    std::println(stderr,
                 "usage: pkgtop [--list [--sizes] | --caches | --path | --stale | --version]");
    return 2;
}
