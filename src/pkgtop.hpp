#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

struct Package {
    std::string_view ecosystem; // static literal, e.g. "apt"
    std::string name;
    std::string version;
    std::string root;              // install root; empty when files are scattered (apt)
    std::vector<std::string> bins; // exposed executables
    bool essential = false;        // never offer uninstall
};

using ScanResult = std::expected<std::vector<Package>, std::string>;

// dpkg: /var/lib/dpkg/status. Missing file = not a Debian system = empty, not an error.
std::vector<Package> parse_dpkg_status(std::string_view text);
ScanResult scan_dpkg(const char* path = "/var/lib/dpkg/status");
