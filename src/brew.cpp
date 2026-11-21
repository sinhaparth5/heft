#include "pkgtop.hpp"

#include <sys/stat.h>

// Homebrew: <prefix>/Cellar/<name>/<version>/INSTALL_RECEIPT.json marks a finished install.
// Every installed version is its own row, since each one takes up disk.
ScanResult scan_brew(const std::string& prefix) {
    std::vector<Package> out;
    auto cellar = prefix + "/Cellar";
    for (auto& name : list_dir(cellar))
        for (auto& version : list_dir(cellar + "/" + name)) {
            auto root = cellar + "/" + name + "/" + version;
            struct stat st;
            if (::stat((root + "/INSTALL_RECEIPT.json").c_str(), &st) != 0)
                continue; // half-installed or not a keg
            Package p;
            p.ecosystem = "brew";
            p.name = name;
            p.version = version;
            p.bins = list_dir(root + "/bin", true); // bins are often symlinks into libexec
            p.root = std::move(root);
            out.push_back(std::move(p));
        }
    return out;
}
