#include "heft.hpp"

// <dir>/package.json → Package; shared with pnpm. Empty name = not a package.
Package read_node_package(const std::string& dir, std::string_view ecosystem) {
    Package p;
    p.ecosystem = ecosystem;
    auto f = map_file((dir + "/package.json").c_str());
    if (!f || f->text.empty())
        return p;
    p.name = json_get(f->text, "name");
    p.version = json_get(f->text, "version");
    p.root = dir;
    auto bin = json_get(f->text, "bin");
    if (bin.starts_with('{')) {
        for (auto [name, _] : json_items(bin))
            p.bins.emplace_back(name);
    } else if (!bin.empty()) { // "bin": "cli.js" → command is the unscoped package name
        auto slash = p.name.rfind('/');
        p.bins.push_back(slash == p.name.npos ? p.name : p.name.substr(slash + 1));
    }
    return p;
}

// npm globals: <prefix>/lib/node_modules/{name,@scope/name}/package.json.
ScanResult scan_npm(const std::string& node_modules) {
    std::vector<Package> out;
    auto add = [&](const std::string& dir) {
        if (auto p = read_node_package(dir, "npm"); !p.name.empty())
            out.push_back(std::move(p));
    };
    for (auto& e : list_dir(node_modules)) {
        if (e.starts_with('@'))
            for (auto& sub : list_dir(node_modules + "/" + e))
                add(node_modules + "/" + e + "/" + sub);
        else
            add(node_modules + "/" + e);
    }
    return out;
}
