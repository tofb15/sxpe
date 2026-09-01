#include "sxpe/core/mapped_file.hpp"

#include "sxpe/core/caps.hpp"

#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#endif

namespace sxpe::core {

std::string MappedFile::open_error_message(bool writable) {
#ifdef _WIN32
    const DWORD e = GetLastError();
    switch (e) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return "file not found";
        case ERROR_ACCESS_DENIED:
            return writable ? "access denied — close The Sims 3 or check file permissions"
                            : "access denied";
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return writable ? "file is in use — close The Sims 3 and try again"
                            : "file is in use";
        default:
            return "open failed (Windows error " + std::to_string(e) + ")";
    }
#else
    switch (errno) {
        case ENOENT:
            return "file not found";
        case EACCES:
        case EPERM:
            return writable ? "access denied — close the game or check file permissions"
                            : "access denied";
        case EBUSY:
        case ETXTBSY:
            return "file is in use";
        default:
            return std::string("open failed: ") + std::strerror(errno);
    }
#endif
}

MappedFile& MappedFile::operator=(MappedFile&& o) noexcept {
    if (this == &o) {
        return *this;
    }
    close();
#ifdef _WIN32
    file_ = o.file_;
    map_ = o.map_;
    o.file_ = reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));
    o.map_ = nullptr;
#else
    fd_ = o.fd_;
    o.fd_ = -1;
#endif
    view_ = o.view_;
    size_ = o.size_;
    writable_ = o.writable_;
    o.view_ = nullptr;
    o.size_ = 0;
    o.writable_ = false;
    return *this;
}

#ifdef _WIN32

namespace {
HANDLE as_handle(void* p) { return static_cast<HANDLE>(p); }
const auto kInvalid = reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));
}  // namespace

Result<MappedFile> MappedFile::open(const std::filesystem::path& path, bool writable) {
    MappedFile m;
    m.writable_ = writable;
    DWORD access = GENERIC_READ | (writable ? GENERIC_WRITE : 0);
    // Writable: share read only so the game cannot write the same .nhd at the same time.
    DWORD share = FILE_SHARE_READ;
    if (!writable) {
        share |= FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    }
    m.file_ = CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (as_handle(m.file_) == INVALID_HANDLE_VALUE) {
        return std::unexpected(err(ErrorCode::io, MappedFile::open_error_message(writable)));
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(as_handle(m.file_), &sz)) {
        m.close();
        return std::unexpected(err(ErrorCode::io, "GetFileSizeEx failed"));
    }
    if (sz.QuadPart < 0 || static_cast<std::uint64_t>(sz.QuadPart) > caps::kMaxMapBytes) {
        m.close();
        return std::unexpected(err(ErrorCode::cap_exceeded, "file exceeds mmap cap"));
    }
    m.size_ = static_cast<std::uint64_t>(sz.QuadPart);
    if (m.size_ == 0) {
        return m;
    }
    DWORD prot = writable ? PAGE_READWRITE : PAGE_READONLY;
    m.map_ = CreateFileMappingW(as_handle(m.file_), nullptr, prot, 0, 0, nullptr);
    if (!m.map_) {
        m.close();
        return std::unexpected(err(ErrorCode::io, "CreateFileMapping failed"));
    }
    DWORD view_acc = writable ? FILE_MAP_WRITE : FILE_MAP_READ;
    m.view_ = MapViewOfFile(as_handle(m.map_), view_acc, 0, 0, 0);
    if (!m.view_) {
        m.close();
        return std::unexpected(err(ErrorCode::io, "MapViewOfFile failed"));
    }
    return m;
}

void MappedFile::flush() {
    if (view_) {
        FlushViewOfFile(view_, 0);
    }
    if (file_ != kInvalid) {
        FlushFileBuffers(as_handle(file_));
    }
}

void MappedFile::close() {
    if (view_) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (map_) {
        CloseHandle(as_handle(map_));
        map_ = nullptr;
    }
    if (file_ != kInvalid) {
        CloseHandle(as_handle(file_));
        file_ = kInvalid;
    }
    size_ = 0;
}

#else

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

Result<MappedFile> MappedFile::open(const std::filesystem::path& path, bool writable) {
    MappedFile m;
    m.writable_ = writable;
    m.fd_ = ::open(path.c_str(), writable ? O_RDWR : O_RDONLY);
    if (m.fd_ < 0) {
        return std::unexpected(err(ErrorCode::io, MappedFile::open_error_message(writable)));
    }
    struct stat st {};
    if (fstat(m.fd_, &st) != 0) {
        m.close();
        return std::unexpected(err(ErrorCode::io, "fstat failed"));
    }
    if (static_cast<std::uint64_t>(st.st_size) > caps::kMaxMapBytes) {
        m.close();
        return std::unexpected(err(ErrorCode::cap_exceeded, "file exceeds mmap cap"));
    }
    m.size_ = static_cast<std::uint64_t>(st.st_size);
    if (m.size_ == 0) {
        return m;
    }
    int prot = PROT_READ | (writable ? PROT_WRITE : 0);
    m.view_ = mmap(nullptr, static_cast<size_t>(m.size_), prot, MAP_SHARED, m.fd_, 0);
    if (m.view_ == MAP_FAILED) {
        m.view_ = nullptr;
        m.close();
        return std::unexpected(err(ErrorCode::io, "mmap failed"));
    }
    return m;
}

void MappedFile::flush() {
    if (view_ && view_ != MAP_FAILED) {
        msync(view_, static_cast<size_t>(size_), MS_SYNC);
    }
}

void MappedFile::close() {
    if (view_) {
        munmap(view_, static_cast<size_t>(size_));
        view_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    size_ = 0;
}

#endif

}  // namespace sxpe::core
