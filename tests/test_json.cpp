#include "heft.hpp"

#include <cassert>
#include <print>

int main() {
    auto pkg = R"({
      "name": "@google/gemini-cli",
      "author": {"name": "not me", "tags": ["}", "]"]},
      "description": "has \"quotes\", commas, and } braces",
      "version": "0.1.0",
      "private": true,
      "bin": {"gemini": "dist/index.js", "g": "x.js"}
    })";
    assert(json_get(pkg, "name") == "@google/gemini-cli"); // not author.name
    assert(json_get(pkg, "version") == "0.1.0");
    assert(json_get(pkg, "private") == "true");
    assert(json_get(pkg, "description") == R"(has \"quotes\", commas, and } braces)");
    assert(json_get(json_get(pkg, "author"), "name") == "not me");
    auto bins = json_items(json_get(pkg, "bin"));
    assert(bins.size() == 2 && bins[0].first == "gemini" && bins[1].first == "g");

    auto arr = json_items(R"([ "a", "b" ,{"c":1}, 2 ])");
    assert(arr.size() == 4 && arr[0].second == "a" && arr[1].second == "b" && arr[3].second == "2");
    assert(arr[0].first.empty());

    assert(json_get("", "x").empty());
    assert(json_items("{}").empty() && json_items("[]").empty());
    assert(json_items("{\"a\": 1,").size() == 1); // truncated: keep what parsed
    assert(json_items("{\"a\" 1}").empty());      // malformed: no crash, nothing
    assert(json_items("not json").empty());

    std::println("test_json: ok");
}
