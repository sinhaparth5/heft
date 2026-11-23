#include "heft.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>

namespace fs = std::filesystem;

namespace {

void exe(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream{p} << "#!/bin/sh\n";
    fs::permissions(p, fs::perms::owner_all);
}

const PathEntry& find(const std::vector<PathEntry>& v, std::string_view name, int rank) {
    for (auto& e : v)
        if (e.name == name && e.rank == rank)
            return e;
    std::println(stderr, "missing {} #{}", name, rank);
    std::abort();
}

} // namespace

int main() {
    char tmpl[] = "/tmp/heft-path-XXXXXX";
    fs::path t = ::mkdtemp(tmpl);

    // a: cargo-style shared bin dir; b: a link farm into a uv tool root; c: pyenv shims.
    exe(t / "a/rg");
    exe(t / "a/tool");
    exe(t / "tools/ruff/bin/ruff");
    fs::create_directories(t / "b");
    fs::create_symlink(t / "tools/ruff/bin/ruff", t / "b/ruff");
    fs::create_symlink(t / "a/rg", t / "b/rg");          // same file as a/rg: not shadowed
    fs::create_symlink(t / "nowhere", t / "b/gone");     // dangling
    exe(t / "b/tool");                                   // different file: shadowed by a/tool
    std::ofstream{t / "b/README"} << "not executable\n"; // skipped
    exe(t / ".pyenv/shims/python");
    fs::create_symlink(t / "a", t / "a-link"); // same real dir as a: listed once

    std::vector<Package> pkgs(2);
    pkgs[0].ecosystem = "cargo", pkgs[0].root = t / "a", pkgs[0].bins = {"rg"};
    pkgs[1].ecosystem = "uv", pkgs[1].root = t / "tools/ruff", pkgs[1].bins = {"ruff"};

    auto path = std::format("{0}/a::relative:{0}/a-link:{0}/b:{0}/.pyenv/shims", t.string());
    auto v = scan_path(path, pkgs);
    assert(v.size() == 7); // a: rg tool; b: gone rg ruff tool; shims: python

    auto& rg = find(v, "rg", 1);
    assert(rg.owner == "cargo" && rg.pkg == 0 && !rg.shadowed);
    assert(!find(v, "rg", 2).shadowed && find(v, "rg", 2).pkg == 0);
    auto& ruff = find(v, "ruff", 2);
    assert(ruff.owner == "uv" && ruff.pkg == 1 && ruff.target == (t / "tools/ruff/bin/ruff"));
    assert(find(v, "tool", 2).shadowed && find(v, "tool", 2).owner == "other");
    assert(!find(v, "tool", 1).shadowed && find(v, "tool", 1).owner == "other");
    auto& gone = find(v, "gone", 2);
    assert(gone.dangling && gone.target.empty());
    auto& py = find(v, "python", 3);
    assert(py.shim && py.owner == "pyenv");

    // pnpm wrappers (sh scripts, not links): cmd-shim-target line, a $basedir exec (older
    // pnpm), and pn exec'ing the pnpm beside it. apt owns files its manifest bins list.
    exe(t / "pnpm/node_modules/pnpm/bin/pnpm.cjs");
    exe(t / "pnpm/node_modules/tsc/bin/tsc");
    exe(t / "apt/bin/curl");
    fs::create_directories(t / "w");
    std::ofstream{t / "w/pnpm"} << "#!/bin/sh\nexec node \"$x\"\n# cmd-shim-target=" << t.string()
                                << "/pnpm/node_modules/pnpm/bin/pnpm.cjs\n";
    std::ofstream{t / "w/tsc"}
        << "#!/bin/sh\nexec \"$basedir/../pnpm/node_modules/tsc/bin/tsc\" \"$@\"\n";
    std::ofstream{t / "w/pn"} << "#!/bin/sh\nexec \"${self%/*}/pnpm\" \"$@\"\n";
    for (auto n : {"pnpm", "tsc", "pn"})
        fs::permissions(t / "w" / n, fs::perms::owner_all);
    pkgs.resize(5);
    pkgs[2].ecosystem = "pnpm", pkgs[2].root = t / "pnpm/node_modules/pnpm";
    pkgs[3].ecosystem = "pnpm", pkgs[3].root = t / "pnpm/node_modules/tsc";
    pkgs[4].ecosystem = "apt", pkgs[4].bins = {t / "apt-link/bin/curl"}; // dir via a symlink
    fs::create_symlink(t / "apt", t / "apt-link");
    auto w = scan_path(std::format("{0}/w:{0}/apt/bin", t.string()), pkgs);
    assert(find(w, "pnpm", 1).pkg == 2 && find(w, "pn", 1).pkg == 2 && find(w, "tsc", 1).pkg == 3);
    assert(find(w, "curl", 2).owner == "apt" && find(w, "curl", 2).pkg == 4);

    fs::remove_all(t);
    std::println("test_path: ok");
}
