#pragma once

#include <expected>
#include <string>
#include <utility>
#include <variant>

namespace sxpe {

enum class ErrorCode {
    io = 1,
    unsupported_game_or_format,
    protected_or_encrypted,
    cap_exceeded,
    corrupt,
    refpack,
    not_found,
    refused,
    invalid_argument,
};

struct Error {
    ErrorCode code{};
    std::string message;
};

inline Error err(ErrorCode c, std::string m) { return Error{c, std::move(m)}; }

template <class T>
using Result = std::expected<T, Error>;

using VoidResult = Result<std::monostate>;

inline VoidResult ok() { return std::monostate{}; }

}  // namespace sxpe
