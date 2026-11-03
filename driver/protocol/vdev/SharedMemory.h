#pragma once

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

namespace spacer::vdev {

/// A mapped POSIX shared-memory object (RAII). Not real-time safe: create,
/// open and close on a non-audio thread.
class SharedMemory {
public:
    SharedMemory() = default;
    ~SharedMemory() { close(); }
    SharedMemory(SharedMemory&& other) noexcept { *this = std::move(other); }
    SharedMemory& operator=(SharedMemory&& other) noexcept {
        if (this != &other) {
            close();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    /// Producer: replaces any existing object called `name` with a fresh,
    /// zero-filled one of `bytes`, readable and writable by every user (the
    /// driver runs as _coreaudiod, the app as the logged-in user). Returns an
    /// empty mapping on failure; `error` gets the reason.
    static SharedMemory create(const char* name, std::size_t bytes, std::string* error = nullptr) {
        ::shm_unlink(name);
        const int fd = ::shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd < 0) return fail("shm_open(create)", error);
        // The umask may have masked the mode; the app must be able to write readIndex.
        ::fchmod(fd, 0666);
        if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
            const int e = errno;
            ::close(fd);
            ::shm_unlink(name);
            errno = e;
            return fail("ftruncate", error);
        }
        return map(fd, bytes, error);
    }

    /// Consumer: maps an existing object read/write. Returns an empty mapping
    /// if it does not exist (driver not installed / not loaded).
    static SharedMemory open(const char* name, std::string* error = nullptr) {
        const int fd = ::shm_open(name, O_RDWR, 0);
        if (fd < 0) return fail("shm_open", error);
        struct stat st {};
        if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
            ::close(fd);
            if (error) *error = "shared memory object has no size";
            return {};
        }
        return map(fd, static_cast<std::size_t>(st.st_size), error);
    }

    static void unlink(const char* name) noexcept { ::shm_unlink(name); }

    void close() noexcept {
        if (data_ != nullptr) ::munmap(data_, size_);
        data_ = nullptr;
        size_ = 0;
    }

    bool valid() const noexcept { return data_ != nullptr; }
    void* data() const noexcept { return data_; }
    std::size_t size() const noexcept { return size_; }

private:
    static SharedMemory fail(const char* what, std::string* error) {
        if (error) *error = std::string(what) + ": " + std::strerror(errno);
        return {};
    }

    static SharedMemory map(int fd, std::size_t bytes, std::string* error) {
        void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        const int e = errno;
        ::close(fd);
        if (p == MAP_FAILED) {
            errno = e;
            return fail("mmap", error);
        }
        SharedMemory m;
        m.data_ = p;
        m.size_ = bytes;
        return m;
    }

    void* data_{nullptr};
    std::size_t size_{0};
};

}  // namespace spacer::vdev
