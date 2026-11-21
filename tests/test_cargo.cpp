#include "pkgtop.hpp"

#include <cassert>
#include <print>

int main() {
    auto r = scan_cargo(FIXTURES "/cargo");
    assert(r);
    auto& p = *r;
    assert(p.size() == 4);
    assert(p[0].name == "cargo-edit" && p[0].version == "0.13.0" && p[0].bins.size() == 4);
    assert(p[0].bins[3] == "cargo-upgrade");
    assert(p[1].name == "mytool" && p[1].bins.empty());
    assert(p[2].name == "ripgrep" && p[2].bins == std::vector<std::string>{"rg"});
    assert(p[2].root == FIXTURES "/cargo/bin" && p[2].ecosystem == "cargo");
    assert(p[3].name == "zellij" && p[3].version == "0.41.0");

    assert(parse_crates_toml("", "/x").empty());
    assert(parse_crates_toml("[v1]\n", "/x").empty());
    assert(scan_cargo("/nonexistent")->empty()); // no cargo = empty, not error

    std::println("test_cargo: ok");
}
