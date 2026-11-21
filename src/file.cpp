#include "pkgtop.hpp"

#include <cerrno>
#include <cstring>
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
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT ? std::expected<MappedFile, std::string>{} : fail("", errno);

    struct stat st {};
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
