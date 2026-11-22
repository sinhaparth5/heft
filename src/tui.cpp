#include "pkgtop.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <print>
#include <ranges>
#include <thread>

#include <sys/stat.h>

using namespace ftxui;

namespace {

// "/home/me/.cache/uv" → "~/.cache/uv" for display.
std::string tilde(std::string_view p) {
    static const std::string home = [] {
        const char* h = std::getenv("HOME");
        return h && *h ? std::string{h} : "";
    }();
    if (!home.empty() && p.starts_with(home) && (p.size() == home.size() || p[home.size()] == '/'))
        return "~" + std::string{p.substr(home.size())};
    return std::string{p};
}

// Palette tokens. This is the only place raw hex appears.
// FTXUI downsamples RGB to the 256-colour palette when COLORTERM isn't truecolor.
struct Palette {
    bool on = true;
    Color bg, bg_alt, bg_sel, fg, fg_dim, border, border_dim, accent, sel_bg, sel_fg, ok, warn,
        danger, info;
    Color hex(uint32_t h) const {
        return on ? Color::RGB(h >> 16, (h >> 8) & 0xff, h & 0xff) : Color::Default;
    }
    explicit Palette(bool color) : on(color) {
        bg = hex(0x282828), bg_alt = hex(0x3c3836), bg_sel = hex(0x504945);
        fg = hex(0xebdbb2), fg_dim = hex(0xa89984);
        border = hex(0xd5c4a1), border_dim = hex(0x665c54), accent = hex(0xfabd2f);
        sel_bg = hex(0x8ec07c), sel_fg = hex(0x282828);
        ok = hex(0xb8bb26), warn = hex(0xfe8019), danger = hex(0xfb4934), info = hex(0x83a598);
    }
    Color eco(std::string_view e) const {
        static constexpr std::pair<std::string_view, uint32_t> tags[] = {
            {"apt", 0xfb4934}, {"pacman", 0x83a598}, {"brew", 0xfabd2f}, {"cargo", 0xfe8019},
            {"go", 0x8ec07c},  {"uv", 0xd3869b},     {"pip", 0xd3869b},  {"pipx", 0xd3869b},
            {"npm", 0xb8bb26}, {"pnpm", 0xb8bb26}};
        for (auto [k, v] : tags)
            if (k == e)
                return hex(v);
        return fg;
    }
    // Filled pill; with NO_COLOR it falls back to reverse video so it stays visible.
    Element pill(std::string s, Color back, Color front = Color::Default) const {
        auto t = text(std::move(s)) | bold;
        return on ? t | bgcolor(back) | color(front == Color::Default ? bg : front) : t | inverted;
    }
};

constexpr std::array<std::string_view, 4> kTabs = {"Packages", "Caches", "PATH", "Stale"};
constexpr int kDetailsW = 38;
constexpr std::array<std::string_view, 3> kSorts = {"size", "name", "eco"};

struct Row {
    std::string_view eco;
    std::string name;
    std::string second; // version (packages), file count (caches) or "#rank dir" (PATH)
    const std::optional<DiskUsage>* size; // null on the PATH tab
    const PathEntry* bin = nullptr;       // PATH tab only
};

// PATH flags: ⚠ shadowed, ↯ shim, ✗ broken.
std::string flags(const PathEntry& e) {
    std::string f;
    for (auto [on, g] : {std::pair{e.shadowed, "⚠"}, {e.shim, "↯"}, {e.dangling, "✗"}})
        if (on)
            f += f.empty() ? g : std::string{" "} + g;
    return f;
}

// --ascii: render as usual, then swap each non-ASCII cell for an ASCII one. FTXUI has no
// ASCII border style, so this covers borders, gauges, the spinner and status glyphs at once.
class Ascii : public Node {
  public:
    explicit Ascii(Element e) : Node({std::move(e)}) {}
    void SetBox(Box b) override {
        Node::SetBox(b);
        children_[0]->SetBox(b);
    }
    void Render(Screen& s) override {
        Node::Render(s);
        for (int y = box_.y_min; y <= box_.y_max; ++y)
            for (int x = box_.x_min; x <= box_.x_max; ++x)
                if (auto& c = s.PixelAt(x, y).character; c.size() > 1)
                    c = std::string{ascii_glyph(c)};
    }
};

// A pending uninstall/purge, shown in the confirmation modal.
struct Confirm {
    std::string title, reclaim;
    std::vector<std::string> argv;
    std::string_view eco;
    std::optional<size_t> cache; // caches index for a purge; nullopt = uninstall
};

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (auto& x : v)
        s += (s.empty() ? "" : " ") + x;
    return s;
}

struct App {
    std::vector<Package> pkgs;
    std::vector<Cache> caches;
    std::vector<PathEntry> path;
    std::vector<std::string> errors;
    std::vector<std::string_view> ecos; // every ecosystem / PATH owner seen, in scan order
    size_t providers = 0;               // how many of ecos come from packages and caches

    int tab = 0;
    std::array<int, 4> cur{}; // cursor row in the current view, per tab
    std::array<int, 4> top{}; // first visible row, per tab
    int page = 10;            // visible table rows, from the last render
    int sort = 0;
    bool rev = false;
    std::string filter;
    bool filtering = false;
    std::string eco; // "" = all
    bool keys = true;
    bool focus_eco = false;  // Tab: j/k move through the Ecosystems pane instead of the table
    bool picking = false;    // e: ecosystem popup open
    std::string picked_from; // eco before the popup opened; Esc restores it

    // Unsized rows on screen, sized before the rest. Written by render, popped by workers.
    std::mutex urgent_mu;
    std::vector<size_t> urgent; // pkgs index, or pkgs.size() + caches index

    std::function<std::vector<Package>()> rescan;
    std::optional<Confirm> confirm;
    std::string status; // last command result or refusal, shown for 5 s
    bool status_ok = false;
    std::chrono::steady_clock::time_point status_until;

    std::atomic<size_t> done{0}; // sizes + usage results computed so far
    size_t total = 0;
    int64_t now = std::time(nullptr);

    size_t count() const { return tab == 2 ? path.size() : tab == 1 ? caches.size() : pkgs.size(); }
    // Stale lists packages whose use can be told (they have binaries), minus essential ones.
    bool in_tab(size_t i) const {
        return tab != 3 ||
               (pkgs[i].usage && pkgs[i].usage->conf != Usage::none && !pkgs[i].essential);
    }

    Row row(size_t i) const {
        if (tab == 2) {
            auto& e = path[i];
            return {e.owner, e.name, std::format("#{} {}", e.rank, tilde(e.dir)), nullptr, &e};
        }
        if (tab == 1) {
            auto& c = caches[i];
            return {c.ecosystem, tilde(c.path), c.size ? std::to_string(c.size->files) : "",
                    &c.size};
        }
        auto& p = pkgs[i];
        if (tab == 3)
            return {p.ecosystem, p.name, p.usage ? last_active(*p.usage, now) : "", &p.size};
        return {p.ecosystem, p.name, outdated(p) ? p.version + " ↑" : p.version, &p.size};
    }

    // Filtered + sorted indices for the current tab. Rebuilt per frame: ~3k rows is < 1 ms.
    std::vector<size_t> view() const {
        auto fuzzy = [](std::string_view hay, std::string_view needle) {
            size_t i = 0;
            for (char c : hay)
                if (i < needle.size() && std::tolower(c) == std::tolower(needle[i]))
                    ++i;
            return i == needle.size();
        };
        std::vector<size_t> v;
        for (size_t i = 0; i < count(); ++i)
            if (auto r = row(i);
                in_tab(i) && (eco.empty() || r.eco == eco) && fuzzy(r.name, filter))
                v.push_back(i);
        auto bytes = [&](size_t i) {
            auto s = row(i).size;
            return s && *s ? int64_t((*s)->bytes) : -1; // unsized rows sink; PATH keeps order
        };
        auto less = [&](size_t x, size_t y) {
            auto a = row(x), b = row(y);
            if (sort == 0 && tab == 3) // bloat: big and long idle first
                return bloat(pkgs[x], now) > bloat(pkgs[y], now);
            if (sort == 0)
                return bytes(x) > bytes(y);
            if (sort == 1)
                return a.name < b.name;
            return std::tie(a.eco, a.name) < std::tie(b.eco, b.name);
        };
        // ponytail: re-sorts every frame, so rows reorder while sizes stream in; throttle
        // to 250 ms if that turns out to be distracting.
        std::ranges::stable_sort(v,
                                 [&](size_t x, size_t y) { return rev ? less(y, x) : less(x, y); });
        return v;
    }

    // The cursor is a row position, not an item: rows may reorder under it while sizes
    // stream in (like htop), but the cursor itself never jumps.
    int pos(size_t n) { return cur[tab] = std::clamp(cur[tab], 0, std::max(0, int(n) - 1)); }

    void move(int delta) {
        cur[tab] = std::clamp(cur[tab] + delta, 0, std::max(0, int(view().size()) - 1));
    }

    std::optional<size_t> selected() const {
        auto v = view();
        return v.empty() ? std::nullopt
                         : std::optional{v[std::clamp(cur[tab], 0, int(v.size()) - 1)]};
    }

    // "all" (as "") then every ecosystem with rows in this tab, with row counts.
    std::vector<std::pair<std::string_view, size_t>> eco_counts() const {
        std::vector<std::pair<std::string_view, size_t>> l{{"", 0}};
        for (auto e : ecos)
            l.emplace_back(e, 0);
        for (size_t i = 0; i < count(); ++i)
            if (in_tab(i)) {
                ++l[0].second;
                ++std::ranges::find(l, row(i).eco, [](auto& x) { return x.first; })->second;
            }
        std::erase_if(l, [&](auto& x) { return !x.first.empty() && !x.second; });
        return l;
    }

    void pick_eco(int k) {
        auto l = eco_counts();
        eco = l[std::clamp(k, 0, int(l.size()) - 1)].first;
        cur[tab] = 0;
    }

    void step_eco(int d) {
        auto l = eco_counts();
        auto it = std::ranges::find(l, eco, [](auto& x) { return x.first; });
        pick_eco(it == l.end() ? 0 : int(it - l.begin()) + d);
    }

    void say(std::string s, bool ok) {
        status = std::move(s), status_ok = ok;
        status_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    }

    // u / p: build the command and open the modal, or say why not.
    void ask(bool purge) {
        auto i = selected();
        if (!i || (purge ? tab != 1 : tab != 0 && tab != 3))
            return say(purge ? "p purges a cache: switch to Caches (2)"
                             : "u uninstalls a package: switch to Packages (1) or Stale (4)",
                       false);
        // Workers index into pkgs/caches; changing them mid-flight would race.
        if (done < total)
            return say("wait for loading to finish", false);
        if (purge) {
            auto& c = caches[*i];
            auto argv = purge_argv(c);
            if (argv.empty())
                return say(std::format("{} has no cache-clean command", c.ecosystem), false);
            confirm =
                Confirm{"Purge " + std::string{c.ecosystem} + " cache",
                        c.size ? "~" + human_size(c.size->bytes) : "unknown", argv, c.ecosystem, i};
            return;
        }
        auto& p = pkgs[*i];
        if (p.essential)
            return say(p.name + " is Essential: refusing", false);
        auto argv = uninstall_argv(p);
        if (argv.empty())
            return say(std::format("{} has no uninstall command", p.ecosystem), false);
        confirm =
            Confirm{"Uninstall " + p.name, p.size ? "~" + human_size(p.size->bytes) : "unknown",
                    argv, p.ecosystem, std::nullopt};
    }

    // After the child exits: rescan only the affected provider. Rows that survived keep their
    // size and usage; new or changed ones are measured here, synchronously.
    // ponytail: sync fill, fine for the usual 0-few changed rows; re-enqueue to the workers
    // if a command ever adds many.
    void refresh(const Confirm& c) {
        if (c.cache) {
            caches[*c.cache].size = disk_usage(caches[*c.cache].path.c_str());
            return;
        }
        auto key = [](const Package& p) { return std::tie(p.name, p.version, p.root); };
        std::vector<Package> next;
        for (auto& p : pkgs)
            if (p.ecosystem != c.eco)
                next.push_back(std::move(p));
        size_t kept = next.size();
        for (auto& p : rescan())
            if (p.ecosystem == c.eco) {
                auto old = std::ranges::find_if(
                    pkgs, [&](auto& o) { return o.ecosystem == c.eco && key(o) == key(p); });
                if (old != pkgs.end())
                    p.size = old->size, p.usage = old->usage, p.latest = old->latest;
                next.push_back(std::move(p));
            }
        pkgs = std::move(next);
        const char* env = std::getenv("PATH");
        path = scan_path(env ? env : "", pkgs);
        std::optional<History> hist;
        for (size_t i = kept; i < pkgs.size(); ++i) {
            auto& p = pkgs[i];
            if (!p.size)
                p.size = package_size(p);
            if (!p.usage) {
                if (!hist) {
                    const char* h = std::getenv("HOME");
                    hist = load_history(h ? h : "");
                }
                p.usage = bin_usage(package_bins(p, int(i), path), *hist);
            }
        }
    }

    uint64_t bytes_of(std::string_view e, bool caches_tab) const {
        uint64_t n = 0;
        auto sum = [&](auto& items) {
            for (auto& x : items)
                if (x.size && (e.empty() || x.ecosystem == e))
                    n += x.size->bytes;
        };
        caches_tab ? sum(caches) : sum(pkgs);
        return n;
    }
};

std::string trunc(std::string_view s, int w) {
    if (w <= 0)
        return "";
    if (int(s.size()) <= w)
        return std::string{s};
    return std::string{s.substr(0, w - 1)} + "…";
}

// Words wider than w (long paths) keep their tail: "…node/v24.18.0/bin". paragraph() won't
// break inside a word, so without this they run past the pane border.
std::string fit_words(std::string_view s, int w) {
    std::string out;
    for (auto word : std::views::split(s, ' ')) {
        std::string_view v{word.begin(), word.end()};
        size_t cols = 0; // UTF-8 code points, close enough for paths and glyphs
        for (char c : v)
            cols += (c & 0xc0) != 0x80;
        if (int(cols) > w)
            v = v.substr(v.size() - (w - 1));
        out += (out.empty() ? "" : " ") + std::string{cols > size_t(w) ? "…" : ""} + std::string{v};
    }
    return out;
}

std::string size_cell(const std::optional<DiskUsage>& s) {
    return s ? human_size(s->bytes) + (s->skipped ? "+" : "") : "···";
}

struct Ui {
    App& a;
    const Palette& P;
    // Screen areas from the last render, for mouse hit-testing. Empty until drawn.
    static constexpr Box kNone{1, 0, 1, 0};
    std::array<Box, 4> tab_box{kNone, kNone, kNone, kNone};
    Box rows_box = kNone, eco_box = kNone;
    bool left = false; // Ecosystems pane on screen

    Element pane(std::string_view title, Element body, bool focused = false) const {
        auto t = text(std::format(" {} ", title));
        t = focused ? t | bold | color(P.accent) : t | color(P.fg_dim);
        return window(t, body | color(P.fg)) | color(focused ? P.border : P.border_dim);
    }

    Element top_bar() {
        Elements e{P.pill(" pkgtop ", P.accent), text(" ")};
        for (int i = 0; i < int(kTabs.size()); ++i) {
            auto label = std::format(" {} {} ", i + 1, kTabs[i]);
            e.push_back((i == a.tab ? P.pill(label, P.accent)
                                    : text(label) | bgcolor(P.bg_alt) | color(P.fg_dim)) |
                        reflect(tab_box[i]));
            e.push_back(text(" "));
        }
        e.push_back(filler());
        uint64_t total = a.bytes_of("", false) + a.bytes_of("", true);
        e.push_back(text(std::format(" {} ", human_size(total))) | bgcolor(P.bg_alt));
        e.push_back(text(" "));
        e.push_back(text(std::format(" {} ecosystems ", a.providers)) | bgcolor(P.bg_alt));
        if (size_t d = a.done; d < a.total) {
            static constexpr std::string_view spin[] = {"⣾", "⣽", "⣻", "⢿", "⡿", "⣟", "⣯", "⣷"};
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
            e.push_back(text(" "));
            e.push_back(text(std::format(" {} loading {}/{} ", spin[ms / 100 % 8], d, a.total)) |
                        bgcolor(P.bg_alt) | color(P.warn));
        }
        return hbox(std::move(e));
    }

    // "all" + one line per ecosystem; shared by the pane and the e popup. Clicks map by line.
    Element eco_lines() {
        Elements list;
        for (auto [e, n] : a.eco_counts()) {
            auto l = hbox({text(a.eco == e ? " ▸ " : "   "),
                           text(e.empty() ? "all" : std::string{e}) | flex,
                           text(std::format("{:>6} ", n))});
            list.push_back(a.eco == e ? l | bold | color(P.accent) : l);
        }
        return vbox(std::move(list)) | reflect(eco_box);
    }

    Element ecosystems() {
        Elements bars;
        uint64_t max = 1;
        for (auto e : a.ecos)
            max = std::max(max, a.bytes_of(e, a.tab == 1));
        for (auto [e, n] : a.eco_counts()) {
            if (e.empty() || a.tab == 2)
                continue;
            uint64_t b = a.bytes_of(e, a.tab == 1);
            bars.push_back(hbox({text(std::format(" {:<6}", e)) | color(P.eco(e)),
                                 gauge(float(b) / float(max)) | color(P.eco(e)) | flex,
                                 text(std::format("{:>6} ", human_size(b)))}));
        }
        Elements body{eco_lines()};
        for (auto& err : a.errors)
            body.push_back(text(" ✗ " + err) | bold | color(P.danger));
        body.push_back(filler());
        if (!bars.empty())
            body.insert(body.end(), {separatorLight() | color(P.border_dim),
                                     text(" Disk") | color(P.fg_dim), vbox(std::move(bars))});
        return pane(a.tab == 2 ? "Owners" : "Ecosystems", vbox(std::move(body)), a.focus_eco) |
               size(WIDTH, EQUAL, 26);
    }

    Element table(int width, int height) {
        auto v = a.view();
        int pos = a.pos(v.size());
        int h = std::max(1, height);
        a.page = h;
        auto& top = a.top[a.tab];
        top = std::clamp(top, std::max(0, pos - h + 1), pos);

        bool caches = a.tab == 1, bins = a.tab == 2, stale = a.tab == 3;
        int second_w = caches ? 9 : bins ? std::clamp(width / 3, 16, 40) : 16;
        int name_w = width - (1 + 1 + second_w + 1 + 6 + 1 + 7 + 1);
        if (name_w < 16) // narrow: VERSION goes first
            name_w += second_w + 1, second_w = 0;

        auto cols = [&](std::string_view name, std::string_view second, std::string_view eco,
                        std::string_view size, bool right_second) {
            auto s2 = second_w ? (right_second
                                      ? std::format("{:>{}} ", second, second_w)
                                      : std::format("{:<{}} ", trunc(second, second_w), second_w))
                               : "";
            return std::tuple{std::format(" {:<{}} {}", trunc(name, name_w), name_w, s2),
                              std::format("{:<6} ", trunc(eco, 6)), std::format("{:>7} ", size)};
        };

        auto sort_mark = [&](int col) { return a.sort == col ? (a.rev ? "▲" : "▼") : ""; };
        auto [h1, h2, h3] =
            cols(std::format("{}{}", caches ? "PATH" : "NAME", sort_mark(1)),
                 caches  ? "FILES"
                 : bins  ? std::format("DIR{}", sort_mark(0))
                 : stale ? "LAST ACTIVE"
                         : "VERSION",
                 std::format("{}{}", bins ? "OWNER" : "ECO", sort_mark(2)),
                 bins ? "FLAGS" : std::format("SIZE{}", stale ? "" : sort_mark(0)), caches);
        auto header = hbox({text(h1), text(h2), text(h3)}) | bold | bgcolor(P.bg_alt);
        Elements rows;
        std::vector<size_t> unsized; // bottom row first: workers pop from the back

        for (int k = top; k < std::min<int>(top + h, v.size()); ++k) {
            auto r = a.row(v[k]);
            if (r.size && !*r.size)
                unsized.insert(unsized.begin(), a.tab == 1 ? a.pkgs.size() + v[k] : v[k]);
            auto [c1, c2, c3] =
                cols(r.name, r.second, r.eco, r.bin ? flags(*r.bin) : size_cell(*r.size), caches);
            bool selected = k == pos;
            auto eco = text(c2);
            auto sz = text(c3);
            if (!selected) {
                eco = eco | color(P.eco(r.eco));
                if (r.bin)
                    sz = sz | color(r.bin->dangling ? P.danger : r.bin->shadowed ? P.warn : P.info);
                else if (!*r.size)
                    sz = sz | color(P.fg_dim);
            }
            auto line = hbox({text(c1), eco, sz});
            if (selected)
                line = P.on ? line | bgcolor(P.sel_bg) | color(P.sel_fg) | bold : line | inverted;
            rows.push_back(line);
        }
        if (v.empty())
            rows.push_back(text(a.count() ? " no matches" : " nothing found") | color(P.fg_dim));

        if (a.done < a.total) {
            std::lock_guard l{a.urgent_mu};
            a.urgent = std::move(unsized);
        }

        auto title = std::format("{} {}/{}", kTabs[a.tab], v.empty() ? 0 : pos + 1, v.size());
        return pane(title, vbox({header, vbox(std::move(rows)) | reflect(rows_box)}),
                    !(left && a.focus_eco)) |
               flex;
    }

    // Key/value lines for the selected item; shared by the Details pane and the narrow strip.
    std::vector<std::pair<std::string, std::string>> details() const {
        auto v = a.view();
        if (v.empty())
            return {};
        size_t i = v[a.pos(v.size())];
        std::vector<std::pair<std::string, std::string>> kv;
        auto sized = [&](const std::optional<DiskUsage>& s) {
            if (!s) {
                kv.emplace_back("Size", "··· sizing");
                return;
            }
            kv.emplace_back("Size", human_size(s->bytes) + " (physical)");
            kv.emplace_back("Files", std::to_string(s->files));
            if (s->skipped)
                kv.emplace_back("Skipped", std::format("{} (permission denied)", s->skipped));
        };
        if (a.tab == 2) {
            auto& e = a.path[i];
            kv.emplace_back(e.name, std::string{e.owner});
            kv.emplace_back("Path", tilde(e.dir + "/" + e.name));
            if (e.dangling)
                kv.emplace_back("Target", "✗ broken link (reported, never deleted)");
            else if (e.target != e.dir + "/" + e.name)
                kv.emplace_back("Target", tilde(e.target));
            if (e.pkg >= 0)
                kv.emplace_back("Package", a.pkgs[e.pkg].name + " " + a.pkgs[e.pkg].version);
            if (e.shim)
                kv.emplace_back("Shim",
                                std::format("{} picks the real binary at run time", e.owner));
            // Which binary wins: every same-named entry in lookup order. ✓ runs, ⚠ loses to
            // a different file, = same file as the winner.
            kv.emplace_back("Lookup", "first match wins");
            const PathEntry* win = &*std::ranges::find(a.path, e.name, &PathEntry::name);
            for (auto& o : a.path)
                if (o.name == e.name)
                    kv.emplace_back(std::format("{}#{}", &o == &e ? "▸" : " ", o.rank),
                                    std::format("{} {}",
                                                o.dangling   ? "✗"
                                                : o.shadowed ? "⚠"
                                                : &o == win  ? "✓"
                                                             : "=",
                                                fit_words(tilde(o.dir), kDetailsW - 15)));
            return kv;
        }
        if (a.tab == 1) {
            auto& c = a.caches[i];
            kv.emplace_back(std::string{c.ecosystem}, "cache");
            kv.emplace_back("Path", tilde(c.path));
            sized(c.size);
            return kv;
        }
        auto& p = a.pkgs[i];
        kv.emplace_back(p.name, std::string{p.ecosystem});
        kv.emplace_back("Version", p.version);
        if (p.latest)
            kv.emplace_back("Latest", p.latest->empty() ? "? lookup failed"
                                      : outdated(p)     ? *p.latest + " ↑ update available"
                                                        : "✓ up to date");
        kv.emplace_back("Root", p.root.empty() ? "/ (dpkg)" : tilde(p.root));
        if (!p.arch.empty())
            kv.emplace_back("Arch", p.arch);
        if (!p.bins.empty()) {
            std::string b;
            for (auto& s : p.bins)
                b += (b.empty() ? "" : " ") + s;
            kv.emplace_back("Bins", b);
        }
        sized(p.size);
        if (p.usage && p.usage->conf != Usage::none) {
            kv.emplace_back("Last used", last_active(*p.usage, a.now));
            kv.emplace_back("Evidence", std::string{p.usage->source});
            if (bloat(p, a.now) >= 0)
                kv.emplace_back("Bloat", std::format("{} idle {}", human_size(p.size->bytes),
                                                     ago(p.usage->last, a.now)));
        }
        if (p.essential)
            kv.emplace_back("Essential", "yes, never uninstalled");
        return kv;
    }

    Element details_pane() const {
        Elements lines;
        auto kv = details();
        for (size_t k = 0; k < kv.size(); ++k) {
            auto& [key, val] = kv[k];
            if (k == 0) { // header: name + ecosystem tag
                lines.push_back(
                    hbox({text(" " + key) | bold, filler(), text(val + " ") | color(P.eco(val))}));
                lines.push_back(text(""));
                continue;
            }
            lines.push_back(hbox({text(std::format(" {:<9} ", key)) | color(P.fg_dim),
                                  paragraph(fit_words(val, kDetailsW - 13))}));
        }
        return pane("Details", vbox(std::move(lines))) | size(WIDTH, EQUAL, kDetailsW);
    }

    Element details_strip() const {
        std::string s;
        for (auto& [k, v] : details())
            s += std::format("  {} {}", k, v);
        return pane("Details", text(s));
    }

    Element keys() const {
        std::pair<std::string_view, std::string_view> hints[] = {
            {"/", "Filter"},    {"s", "Sort"},      {"S", "Reverse"},
            {"e", "Ecosystem"}, {"u", "Uninstall"}, {"p", "Purge"},
            {"Tab", "Focus"},   {"?", "Hide keys"}, {"q", "Quit"}};
        Elements e;
        for (auto [k, what] : hints) {
            e.push_back(text(std::format("  {} ", k)) | bold | color(P.accent));
            e.push_back(text(std::format("→ {}  ", what)));
        }
        return pane("Keys", hbox(std::move(e)));
    }

    Element status_line() const {
        auto mode = a.confirm     ? P.pill(" CONFIRM ", P.danger)
                    : a.filtering ? P.pill(" FILTER ", P.info)
                                  : P.pill(" NORMAL ", P.accent);
        std::string crumb;
        if (a.count()) {
            auto v = a.view();
            if (!v.empty()) {
                auto r = a.row(v[a.pos(v.size())]);
                crumb = std::format("{} ▸ {}", r.eco, r.name);
            }
        }
        auto filter = a.filtering ? a.filter + "▏" : a.filter.empty() ? "—" : a.filter;
        auto note = text("");
        if (!a.status.empty() && std::chrono::steady_clock::now() < a.status_until)
            note = text("  " + a.status) | bold | color(a.status_ok ? P.ok : P.danger);
        return hbox({mode, text(" " + crumb) | color(P.fg), note, filler(),
                     text(std::format("sort: {} {}   filter: {}   ",
                                      a.sort == 0 && a.tab == 2   ? "lookup"
                                      : a.sort == 0 && a.tab == 3 ? "bloat"
                                                                  : kSorts[a.sort],
                                      a.rev ? "▲" : "▼", filter)) |
                         color(P.fg_dim),
                     P.pill(std::format(" {} in caches ", human_size(a.bytes_of("", true))),
                            P.bg_sel, P.fg)});
    }

    Element render() {
        auto [w, h] = Terminal::Size();
        tab_box.fill(kNone), rows_box = eco_box = kNone;
        if (w < 80 || h < 24)
            return vbox({filler(),
                         text(std::format("terminal too small: {}×{} (need 80×24)", w, h)) | center,
                         filler()}) |
                   bgcolor(P.bg) | color(P.fg);

        left = w >= 100;
        bool right = w >= 120;
        Element body;
        {
            int table_w = w - (left ? 26 : 0) - (right ? 38 : 0) - 2;
            // top bar, header row, borders, status, + keys/strip panes
            int table_h = h - 5 - (a.keys ? 3 : 0) - (right ? 0 : 3);
            Elements cols;
            if (left)
                cols.push_back(ecosystems());
            cols.push_back(table(table_w, table_h));
            if (right)
                cols.push_back(details_pane());
            body = hbox(std::move(cols)) | flex;
        }

        Elements page{top_bar(), body};
        if (!right)
            page.push_back(details_strip());
        if (a.keys)
            page.push_back(keys());
        page.push_back(status_line());
        auto screen = vbox(std::move(page)) | bgcolor(P.bg) | color(P.fg);
        if (a.picking)
            return dbox({screen, window(text(" Ecosystem ") | bold | color(P.accent),
                                        eco_lines() | color(P.fg) | size(WIDTH, EQUAL, 24)) |
                                     color(P.accent) | bgcolor(P.bg) | clear_under | center});
        if (!a.confirm)
            return screen;
        return dbox({screen, modal() | clear_under | center});
    }

    // Default is Cancel: only y runs.
    Element modal() const {
        auto& c = *a.confirm;
        auto kv = [&](std::string_view k, std::string v) {
            return hbox(
                {text(std::format("  {:<11}", k)) | color(P.fg_dim), text(v) | bold, text("  ")});
        };
        auto body = vbox(
            {text(""), kv("Will run:", join(c.argv)), kv("Reclaims:", c.reclaim), text(""),
             text("  pkgtop will leave the screen while this runs.  ") | color(P.fg_dim), text(""),
             hbox({filler(), P.pill(" y  Run ", P.danger), text("      "),
                   P.pill(" n  Cancel ", P.accent), filler()}),
             text("")});
        return window(text(" " + c.title + " ") | bold | color(P.danger), body | color(P.fg)) |
               color(P.danger) | bgcolor(P.bg);
    }

    bool on_event(Event e, ScreenInteractive& screen) {
        if (a.confirm) { // modal: y runs, anything else that's a key cancels
            if (e == Event::Custom || e.is_mouse())
                return false;
            auto c = std::exchange(a.confirm, std::nullopt);
            if (e != Event::Character('y'))
                return true;
            int rc = 0;
            screen.WithRestoredIO([&] {
                std::println("$ {}", join(c->argv));
                std::fflush(stdout);
                rc = run_foreground(c->argv);
                std::print("\n{} exit {}. Press Enter to return to pkgtop.", rc ? "✗" : "✓", rc);
                std::fflush(stdout);
                for (int ch; (ch = std::getchar()) != '\n' && ch != EOF;) {
                }
            })();
            // Rescan even on failure: a partial uninstall is still a state change.
            a.refresh(*c);
            a.say(std::format("{} exit {}: {}", rc ? "✗" : "✓", rc, join(c->argv)), rc == 0);
            return true;
        }
        if (a.filtering) {
            if (e == Event::Escape)
                return a.filter.clear(), a.filtering = false, true;
            if (e == Event::Return)
                return a.filtering = false, true;
            if (e == Event::Backspace)
                return a.filter.empty() || (a.filter.pop_back(), true), a.cur = {}, true;
            if (e.is_character())
                return a.filter += e.character(), a.cur = {}, true;
        }
        auto key = [&](char c) { return e == Event::Character(c); };
        bool down = key('j') || e == Event::ArrowDown, up = key('k') || e == Event::ArrowUp;
        if (e.is_mouse())
            return click(e.mouse());
        if (a.picking) { // popup: j/k pick live, Enter keeps, Esc restores
            if (down || up)
                a.step_eco(down ? 1 : -1);
            else if (e == Event::Escape)
                a.eco = a.picked_from, a.picking = false;
            else if (e == Event::Return || key('e') || key('q'))
                a.picking = false;
            return true;
        }
        if (left && a.focus_eco && (down || up))
            return a.step_eco(down ? 1 : -1), true;
        if (left && a.focus_eco && e == Event::Return)
            return a.focus_eco = false, true;
        if (key('q'))
            return screen.Exit(), true;
        if (down || up)
            return a.move(down ? 1 : -1), true;
        if (e == Event::PageDown)
            return a.move(a.page), true;
        if (e == Event::PageUp)
            return a.move(-a.page), true;
        if (key('g') || e == Event::Home)
            return a.move(-1'000'000), true;
        if (key('G') || e == Event::End)
            return a.move(1'000'000), true;
        for (char c = '1'; c <= '4'; ++c)
            if (key(c))
                return a.tab = c - '1', a.eco.clear(), true;
        if (e == Event::Tab || e == Event::TabReverse)
            return a.focus_eco = left && !a.focus_eco, true;
        if (key('/'))
            return a.filtering = true, true;
        if (e == Event::Escape)
            return a.filter.clear(), a.eco.clear(), true;
        if (key('s'))
            return a.sort = (a.sort + 1) % int(kSorts.size()), true;
        if (key('S'))
            return a.rev = !a.rev, true;
        if (key('?'))
            return a.keys = !a.keys, true;
        if (key('u') || key('p'))
            return a.ask(key('p')), true;
        if (key('e'))
            return a.picked_from = a.eco, a.picking = true, true;
        return false;
    }

    // Wheel scrolls the table; left click picks a tab, row or ecosystem.
    bool click(const Mouse& m) {
        if (m.button == Mouse::WheelDown || m.button == Mouse::WheelUp)
            return a.move(m.button == Mouse::WheelDown ? 3 : -3), true;
        if (m.button != Mouse::Left || m.motion != Mouse::Pressed)
            return false;
        for (int i = 0; i < int(tab_box.size()); ++i)
            if (tab_box[i].Contain(m.x, m.y))
                return a.tab = i, a.eco.clear(), a.picking = false, true;
        if (eco_box.Contain(m.x, m.y))
            return a.pick_eco(m.y - eco_box.y_min), a.picking = false, true;
        if (!a.picking && rows_box.Contain(m.x, m.y)) {
            a.cur[a.tab] = a.top[a.tab] + (m.y - rows_box.y_min);
            a.move(0), a.focus_eco = false;
            return true;
        }
        return false;
    }
};

} // namespace

int run_tui(std::vector<Package> pkgs, std::vector<Cache> caches, std::vector<PathEntry> path,
            std::vector<std::string> errors, std::function<std::vector<Package>()> rescan,
            bool updates, bool ascii) {
    App a;
    a.rescan = std::move(rescan);
    a.pkgs = std::move(pkgs);
    a.caches = std::move(caches);
    a.path = std::move(path);
    a.errors = std::move(errors);
    auto seen = [&](std::string_view e) {
        if (std::ranges::find(a.ecos, e) == a.ecos.end())
            a.ecos.push_back(e);
    };
    for (auto& p : a.pkgs)
        seen(p.ecosystem);
    for (auto& c : a.caches)
        seen(c.ecosystem);
    a.providers = a.ecos.size();
    for (auto& e : a.path)
        seen(e.owner);
    size_t sizes = a.pkgs.size() + a.caches.size();
    a.total = sizes + a.pkgs.size(); // + one usage result per package
    if (updates)                     // + one per package with a local index or registry
        for (auto& p : a.pkgs)
            a.total += checkable(p);

    const char* nc = std::getenv("NO_COLOR");
    Palette pal(!(nc && *nc));
    Ui ui{a, pal};

    auto screen = ScreenInteractive::Fullscreen();
    auto root = CatchEvent(Renderer([&] {
                               auto e = ui.render();
                               return ascii ? std::make_shared<Ascii>(std::move(e)) : e;
                           }),
                           [&](Event e) { return ui.on_event(e, screen); });

    // Size workers. Results go back through screen.Post so the UI thread stays the only
    // writer of App. Workers only read name/arch/root/bins/path, which never change.
    // Rows on screen first (a.urgent), then packages and caches in order. claimed keeps
    // each item sized once whichever way a worker reached it.
    std::atomic<size_t> next{0};
    std::vector<std::atomic<bool>> claimed(sizes);
    // Last run's sizes paint at once. A package hit skips its walk; a cache shows the old
    // size until its rescan lands (files change deep inside without touching the top dir).
    const char *xdg = std::getenv("XDG_CACHE_HOME"), *home = std::getenv("HOME");
    std::string cache_dir = xdg && *xdg ? xdg : std::string(home ? home : "") + "/.cache";
    std::string sizes_file = cache_dir + "/pkgtop/sizes";
    auto cache_key = [](const Cache& c) { return "cache\t" + c.path; };
    {
        auto old = load_sizes(sizes_file);
        for (size_t i = 0; i < a.pkgs.size(); ++i)
            if (auto it = old.find(size_key(a.pkgs[i])); it != old.end())
                a.pkgs[i].size = it->second, claimed[i] = true, ++a.done;
        for (auto& c : a.caches)
            if (auto it = old.find(cache_key(c)); it != old.end())
                c.size = it->second;
    }
    auto take = [&]() -> std::optional<size_t> {
        {
            std::lock_guard l{a.urgent_mu};
            while (!a.urgent.empty()) {
                size_t i = a.urgent.back();
                a.urgent.pop_back();
                if (i < sizes && !claimed[i].exchange(true))
                    return i;
            }
        }
        for (size_t i; (i = next++) < sizes;)
            if (!claimed[i].exchange(true))
                return i;
        return std::nullopt;
    };
    auto worker = [&](std::stop_token st) {
        for (std::optional<size_t> j; !st.stop_requested() && (j = take()); ++a.done) {
            size_t i = *j;
            if (i < a.pkgs.size()) {
                auto& p = a.pkgs[i];
                auto u = package_size(p);
                screen.Post([&a, i, u] { a.pkgs[i].size = u; });
            } else {
                size_t c = i - a.pkgs.size();
                auto u = disk_usage(a.caches[c].path.c_str());
                screen.Post([&a, c, u] { a.caches[c].size = u; });
            }
        }
    };
    // Closures don't trigger a redraw, so a ticker batches them into ~10 frames/s and drives
    // the spinner. It stops once sizing is done.
    auto ticker = [&](std::stop_token st) {
        while (!st.stop_requested() && a.done < a.total) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            screen.PostEvent(Event::Custom);
        }
        screen.PostEvent(Event::Custom);
    };

    // Declared after screen/App so they are joined (on scope exit) before those die.
    // A walk already in progress finishes before join returns.
    std::vector<std::jthread> threads;
    for (unsigned n = std::clamp(std::thread::hardware_concurrency(), 1u, 8u); n--;)
        threads.emplace_back(worker);
    // Usage: one thread, history first. ponytail: serial; stat-only, ~100 ms for 2k packages.
    threads.emplace_back([&](std::stop_token st) {
        const char* h = std::getenv("HOME");
        auto hist = load_history(h ? h : "");
        for (size_t i = 0; i < a.pkgs.size() && !st.stop_requested(); ++i, ++a.done) {
            auto u = bin_usage(package_bins(a.pkgs[i], int(i), a.path), hist);
            screen.Post([&a, i, u] { a.pkgs[i].usage = u; });
        }
    });
    if (updates)
        threads.emplace_back([&](std::stop_token st) {
            check_updates(a.pkgs, st, [&](size_t i, std::string v) {
                screen.Post([&a, i, v] { a.pkgs[i].latest = v; });
                ++a.done;
            });
        });
    threads.emplace_back(ticker);

    screen.Loop(root);
    SizeCache now;
    for (auto& p : a.pkgs)
        if (p.size)
            now.emplace(size_key(p), *p.size);
    for (auto& c : a.caches)
        if (c.size)
            now.emplace(cache_key(c), *c.size);
    ::mkdir(cache_dir.c_str(), 0755), ::mkdir((cache_dir + "/pkgtop").c_str(), 0755);
    save_sizes(sizes_file, now);
    return 0;
}
