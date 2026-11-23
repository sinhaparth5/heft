#include "heft.hpp"

#include <cstdlib>
#include <sys/stat.h>

namespace {

std::string env_or(const char* name, std::string fallback) {
    const char* v = std::getenv(name);
    return v && *v ? v : std::move(fallback);
}

bool is_dir(const std::string& p) {
    struct stat st;
    return ::lstat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

} // namespace

std::vector<Cache> find_caches(const std::string& home) {
    auto xdg = env_or("XDG_CACHE_HOME", home + "/.cache");
    auto cargo = env_or("CARGO_HOME", home + "/.cargo");
    auto gopath = env_or("GOPATH", home + "/go");

    // ponytail: fixed default locations + the env vars that move them; reading each tool's
    // config file (pip.conf, .npmrc, pnpm's store-dir) comes with that tool's provider.
    std::pair<std::string_view, std::string> known[] = {
        {"apt", "/var/cache/apt/archives"},
        {"cargo", cargo + "/registry"},
        {"cargo", cargo + "/git"},
        {"pip", env_or("PIP_CACHE_DIR", xdg + "/pip")},
        {"uv", env_or("UV_CACHE_DIR", xdg + "/uv")},
        {"npm", home + "/.npm"},
        {"pnpm", env_or("PNPM_HOME", home + "/.local/share/pnpm") + "/store"},
        {"go", gopath + "/pkg/mod"},
        {"go", env_or("GOCACHE", xdg + "/go-build")},
        {"brew", xdg + "/Homebrew"},
    };
    std::vector<Cache> out;
    for (auto& [eco, path] : known)
        if (is_dir(path))
            out.push_back({eco, path, {}});
    return out;
}
