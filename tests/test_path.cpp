#include "pkgtop.hpp"

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
    char tmpl[] = "/tmp/pkgtop-path-XXXXXX";
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

    fs::remove_all(t);
    std::println("test_path: ok");
}
