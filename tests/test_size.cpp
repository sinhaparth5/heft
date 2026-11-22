#include "pkgtop.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <print>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& p, size_t bytes) {
    std::string data(bytes, 'x');
    int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd >= 0);
    assert(::write(fd, data.data(), data.size()) == static_cast<ssize_t>(data.size()));
    ::fsync(fd);
    ::close(fd);
}

uint64_t du(const fs::path& p) {
    auto cmd = "du -sx -B1 '" + p.string() + "' 2>/dev/null";
    FILE* f = ::popen(cmd.c_str(), "r");
    assert(f);
    unsigned long long n = 0;
    assert(std::fscanf(f, "%llu", &n) == 1);
    ::pclose(f);
    return n;
}

} // namespace

int main() {
    // Hardlinks can't live in git, so build the tree at runtime.
    std::string tmpl = (fs::temp_directory_path() / "pkgtop-size-XXXXXX").string();
    fs::path root = ::mkdtemp(tmpl.data());

    write_file(root / "big", 1 << 20);
    fs::create_hard_link(root / "big", root / "big.hardlink"); // must count once
    fs::create_directories(root / "sub/deeper");
    write_file(root / "sub/deeper/small", 100);
    fs::create_hard_link(root / "big", root / "sub/big.again"); // hardlink in another dir
    fs::create_symlink("big", root / "link");                   // short: no blocks, never followed
    fs::create_symlink("..", root / "sub/loop");                // cycle: must not recurse

    int fd = ::open((root / "sparse").c_str(), O_WRONLY | O_CREAT, 0644);
    assert(::ftruncate(fd, 100 << 20) == 0); // 100 MiB logical, ~0 physical
    assert(::pwrite(fd, "x", 1, 50 << 20) == 1);
    ::close(fd);

    fs::create_directories(root / "locked/hidden");
    write_file(root / "locked/hidden/secret", 8192);

    auto u = disk_usage(root.c_str());
    assert(u.bytes == du(root)); // exact match with du on a clean tree
    assert(u.bytes < (2 << 20)); // the sparse file did not count as 100 MiB
    assert(u.files == 6);        // big, small, sparse, secret, 2 symlinks; hardlinks once
    assert(u.skipped == 0);

    // Unreadable directory: counted as skipped, walk keeps going, never aborts.
    ::chmod((root / "locked").c_str(), 0);
    if (::geteuid() != 0) { // root can read anything
        auto l = disk_usage(root.c_str());
        assert(l.skipped == 1 && l.bytes < u.bytes && l.bytes > 0);
    }
    ::chmod((root / "locked").c_str(), 0755);

    // files_usage: manifest semantics (dpkg .list): dirs ignored, hardlinks once, missing ok.
    std::vector<std::string> list = {root.string(), (root / "big").string(),
                                     (root / "big.hardlink").string(), (root / "gone").string()};
    auto f = files_usage(list);
    assert(f.files == 1 && f.skipped == 0 && f.bytes >= (1 << 20));

    assert(disk_usage("/nonexistent/path").bytes == 0);
    assert(human_size(512) == "512B" && human_size(1536) == "1.5K" &&
           human_size(3ull << 30) == "3.0G");

    // --ascii glyphs: borders, gauges, spinner, status marks; ASCII and unknowns pass through.
    for (auto [g, a] : {std::pair{"┌", "+"},
                        {"─", "-"},
                        {"│", "|"},
                        {"█", "#"},
                        {"⣾", "|"},
                        {"⣽", "/"},
                        {"●", "*"},
                        {"✗", "x"},
                        {"·", "."},
                        {"a", "a"},
                        {"é", "é"}})
        assert(ascii_glyph(g) == a);

    fs::remove_all(root);
    std::println("test_size: ok");
}
