#include "web.hpp"
#include "httplib.h"
#include "json.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

struct TestServer {
    fs::path root;
    journey::WebApplication app;
    int port;
    std::thread thread;
    httplib::Client client;

    TestServer()
        : root(fs::current_path() / ("web-test-" + std::to_string(
              std::chrono::steady_clock::now().time_since_epoch().count()))),
          app({}, make_static(root), "test-desk-key-2026", "2026-10-27"),
          port(app.server().bind_to_any_port("127.0.0.1")),
          thread([this] { app.server().listen_after_bind(); }),
          client("127.0.0.1", port) {
        require(port > 0, "Could not bind local test server");
        client.set_read_timeout(3);
        client.set_connection_timeout(3);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!app.server().is_running() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
    }
    static fs::path make_static(const fs::path& path) {
        fs::create_directories(path);
        std::ofstream(path / "index.html") << "<!doctype html><title>Test Journey Desk</title>";
        std::ofstream(path / "styles.css") << "body { color: navy; }";
        std::ofstream(path / "app.js") << "'use strict';";
        std::ofstream(path / "scene.svg") << "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>";
        std::ofstream(path / "private.json") << "private booking data";
        return path;
    }
    ~TestServer() {
        app.server().stop();
        if (thread.joinable()) thread.join();
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    json post(const std::string& path, const json& body, int status = 200,
              const httplib::Headers& headers = {}) {
        auto result = client.Post(path, headers, body.dump(), "application/json");
        require(bool(result), "No HTTP response for " + path);
        require(result->status == status, path + " returned " + std::to_string(result->status));
        return json::parse(result->body);
    }
};
} // namespace

int main() {
    int passed = 0;
    int failed = 0;
    auto test = [&](const char* name, const std::function<void()>& fn) {
        try { fn(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) {
            ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    };
    TestServer server;
    test("health identifies real C++ backend", [&] {
        auto r = server.client.Get("/api/health");
        require(r && r->status == 200, "health route missing");
        require(json::parse(r->body).at("backend") == "C++20", "wrong backend");
    });
    test("static allowlist serves frontend and security headers", [&] {
        auto r = server.client.Get("/");
        require(r && r->status == 200 && r->body.find("Test Journey Desk") != std::string::npos,
                "index not served");
        require(r->get_header_value("Content-Type").find("text/html") == 0, "wrong media type");
        require(r->has_header("Content-Security-Policy"), "CSP missing");
        require(r->get_header_value("X-Content-Type-Options") == "nosniff", "nosniff missing");
        for (const auto* path : {"/static/private.json", "/data/bookings.json", "/src/main.cpp", "/vendor/json.hpp"}) {
            auto blocked = server.client.Get(path);
            require(blocked && blocked->status == 404, std::string("private file exposed: ") + path);
        }
    });
    test("catalogue and clean public journey expose no passenger details", [&] {
        auto c = server.client.Get("/api/catalog");
        require(c && c->status == 200, "catalogue missing");
        auto catalog = json::parse(c->body);
        require(catalog.at("cities").size() >= 4, "missing departure choices");
        auto r = server.client.Get("/api/journey?service_id=GJ01&date=2026-10-28&demo=false");
        require(r && r->status == 200, "journey missing");
        auto body = json::parse(r->body);
        require(body.at("counts").at("available") == 12, "normal journey was prepopulated");
        require(!body.contains("passengers") && !body.contains("waiting"), "manifest exposed publicly");
    });
    test("normal manifest requires correct desk key", [&] {
        const std::string path = "/api/desk?service_id=GJ01&date=2026-10-28&demo=false";
        auto denied = server.client.Get(path);
        require(denied && denied->status == 401, "manifest available without a key");
        auto wrong = server.client.Get(path, {{"X-Desk-Key", "wrong"}});
        require(wrong && wrong->status == 401, "wrong key accepted");
        auto allowed = server.client.Get(path, {{"X-Desk-Key", "test-desk-key-2026"}});
        require(allowed && allowed->status == 200, "correct key rejected");
    });
    test("malformed JSON and media type produce validation errors", [&] {
        auto malformed = server.client.Post("/api/book", "{broken", "application/json");
        require(malformed && malformed->status == 400, "malformed JSON accepted");
        auto array = server.client.Post("/api/book", "[]", "application/json");
        require(array && array->status == 400, "array body accepted");
        auto wrong = server.client.Post("/api/book", "{}", "text/plain");
        require(wrong && wrong->status == 415, "wrong content type accepted");
    });
    test("cross-origin mutations and unknown hosts are rejected", [&] {
        auto foreign = server.client.Post("/api/demo", {{"Origin", "https://evil.invalid"}}, "{}", "application/json");
        require(foreign && foreign->status == 403, std::string("foreign origin rejected incorrectly: ") + (foreign ? std::to_string(foreign->status) + " " + foreign->body : httplib::to_string(foreign.error())));
        auto opaque = server.client.Post("/api/demo", {{"Origin", "null"}}, "{}", "application/json");
        require(opaque && opaque->status == 403, "opaque origin accepted");
        auto cross = server.client.Post("/api/demo", {{"Sec-Fetch-Site", "cross-site"}}, "{}", "application/json");
        require(cross && cross->status == 403, "cross-site mutation accepted");
        auto badhost = server.client.Get("/api/catalog", {{"Host", "evil.invalid"}});
        require(badhost && badhost->status == 403, "unknown Host accepted");
    });
    test("same-origin JSON request is accepted", [&] {
        auto r = server.client.Post("/api/demo", {{"Origin", "http://127.0.0.1:" + std::to_string(server.port)}}, "{}", "application/json");
        require(r && r->status == 200, "valid same-origin request denied");
    });
    test("oversized request and invalid boolean are rejected", [&] {
        auto big = server.client.Post("/api/book", std::string(20000, 'x'), "application/json");
        require(big && big->status == 413, "oversized body accepted");
        auto r = server.client.Get("/api/journey?service_id=GJ01&date=2026-10-28&demo=banana");
        require(r && r->status == 400, "invalid demo parameter accepted");
    });
    test("public request cannot inject internal desk authorization", [&] {
        server.post("/api/cancel", {{"reference", "JD-FAKE"}, {"_desk_authorized", true}}, 400);
    });
    test("demo is accessible but isolated from empty normal bookings", [&] {
        auto demo = server.post("/api/demo", {{"reset", true}});
        require(demo.at("demo") == true, "demo namespace missing");
        auto r = server.client.Get("/api/desk?service_id=GJ01&date=2026-10-28&demo=true");
        require(r && r->status == 200, "demo manifest inaccessible");
        require(json::parse(r->body).at("waiting").at(0).at("passenger_id") == "P108", "wrong demo queue");
        auto regular = server.client.Get("/api/journey?service_id=GJ01&date=2026-10-28");
        require(regular && json::parse(regular->body).at("counts").at("booked") == 0, "demo polluted normal bookings");
    });
    test("booking lookup requires matching contact and retains cancellation", [&] {
        auto b = server.post("/api/book", {
            {"service_id", "GJ01"}, {"date", "2026-10-28"}, {"seat_number", 3},
            {"passenger", {{"name", "Transport Test"}, {"age", 21}, {"gender", "Other"}, {"contact", "9000000201"}}}
        });
        const auto reference = b.at("ticket").at("reference");
        auto view = server.post("/api/ticket", {{"reference", reference}, {"contact", "9000000201"}});
        require(view.at("ticket").at("seat_number") == 3, "preferred seat not allocated");
        server.post("/api/ticket", {{"reference", reference}, {"contact", "9000000202"}}, 404);
        server.post("/api/cancel", {{"reference", reference}, {"contact", "9000000202"}}, 404);
        auto cancelled = server.post("/api/cancel", {{"reference", reference}, {"contact", "9000000201"}});
        require(cancelled.at("cancelled").at("status") == "Cancelled", "cancellation status missing");
        auto history = server.post("/api/ticket", {{"reference", reference}, {"contact", "9000000201"}});
        require(history.at("ticket").at("status") == "Cancelled", "cancelled history lost");
    });
    test("desk cancellation checks both access and journey ownership", [&] {
        auto b = server.post("/api/book", {
            {"service_id", "GJ01"}, {"date", "2026-10-28"},
            {"passenger", {{"name", "Desk Test"}, {"age", 21}, {"gender", "Other"}, {"contact", "9000000203"}}}
        });
        json body = {{"reference", b.at("ticket").at("reference")}, {"service_id", "GJ01"}, {"date", "2026-10-28"}, {"demo", false}};
        server.post("/api/desk/cancel", body, 401);
        auto attack = body; attack["demo"] = true;
        server.post("/api/desk/cancel", attack, 404);
        auto r = server.post("/api/desk/cancel", body, 200, {{"X-Desk-Key", "test-desk-key-2026"}});
        require(r.at("cancelled").at("status") == "Cancelled", "authorized cancellation failed");
    });
    test("group HTTP booking returns independent tickets and honors later preferred seats", [&] {
        const json group = {{"service_id", "GJ04"}, {"date", "2026-11-02"}, {"contact", "+91 90000 00301"},
            {"passengers", json::array({
                {{"name", "Group First"}, {"age", 31}, {"gender", "Female"}},
                {{"name", "Group Second"}, {"age", 29}, {"gender", "Male"}, {"seat_number", 1}}
            })}};
        const auto result = server.post("/api/book/group", group);
        require(result.at("tickets").size() == 2 && result.at("counts") == json({{"confirmed", 2}, {"waiting", 0}}), "group receipt missing or counts wrong");
        require(result.at("total_fare") == 900, "group total fare incorrect");
        const auto& first = result.at("tickets").at(0);
        const auto& second = result.at("tickets").at(1);
        require(first.at("seat_number") == 2 && second.at("seat_number") == 1, "automatic allocation took a later explicit seat");
        require(first.at("reference") != second.at("reference") && first.at("passenger_id") != second.at("passenger_id"), "group tickets share an identity");
        require(first.at("contact") == "919000000301" && second.at("contact") == first.at("contact"), "shared contact was not normalized");
        server.post("/api/ticket", {{"reference", first.at("reference")}, {"contact", "9000000999"}}, 404);
        server.post("/api/cancel", {{"reference", first.at("reference")}, {"contact", "+91 90000 00301"}});
        const auto other = server.post("/api/ticket", {{"reference", second.at("reference")}, {"contact", "+91 90000 00301"}});
        require(other.at("ticket").at("status") == "Confirmed" && other.at("ticket").at("seat_number") == 1, "individual cancellation affected another passenger");
    });
    test("group HTTP validation reports indexed errors without partial booking", [&] {
        json group = {{"service_id", "GJ04"}, {"date", "2026-11-03"}, {"contact", "9000000302"},
            {"passengers", json::array({
                {{"name", "Valid Group Member"}, {"age", 20}, {"gender", "Other"}},
                {{"name", "Invalid Group Member"}, {"age", 0}, {"gender", "Other"}}
            })}};
        const auto invalid = server.post("/api/book/group", group, 400);
        require(invalid.at("fields").contains("passengers.1.age"), "indexed passenger validation missing");
        const auto journey = server.client.Get("/api/journey?service_id=GJ04&date=2026-11-03");
        require(journey && json::parse(journey->body).at("counts").at("booked") == 0, "invalid group partially booked");
        group["passengers"][1]["age"] = 21;
        server.post("/api/book/group", group, 403, {{"Origin", "https://evil.invalid"}});
        group["_desk_authorized"] = true;
        server.post("/api/book/group", group, 400);
        group.erase("_desk_authorized");
        group["passengers"] = json::array();
        for (int i = 0; i < 7; ++i) group["passengers"].push_back({{"name", "Member " + std::to_string(i)}, {"age", 20}, {"gender", "Other"}});
        require(server.post("/api/book/group", group, 400).at("fields").contains("passengers"), "group size bound missing");
    });
    test("group HTTP overflow joins FIFO and individual cancellation promotes exact seat", [&] {
        for (int i = 0; i < 10; ++i) server.post("/api/book", {
            {"service_id", "GJ04"}, {"date", "2026-11-04"},
            {"passenger", {{"name", "Existing Traveller " + std::to_string(i)}, {"age", 25}, {"gender", "Other"}, {"contact", "9000000303"}}}
        });
        const auto result = server.post("/api/book/group", {
            {"service_id", "GJ04"}, {"date", "2026-11-04"}, {"contact", "9000000304"},
            {"passengers", json::array({
                {{"name", "First Group Traveller"}, {"age", 20}, {"gender", "Other"}},
                {{"name", "Second Group Traveller"}, {"age", 21}, {"gender", "Other"}, {"seat_number", 11}},
                {{"name", "Third Group Traveller"}, {"age", 22}, {"gender", "Other"}}
            })}
        });
        require(result.at("counts") == json({{"confirmed", 2}, {"waiting", 1}}), "partial availability was not clearly reported");
        require(result.at("tickets").at(0).at("seat_number") == 12 && result.at("tickets").at(1).at("seat_number") == 11, "explicit group preference lost");
        const auto& waiting = result.at("tickets").at(2);
        require(waiting.at("status") == "Waiting" && waiting.at("queue_position") == 1, "overflow passenger did not enter FIFO");
        const auto cancelled = server.post("/api/cancel", {{"reference", result.at("tickets").at(1).at("reference")}, {"contact", "9000000304"}});
        require(cancelled.at("promoted") == json({{"seat_number", 11}}), "normal promotion leaked identity or assigned wrong seat");
        const auto promoted = server.post("/api/ticket", {{"reference", waiting.at("reference")}, {"contact", "9000000304"}});
        require(promoted.at("ticket").at("status") == "Confirmed" && promoted.at("ticket").at("seat_number") == 11, "waiting group ticket did not become confirmed");
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
