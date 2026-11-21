#include "pkgtop.hpp"

#include <cctype>

namespace {

// Go module proxy paths escape capitals as "!" + lowercase (github.com/BurntSushi -> !burnt...).
std::string go_escape(std::string_view module) {
    std::string out;
    for (char c : module)
        if (std::isupper(static_cast<unsigned char>(c)))
            out += '!', out += char(std::tolower(static_cast<unsigned char>(c)));
        else
            out += c;
    return out;
}

} // namespace

std::string update_url(const Package& p) {
    auto e = p.ecosystem;
    if (e == "cargo")
        return "https://crates.io/api/v1/crates/" + p.name;
    if (e == "uv" || e == "pipx")
        return "https://pypi.org/pypi/" + p.name + "/json";
    if (e == "npm" || e == "pnpm")
        return "https://registry.npmjs.org/" + p.name + "/latest";
    if (e == "go" && !p.module.empty() && p.version.starts_with('v'))
        return "https://proxy.golang.org/" + go_escape(p.module) + "/@latest";
    // ponytail: apt/pacman/brew need their own index files (or formulae.brew.sh); add them
    // when someone asks, they're also what `apt list --upgradable` already answers.
    return {};
}

std::string parse_latest(std::string_view eco, std::string_view body) {
    if (eco == "cargo")
        return std::string{json_get(json_get(body, "crate"), "max_stable_version")};
    if (eco == "uv" || eco == "pipx")
        return std::string{json_get(json_get(body, "info"), "version")};
    if (eco == "npm" || eco == "pnpm")
        return std::string{json_get(body, "version")};
    if (eco == "go")
        return std::string{json_get(body, "Version")};
    return {};
}

// ponytail: plain inequality, not semver; a pre-release newer than the latest stable shows
// as outdated. Compare versions properly when that bites.
bool outdated(const Package& p) { return p.latest && !p.latest->empty() && *p.latest != p.version; }
