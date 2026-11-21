#include "pkgtop.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    add("apt", scan_dpkg());
    add("cargo", scan_cargo(env_or("CARGO_HOME", home() + "/.cargo")));
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
            p.size = p.ecosystem == "apt" ? dpkg_package_size(p) : cargo_package_size(p);
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
    if (arg.empty() && ::isatty(STDIN_FILENO) && ::isatty(STDOUT_FILENO)) {
        auto inv = scan_all();
        return run_tui(std::move(inv.pkgs), find_caches(home()), std::move(inv.errors));
    }
    std::println(stderr, "usage: pkgtop [--list [--sizes] | --caches | --version]");
    return 2;
}
