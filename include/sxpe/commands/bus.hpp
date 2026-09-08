#pragma once

#include "sxpe/error.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sxpe::commands {

struct UiRow {
    std::uint32_t index{0};
    std::uint32_t type{0};
    std::uint32_t group{0};
    std::uint64_t instance{0};
    std::uint32_t ordinal{0};
    std::uint32_t chunk_offset{0};
    std::uint32_t file_size{0};
    std::uint32_t mem_size{0};
    std::string tag;
    std::string name;
    bool compressed{false};
    bool deleted{false};
};

struct Tool {
    std::string id;
    std::string title;
    std::string description;
    nlohmann::json input_schema;
    nlohmann::json output_schema;
    bool read_only{false};
    bool destructive{true};
    bool idempotent{false};
    bool open_world{false};
};

nlohmann::json envelope_ok(nlohmann::json data);
nlohmann::json envelope_err(const Error& e, bool retryable = false,
                            std::string_view side_effects = "none");
std::string error_name(ErrorCode c);
int exit_code_for(ErrorCode c);

class Bus {
public:
    Bus();
    ~Bus();
    Bus(Bus&&) noexcept;
    Bus& operator=(Bus&&) noexcept;

    [[nodiscard]] std::vector<Tool> tools() const;
    [[nodiscard]] nlohmann::json manifest() const;
    nlohmann::json execute(std::string_view id, const nlohmann::json& args);
    /// Full metadata snapshot for the GUI grid (no payloads, not an MCP tool).
    Result<std::vector<UiRow>> ui_index(std::string_view session_id);

    /// Optional sink for long-running command progress (merge/import/scan). Thread: caller of execute.
    using ProgressHandler = std::function<void(const nlohmann::json& event)>;
    void set_progress_handler(ProgressHandler handler);
    void clear_progress_handler();

    /// Cooperative cancel for the in-flight execute (merge/import/scan).
    /// request_cancel is async-signal-safe (atomic flag only) — safe from SIGINT.
    /// set_cancel_check is polled at package/resource boundaries (GUI wasCanceled).
    using CancelCheck = std::function<bool()>;
    void set_cancel_check(CancelCheck check);
    void clear_cancel_check();
    void request_cancel();
    void clear_cancel();
    [[nodiscard]] bool cancel_requested() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sxpe::commands
