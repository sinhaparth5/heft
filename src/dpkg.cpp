#include "pkgtop.hpp"

namespace {

bool installed(std::string_view status) {
    // "install ok installed", "hold ok installed"; skip config-files, half-installed, ...
    return status.ends_with(" installed");
}

} // namespace

std::vector<Package> parse_dpkg_status(std::string_view text) {
    std::vector<Package> out;
    Package cur;
    cur.ecosystem = "apt";
    bool is_installed = false;

    auto flush = [&] {
        if (is_installed && !cur.name.empty())
            out.push_back(std::move(cur));
        cur = Package{};
        cur.ecosystem = "apt";
        is_installed = false;
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
        else if (key == "Essential")
            cur.essential = val == "yes";
    }
    flush();
    return out;
}

ScanResult scan_dpkg(const char* path) {
    auto f = map_file(path);
    if (!f)
        return std::unexpected(f.error());
    return parse_dpkg_status(f->text);
}
