#include "pkgtop.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <sys/stat.h>

namespace {

int64_t number(std::string_view s) {
    int64_t v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    return ec == std::errc{} ? v : 0;
}

void note(History& h, std::string_view name, int64_t when) {
    auto [it, fresh] = h.try_emplace(std::string{name}, when);
    if (!fresh)
        it->second = std::max(it->second, when);
}

bool bin_path(std::string_view p) {
    for (std::string_view dir :
         {"/usr/bin/", "/usr/sbin/", "/bin/", "/sbin/", "/usr/games/", "/usr/local/bin/"})
        if (p.starts_with(dir) && p.find('/', dir.size()) == p.npos)
            return true;
    return false;
}

} // namespace

std::vector<std::string_view> command_names(std::string_view line) {
    // Split on | ; & outside quotes, then take each segment's first real word.
    std::vector<std::string_view> segs;
    char quote = 0;
    size_t start = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (quote)
            quote = c == quote ? 0 : quote;
        else if (c == '\'' || c == '"')
            quote = c;
        else if (c == '|' || c == ';' || c == '&')
            segs.push_back(line.substr(start, i - start)), start = i + 1;
    }
    segs.push_back(line.substr(start));

    static constexpr std::string_view prefixes[] = {"sudo",  "doas",  "time",    "nohup",
                                                    "exec",  "env",   "command", "nice",
                                                    "xargs", "watch", "strace"};
    std::vector<std::string_view> out;
    for (auto seg : segs) {
        // ponytail: flags of prefix commands (sudo -u x) are not skipped; their argument is
        // taken as the command and simply never matches a binary.
        while (!seg.empty()) {
            auto b = seg.find_first_not_of(" \t({");
            if (b == seg.npos)
                break;
            seg.remove_prefix(b);
            auto e = seg.find_first_of(" \t)}"); // "(cd x; make)"
            auto word = seg.substr(0, e);
            seg = e == seg.npos ? std::string_view{} : seg.substr(e);
            if (word.find('=') != word.npos || std::ranges::contains(prefixes, word))
                continue; // FOO=bar cmd, sudo cmd
            if (word.front() != '-')
                out.push_back(word.substr(word.rfind('/') + 1)); // ./build/pkgtop -> pkgtop
            break;
        }
    }
    return out;
}

void parse_history(std::string_view text, History& out) {
    int64_t pending = 0; // bash "#<ts>" or fish "when:" applies to the command next to it
    std::vector<std::string_view> fish_cmd;
    while (!text.empty()) {
        auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == text.npos ? std::string_view{} : text.substr(nl + 1);

        if (line.starts_with(": ")) { // zsh extended: ": <start>:<elapsed>;<command>"
            auto semi = line.find(';');
            if (semi != line.npos) {
                auto ts = number(line.substr(2, line.find(':', 2) - 2));
                for (auto n : command_names(line.substr(semi + 1)))
                    note(out, n, ts);
            }
        } else if (line.starts_with("- cmd: ")) { // fish: the time follows on "  when:"
            for (auto n : fish_cmd)
                note(out, n, 0);
            fish_cmd = command_names(line.substr(7));
        } else if (line.starts_with("  when: ")) {
            for (auto n : fish_cmd)
                note(out, n, number(line.substr(8)));
            fish_cmd.clear();
        } else if (line.starts_with('#') && line.size() > 1 && number(line.substr(1))) {
            pending = number(line.substr(1)); // bash with HISTTIMEFORMAT
        } else if (!line.empty() && line[0] != ' ') {
            for (auto n : command_names(line))
                note(out, n, pending);
            pending = 0;
        }
    }
    for (auto n : fish_cmd)
        note(out, n, 0);
}

History load_history(const std::string& home) {
    History h;
    std::vector<std::string> files;
    if (const char* f = std::getenv("HISTFILE"); f && *f)
        files.emplace_back(f);
    const char* zd = std::getenv("ZDOTDIR");
    const char* xd = std::getenv("XDG_DATA_HOME");
    files.push_back((zd && *zd ? std::string{zd} : home) + "/.zsh_history");
    files.push_back(home + "/.bash_history");
    files.push_back((xd && *xd ? std::string{xd} : home + "/.local/share") + "/fish/fish_history");
    std::ranges::sort(files);
    files.erase(std::ranges::unique(files).begin(), files.end());
    for (auto& f : files)
        if (auto m = map_file(f.c_str()); m)
            parse_history(m->text, h);
    return h;
}

std::vector<Bin> package_bins(const Package& p, int index, std::span<const PathEntry> path) {
    std::vector<Bin> out;
    if (p.ecosystem == "apt" || p.ecosystem == "pacman") {
        auto files = p.ecosystem == "apt" ? dpkg_files(p) : pacman_files(p);
        for (auto& f : files.value_or(std::vector<std::string>{}))
            if (bin_path(f))
                out.emplace_back(f.substr(f.rfind('/') + 1), f);
        return out;
    }
    // ponytail: only binaries that are on $PATH count; a tool reachable only by full path
    // looks unused.
    for (auto& e : path)
        if (e.pkg == index && !e.dangling)
            out.emplace_back(e.name, e.target);
    return out;
}

Usage bin_usage(std::span<const Bin> bins, const History& h, int64_t activity) {
    Usage u;
    Usage seen{activity, Usage::low, "npm cache", true};
    if (bins.empty())
        return activity ? seen : u; // libraries, data packages: no way to tell
    int64_t hist = 0, atime = 0, installed = 0;
    bool untimed = false;
    for (auto& [name, file] : bins) {
        if (auto it = h.find(name); it != h.end())
            it->second ? hist = std::max(hist, it->second) : untimed = true;
        struct stat st;
        if (::stat(file.c_str(), &st) != 0)
            continue;
        // ctime, not mtime: dpkg keeps the build-time mtime but the inode changes at install.
        // An atime after install means something read it, normally an exec (relatime: at most
        // once a day). Read-only scanners (indexers, backups) look like use too.
        installed = std::max<int64_t>(installed, st.st_ctime);
        if (st.st_atime > st.st_ctime + 60)
            atime = std::max<int64_t>(atime, st.st_atime);
    }
    if (hist)
        u = {std::max(hist, atime), Usage::high, "shell history", true};
    else if (atime)
        u = {atime, untimed ? Usage::high : Usage::medium,
             untimed ? "atime + shell history" : "atime", true};
    else if (untimed)
        u = {installed, Usage::low, "shell history (no time)", true};
    else
        u = {installed, Usage::low, "never seen; install time", false};
    return u.conf == Usage::low && activity > u.last ? seen : u;
}

void parse_npm_index(std::string_view text, History& out) {
    // "<sha1>\t{"key":"make-fetch-happen:request-cache:https://registry.npmjs.org/@a%2fb",
    // ..."time":1783771436389,...}", one line per write. Tarballs: <host>/<name>/-/<file>.tgz.
    // ponytail: assumes the registry sits at the host root (not artifactory's /api/npm/x/).
    static constexpr std::string_view tag = "request-cache:", time = "\"time\":";
    for (size_t at = 0; (at = text.find(tag, at)) != text.npos;) {
        auto url = text.substr(at += tag.size());
        url = url.substr(0, url.find('"'));
        auto host = url.find("//");
        auto path = url.substr(std::min(url.size(), url.find('/', host + 2) + 1));
        path = path.substr(0, path.find("/-/"));
        std::string name{path};
        if (auto pct = name.find("%2"); pct != name.npos && (name[pct + 2] | 32) == 'f')
            name.replace(pct, 3, "/");
        auto t = text.find(time, at);
        if (t == text.npos)
            break;
        if (!name.empty())
            note(out, name, number(text.substr(t + time.size(), 20)) / 1000);
    }
}

History load_npm_cache(const std::string& home) {
    const char* c = std::getenv("npm_config_cache");
    auto index = (c && *c ? std::string{c} : home + "/.npm") + "/_cacache/index-v5";
    History h;
    for (auto& a : list_dir(index)) // index-v5/xx/yy/<rest of the hash>
        for (auto& b : list_dir(index + "/" + a))
            for (auto& f : list_dir(index + "/" + a + "/" + b))
                if (auto m = map_file((index + "/" + a + "/" + b + "/" + f).c_str()); m)
                    parse_npm_index(m->text, h);
    return h;
}

int64_t npm_activity(const Package& p, const History& npm) {
    auto it = npm.find(p.name);
    return (p.ecosystem == "npm" || p.ecosystem == "pnpm") && it != npm.end() ? it->second : 0;
}

void dependents_usage(std::vector<Package>& pkgs, const char* info_dir,
                      const std::string& local_dir) {
    auto managed = [](const Package& p) { return p.ecosystem == "apt" || p.ecosystem == "pacman"; };
    std::unordered_map<std::string_view, std::vector<size_t>> by_name; // real or provided name
    for (size_t i = 0; i < pkgs.size(); ++i)
        if (managed(pkgs[i])) {
            by_name[pkgs[i].name].push_back(i);
            for (auto& v : pkgs[i].provides)
                by_name[v].push_back(i);
        }
    std::vector<std::vector<size_t>> rdeps(pkgs.size());
    for (size_t i = 0; i < pkgs.size(); ++i)
        for (auto& d : pkgs[i].deps)
            if (auto it = by_name.find(d); it != by_name.end())
                for (auto j : it->second)
                    if (j != i && pkgs[j].ecosystem == pkgs[i].ecosystem)
                        rdeps[j].push_back(i);

    // Libraries: no binaries (or set by an earlier run: refresh re-runs this). Explicit installs
    // without binaries are apps living elsewhere (/opt, /usr/share): still can't tell those.
    std::vector<size_t> libs;
    std::vector<char> lib(pkgs.size());
    for (size_t j = 0; j < pkgs.size(); ++j)
        if (auto& u = pkgs[j].usage;
            managed(pkgs[j]) && pkgs[j].automatic && u &&
            (u->conf == Usage::none || u->source.starts_with("dependents")))
            libs.push_back(j), lib[j] = 1, u = Usage{};
    // A use only ever moves later, so this settles; each pass reaches one level deeper.
    for (bool changed = true; changed;) {
        changed = false;
        for (auto j : libs) {
            int64_t last = 0;
            for (auto i : rdeps[j])
                if (auto& u = pkgs[i].usage; u && u->used)
                    last = std::max(last, u->last);
            if (last > pkgs[j].usage->last)
                pkgs[j].usage = Usage{last, Usage::low, "dependents: used", true}, changed = true;
        }
    }
    // Unknown spreads the same way: a dependent we can't tell about, or an unknown library.
    std::vector<char> unknown(pkgs.size());
    for (bool changed = true; changed;) {
        changed = false;
        for (auto j : libs)
            if (!pkgs[j].usage->used && !unknown[j])
                for (auto i : rdeps[j])
                    if (auto& u = pkgs[i].usage;
                        lib[i] ? unknown[i] : !u || u->conf == Usage::none) {
                        unknown[j] = changed = true;
                        break;
                    }
    }
    for (auto j : libs) {
        auto& p = pkgs[j];
        if (p.usage->used || unknown[j])
            continue;
        // Install time: dpkg rewrites name.list, pacman the desc, on every install or upgrade.
        auto f = p.ecosystem == "apt" ? std::string{info_dir} + "/" + p.name + ".list"
                                      : local_dir + "/" + p.name + "-" + p.version + "/desc";
        struct stat st;
        if (::stat(f.c_str(), &st) != 0 && p.ecosystem == "apt")
            f = std::string{info_dir} + "/" + p.name + ":" + p.arch + ".list";
        int64_t installed = ::stat(f.c_str(), &st) == 0 ? st.st_mtime : 0;
        p.usage = Usage{installed, Usage::low,
                        rdeps[j].empty() ? "dependents: none" : "dependents: unused", false};
    }
}

std::string ago(int64_t ts, int64_t now) {
    int64_t d = (now - ts) / 86400;
    if (d < 1)
        return "today";
    if (d < 60)
        return std::format("{}d ago", d);
    if (d < 730)
        return std::format("{}mo ago", d / 30);
    return std::format("{}y ago", d / 365);
}

double bloat(const Package& p, int64_t now) {
    if (!p.size || !p.usage || p.usage->conf == Usage::none)
        return -1;
    double idle_days = std::max<double>(1, double(now - p.usage->last) / 86400);
    return double(p.size->bytes) * idle_days;
}

std::string last_active(const Usage& u, int64_t now) {
    static constexpr std::string_view glyph[] = {" ", "◌", "◐", "●"};
    auto when = !u.used                   ? std::string{"never"}
                : u.source.ends_with(")") ? std::string{"in history"} // untimed: date unknown
                                          : ago(u.last, now);
    return std::format("{} {}", glyph[u.conf], when);
}
