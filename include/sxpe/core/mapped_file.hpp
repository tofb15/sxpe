#pragma once

#include "sxpe/error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace sxpe::core {

class MappedFile {
public:
    MappedFile() = default;
    MappedFile(MappedFile&& o) noexcept { *this = std::move(o); }
    MappedFile& operator=(MappedFile&& o) noexcept;
    ~MappedFile() { close(); }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    static Result<MappedFile> open(const std::filesystem::path& path, bool writable);

    [[nodiscard]] std::uint64_t size() const { return size_; }
    [[nodiscard]] bool writable() const { return writable_; }
    [[nodiscard]] std::span<const std::byte> bytes() const {
        return {static_cast<const std::byte*>(view_), static_cast<std::size_t>(size_)};
    }
    [[nodiscard]] std::span<std::byte> writable_bytes() {
        if (!writable_ || !view_) {
            return {};
        }
        return {static_cast<std::byte*>(view_), static_cast<std::size_t>(size_)};
    }
    void flush();

    /// Unmap view and close mapping + file (required before ReplaceFile on Windows).
    void close();

private:
#ifdef _WIN32
    void* file_{reinterpret_cast<void*>(static_cast<std::intptr_t>(-1))};
    void* map_{nullptr};
#else
    int fd_{-1};
#endif
    void* view_{nullptr};
    std::uint64_t size_{0};
    bool writable_{false};
};

}  // namespace sxpe::core
