#include "pkgtop.hpp"

#include <csignal>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

std::vector<std::string> uninstall_argv(const Package& p) {
    if (p.essential)
        return {};
    auto e = p.ecosystem;
    if (e == "apt") // name:arch, or apt may pick another arch's copy of a Multi-Arch package
        return {"sudo", "apt", "remove",
                p.arch.empty() || p.arch == "all" ? p.name : p.name + ":" + p.arch};
    if (e == "pacman")
        return {"sudo", "pacman", "-R", p.name};
    // ponytail: removes the linked version; other Cellar versions of the same formula need
    // `brew uninstall --force`, add it when rows per version get their own action.
    if (e == "brew")
        return {"brew", "uninstall", p.name};
    if (e == "cargo")
        return {"cargo", "uninstall", p.name};
    if (e == "uv")
        return {"uv", "tool", "uninstall", p.name};
    if (e == "pipx")
        return {"pipx", "uninstall", p.name};
    if (e == "pnpm")
        return {"pnpm", "remove", "-g", p.name};
    if (e == "npm") { // the prefix it lives under, not whatever npm on $PATH defaults to
        auto at = p.root.rfind("/lib/node_modules/");
        if (at == p.root.npos)
            return {};
        return {"npm", "uninstall", "-g", "--prefix", p.root.substr(0, at), p.name};
    }
    return {}; // go: no uninstall command, and pkgtop never deletes files itself
}

std::vector<std::string> purge_argv(const Cache& c) {
    auto e = c.ecosystem;
    if (e == "apt")
        return {"sudo", "apt", "clean"};
    if (e == "pip")
        return {"pip", "cache", "purge"};
    if (e == "uv")
        return {"uv", "cache", "clean"};
    if (e == "npm")
        return {"npm", "cache", "clean", "--force"};
    if (e == "pnpm")
        return {"pnpm", "store", "prune"};
    if (e == "brew")
        return {"brew", "cleanup", "--prune=all"};
    if (e == "go") // two caches: module downloads and build outputs
        return {"go", "clean", c.path.ends_with("/pkg/mod") ? "-modcache" : "-cache"};
    return {}; // cargo: no built-in cache command
}

int run_foreground(const std::vector<std::string>& argv) {
    if (argv.empty())
        return 127;
    std::vector<char*> args;
    for (auto& a : argv)
        args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    // Ctrl-C goes to the whole foreground group: it should stop the child, not pkgtop.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGINT);
    sigaddset(&def, SIGQUIT);
    posix_spawnattr_setsigdefault(&attr, &def);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);
    auto old_int = std::signal(SIGINT, SIG_IGN), old_quit = std::signal(SIGQUIT, SIG_IGN);

    pid_t pid;
    int status = 0, rc = 127;
    if (::posix_spawnp(&pid, args[0], nullptr, &attr, args.data(), environ) == 0) {
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    std::signal(SIGINT, old_int), std::signal(SIGQUIT, old_quit);
    posix_spawnattr_destroy(&attr);
    return rc;
}
