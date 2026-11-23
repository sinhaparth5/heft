#include "heft.hpp"

#include <algorithm>

namespace {

// PEP 503-ish: "Foo.Bar-baz" and "foo_bar_baz" compare equal.
std::string normalize(std::string_view s) {
    std::string out;
    for (char c : s)
        out += c == '-' || c == '.' ? '_' : char(std::tolower(c));
    return out;
}

// uv receipts don't record the version; the venv's <name>-<ver>.dist-info does.
std::string dist_info_version(const std::string& venv, std::string_view name) {
    auto want = normalize(name);
    for (auto& py : list_dir(venv + "/lib"))
        for (auto& e : list_dir(venv + "/lib/" + py + "/site-packages"))
            if (e.ends_with(".dist-info")) {
                auto stem = std::string_view{e}.substr(0, e.size() - 10);
                auto dash = stem.find('-');
                if (dash != stem.npos && normalize(stem.substr(0, dash)) == want)
                    return std::string{stem.substr(dash + 1)};
            }
    return "";
}

} // namespace

// uv-receipt.toml:
//   [tool]
//   requirements = [{ name = "ruff" }]
//   entrypoints = [
//       { name = "ruff", install-path = "/home/u/.local/bin/ruff" },
//   ]
std::vector<std::string> parse_uv_receipt(std::string_view text) {
    std::vector<std::string> bins;
    auto at = text.find("entrypoints");
    if (at == text.npos)
        return bins;
    text = text.substr(at);
    text = text.substr(0, text.find("\n[")); // stop at the next table
    constexpr std::string_view key = "name = \"";
    for (size_t i = 0; (i = text.find(key, i)) != text.npos;) {
        i += key.size();
        auto end = text.find('"', i);
        if (end == text.npos)
            break;
        bins.emplace_back(text.substr(i, end - i));
        i = end;
    }
    return bins;
}

ScanResult scan_uv_tools(const std::string& tools_dir) {
    std::vector<Package> out;
    for (auto& name : list_dir(tools_dir)) {
        auto root = tools_dir + "/" + name;
        auto f = map_file((root + "/uv-receipt.toml").c_str());
        if (!f || f->text.empty())
            continue; // not a tool venv (or unreadable): skip, don't fail the provider
        Package p;
        p.ecosystem = "uv";
        p.name = name;
        p.version = dist_info_version(root, name);
        p.bins = parse_uv_receipt(f->text);
        p.root = std::move(root);
        out.push_back(std::move(p));
    }
    return out;
}
