#include "pkgtop.hpp"

#include <cassert>
#include <fstream>
#include <print>
#include <sstream>

int main() {
    std::ifstream f(FIXTURES "/dpkg_status");
    std::stringstream ss;
    ss << f.rdbuf();
    auto pkgs = parse_dpkg_status(ss.str());

    // "half" has no blank line before the next stanza, so its fields get overwritten by
    // the next Package: line. Real dpkg files always separate stanzas; this just must not crash.
    assert(pkgs.size() == 3);
    assert(pkgs[0].name == "bash" && pkgs[0].essential && pkgs[0].version == "5.2.37-2ubuntu1");
    assert(pkgs[1].name == "ripgrep" && !pkgs[1].essential); // hold counts as installed
    assert(pkgs[2].name == "no-trailing-newline" && pkgs[2].version == "2:3.4~rc1");
    for (auto& p : pkgs)
        assert(p.ecosystem == "apt");

    assert(parse_dpkg_status("").empty());
    assert(scan_dpkg("/nonexistent/status")->empty()); // not Debian = empty, not error

    std::println("test_dpkg: ok");
}
