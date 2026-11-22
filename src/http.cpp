#include "pkgtop.hpp"

#include <algorithm>
#include <atomic>
#include <map>
#include <thread>

#ifdef PKGTOP_CURL
#include <curl/curl.h>
#include <mutex>

namespace {

// Body of a 2xx response; "" on any failure. Aborts as soon as st is stopped (quit).
std::string http_get(const std::string& url, std::stop_token st) {
    static std::once_flag init;
    std::call_once(init, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* c = curl_easy_init();
    if (!c)
        return {};
    std::string body;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, "pkgtop/" PKGTOP_VERSION); // crates.io requires one
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(
        c, CURLOPT_WRITEFUNCTION, +[](char* p, size_t, size_t n, void* out) {
            static_cast<std::string*>(out)->append(p, n);
            return n;
        });
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &st);
    curl_easy_setopt(
        c, CURLOPT_XFERINFOFUNCTION, +[](void* st, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            return int(static_cast<std::stop_token*>(st)->stop_requested());
        });
    bool ok = curl_easy_perform(c) == CURLE_OK;
    curl_easy_cleanup(c);
    return ok ? body : "";
}

} // namespace

bool updates_supported() { return true; }
#else
namespace {
std::string http_get(const std::string&, std::stop_token) { return {}; }
} // namespace
bool updates_supported() { return false; }
#endif

bool checkable(const Package& p) {
    return local_update_source(p) || (updates_supported() && !update_url(p).empty());
}

size_t check_updates(const std::vector<Package>& pkgs, std::stop_token st,
                     std::function<void(size_t, std::string)> done) {
    local_latest(pkgs, done);
    size_t n = std::ranges::count_if(pkgs, local_update_source);
    if (!updates_supported())
        return n;
    // One request per registry URL: the same npm package under several nvm nodes asks once.
    std::map<std::string, std::vector<size_t>> by_url;
    for (size_t i = 0; i < pkgs.size(); ++i)
        if (auto u = update_url(pkgs[i]); !u.empty())
            by_url[u].push_back(i);
    std::vector<const decltype(by_url)::value_type*> jobs;
    for (auto& j : by_url)
        jobs.push_back(&j);

    std::atomic<size_t> next{0};
    {
        std::vector<std::jthread> pool;
        for (int n = 0; n < 6; ++n) // polite to the registries, still ~1 s for 50 packages
            pool.emplace_back([&] {
                for (size_t k; !st.stop_requested() && (k = next++) < jobs.size();) {
                    auto& [url, idx] = *jobs[k];
                    auto latest = parse_latest(pkgs[idx[0]].ecosystem, http_get(url, st));
                    for (auto i : idx)
                        done(i, latest);
                }
            });
    }
    for (auto& [_, idx] : by_url)
        n += idx.size();
    return n;
}
