#include "pkgtop.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <pwd.h>
#include <unistd.h>
#include <print>
#include <string_view>

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

int list() {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Package> all;
    int errors = 0;

    auto add = [&](std::string_view eco, ScanResult r) {
        if (r)
            std::ranges::move(*r, std::back_inserter(all));
        else
            std::println(stderr, "{}: {}", eco, r.error()), ++errors;
    };
    add("apt", scan_dpkg());
    add("cargo", scan_cargo(env_or("CARGO_HOME", home() + "/.cargo")));

    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0);

    size_t w = 4, vw = 7;
    for (auto& p : all)
        w = std::max(w, p.name.size()), vw = std::max(vw, p.version.size());
    w = std::min<size_t>(w, 40), vw = std::min<size_t>(vw, 32);

    std::println("{:<{}}  {:<5}  {:<{}}  {}", "NAME", w, "ECO", "VERSION", vw, "BINS");
    for (auto& p : all) {
        std::string bins;
        for (auto& b : p.bins)
            bins += (bins.empty() ? "" : " ") + b;
        if (p.essential)
            bins += bins.empty() ? "[essential]" : " [essential]";
        auto row = std::format("{:<{}}  {:<5}  {:<{}}  {}", p.name, w, p.ecosystem, p.version, vw, bins);
        row.erase(row.find_last_not_of(' ') + 1);
        std::println("{}", row);
    }
    std::println(stderr, "{} packages in {:.2f} ms", all.size(), ms.count());
    return errors ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string_view arg = argc > 1 ? argv[1] : "";
    if (arg == "--version") {
        std::println("pkgtop {}", PKGTOP_VERSION);
        return 0;
    }
    if (arg == "--list")
        return list();
    std::println(stderr, "usage: pkgtop --list | --version   (TUI coming in M3)");
    return 2;
}
