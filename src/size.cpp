#include "pkgtop.hpp"

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
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
