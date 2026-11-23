#include "heft.hpp"

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

int64_t num(std::string_view s) {
    int64_t n = -1;
    std::from_chars(s.data(), s.data() + s.size(), n);
    return n;
}

std::optional<DiskUsage> bytes(std::string_view s) {
    auto n = num(s);
    return n < 0 ? std::nullopt : std::optional{DiskUsage{uint64_t(n), 0, 0}};
}

} // namespace

std::vector<DockerItem> parse_docker(std::string_view df, std::string_view networks) {
    std::vector<DockerItem> out;
    for (auto [_, c] : json_items(json_get(df, "Containers"))) {
        DockerItem d;
        d.kind = "container";
        d.id = json_get(c, "Id");
        auto names = json_items(json_get(c, "Names"));
        d.name = names.empty() ? d.id.substr(0, 12) : std::string{names[0].second};
        if (d.name.starts_with('/'))
            d.name.erase(0, 1);
        d.status = json_get(c, "Status"), d.state = json_get(c, "State");
        d.image = json_get(c, "Image");
        d.size = bytes(json_get(c, "SizeRw"));
        out.push_back(std::move(d));
    }
    for (auto [_, i] : json_items(json_get(df, "Images"))) {
        DockerItem d;
        d.kind = "image";
        d.id = json_get(i, "Id");
        if (d.id.starts_with("sha256:"))
            d.id.erase(0, 7);
        for (auto [_, t] : json_items(json_get(i, "RepoTags")))
            if (t != "<none>:<none>")
                d.tags.emplace_back(t);
        d.name = d.tags.empty()       ? "<none>"
                 : d.tags.size() == 1 ? d.tags[0]
                                      : std::format("{} +{}", d.tags[0], d.tags.size() - 1);
        d.status = d.id.substr(0, 12);
        d.size = bytes(json_get(i, "Size"));
        d.shared = std::max<int64_t>(0, num(json_get(i, "SharedSize")));
        d.refs = num(json_get(i, "Containers"));
        out.push_back(std::move(d));
    }
    for (auto [_, v] : json_items(json_get(df, "Volumes"))) {
        DockerItem d;
        d.kind = "volume";
        d.id = d.name = json_get(v, "Name");
        d.status = json_get(v, "Driver"), d.image = json_get(v, "Mountpoint");
        auto use = json_get(v, "UsageData");
        d.size = bytes(json_get(use, "Size")); // -1: driver can't tell
        d.refs = num(json_get(use, "RefCount"));
        out.push_back(std::move(d));
    }
    for (auto [_, n] : json_items(networks)) {
        DockerItem d;
        d.kind = "network";
        d.id = json_get(n, "Id"), d.name = json_get(n, "Name");
        d.status = std::format("{} {}", json_get(n, "Driver"), json_get(n, "Scope"));
        out.push_back(std::move(d));
    }
    return out;
}

std::expected<std::string, std::string> docker_get(const std::string& sock, std::string_view path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (sock.size() >= sizeof addr.sun_path)
        return std::unexpected("socket path too long");
    std::memcpy(addr.sun_path, sock.c_str(), sock.size() + 1);
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return std::unexpected(std::strerror(errno));
    timeval tv{30, 0}; // a hung daemon must not hang heft's exit (threads are joined)
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    std::string r;
    // HTTP/1.0: the daemon answers with Content-Length or closes, never chunked.
    auto req = std::format("GET {} HTTP/1.0\r\nHost: docker\r\n\r\n", path);
    if (::connect(fd, (sockaddr*)&addr, sizeof addr) < 0 ||
        ::write(fd, req.data(), req.size()) != ssize_t(req.size())) {
        int e = errno;
        ::close(fd);
        return std::unexpected(e == EACCES         ? "permission denied (join the docker group)"
                               : e == ECONNREFUSED ? "daemon not running"
                                                   : std::strerror(e));
    }
    char buf[65536];
    for (ssize_t n; (n = ::read(fd, buf, sizeof buf)) > 0;)
        r.append(buf, n);
    ::close(fd);
    auto body = r.find("\r\n\r\n");
    if (!r.starts_with("HTTP/1.") || r.size() < 12 || body == r.npos)
        return std::unexpected("no reply from daemon");
    if (r.substr(9, 3) != "200")
        return std::unexpected("HTTP " + r.substr(9, 3) + " for " + std::string{path});
    return r.substr(body + 4);
}

std::expected<std::vector<DockerItem>, std::string> scan_docker() {
    const char* host = std::getenv("DOCKER_HOST");
    std::string sock = "/var/run/docker.sock";
    if (host && *host) {
        std::string_view h = host;
        if (!h.starts_with("unix://"))
            return std::vector<DockerItem>{}; // ponytail: tcp/ssh hosts unsupported
        sock = h.substr(7);
    }
    if (::access(sock.c_str(), F_OK) != 0)
        return std::vector<DockerItem>{}; // no daemon on this machine
    auto df = docker_get(sock, "/system/df");
    if (!df)
        return std::unexpected(df.error());
    auto nets = docker_get(sock, "/networks");
    if (!nets)
        return std::unexpected(nets.error());
    return parse_docker(*df, *nets);
}

std::vector<std::string> docker_argv(const DockerItem& d, bool toggle) {
    bool running = d.state == "running" || d.state == "restarting" || d.state == "paused";
    if (toggle)
        return d.kind != "container"
                   ? std::vector<std::string>{}
                   : std::vector<std::string>{"docker", running ? "stop" : "start", d.name};
    if (d.kind == "container")
        return running ? std::vector<std::string>{}
                       : std::vector<std::string>{"docker", "rm", d.name};
    if (d.kind == "image") { // every tag, so the image itself goes, not just one name
        std::vector<std::string> v{"docker", "rmi"};
        if (d.tags.empty())
            v.push_back(d.id.substr(0, 12));
        v.insert(v.end(), d.tags.begin(), d.tags.end());
        return v;
    }
    if (d.kind == "volume")
        return {"docker", "volume", "rm", d.name};
    if (d.name == "bridge" || d.name == "host" || d.name == "none")
        return {}; // built in: the daemon refuses anyway
    return {"docker", "network", "rm", d.id.substr(0, 12)};
}
