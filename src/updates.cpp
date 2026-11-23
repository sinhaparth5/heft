#include "heft.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

#ifdef HEFT_ZLIB
#include <zlib.h>
#endif

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

char at(std::string_view s, size_t i) { return i < s.size() ? s[i] : '\0'; }
bool digit(char c) { return c >= '0' && c <= '9'; }

// dpkg's character order: ~ before the end of the string, then letters, then the rest.
int order(char c) {
    if (digit(c))
        return 0;
    if (std::isalpha(static_cast<unsigned char>(c)))
        return c;
    if (c == '~')
        return -1;
    return c ? c + 256 : 0;
}

// dpkg's verrevcmp: alternating non-digit and digit runs.
int runs_cmp(std::string_view a, std::string_view b) {
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        while ((i < a.size() && !digit(a[i])) || (j < b.size() && !digit(b[j]))) {
            if (int d = order(at(a, i)) - order(at(b, j)))
                return d;
            ++i, ++j;
        }
        while (at(a, i) == '0')
            ++i;
        while (at(b, j) == '0')
            ++j;
        int first = 0;
        for (; digit(at(a, i)) && digit(at(b, j)); ++i, ++j)
            if (!first)
                first = a[i] - b[j];
        if (digit(at(a, i)))
            return 1;
        if (digit(at(b, j)))
            return -1;
        if (first)
            return first;
    }
    return 0;
}

// "2:1.0" -> (2, "1.0"); no epoch = 0.
std::pair<long, std::string_view> split_epoch(std::string_view v) {
    auto c = v.find(':');
    if (c == v.npos || c == 0 || !std::all_of(v.begin(), v.begin() + c, digit))
        return {0, v};
    return {std::stol(std::string{v.substr(0, c)}), v.substr(c + 1)};
}

// semver: "1.0.0-rc1+build" -> "1.0.0~rc1", so the pre-release sorts before 1.0.0.
std::string semver(std::string_view v) {
    std::string s{v.substr(0, v.find('+'))};
    std::ranges::replace(s, '-', '~');
    return s;
}

#ifdef HEFT_ZLIB
// Calls f(name, contents) for each regular file in a tar, gzip-compressed or plain.
template <class F> void each_tar_file(const std::string& path, F f) {
    gzFile z = gzopen(path.c_str(), "rb");
    if (!z)
        return;
    char h[512];
    std::string body;
    // ponytail: gzip/plain only; a zstd-compressed sync db stops at the magic check.
    while (gzread(z, h, 512) == 512 && std::memcmp(h + 257, "ustar", 5) == 0) {
        auto size = std::strtoull(std::string(h + 124, 12).c_str(), nullptr, 8);
        body.resize((size + 511) / 512 * 512);
        if (!body.empty() && gzread(z, body.data(), unsigned(body.size())) != int(body.size()))
            break;
        if (h[156] == '0' || h[156] == '\0')
            f(std::string_view(h, strnlen(h, 100)), std::string_view(body).substr(0, size));
    }
    gzclose(z);
}
#endif

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
    if (e == "brew") // ponytail: homebrew/core only; tap formulae 404 and show as unknown
        return "https://formulae.brew.sh/api/formula/" + p.name + ".json";
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
    if (eco == "brew") { // Cellar dirs carry the formula revision: 1.7.1_1
        std::string v{json_get(json_get(body, "versions"), "stable")};
        if (auto rev = json_get(body, "revision"); !v.empty() && !rev.empty() && rev != "0")
            v += "_" + std::string{rev};
        return v;
    }
    return {};
}

int vercmp(std::string_view a, std::string_view b) {
    auto [ea, va] = split_epoch(a);
    auto [eb, vb] = split_epoch(b);
    return ea != eb ? (ea < eb ? -1 : 1) : runs_cmp(va, vb);
}

// ponytail: dpkg rules for PyPI too, so a local 2.0rc1 counts as newer than 2.0; PEP 440 when
// that bites.
bool outdated(const Package& p) {
    if (!p.latest || p.latest->empty())
        return false;
    auto e = p.ecosystem;
    if (e == "cargo" || e == "npm" || e == "pnpm" || e == "go")
        return vercmp(semver(p.version), semver(*p.latest)) < 0;
    return vercmp(p.version, *p.latest) < 0;
}

bool local_update_source(const Package& p) {
#ifdef HEFT_ZLIB
    if (p.ecosystem == "pacman")
        return true;
#endif
    return p.ecosystem == "apt";
}

void local_latest(const std::vector<Package>& pkgs,
                  const std::function<void(size_t, std::string)>& done,
                  const std::string& apt_lists, const std::string& pacman_sync) {
    // Keyed "eco name arch" (apt only: amd64 and i386 builds differ); value = best version.
    auto key = [](const Package& p) {
        return std::string{p.ecosystem} + " " + p.name + " " + (p.ecosystem == "apt" ? p.arch : "");
    };
    std::unordered_map<std::string, std::string> best;
    for (auto& p : pkgs)
        if (local_update_source(p))
            best.try_emplace(key(p));
    if (best.empty())
        return;
    auto offer = [&](const Package& p) {
        if (auto it = best.find(key(p));
            it != best.end() && (it->second.empty() || vercmp(it->second, p.version) < 0))
            it->second = p.version;
    };
    // ponytail: highest version in any list, ignoring apt pinning and priorities.
    for (auto& f : list_dir(apt_lists))
        if (f.ends_with("_Packages"))
            if (auto m = map_file((apt_lists + "/" + f).c_str()))
                for (auto& p : parse_dpkg_status(m->text, true))
                    offer(p);
#ifdef HEFT_ZLIB
    for (auto& f : list_dir(pacman_sync))
        if (f.ends_with(".db"))
            each_tar_file(pacman_sync + "/" + f, [&](std::string_view name, std::string_view body) {
                if (name.ends_with("/desc"))
                    offer(parse_pacman_desc(body));
            });
#else
    (void)pacman_sync;
#endif
    for (size_t i = 0; i < pkgs.size(); ++i)
        if (local_update_source(pkgs[i]))
            done(i, best[key(pkgs[i])]);
}
