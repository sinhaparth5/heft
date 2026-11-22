#include "pkgtop.hpp"

#include <cassert>
#include <fstream>
#include <print>
#include <sstream>

using V = std::vector<std::string>;

static std::string slurp(const char* f) {
    std::stringstream s;
    s << std::ifstream(f).rdbuf();
    return s.str();
}

int main() {
    auto df = slurp(FIXTURES "/docker/df.json"), nets = slurp(FIXTURES "/docker/networks.json");
    auto d = parse_docker(df, nets);
    assert(d.size() == 8);
    auto &web = d[0], &old = d[1], &wp = d[2], &dangling = d[3], &vol = d[4], &nfs = d[5];
    assert(web.kind == "container" && web.name == "web" && web.state == "running");
    assert(web.size->bytes == 4096 && web.image == "wordpress:7");
    assert(wp.kind == "image" && wp.name == "wordpress:7 +1" && wp.status == "4bf2c44f19a5");
    assert(wp.refs == 1 && wp.shared == 32 && wp.size->bytes == 1132925532);
    assert(dangling.name == "<none>" && dangling.shared == 0);
    assert(vol.name == "wp_data" && vol.refs == 2 && vol.size->bytes == 130952847);
    assert(!nfs.size); // -1: unknown, not 0
    assert(d[6].name == "bridge" && d[7].status == "bridge local");

    assert(docker_argv(web, false).empty()); // running: stop it first
    assert((docker_argv(web, true) == V{"docker", "stop", "web"}));
    assert((docker_argv(old, true) == V{"docker", "start", "old-job"}));
    assert((docker_argv(old, false) == V{"docker", "rm", "old-job"}));
    assert((docker_argv(wp, false) == V{"docker", "rmi", "wordpress:7", "wp:latest"}));
    assert((docker_argv(dangling, false) == V{"docker", "rmi", "9c7f200c07e3"}));
    assert((docker_argv(vol, false) == V{"docker", "volume", "rm", "wp_data"}));
    assert(docker_argv(vol, true).empty());
    assert(docker_argv(d[6], false).empty()); // built-in network
    assert((docker_argv(d[7], false) == V{"docker", "network", "rm", "77aa88bb99cc"}));

    assert(parse_docker("", "").empty());
    std::println("test_docker: ok");
}
