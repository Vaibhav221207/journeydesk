#pragma once
#include "json.hpp"
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

namespace journey {
using json = nlohmann::json;
class AppError : public std::runtime_error {
public:
    int status;
    json fields;
    AppError(std::string message, int status = 400, json fields = json::object());
};
class ReservationEngine {
public:
    explicit ReservationEngine(std::filesystem::path storage_path, std::string today_override = "");
    ~ReservationEngine();
    ReservationEngine(const ReservationEngine&) = delete;
    ReservationEngine& operator=(const ReservationEngine&) = delete;
    json catalog() const;
    json search(const json& input) const;
    json journey(const json& input) const;
    json book(const json& input);
    json book_group(const json& input);
    json ticket(const json& input) const;
    json cancel(const json& input);
    json manifest(const json& input) const;
    json load_demo(const json& input);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
