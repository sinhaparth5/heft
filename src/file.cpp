#include "heft.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

MappedFile::~MappedFile() {
    if (!text.empty())
        ::munmap(const_cast<char*>(text.data()), text.size());
}

std::expected<MappedFile, std::string> map_file(const char* path) {
    auto fail = [&](const char* what, int e) {
        return std::unexpected(std::string{path} + ": " + what + std::strerror(e));
    };
    // O_NOATIME so reading a binary (go buildinfo) doesn't look like a use of it. The kernel
    // only allows it on files we own, so retry without.
    int fd = ::open(path, O_RDONLY | O_CLOEXEC | O_NOATIME);
    if (fd < 0 && errno == EPERM)
        fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? std::expected<MappedFile, std::string>{} : fail("", errno);

    struct stat st{};
    if (::fstat(fd, &st) < 0) {
        int e = errno;
        ::close(fd);
        return fail("", e);
    }
    if (st.st_size == 0) {
        ::close(fd);
        return {};
    }
    void* p = ::mmap(nullptr, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    int e = errno;
    ::close(fd);
    if (p == MAP_FAILED)
        return fail("mmap: ", e);
    ::madvise(p, st.st_size, MADV_SEQUENTIAL);

    std::expected<MappedFile, std::string> out{std::in_place};
    out->text = {static_cast<const char*>(p), static_cast<size_t>(st.st_size)};
    return out;
}

std::vector<std::string> list_dir(const std::string& dir, bool symlinks) {
    std::vector<std::string> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d)
        return out;
    while (dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.')
            continue;
        bool link = e->d_type == DT_LNK;
        if (e->d_type == DT_UNKNOWN) { // some filesystems don't fill d_type
            struct stat st;
            link = ::fstatat(::dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
                   S_ISLNK(st.st_mode);
        }
        if (symlinks || !link)
            out.emplace_back(e->d_name);
    }
    ::closedir(d);
    std::ranges::sort(out);
    return out;
}
