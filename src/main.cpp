#include "web.hpp"
#include "httplib.h"

#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <shellapi.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace {
std::string env(const char* name, std::string fallback = {}) {
    const auto* value = std::getenv(name);
    return value && *value ? value : std::move(fallback);
}

std::string random_key() {
    std::random_device random;
    std::ostringstream key;
    key << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) key << std::setw(8) << random();
    return key.str();
}

int port_number(const std::string& value) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Port must be a number between 1 and 65535.");
    const auto number = std::stoul(value);
    if (number < 1 || number > 65535) throw std::invalid_argument("Port must be between 1 and 65535.");
    return static_cast<int>(number);
}

#ifndef _WIN32
void prepare_unix_socket(const fs::path& path) {
    std::error_code error;
    if (!fs::exists(path, error)) {
        if (error) throw std::runtime_error("Could not inspect the configured Unix socket.");
        return;
    }
    if (!fs::is_socket(path, error))
        throw std::invalid_argument("Refusing to replace a non-socket file at the configured socket path.");
    const auto name = path.string();
    sockaddr_un address{};
    if (name.size() >= sizeof(address.sun_path))
        throw std::invalid_argument("The Unix socket path is too long.");
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, name.c_str(), name.size() + 1);
    const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0) throw std::runtime_error("Could not check the existing Unix socket.");
    const int connected = ::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    const int reason = errno;
    ::close(probe);
    if (connected == 0)
        throw std::runtime_error("Another server already owns the configured Unix socket. Stop it before starting this one.");
    if (reason != ECONNREFUSED && reason != ENOENT)
        throw std::runtime_error("Could not verify that the existing Unix socket is stale; it was preserved.");
    fs::remove(path, error);
    if (error) throw std::runtime_error("Could not remove the stale Unix socket.");
}
#endif
} // namespace

int main(int argc, char** argv) {
    try {
        const auto executable_directory = fs::absolute(argv[0]).parent_path();
        fs::path static_root = executable_directory / "static";
        if (!fs::exists(static_root / "index.html")) static_root = fs::current_path() / "static";
        fs::path storage = env("RESERVATION_DATA_FILE", (executable_directory / "data" / "bookings.json").string());
        std::string host = env("PORT").empty() ? "127.0.0.1" : "0.0.0.0";
        int port = port_number(env("PORT", "8080"));
        std::string socket = env("DOMAIN_SOCKET");
        bool open_browser = false;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            auto argument = [&]() -> std::string {
                if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + option);
                return argv[++i];
            };
            if (option == "--host") host = argument();
            else if (option == "--port") port = port_number(argument());
            else if (option == "--data") storage = argument();
            else if (option == "--static") static_root = argument();
            else if (option == "--unix-socket") socket = argument();
            else if (option == "--open") open_browser = true;
            else if (option == "--help") {
                std::cout << "Journey Desk - C++20 reservation server\n"
                             "Usage: journeydesk [--host ADDRESS] [--port NUMBER] [--data FILE]\n"
                             "                   [--static DIRECTORY] [--unix-socket PATH] [--open]\n"
                             "Environment: PORT, DOMAIN_SOCKET, RESERVATION_DATA_FILE,\n"
                             "             RESERVATION_PUBLIC_HOST, RESERVATION_DESK_KEY\n"
                             "Default: http://127.0.0.1:8080; use Ctrl+C to stop.\n";
                return 0;
            } else throw std::invalid_argument("Unknown option: " + option);
        }
        if (!fs::is_regular_file(static_root / "index.html"))
            throw std::invalid_argument("Frontend files are missing. Run from the project folder or use --static DIRECTORY.");
        const auto desk_key = env("RESERVATION_DESK_KEY", random_key());
        journey::WebApplication application(storage, static_root, desk_key);
        auto& server = application.server();
        bool bound = false;
        if (!socket.empty()) {
#ifdef _WIN32
            throw std::invalid_argument("Use TCP hosting on Windows; Unix-socket hosting is intended for Linux.");
#else
            prepare_unix_socket(socket);
            server.set_address_family(AF_UNIX);
            bound = server.bind_to_port(socket, 0);
#endif
        } else bound = server.bind_to_port(host, port);
        if (!bound) throw std::runtime_error("Could not start the server. The port or socket may already be in use.");
        std::cout << "\nJourney Desk - C++20 backend\n";
        if (socket.empty()) std::cout << "Open: http://" << (host == "0.0.0.0" ? "127.0.0.1" : host) << ':' << port << "/\n";
        else std::cout << "Listening on the hosting platform's Unix socket.\n";
        std::cout << "Desk access key: " << desk_key << "\n"
                  << "Keep this key private. The fictional demo does not need a key.\n"
                  << "Sample schedules and fares. Normal journeys start empty.\n"
                  << "Press Ctrl+C to stop.\n\n" << std::flush;
#ifdef _WIN32
        if (open_browser && socket.empty()) {
            const std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
#else
        (void)open_browser;
#endif
        return server.listen_after_bind() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "Journey Desk: " << error.what() << '\n';
        return 1;
    }
}
