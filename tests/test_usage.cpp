#include "pkgtop.hpp"

#include <cassert>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <print>
#include <sys/stat.h>

namespace fs = std::filesystem;
using V = std::vector<std::string_view>;

void touch(const fs::path& p, time_t atime) {
    std::ofstream{p} << "x";
    timespec ts[2] = {{atime, 0}, {0, UTIME_OMIT}};
    ::utimensat(AT_FDCWD, p.c_str(), ts, 0);
}

int main() {
    assert(command_names("git status") == V{"git"});
    assert((command_names("FOO=1 sudo ./build/pkgtop --list | rg -i x && ls") ==
            V{"pkgtop", "rg", "ls"}));
    assert(command_names("git commit -m 'a | b; c'") == V{"git"}); // quoted separators
    assert((command_names("(cd x; make)") == V{"cd", "make"}));
    assert(command_names("  ").empty());

    History h;
    parse_history(": 1700000000:0;cargo build\n"
                  ": 1700000500:3;cargo test | rg ok\n" // later run wins
                  "#1700000100\n"
                  "npm install\n" // bash with timestamps
                  "htop\n"        // bash without: 0
                  "- cmd: fish_cmd --x\n"
                  "  when: 1700000200\n"
                  "  paths:\n"
                  "    - ignored\n"
                  "- cmd: untimed_fish\n",
                  h);
    assert(h.at("cargo") == 1700000500 && h.at("rg") == 1700000500);
    assert(h.at("npm") == 1700000100 && h.at("htop") == 0);
    assert(h.at("fish_cmd") == 1700000200 && h.at("untimed_fish") == 0);
    assert(!h.contains("ignored") && !h.contains("-"));

    char tmpl[] = "/tmp/pkgtop-usage-XXXXXX";
    fs::path t = ::mkdtemp(tmpl);
    time_t now = std::time(nullptr);
    touch(t / "used", now + 3600); // read after install
    touch(t / "idle", now - 3600); // never read since install

    std::vector<Bin> used{{"used", t / "used"}}, idle{{"idle", t / "idle"}};
    auto u = bin_usage(used, {});
    assert(u.used && u.conf == Usage::medium && u.last == now + 3600);
    u = bin_usage(used, {{"used", 0}});
    assert(u.conf == Usage::high); // atime corroborated by untimed history
    u = bin_usage(idle, {{"idle", 1700000000}});
    assert(u.used && u.conf == Usage::high && u.last == 1700000000);
    u = bin_usage(idle, {{"idle", 0}});
    assert(u.used && u.conf == Usage::low);
    u = bin_usage(idle, {});
    assert(!u.used && u.conf == Usage::low && u.last >= now - 5);
    assert(bin_usage({}, {}).conf == Usage::none);

    fs::remove_all(t);
    std::println("test_usage: ok");
}
