#include "pkgtop.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <ranges>
#include <sys/stat.h>
#include <unistd.h>

namespace {

struct Walk {
    dev_t dev;
    std::unordered_set<ino_t>& seen;
    DiskUsage& out;

    // Counts a stat'd entry once. Hardlinks (nlink > 1) are deduped by inode;
    // the walk never leaves `dev`, so the inode alone is unique.
    bool count(const struct stat& st) {
        if (st.st_nlink > 1 && !S_ISDIR(st.st_mode) && !seen.insert(st.st_ino).second)
            return false;
        out.bytes += static_cast<uint64_t>(st.st_blocks) * 512;
        if (!S_ISDIR(st.st_mode))
            ++out.files;
        return true;
    }

    // ponytail: recursion holds one fd per depth level; fine below ~1000 levels (ulimit -n)
    void dir(int fd) {
        DIR* d = ::fdopendir(fd);
        if (!d) {
            ::close(fd);
            ++out.skipped;
            return;
        }
        while (dirent* e = ::readdir(d)) {
            const char* n = e->d_name;
            if (n[0] == '.' && (n[1] == 0 || (n[1] == '.' && n[2] == 0)))
                continue;
            // ponytail: symlinks are counted as 0 bytes without a stat (fast symlinks have no
            // blocks; long targets on ext4 use one block, so we undercount du by 4 KiB each).
            // pnpm/npm trees are mostly symlinks, so this skips a lot of syscalls.
            if (e->d_type == DT_LNK) {
                ++out.files;
                continue;
            }
            struct stat st;
            if (::fstatat(::dirfd(d), n, &st, AT_SYMLINK_NOFOLLOW) < 0) {
                ++out.skipped;
                continue;
            }
            if (st.st_dev != dev || !count(st))
                continue; // mount point (not ours) or a hardlink already counted
            if (S_ISDIR(st.st_mode)) {
                int sub = ::openat(::dirfd(d), n, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (sub < 0)
                    ++out.skipped; // EACCES etc: dir itself counted, contents unknown
                else
                    dir(sub);
            }
        }
        ::closedir(d);
    }
};

} // namespace

DiskUsage disk_usage(const char* path) {
    DiskUsage out;
    std::unordered_set<ino_t> seen;
    struct stat st;
    if (::lstat(path, &st) < 0) {
        if (errno != ENOENT)
            ++out.skipped;
        return out;
    }
    Walk w{st.st_dev, seen, out};
    w.count(st);
    if (S_ISDIR(st.st_mode)) {
        int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0)
            ++out.skipped;
        else
            w.dir(fd);
    }
    return out;
}

DiskUsage files_usage(std::span<const std::string> paths) {
    DiskUsage out;
    std::unordered_set<ino_t> seen; // ponytail: inode-only key; a package spanning two filesystems
                                    // could collide, use (dev, ino) if that shows up
    for (auto& p : paths) {
        struct stat st;
        if (::lstat(p.c_str(), &st) < 0) {
            if (errno != ENOENT)
                ++out.skipped; // ENOENT = file deleted by hand; just not there anymore
            continue;
        }
        if (S_ISDIR(st.st_mode))
            continue; // directories in a manifest are shared (/usr/bin), not owned
        Walk{st.st_dev, seen, out}.count(st);
    }
    return out;
}

std::string human_size(uint64_t bytes) {
    const char* units[] = {"B", "K", "M", "G", "T"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024 && u < 4)
        v /= 1024, ++u;
    return u == 0 ? std::format("{}B", bytes) : std::format("{:.1f}{}", v, units[u]);
}

DiskUsage bins_usage(const Package& p) {
    std::vector<std::string> paths;
    for (auto& b : p.bins)
        paths.push_back(p.root + "/" + b);
    return files_usage(paths);
}

DiskUsage package_size(const Package& p) {
    if (p.ecosystem == "apt")
        return dpkg_package_size(p);
    if (p.ecosystem == "pacman")
        return pacman_package_size(p);
    if (p.ecosystem == "cargo" || p.ecosystem == "go")
        return bins_usage(p);
    return disk_usage(p.root.c_str());
}

std::string_view ascii_glyph(std::string_view g) {
    auto b = [&](size_t i) { return char32_t(static_cast<unsigned char>(g[i])); };
    char32_t u = g.size() == 2   ? (b(0) & 0x1f) << 6 | (b(1) & 0x3f)
                 : g.size() == 3 ? (b(0) & 0x0f) << 12 | (b(1) & 0x3f) << 6 | (b(2) & 0x3f)
                                 : 0;
    static constexpr std::pair<char32_t, std::string_view> map[] = {
        {0x00b7, "."}, {0x00d7, "x"}, {0x2014, "-"}, {0x2026, "."}, {0x2191, "^"}, {0x2192, ">"},
        {0x21af, "~"}, {0x25b2, "^"}, {0x25bc, "v"}, {0x25b8, ">"}, {0x25cf, "*"}, {0x25d0, "+"},
        {0x25cc, "."}, {0x26a0, "!"}, {0x2713, "+"}, {0x2717, "x"}, {0x258f, "|"}, {0x2500, "-"},
        {0x2501, "-"}, {0x2550, "-"}, {0x2502, "|"}, {0x2503, "|"}, {0x2551, "|"}};
    for (auto [c, a] : map)
        if (u == c)
            return a;
    static constexpr char32_t spin[] = {0x28fe, 0x28fd, 0x28fb, 0x28bf,
                                        0x287f, 0x28df, 0x28ef, 0x28f7};
    if (auto i = std::ranges::find(spin, u) - spin; i < 8)
        return std::string_view{"|/-\\"}.substr(i % 4, 1);
    if (u >= 0x2500 && u < 0x2580) // other box drawing: corners, tees
        return "+";
    if (u >= 0x2580 && u < 0x25a0) // block elements: gauges
        return "#";
    return g;
}

std::string size_key(const Package& p) {
    struct stat st{};
    if (!p.root.empty())
        ::lstat(p.root.c_str(), &st);
    // ponytail: root's mtime only sees top-level changes; a version bump covers the rest.
    return std::format("{}\t{}\t{}\t{}\t{}\t{}.{}", p.ecosystem, p.name, p.version, p.arch, p.root,
                       st.st_mtim.tv_sec, st.st_mtim.tv_nsec);
}

SizeCache load_sizes(const std::string& file) {
    SizeCache out;
    auto m = map_file(file.c_str());
    if (!m)
        return out;
    for (auto line : std::views::split(m->text, '\n')) {
        std::string_view l(line.begin(), line.end());
        DiskUsage u;
        auto num = [&](uint64_t& v) {
            auto [p, ec] = std::from_chars(l.data(), l.data() + l.size(), v);
            if (ec != std::errc{} || p == l.data() + l.size() || *p != ' ')
                return false;
            l.remove_prefix(p - l.data() + 1);
            return true;
        };
        if (num(u.bytes) && num(u.files) && num(u.skipped) && !l.empty())
            out.emplace(l, u);
    }
    return out;
}

void save_sizes(const std::string& file, const SizeCache& sizes) {
    std::string text;
    for (auto& [k, u] : sizes)
        if (k.find('\n') == std::string::npos)
            text += std::format("{} {} {} {}\n", u.bytes, u.files, u.skipped, k);
    auto tmp = file + ".tmp";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return;
    bool ok = ::write(fd, text.data(), text.size()) == ssize_t(text.size());
    ::close(fd);
    if (!ok || ::rename(tmp.c_str(), file.c_str()) < 0)
        ::unlink(tmp.c_str());
}
