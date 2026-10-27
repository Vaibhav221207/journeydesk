#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace httplib { class Server; }

namespace journey {

// HTTP transport owns one reservation engine for the lifetime of the server.
class WebApplication {
public:
    WebApplication(std::filesystem::path storage_path,
                   std::filesystem::path static_root,
                   std::string desk_key,
                   std::string today_override = {});
    ~WebApplication();
    WebApplication(const WebApplication&) = delete;
    WebApplication& operator=(const WebApplication&) = delete;
    httplib::Server& server();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace journey
