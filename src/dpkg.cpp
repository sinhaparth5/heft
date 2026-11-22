#include "pkgtop.hpp"

#include <algorithm>

namespace {

bool installed(std::string_view status) {
    // "install ok installed", "hold ok installed"; skip config-files, half-installed, ...
    return status.ends_with(" installed");
}

// "a (>= 1), b:any | c" -> a, b, c
void names(std::string_view list, std::vector<std::string>& out) {
    while (!list.empty()) {
        auto e = list.find_first_of(",|");
        auto n = list.substr(0, e);
        list = e == list.npos ? std::string_view{} : list.substr(e + 1);
        n.remove_prefix(std::min(n.size(), n.find_first_not_of(' ')));
        if (n = n.substr(0, n.find_first_of(" (:")); !n.empty())
            out.emplace_back(n);
    }
}

} // namespace

std::vector<Package> parse_dpkg_status(std::string_view text, bool index) {
    std::vector<Package> out;
    Package cur;
    cur.ecosystem = "apt";
    bool is_installed = index;

    auto flush = [&] {
        if (is_installed && !cur.name.empty())
            out.push_back(std::move(cur));
        cur = Package{};
        cur.ecosystem = "apt";
        is_installed = index;
    };

    while (!text.empty()) {
        auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text = nl == text.npos ? std::string_view{} : text.substr(nl + 1);

        if (line.empty()) {
            flush();
            continue;
        }
        if (line[0] == ' ' || line[0] == '\t')
            continue; // continuation of a multi-line field (Description, Conffiles)

        auto colon = line.find(':');
        if (colon == line.npos)
            continue;
        auto key = line.substr(0, colon);
        auto val = line.substr(colon + 1);
        while (!val.empty() && val.front() == ' ')
            val.remove_prefix(1);

        // ponytail: ignores Multi-Arch name:arch suffixes, add when a foreign arch shows up
        if (key == "Package")
            cur.name = val;
        else if (key == "Version")
            cur.version = val;
        else if (key == "Status")
            is_installed = installed(val);
        else if (key == "Architecture")
            cur.arch = val;
        else if (key == "Essential")
            cur.essential = val == "yes";
        else if (key == "Auto-Installed")
            cur.automatic = val == "1";
        else if (index) // apt lists: only versions are needed
            continue;
        else if (key == "Depends" || key == "Pre-Depends" || key == "Recommends")
            names(val, cur.deps);
        else if (key == "Provides")
            names(val, cur.provides);
    }
    flush();
    return out;
}

ScanResult scan_dpkg(const char* path, const char* states) {
    auto f = map_file(path);
    if (!f)
        return std::unexpected(f.error());
    auto pkgs = parse_dpkg_status(f->text);
    // ponytail: matched by name only; a Multi-Arch package marked on one arch counts for both.
    std::unordered_set<std::string> autos;
    if (auto s = map_file(states); s)
        for (auto& e : parse_dpkg_status(s->text, true))
            if (e.automatic)
                autos.insert(std::move(e.name));
    for (auto& p : pkgs)
        p.automatic = autos.contains(p.name);
    return pkgs;
}

std::optional<std::vector<std::string>> dpkg_files(const Package& p, const char* info_dir) {
    // Multi-Arch: same packages use name:arch.list; everything else uses name.list.
    auto base = std::string{info_dir} + "/" + p.name;
    auto f = map_file((base + ".list").c_str());
    if (f && f->text.empty())
        f = map_file((base + ":" + p.arch + ".list").c_str());
    if (!f)
        return std::nullopt;

    std::vector<std::string> paths;
    for (auto t = f->text; !t.empty();) {
        auto nl = t.find('\n');
        if (auto line = t.substr(0, nl); !line.empty())
            paths.emplace_back(line);
        t = nl == t.npos ? std::string_view{} : t.substr(nl + 1);
    }
    return paths;
}

DiskUsage dpkg_package_size(const Package& p, const char* info_dir) {
    auto paths = dpkg_files(p, info_dir);
    return paths ? files_usage(*paths) : DiskUsage{.skipped = 1};
}
