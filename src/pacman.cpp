#include "pkgtop.hpp"

namespace {

// Calls f(section, value) for each value line of a pacman db file:
//   %NAME%\nbash\n\n%VERSION%\n5.2.037-1\n\n%FILES%\nusr/\nusr/bin/bash\n
template <class F> void each_value(std::string_view text, F f) {
    std::string_view section;
    while (!text.empty()) {
        auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == text.npos ? std::string_view{} : text.substr(nl + 1);
        if (line.size() > 2 && line.front() == '%' && line.back() == '%')
            section = line;
        else if (!line.empty())
            f(section, line);
    }
}

} // namespace

Package parse_pacman_desc(std::string_view text) {
    Package p;
    p.ecosystem = "pacman";
    each_value(text, [&](std::string_view sec, std::string_view v) {
        if (sec == "%NAME%")
            p.name = v;
        else if (sec == "%VERSION%")
            p.version = v;
        else if (sec == "%ARCH%")
            p.arch = v;
    });
    // ponytail: no Essential flag; pacman itself refuses to remove packages others depend on
    // (glibc, systemd, ...). Read the `base` group/%REASON% if that proves not enough.
    return p;
}

ScanResult scan_pacman(const std::string& local_dir) {
    std::vector<Package> out;
    for (auto& entry : list_dir(local_dir)) {
        auto f = map_file((local_dir + "/" + entry + "/desc").c_str());
        if (!f)
            continue; // ALPM_DB_VERSION and other non-entries
        if (auto p = parse_pacman_desc(f->text); !p.name.empty())
            out.push_back(std::move(p));
    }
    return out;
}

std::optional<std::vector<std::string>> pacman_files(const Package& p,
                                                     const std::string& local_dir) {
    // The db entry is "<name>-<version>"; %FILES% paths are relative to /.
    auto f = map_file((local_dir + "/" + p.name + "-" + p.version + "/files").c_str());
    if (!f)
        return std::nullopt;
    std::vector<std::string> paths;
    each_value(f->text, [&](std::string_view sec, std::string_view v) {
        if (sec == "%FILES%" && !v.ends_with('/'))
            paths.push_back("/" + std::string{v});
    });
    return paths;
}

DiskUsage pacman_package_size(const Package& p, const std::string& local_dir) {
    auto paths = pacman_files(p, local_dir);
    return paths ? files_usage(*paths) : DiskUsage{.skipped = 1};
}
