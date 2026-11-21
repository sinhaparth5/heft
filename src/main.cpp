#include "pkgtop.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <print>
#include <string_view>

namespace {

int list() {
    auto t0 = std::chrono::steady_clock::now();
    std::vector<Package> all;
    int errors = 0;

    if (auto r = scan_dpkg()) {
        std::ranges::move(*r, std::back_inserter(all));
    } else {
        std::println(stderr, "apt: {}", r.error());
        ++errors;
    }

    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0);

    size_t w = 4;
    for (auto& p : all)
        w = std::max(w, p.name.size());
    w = std::min<size_t>(w, 40);

    std::println("{:<{}}  {:<5}  {}", "NAME", w, "ECO", "VERSION");
    for (auto& p : all)
        std::println("{:<{}}  {:<5}  {}{}", p.name, w, p.ecosystem, p.version,
                     p.essential ? "  [essential]" : "");
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
