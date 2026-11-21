#include "pkgtop.hpp"

#include <climits>
#include <cstdlib>

// pnpm globals: <PNPM_HOME>/global/<layout version>/<id>/package.json lists what was added
// globally; each one resolves through node_modules/<name> (a symlink into .pnpm).
ScanResult scan_pnpm(const std::string& global_dir) {
    std::vector<Package> out;
    for (auto& v : list_dir(global_dir))
        for (auto& id : list_dir(global_dir + "/" + v)) { // skips the symlinked alias of each id
            auto base = global_dir + "/" + v + "/" + id;
            auto f = map_file((base + "/package.json").c_str());
            if (!f || f->text.empty())
                continue;
            for (auto [name, _] : json_items(json_get(f->text, "dependencies"))) {
                auto link = base + "/node_modules/" + std::string{name};
                char real[PATH_MAX];
                auto p = read_node_package(::realpath(link.c_str(), real) ? real : link, "pnpm");
                // ponytail: files in .pnpm are hardlinks into the pnpm store, so this size also
                // shows up under the pnpm cache. Dedupe across rows if totals get confusing.
                if (!p.name.empty())
                    out.push_back(std::move(p));
            }
        }
    return out;
}
