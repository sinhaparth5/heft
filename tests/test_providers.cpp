#include "heft.hpp"

#include <cassert>
#include <cstring>
#include <elf.h>
#include <filesystem>
#include <fstream>
#include <print>

namespace {

std::vector<std::string> names(const std::vector<Package>& ps) {
    std::vector<std::string> out;
    for (auto& p : ps)
        out.push_back(p.name + "@" + p.version);
    return out;
}

using V = std::vector<std::string>;

// Smallest ELF64 the Go parser accepts: header, .go.buildinfo, .shstrtab, section headers.
std::string fake_go_binary(std::string_view go_version, std::string_view modinfo) {
    std::string info = "\xff Go buildinf:\x08\x02";
    info.resize(32, '\0');
    auto put = [&](std::string_view s) {
        for (uint64_t n = s.size();; n >>= 7) { // uvarint
            info += char((n & 0x7f) | (n > 0x7f ? 0x80 : 0));
            if (n <= 0x7f)
                break;
        }
        info += s;
    };
    put(go_version);
    put(modinfo);
    std::string strtab{"\0.shstrtab\0.go.buildinfo\0", 25};

    Elf64_Ehdr eh{};
    std::memcpy(eh.e_ident, ELFMAG, SELFMAG);
    eh.e_ident[EI_CLASS] = ELFCLASS64;
    eh.e_ident[EI_DATA] = ELFDATA2LSB;
    eh.e_shentsize = sizeof(Elf64_Shdr);
    eh.e_shnum = 3;
    eh.e_shstrndx = 1;
    size_t info_off = sizeof eh, str_off = info_off + info.size();
    eh.e_shoff = str_off + strtab.size();

    Elf64_Shdr sh[3]{};
    sh[1].sh_name = 1, sh[1].sh_offset = str_off, sh[1].sh_size = strtab.size();
    sh[2].sh_name = 11, sh[2].sh_offset = info_off, sh[2].sh_size = info.size();
    std::string img(reinterpret_cast<char*>(&eh), sizeof eh);
    img += info + strtab;
    img.append(reinterpret_cast<char*>(sh), sizeof sh);
    return img;
}

} // namespace

int main() {
    // pacman: ALPM_DB_VERSION file is not an entry; desc without trailing newline still parses.
    auto pac = scan_pacman(FIXTURES "/pacman/local");
    assert(pac && names(*pac) == (V{"bash@5.2.037-1", "zstd@1.5.6-1"}));
    assert((*pac)[0].arch == "x86_64" && (*pac)[0].ecosystem == "pacman");
    assert(((*pac)[0].deps == std::vector<std::string>{"glibc", "readline"}));
    assert(!(*pac)[0].automatic && (*pac)[1].automatic); // %REASON% 1
    assert(scan_pacman("/nonexistent")->empty());

    // brew: one row per installed version; kegs without a receipt are skipped; symlinked bins kept.
    auto brew = scan_brew(FIXTURES "/brew");
    assert(brew && names(*brew) == (V{"jq@1.6", "jq@1.7.1"}));
    assert((*brew)[1].bins == (V{"jq", "jq-alias"}));
    assert((*brew)[1].root == FIXTURES "/brew/Cellar/jq/1.7.1");

    // uv: version from dist-info (pre-commit → pre_commit), bins from entrypoints only.
    auto uv = scan_uv_tools(FIXTURES "/uv/tools");
    assert(uv && names(*uv) == (V{"pre-commit@4.0.1", "ruff@0.6.9"}));
    assert((*uv)[0].bins == (V{"pre-commit", "pre-commit-hook"}));
    assert((*uv)[1].bins == V{"ruff"});
    assert(parse_uv_receipt("").empty() && parse_uv_receipt("entrypoints = [").empty());

    // pipx: unparseable metadata falls back to the dir name rather than failing the provider.
    auto pipx = scan_pipx(FIXTURES "/pipx/venvs");
    assert(pipx && names(*pipx) == (V{"broken@", "greynoise@3.1.0"}));
    assert((*pipx)[1].bins == V{"greynoise"});

    // npm: scoped packages, string vs object "bin", dirs without package.json skipped.
    auto npm = scan_npm(FIXTURES "/npm/lib/node_modules");
    assert(npm && names(*npm) == (V{"@google/gemini-cli@0.1.5", "corepack@0.36.0"}));
    assert((*npm)[0].bins == V{"gemini-cli"});
    assert((*npm)[1].bins == (V{"corepack", "pnpm"}));

    // pnpm: the symlinked alias dir is not scanned twice; root resolves into .pnpm.
    auto pnpm = scan_pnpm(FIXTURES "/pnpm/global");
    assert(pnpm && names(*pnpm) == V{"cowsay@1.6.0"});
    assert((*pnpm)[0].root.ends_with("/.pnpm/cowsay@1.6.0/node_modules/cowsay"));

    // go: parse a synthetic ELF; > 127-byte modinfo exercises multi-byte varints.
    std::string mod = "0w\xaf\x0c\x92t\x08\x02\x41\xe1\xc1\x07\xe6\xd6\x18\xe6"
                      "path\tgolang.org/x/tools/gopls\n"
                      "mod\tgolang.org/x/tools/gopls\tv0.23.0\th1:abc=\n";
    for (int i = 0; i < 10; ++i)
        mod += "dep\tgithub.com/some/dependency\tv1.0.0\th1:xyz=\n";
    auto bi = parse_go_buildinfo(fake_go_binary("go1.26.0", mod));
    assert(bi && bi->go_version == "go1.26.0" && bi->module == "golang.org/x/tools/gopls" &&
           bi->version == "v0.23.0");
    assert(!parse_go_buildinfo("#!/bin/sh\necho hi\n"));
    assert(!parse_go_buildinfo(""));
    assert(!parse_go_buildinfo(fake_go_binary("go1.26.0", "").substr(0, 70))); // truncated

    char dir[] = "/tmp/heft-go-XXXXXX";
    assert(::mkdtemp(dir));
    std::string d = dir;
    std::ofstream(d + "/gopls") << fake_go_binary("go1.26.0", mod);
    std::ofstream(d + "/script") << "#!/bin/sh\n";
    auto go = scan_go(d);
    assert(go && names(*go) == V{"gopls@v0.23.0"} && (*go)[0].bins == V{"gopls"});
    std::filesystem::remove_all(d);

    std::println("test_providers: ok");
}
