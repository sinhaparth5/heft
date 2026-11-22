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

    // npm cache: packuments and tarballs, scoped names unescaped; later fetch wins.
    History npm;
    parse_npm_index("a\t{\"key\":\"make-fetch-happen:request-cache:https://registry.npmjs.org/"
                    "@scope%2fcli\",\"time\":1700000000123,\"metadata\":{\"time\":1}}\n"
                    "b\t{\"key\":\"make-fetch-happen:request-cache:https://registry.npmjs.org/"
                    "typescript/-/typescript-5.0.0.tgz\",\"time\":1700000500000}\n"
                    "c\t{\"key\":\"make-fetch-happen:request-cache:https://registry.npmjs.org/"
                    "typescript\",\"time\":1700000100000}\n",
                    npm);
    assert(npm.at("@scope/cli") == 1700000000 && npm.at("typescript") == 1700000500);
    Package ts;
    ts.ecosystem = "npm", ts.name = "typescript";
    assert(npm_activity(ts, npm) == 1700000500);
    ts.ecosystem = "cargo";
    assert(npm_activity(ts, npm) == 0);
    u = bin_usage(idle, {}, now + 60); // beats install time; still only low
    assert(u.used && u.conf == Usage::low && u.last == now + 60 && u.source == "npm cache");
    assert(bin_usage(used, {}, now + 7200).conf == Usage::medium); // atime outranks it
    assert(bin_usage({}, {}, 5).used);                             // libraries too

    // Libraries: dependents' use, transitively and through Provides; orphans are unused.
    std::vector<Package> pk(7);
    for (auto& p : pk)
        p.ecosystem = "apt", p.usage = Usage{}, p.automatic = true;
    pk[0].name = "app", pk[0].deps = {"liba"}, pk[0].usage = Usage{1000, Usage::high, "x", true};
    pk[1].name = "liba", pk[1].deps = {"virt", "app"};
    pk[2].name = "libb", pk[2].provides = {"virt"};
    pk[3].name = "orphan";
    pk[4].name = "idle-dep", pk[4].deps = {"orphan"}, pk[4].usage = Usage{9, Usage::low, "x"};
    pk[5].name = "app-in-opt", pk[5].deps = {"libc"}, pk[5].automatic = false; // can't tell
    pk[6].name = "libc";
    touch(t / "orphan.list", now);
    for (int run = 0; run < 2; ++run) { // a second run (refresh) gives the same answer
        dependents_usage(pk, t.c_str());
        assert(pk[1].usage->used && pk[1].usage->last == 1000);
        assert(pk[2].usage->used && pk[2].usage->last == 1000 && pk[2].usage->conf == Usage::low);
        assert(!pk[3].usage->used && pk[3].usage->source == "dependents: unused");
        assert(pk[3].usage->last >= now - 5);     // the .list mtime
        assert(pk[0].usage->conf == Usage::high); // apps untouched
        assert(pk[5].usage->conf == Usage::none); // explicit, no binaries: not a library
        assert(pk[6].usage->conf == Usage::none); // its dependent is unknown, so is it
    }
    pk[4].deps.clear();
    dependents_usage(pk, t.c_str());
    assert(pk[3].usage->source == "dependents: none");

    fs::remove_all(t);
    std::println("test_usage: ok");
}
