#include "heft.hpp"

#include <cassert>
#include <print>

using V = std::vector<std::string>;

int main() {
    Package p;
    p.ecosystem = "cargo", p.name = "ripgrep";
    assert((uninstall_argv(p) == V{"cargo", "uninstall", "ripgrep"}));
    p.essential = true;
    assert(uninstall_argv(p).empty()); // never offered

    Package npm;
    npm.ecosystem = "npm", npm.name = "@scope/cli";
    npm.root = "/home/u/.nvm/versions/node/v24/lib/node_modules/@scope/cli";
    assert((uninstall_argv(npm) == V{"npm", "uninstall", "-g", "--prefix",
                                     "/home/u/.nvm/versions/node/v24", "@scope/cli"}));

    Package apt;
    apt.ecosystem = "apt", apt.name = "libc6", apt.arch = "i386";
    assert(uninstall_argv(apt).back() == "libc6:i386");
    apt.arch = "all";
    assert(uninstall_argv(apt).back() == "libc6");

    // brew: the version opt/ links to is uninstalled; an older keg is cleaned up instead.
    auto brew = *scan_brew(FIXTURES "/brew");
    assert((uninstall_argv(brew[1]) == V{"brew", "uninstall", "jq"})); // 1.7.1, linked
    assert((uninstall_argv(brew[0]) == V{"brew", "cleanup", "jq"}));   // 1.6

    Package go;
    go.ecosystem = "go", go.name = "gopls";
    assert(uninstall_argv(go).empty()); // no native command; heft won't rm it

    assert((purge_argv({"go", "/home/u/go/pkg/mod", {}}) == V{"go", "clean", "-modcache"}));
    assert(purge_argv({"cargo", "/home/u/.cargo/registry", {}}).empty());

    // A name with shell metacharacters stays one argument: no shell is involved.
    assert(run_foreground({"sh", "-c", "exit 3"}) == 3);
    assert(run_foreground({"true", "; exit 9"}) == 0);
    assert(run_foreground({"/nonexistent/heft-test"}) == 127);
    assert(run_foreground({"sh", "-c", "kill -TERM $$"}) == 128 + 15);
    std::println("test_mutate: ok");
}
