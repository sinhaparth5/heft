#include <print>
#include <string_view>

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view{argv[1]} == "--version") {
        std::println("pkgtop {}", PKGTOP_VERSION);
        return 0;
    }
    std::println("pkgtop {}: nothing here yet, see ROADMAP.md", PKGTOP_VERSION);
}
