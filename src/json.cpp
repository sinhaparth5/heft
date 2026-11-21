#include "pkgtop.hpp"

#include <cstring>

// Just enough JSON to read package.json, pipx_metadata.json and friends: walk one level of
// an object or array and hand back raw values. Nested values stay raw text for the caller
// to walk again.
// ponytail: string escapes are left as-is (\" and é stay literal); fine for package
// names, versions and bin names. Unescape if a provider ever needs free text.

namespace {

size_t skip_ws(std::string_view s, size_t i) {
    while (i < s.size() && std::strchr(" \t\r\n", s[i]))
        ++i;
    return i;
}

size_t string_end(std::string_view s, size_t i) { // s[i] == '"'; returns index past closing quote
    for (++i; i < s.size(); ++i) {
        if (s[i] == '\\')
            ++i;
        else if (s[i] == '"')
            return i + 1;
    }
    return s.size();
}

size_t value_end(std::string_view s, size_t i) {
    if (i >= s.size())
        return i;
    if (s[i] == '"')
        return string_end(s, i);
    if (s[i] == '{' || s[i] == '[') {
        for (int depth = 0; i < s.size(); ++i) {
            if (s[i] == '"')
                i = string_end(s, i) - 1;
            else if (s[i] == '{' || s[i] == '[')
                ++depth;
            else if ((s[i] == '}' || s[i] == ']') && --depth == 0)
                return i + 1;
        }
        return i;
    }
    while (i < s.size() && !std::strchr(",}] \t\r\n", s[i])) // number, true, false, null
        ++i;
    return i;
}

std::string_view unquote(std::string_view v) {
    return v.size() >= 2 && v.front() == '"' ? v.substr(1, v.size() - 2) : v;
}

} // namespace

std::vector<std::pair<std::string_view, std::string_view>> json_items(std::string_view s) {
    std::vector<std::pair<std::string_view, std::string_view>> out;
    size_t i = skip_ws(s, 0);
    if (i >= s.size() || (s[i] != '{' && s[i] != '['))
        return out;
    bool object = s[i++] == '{';
    while (true) {
        i = skip_ws(s, i);
        if (i >= s.size() || s[i] == '}' || s[i] == ']')
            return out;
        std::string_view key;
        if (object) {
            if (s[i] != '"')
                return out;
            auto e = string_end(s, i);
            key = unquote(s.substr(i, e - i));
            i = skip_ws(s, e);
            if (i >= s.size() || s[i] != ':')
                return out;
            i = skip_ws(s, i + 1);
        }
        auto e = value_end(s, i);
        if (e == i)
            return out; // malformed
        out.emplace_back(key, unquote(s.substr(i, e - i)));
        i = skip_ws(s, e);
        if (i >= s.size() || s[i] != ',')
            return out;
        ++i;
    }
}

std::string_view json_get(std::string_view object, std::string_view key) {
    for (auto [k, v] : json_items(object))
        if (k == key)
            return v;
    return {};
}
