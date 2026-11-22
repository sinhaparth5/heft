#include "pkgtop.hpp"

#include <cassert>
#include <print>

int main() {
    Package p;
    p.ecosystem = "cargo", p.name = "ripgrep", p.version = "14.1.0";
    assert(update_url(p) == "https://crates.io/api/v1/crates/ripgrep");
    assert(parse_latest("cargo", R"({"crate":{"id":"ripgrep","versions":[1,2],
        "max_stable_version":"14.1.1","max_version":"15.0.0-rc1"}})") == "14.1.1");
    assert(!outdated(p)); // not checked yet
    p.latest = "14.1.1";
    assert(outdated(p));
    p.latest = "";
    assert(!outdated(p)); // lookup failed: say nothing

    Package go;
    go.ecosystem = "go", go.name = "gopls", go.version = "v0.17.0";
    assert(update_url(go).empty()); // no module recorded
    go.module = "github.com/BurntSushi/toml/cmd/tomlv";
    assert(update_url(go) ==
           "https://proxy.golang.org/github.com/!burnt!sushi/toml/cmd/tomlv/@latest");
    go.version = "(devel)";
    assert(update_url(go).empty()); // built from a checkout: nothing to compare
    assert(parse_latest("go", R"({"Version":"v0.24.0","Time":"2026-10-14T17:42:10Z"})") ==
           "v0.24.0");

    assert(parse_latest("pipx", R"({"info":{"author":null,"version":"0.8.0"},"releases":{}})") ==
           "0.8.0");
    assert(parse_latest("npm", R"({"bin":{"x":"y"},"name":"x","version":"2.0.1"})") == "2.0.1");
    assert(parse_latest("npm", "") == "");

    Package apt;
    apt.ecosystem = "apt", apt.name = "bash";
    assert(update_url(apt).empty());

    // Version order: dpkg rules, semver pre-releases for registries.
    assert(vercmp("1.10", "1.9") > 0);
    assert(vercmp("1.0", "1.0") == 0 && vercmp("1.01", "1.1") == 0);
    assert(vercmp("1:1.0", "2.0") > 0); // epoch wins
    assert(vercmp("1.0~rc1", "1.0") < 0);
    assert(vercmp("5.2.37-2ubuntu1", "5.2.37-2ubuntu2") < 0);
    assert(vercmp("1.7.1_1", "1.7.1") > 0); // brew revision
    p.version = "15.0.0", p.latest = "14.1.1";
    assert(!outdated(p)); // installed is newer than the registry's stable
    p.version = "15.0.0-rc1", p.latest = "15.0.0";
    assert(outdated(p)); // semver: pre-release before release
    p.version = "15.0.0+build5";
    assert(!outdated(p));

    Package brew;
    brew.ecosystem = "brew", brew.name = "jq";
    assert(update_url(brew) == "https://formulae.brew.sh/api/formula/jq.json");
    assert(parse_latest("brew", R"({"name":"jq","versions":{"stable":"1.7.1","head":"HEAD"},
        "revision":1})") == "1.7.1_1");
    assert(parse_latest("brew", R"({"versions":{"stable":"1.8"},"revision":0})") == "1.8");

    // Local indexes: highest version across apt lists, per arch; pacman sync dbs.
    std::vector<Package> pkgs(4);
    pkgs[0].ecosystem = "apt", pkgs[0].name = "bash", pkgs[0].arch = "amd64";
    pkgs[1].ecosystem = "apt", pkgs[1].name = "local-only", pkgs[1].arch = "amd64";
    pkgs[2].ecosystem = "pacman", pkgs[2].name = "bash";
    pkgs[3].ecosystem = "cargo", pkgs[3].name = "ripgrep";
    std::vector<std::string> got(4, "-");
    local_latest(
        pkgs, [&](size_t i, std::string v) { got[i] = v; }, FIXTURES "/apt_lists",
        FIXTURES "/pacman_sync");
    assert(got[0] == "5.2.37-2ubuntu2"); // not the i386 9.9-1, not the Description line
    assert(got[1] == "");                // in no index
    assert(got[2] == (local_update_source(pkgs[2]) ? "5.3.0-1" : "-")); // needs zlib
    assert(got[3] == "-");                                              // registry, not local
    std::println("test_updates: ok");
}
