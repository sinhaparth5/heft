#include "pkgtop.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

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

// Palette tokens from design/tui.md. This is the only place raw hex appears.
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
constexpr std::array<std::string_view, 3> kSorts = {"size", "name", "eco"};

struct Row {
    std::string_view eco;
    std::string name;
    std::string second; // version (packages) or file count (caches)
    const std::optional<DiskUsage>* size;
};

struct App {
    std::vector<Package> pkgs;
    std::vector<Cache> caches;
    std::vector<std::string> errors;
    std::vector<std::string_view> ecos; // every ecosystem seen, in scan order

    int tab = 0;
    std::array<int, 2> cur{}; // cursor row in the current view, per table tab
    std::array<int, 2> top{}; // first visible row, per table tab
    int page = 10;            // visible table rows, from the last render
    int sort = 0;
    bool rev = false;
    std::string filter;
    bool filtering = false;
    std::string eco; // "" = all
    bool keys = true;

    std::atomic<size_t> done{0};
    size_t total = 0;

    bool table_tab() const { return tab < 2; }
    size_t count() const { return tab == 1 ? caches.size() : pkgs.size(); }

    Row row(size_t i) const {
        if (tab == 1) {
            auto& c = caches[i];
            return {c.ecosystem, tilde(c.path), c.size ? std::to_string(c.size->files) : "",
                    &c.size};
        }
        auto& p = pkgs[i];
        return {p.ecosystem, p.name, p.version, &p.size};
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
            if (auto r = row(i); (eco.empty() || r.eco == eco) && fuzzy(r.name, filter))
                v.push_back(i);
        auto bytes = [&](size_t i) {
            auto& s = *row(i).size;
            return s ? int64_t(s->bytes) : -1; // unsized rows sink to the bottom
        };
        auto less = [&](size_t x, size_t y) {
            auto a = row(x), b = row(y);
            if (sort == 0)
                return bytes(x) > bytes(y);
            if (sort == 1)
                return a.name < b.name;
            return std::tie(a.eco, a.name) < std::tie(b.eco, b.name);
        };
        // ponytail: re-sorts every frame, so rows reorder while sizes stream in; throttle
        // to 250 ms (design/tui.md) if that turns out to be distracting.
        std::ranges::stable_sort(v,
                                 [&](size_t x, size_t y) { return rev ? less(y, x) : less(x, y); });
        return v;
    }

    // The cursor is a row position, not an item: rows may reorder under it while sizes
    // stream in (like htop), but the cursor itself never jumps.
    int pos(size_t n) { return cur[tab] = std::clamp(cur[tab], 0, std::max(0, int(n) - 1)); }

    void move(int delta) {
        if (table_tab())
            cur[tab] = std::clamp(cur[tab] + delta, 0, std::max(0, int(view().size()) - 1));
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

std::string size_cell(const std::optional<DiskUsage>& s) {
    return s ? human_size(s->bytes) + (s->skipped ? "+" : "") : "···";
}

struct Ui {
    App& a;
    const Palette& P;

    Element pane(std::string_view title, Element body, bool focused = false) const {
        auto t = text(std::format(" {} ", title));
        t = focused ? t | bold | color(P.accent) : t | color(P.fg_dim);
        return window(t, body | color(P.fg)) | color(focused ? P.border : P.border_dim);
    }

    Element top_bar() const {
        Elements e{P.pill(" pkgtop ", P.accent), text(" ")};
        for (int i = 0; i < int(kTabs.size()); ++i) {
            auto label = std::format(" {} {} ", i + 1, kTabs[i]);
            e.push_back(i == a.tab ? P.pill(label, P.accent)
                                   : text(label) | bgcolor(P.bg_alt) | color(P.fg_dim));
            e.push_back(text(" "));
        }
        e.push_back(filler());
        uint64_t total = a.bytes_of("", false) + a.bytes_of("", true);
        e.push_back(text(std::format(" {} ", human_size(total))) | bgcolor(P.bg_alt));
        e.push_back(text(" "));
        e.push_back(text(std::format(" {} ecosystems ", a.ecos.size())) | bgcolor(P.bg_alt));
        if (size_t d = a.done; d < a.total) {
            static constexpr std::string_view spin[] = {"⣾", "⣽", "⣻", "⢿", "⡿", "⣟", "⣯", "⣷"};
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
            e.push_back(text(" "));
            e.push_back(text(std::format(" {} sizing {}/{} ", spin[ms / 100 % 8], d, a.total)) |
                        bgcolor(P.bg_alt) | color(P.warn));
        }
        return hbox(std::move(e));
    }

    Element ecosystems() const {
        Elements list, bars;
        auto line = [&](std::string_view name, size_t n, bool cur) {
            auto e = hbox({text(cur ? " ▸ " : "   "), text(std::string{name}) | flex,
                           text(std::format("{:>6} ", n))});
            return cur ? e | bold | color(P.accent) : e;
        };
        list.push_back(line("all", a.count(), a.eco.empty()));

        uint64_t max = 1;
        for (auto e : a.ecos)
            max = std::max(max, a.bytes_of(e, a.tab == 1));
        for (auto e : a.ecos) {
            size_t n = 0;
            for (size_t i = 0; i < a.count(); ++i)
                n += a.row(i).eco == e;
            if (n == 0)
                continue;
            list.push_back(line(e, n, a.eco == e));
            uint64_t b = a.bytes_of(e, a.tab == 1);
            bars.push_back(hbox({text(std::format(" {:<6}", e)) | color(P.eco(e)),
                                 gauge(float(b) / float(max)) | color(P.eco(e)) | flex,
                                 text(std::format("{:>6} ", human_size(b)))}));
        }
        for (auto& err : a.errors)
            list.push_back(text(" ✗ " + err) | bold | color(P.danger));
        return window(text(" Ecosystems ") | color(P.fg_dim),
                      vbox({vbox(std::move(list)) | color(P.fg), filler(),
                            separatorLight() | color(P.border_dim), text(" Disk") | color(P.fg_dim),
                            vbox(std::move(bars))})) |
               color(P.border_dim) | size(WIDTH, EQUAL, 26);
    }

    Element table(int width, int height) {
        auto v = a.view();
        int pos = a.pos(v.size());
        int h = std::max(1, height);
        a.page = h;
        auto& top = a.top[a.tab];
        top = std::clamp(top, std::max(0, pos - h + 1), pos);

        bool caches = a.tab == 1;
        int second_w = caches ? 9 : 16;
        int name_w = width - (1 + 1 + second_w + 1 + 6 + 1 + 7 + 1);
        if (name_w < 16) // narrow: VERSION goes first (design/tui.md column order)
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
        auto [h1, h2, h3] = cols(std::format("{}{}", caches ? "PATH" : "NAME", sort_mark(1)),
                                 caches ? "FILES" : "VERSION", std::format("ECO{}", sort_mark(2)),
                                 std::format("SIZE{}", sort_mark(0)), caches);
        Elements rows{hbox({text(h1), text(h2), text(h3)}) | bold | bgcolor(P.bg_alt)};

        for (int k = top; k < std::min<int>(top + h, v.size()); ++k) {
            auto r = a.row(v[k]);
            auto [c1, c2, c3] = cols(r.name, r.second, r.eco, size_cell(*r.size), caches);
            bool selected = k == pos;
            auto eco = text(c2);
            auto sz = text(c3);
            if (!selected) {
                eco = eco | color(P.eco(r.eco));
                if (!*r.size)
                    sz = sz | color(P.fg_dim);
            }
            auto line = hbox({text(c1), eco, sz});
            if (selected)
                line = P.on ? line | bgcolor(P.sel_bg) | color(P.sel_fg) | bold : line | inverted;
            rows.push_back(line);
        }
        if (v.empty())
            rows.push_back(text(a.count() ? " no matches" : " nothing found") | color(P.fg_dim));

        auto title = std::format("{} {}/{}", kTabs[a.tab], v.empty() ? 0 : pos + 1, v.size());
        return pane(title, vbox(std::move(rows)), true) | flex;
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
            lines.push_back(
                hbox({text(std::format(" {:<9} ", key)) | color(P.fg_dim), paragraph(val)}));
        }
        return pane("Details", vbox(std::move(lines))) | size(WIDTH, EQUAL, 38);
    }

    Element details_strip() const {
        std::string s;
        for (auto& [k, v] : details())
            s += std::format("  {} {}", k, v);
        return pane("Details", text(s));
    }

    Element keys() const {
        std::pair<std::string_view, std::string_view> hints[] = {
            {"/", "Filter"}, {"s", "Sort"},      {"S", "Reverse"}, {"e", "Ecosystem"},
            {"1-4", "Tab"},  {"?", "Hide keys"}, {"q", "Quit"}};
        Elements e;
        for (auto [k, what] : hints) {
            e.push_back(text(std::format("  {} ", k)) | bold | color(P.accent));
            e.push_back(text(std::format("→ {}  ", what)));
        }
        return pane("Keys", hbox(std::move(e)));
    }

    Element status_line() const {
        auto mode = a.filtering ? P.pill(" FILTER ", P.info) : P.pill(" NORMAL ", P.accent);
        std::string crumb;
        if (a.table_tab() && a.count()) {
            auto v = a.view();
            if (!v.empty()) {
                auto r = a.row(v[a.pos(v.size())]);
                crumb = std::format("{} ▸ {}", r.eco, r.name);
            }
        }
        auto filter = a.filtering ? a.filter + "▏" : a.filter.empty() ? "—" : a.filter;
        return hbox({mode, text(" " + crumb) | color(P.fg), filler(),
                     text(std::format("sort: {} {}   filter: {}   ", kSorts[a.sort],
                                      a.rev ? "▲" : "▼", filter)) |
                         color(P.fg_dim),
                     P.pill(std::format(" {} in caches ", human_size(a.bytes_of("", true))),
                            P.bg_sel, P.fg)});
    }

    Element render() {
        auto [w, h] = Terminal::Size();
        if (w < 80 || h < 24)
            return vbox({filler(),
                         text(std::format("terminal too small: {}×{} (need 80×24)", w, h)) | center,
                         filler()}) |
                   bgcolor(P.bg) | color(P.fg);

        bool left = w >= 100, right = w >= 120;
        Element body;
        if (a.table_tab()) {
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
        } else {
            auto msg = a.tab == 2
                           ? "The $PATH graph (shadowing, shims, dangling links) lands in M5."
                           : "Staleness from shell history and atime lands in M6.";
            body = pane(kTabs[a.tab], vbox({filler(), text(msg) | center, filler()}), true) | flex;
        }

        Elements page{top_bar(), body};
        if (a.table_tab() && !right)
            page.push_back(details_strip());
        if (a.keys)
            page.push_back(keys());
        page.push_back(status_line());
        return vbox(std::move(page)) | bgcolor(P.bg) | color(P.fg);
    }

    bool on_event(Event e, ScreenInteractive& screen) {
        if (a.filtering) {
            if (e == Event::Escape)
                return a.filter.clear(), a.filtering = false, true;
            if (e == Event::Return)
                return a.filtering = false, true;
            if (e == Event::Backspace)
                return a.filter.empty() || (a.filter.pop_back(), true), a.cur[0] = a.cur[1] = 0,
                                                                        true;
            if (e.is_character())
                return a.filter += e.character(), a.cur[0] = a.cur[1] = 0, true;
        }
        auto key = [&](char c) { return e == Event::Character(c); };
        if (key('q'))
            return screen.Exit(), true;
        if (key('j') || e == Event::ArrowDown)
            return a.move(1), true;
        if (key('k') || e == Event::ArrowUp)
            return a.move(-1), true;
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
        // ponytail: Tab cycles tabs; pane focus cycling comes with a second focusable pane.
        if (e == Event::Tab)
            return a.tab = (a.tab + 1) % int(kTabs.size()), a.eco.clear(), true;
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
        // ponytail: `e` cycles ecosystems instead of opening the popup from design/tui.md.
        if (key('e')) {
            auto it = std::ranges::find(a.ecos, a.eco);
            a.eco = a.eco.empty() ? std::string{a.ecos.empty() ? "" : a.ecos[0]}
                    : it == a.ecos.end() || it + 1 == a.ecos.end() ? ""
                                                                   : std::string{*(it + 1)};
            a.cur[0] = a.cur[1] = 0;
            return true;
        }
        return false;
    }
};

} // namespace

int run_tui(std::vector<Package> pkgs, std::vector<Cache> caches, std::vector<std::string> errors) {
    App a;
    a.pkgs = std::move(pkgs);
    a.caches = std::move(caches);
    a.errors = std::move(errors);
    auto seen = [&](std::string_view e) {
        if (std::ranges::find(a.ecos, e) == a.ecos.end())
            a.ecos.push_back(e);
    };
    for (auto& p : a.pkgs)
        seen(p.ecosystem);
    for (auto& c : a.caches)
        seen(c.ecosystem);
    a.total = a.pkgs.size() + a.caches.size();

    const char* nc = std::getenv("NO_COLOR");
    Palette pal(!(nc && *nc));
    Ui ui{a, pal};

    auto screen = ScreenInteractive::Fullscreen();
    auto root = CatchEvent(Renderer([&] { return ui.render(); }),
                           [&](Event e) { return ui.on_event(e, screen); });

    // Size workers. Results go back through screen.Post so the UI thread stays the only
    // writer of App. Workers only read name/arch/root/bins/path, which never change.
    // ponytail: plain atomic cursor (packages first, then caches) instead of the priority
    // queue in design/architecture.md; add it when visible rows visibly wait on others.
    std::atomic<size_t> next{0};
    auto worker = [&](std::stop_token st) {
        for (size_t i; !st.stop_requested() && (i = next++) < a.total; ++a.done) {
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
    threads.emplace_back(ticker);

    screen.Loop(root);
    return 0;
}
