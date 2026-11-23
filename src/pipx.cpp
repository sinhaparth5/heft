#include "heft.hpp"

// pipx: <venvs>/<name>/pipx_metadata.json, main_package.{package, package_version, apps}.
ScanResult scan_pipx(const std::string& venvs_dir) {
    std::vector<Package> out;
    for (auto& dir : list_dir(venvs_dir)) {
        auto root = venvs_dir + "/" + dir;
        auto f = map_file((root + "/pipx_metadata.json").c_str());
        if (!f || f->text.empty())
            continue;
        auto main = json_get(f->text, "main_package");
        Package p;
        p.ecosystem = "pipx";
        p.name = json_get(main, "package");
        if (p.name.empty())
            p.name = dir;
        p.version = json_get(main, "package_version");
        for (auto [_, app] : json_items(json_get(main, "apps")))
            p.bins.emplace_back(app);
        p.root = std::move(root);
        out.push_back(std::move(p));
    }
    return out;
}
