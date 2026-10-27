#include "web.hpp"
#include "engine.hpp"
#include "httplib.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>

namespace journey {
namespace {
constexpr std::size_t max_body = 16 * 1024;

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string host_name(const std::string& authority) {
    if (authority.empty() || authority.find_first_of(" /\\@?#\t\r\n") != std::string::npos)
        return {};
    if (authority.front() == '[') {
        const auto end = authority.find(']');
        if (end == std::string::npos) return {};
        if (end + 1 < authority.size() && authority[end + 1] != ':') return {};
        return lowercase(authority.substr(0, end + 1));
    }
    return lowercase(authority.substr(0, authority.find(':')));
}

bool same_key(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left[i] ^ right[i]);
    return difference == 0;
}

bool demo_value(const json& input) {
    if (!input.contains("demo")) return false;
    if (!input["demo"].is_boolean()) throw AppError("The demo option must be true or false.");
    return input["demo"].get<bool>();
}

void respond(httplib::Response& response, const json& body, int status = 200) {
    response.status = status;
    response.set_content(body.dump(), "application/json; charset=utf-8");
}

void error(httplib::Response& response, const std::string& message, int status,
           const json& fields = json::object()) {
    respond(response, {{"error", message}, {"fields", fields}}, status);
}

json query_input(const httplib::Request& request) {
    json input = json::object();
    for (const auto& [key, value] : request.params) {
        if (input.contains(key)) throw AppError("Duplicate query parameters are not allowed.");
        if (key.empty() || key.front() == '_') throw AppError("Invalid query parameter.");
        if (key == "demo") {
            if (value != "true" && value != "false")
                throw AppError("The demo option must be true or false.");
            input[key] = value == "true";
        } else input[key] = value;
    }
    return input;
}

json body_input(const httplib::Request& request) {
    auto media = lowercase(request.get_header_value("Content-Type"));
    media = media.substr(0, media.find(';'));
    while (!media.empty() && media.back() == ' ') media.pop_back();
    if (media != "application/json")
        throw AppError("Send this request as application/json.", 415);
    if (request.body.size() > max_body) throw AppError("The request is too large.", 413);
    json body;
    try {
        body = json::parse(request.body, [](int depth, json::parse_event_t, json&) {
            if (depth > 24) throw AppError("The JSON request is too deeply nested.");
            return true;
        });
    } catch (const json::exception&) { throw AppError("The request contains invalid JSON."); }
    if (!body.is_object()) throw AppError("Send a JSON object.");
    for (const auto& [key, value] : body.items()) {
        (void)value;
        if (key.empty() || key.front() == '_')
            throw AppError("Internal authorization fields are not accepted.");
    }
    return body;
}
} // namespace

struct WebApplication::Impl {
    ReservationEngine engine;
    httplib::Server server;
    std::filesystem::path static_root;
    std::string desk_key;
    std::set<std::string> allowed_hosts = {"localhost", "127.0.0.1", "[::1]"};

    Impl(std::filesystem::path storage, std::filesystem::path root,
         std::string key, std::string today)
        : engine(std::move(storage), std::move(today)),
          static_root(std::move(root)), desk_key(std::move(key)) {
        if (desk_key.size() < 12) throw std::invalid_argument("The desk key must have at least 12 characters.");
        if (const auto* host = std::getenv("RESERVATION_PUBLIC_HOST")) {
            const auto name = host_name(host);
            if (name.empty()) throw std::invalid_argument("RESERVATION_PUBLIC_HOST must be a hostname.");
            allowed_hosts.insert(name);
        }
        server.new_task_queue = [] { return new httplib::ThreadPool(4); };
        server.set_payload_max_length(max_body);
        server.set_read_timeout(10);
        server.set_write_timeout(10);
        server.set_keep_alive_max_count(50);
        server.set_default_headers({
            {"Cache-Control", "no-store"},
            {"X-Content-Type-Options", "nosniff"},
            {"Referrer-Policy", "same-origin"},
            {"X-Frame-Options", "DENY"},
            {"Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'; form-action 'self'"}
        });
        // Validate after the bounded request body has been read. Rejecting a
        // POST before consuming its body can reset the connection before the
        // client receives the JSON error response.
        server.set_pre_request_handler([this](const httplib::Request& req, httplib::Response& res) {
            const auto authority = req.get_header_value("Host");
            if (!allowed_hosts.contains(host_name(authority))) {
                error(res, "This hostname is not configured for Journey Desk.", 403);
                return httplib::Server::HandlerResponse::Handled;
            }
            if (req.method == "POST") {
                const auto site = req.get_header_value("Sec-Fetch-Site");
                const auto origin = req.get_header_value("Origin");
                bool valid_origin = origin.empty();
                if (origin.starts_with("http://"))
                    valid_origin = lowercase(origin.substr(7)) == lowercase(authority);
                else if (origin.starts_with("https://"))
                    valid_origin = lowercase(origin.substr(8)) == lowercase(authority);
                if (site == "cross-site" || !valid_origin) {
                    error(res, "Use the booking form on this website.", 403);
                    return httplib::Server::HandlerResponse::Handled;
                }
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });
        server.set_exception_handler([](const auto&, auto& res, std::exception_ptr problem) {
            try { if (problem) std::rethrow_exception(problem); }
            catch (const AppError& e) { error(res, e.what(), e.status, e.fields); }
            catch (const json::exception&) { error(res, "Check the request fields and try again.", 400); }
            catch (...) { error(res, "The server could not complete the request. Please try again.", 500); }
        });
        server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
            if (res.body.empty()) {
                const std::string message = res.status == 413 ? "The request is too large."
                    : res.status == 404 ? "This page or record was not found."
                    : res.status == 400 ? "The request could not be read." : "The request could not be completed.";
                error(res, message, res.status);
            }
        });
        routes();
    }

    void authorize_desk(const httplib::Request& req, json& input) const {
        if (!demo_value(input) && !same_key(req.get_header_value("X-Desk-Key"), desk_key))
            throw AppError("Enter the reservation desk access key.", 401, {{"desk_key", "The desk access key is missing or incorrect."}});
        input["_desk_authorized"] = true;
    }

    void static_file(const std::string& route, const std::string& file, const std::string& media) {
        server.Get(route, [this, file, media](const auto&, auto& res) {
            std::ifstream stream(static_root / file, std::ios::binary);
            if (!stream) throw AppError("The page file could not be found.", 404);
            std::string content((std::istreambuf_iterator<char>(stream)), {});
            res.set_content(std::move(content), media);
            res.set_header("Cache-Control", "no-cache");
        });
    }

    void routes() {
        static_file("/", "index.html", "text/html; charset=utf-8");
        static_file("/static/index\\.html", "index.html", "text/html; charset=utf-8");
        static_file("/static/styles\\.css", "styles.css", "text/css; charset=utf-8");
        static_file("/static/app\\.js", "app.js", "text/javascript; charset=utf-8");
        static_file("/static/scene\\.svg", "scene.svg", "image/svg+xml");
        server.Get("/api/health", [](const auto&, auto& res) {
            respond(res, {{"ok", true}, {"backend", "C++20"}});
        });
        server.Get("/api/catalog", [this](const auto&, auto& res) { respond(res, engine.catalog()); });
        server.Get("/api/search", [this](const auto& req, auto& res) { respond(res, engine.search(query_input(req))); });
        server.Get("/api/journey", [this](const auto& req, auto& res) { respond(res, engine.journey(query_input(req))); });
        server.Get("/api/desk", [this](const auto& req, auto& res) {
            auto input = query_input(req);
            authorize_desk(req, input);
            respond(res, engine.manifest(input));
        });
        server.Post("/api/book", [this](const auto& req, auto& res) { respond(res, engine.book(body_input(req))); });
        server.Post("/api/book/group", [this](const auto& req, auto& res) { respond(res, engine.book_group(body_input(req))); });
        server.Post("/api/ticket", [this](const auto& req, auto& res) { respond(res, engine.ticket(body_input(req))); });
        server.Post("/api/cancel", [this](const auto& req, auto& res) { respond(res, engine.cancel(body_input(req))); });
        server.Post("/api/demo", [this](const auto& req, auto& res) { respond(res, engine.load_demo(body_input(req))); });
        server.Post("/api/desk/cancel", [this](const auto& req, auto& res) {
            auto input = body_input(req);
            authorize_desk(req, input);
            auto requested_journey = input;
            requested_journey.erase("q");
            const auto manifest = engine.manifest(requested_journey);
            bool found = false;
            for (const auto* list : {"passengers", "waiting"})
                for (const auto& record : manifest.at(list))
                    if (record.at("reference") == input.value("reference", "")) found = true;
            if (!found) throw AppError("This reservation was not found in the selected journey.", 404);
            respond(res, engine.cancel(input));
        });
    }
};

WebApplication::WebApplication(std::filesystem::path storage, std::filesystem::path root,
                               std::string key, std::string today)
    : impl_(std::make_unique<Impl>(std::move(storage), std::move(root), std::move(key), std::move(today))) {}
WebApplication::~WebApplication() = default;
httplib::Server& WebApplication::server() { return impl_->server; }
} // namespace journey
