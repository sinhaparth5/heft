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

Usage bin_usage(std::span<const Bin> bins, const History& h) {
    Usage u;
    if (bins.empty())
        return u; // libraries, data packages: no way to tell
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
    return u;
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
