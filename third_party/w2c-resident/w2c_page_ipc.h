#pragma once
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace w2c_page_ipc {

constexpr std::uint32_t kSignature = 0x31504757; // WGP1
constexpr std::uint32_t kMaximumBytes = 32u * 1024u * 1024u;
constexpr std::uint32_t kMaximumSection = 65535;
constexpr DWORD kClientDeadlineMs = 80;
constexpr wchar_t kTokenVariable[] = L"W2C64_PAGE_TOKEN";

struct Request {
    std::uint32_t signature{}, section{}, bytes{}, prefix{}, nonce{};
    std::array<char, 64> generation{}, sha256{};
};
struct Reply {
    std::uint32_t signature{}, nonce{}, section{}, bytes{};
};
static_assert(sizeof(Request) == 148 && sizeof(Reply) == 16);

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value) {}
    ~Handle() { if (*this) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (*this) CloseHandle(value_);
            value_ = std::exchange(other.value_, nullptr);
        }
        return *this;
    }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return value_; }

private:
    HANDLE value_;
};

template<typename Char>
bool is_hex(std::basic_string_view<Char> value, std::size_t size) {
    return value.size() == size && std::all_of(value.begin(), value.end(), [](Char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

inline std::wstring pipe_name(std::wstring_view token) {
    return is_hex(token, 32) ? L"\\\\.\\pipe\\W2C64PageV1-" + std::wstring(token) : std::wstring{};
}

inline std::wstring stop_name(std::wstring_view token) {
    return is_hex(token, 32) ? L"Local\\W2C64PageV1-" + std::wstring(token) + L"-stop" : std::wstring{};
}

inline std::wstring token_from_environment() {
    wchar_t value[33]{};
    return GetEnvironmentVariableW(kTokenVariable, value, 33) == 32 && is_hex(std::wstring_view(value, 32), 32)
        ? std::wstring(value, 32) : std::wstring{};
}

inline DWORD remaining(ULONGLONG deadline) {
    const auto now = GetTickCount64();
    if (now >= deadline) return 0;
    const auto ms = deadline - now;
    return ms > MAXDWORD ? MAXDWORD : static_cast<DWORD>(ms);
}

inline bool complete(HANDLE pipe, OVERLAPPED& io, ULONGLONG deadline, DWORD& bytes) {
    if (WaitForSingleObject(io.hEvent, remaining(deadline)) == WAIT_OBJECT_0) {
        return GetOverlappedResult(pipe, &io, &bytes, FALSE) != FALSE;
    }
    // Drain cancellation before returning so the kernel no longer owns the stack
    // OVERLAPPED or the caller's output buffer.
    CancelIoEx(pipe, &io);
    GetOverlappedResult(pipe, &io, &bytes, TRUE);
    return false;
}

template<bool Write, typename Byte>
bool transfer(HANDLE pipe, HANDLE event, Byte* data, DWORD bytes, ULONGLONG deadline) {
    auto* cursor = reinterpret_cast<std::conditional_t<Write, const unsigned char*, unsigned char*>>(data);
    while (bytes) {
        if (!remaining(deadline) || !ResetEvent(event)) return false;
        OVERLAPPED io{};
        io.hEvent = event;
        DWORD moved{};
        BOOL immediate{};
        if constexpr (Write) immediate = WriteFile(pipe, cursor, bytes, &moved, &io);
        else immediate = ReadFile(pipe, cursor, bytes, &moved, &io);
        if (!immediate && (GetLastError() != ERROR_IO_PENDING || !complete(pipe, io, deadline, moved))) return false;
        if (!moved || moved > bytes) return false;
        cursor += moved;
        bytes -= moved;
    }
    return true;
}

inline bool valid(const Request& request) {
    return request.signature == kSignature && request.section && request.section <= kMaximumSection &&
        request.bytes && request.bytes <= kMaximumBytes && request.prefix <= request.bytes &&
        is_hex(std::string_view(request.generation.data(), request.generation.size()), 64) &&
        is_hex(std::string_view(request.sha256.data(), request.sha256.size()), 64);
}

// One request per connection; all timeouts/errors fall back to the caller's original disk read.
inline bool read_page(std::wstring_view token, std::string_view generation, std::string_view expected_sha,
                      std::uint32_t section, std::uint32_t expected_bytes, std::uint32_t prefix_bytes,
                      std::vector<unsigned char>& out) noexcept {
    out.clear();
    try {
        if (!is_hex(generation, 64) || !is_hex(expected_sha, 64)) return false;
        const auto name = pipe_name(token);
        if (name.empty()) return false;
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        Request request{kSignature, section, expected_bytes, prefix_bytes,
                        static_cast<std::uint32_t>(qpc.LowPart ^ GetCurrentThreadId() ^ GetCurrentProcessId())};
        std::copy(generation.begin(), generation.end(), request.generation.begin());
        std::copy(expected_sha.begin(), expected_sha.end(), request.sha256.begin());
        if (!valid(request)) return false;
        const auto deadline = GetTickCount64() + kClientDeadlineMs;
        auto raw = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
            OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (raw == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY && WaitNamedPipeW(name.c_str(), 4)) {
            raw = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        }
        const Handle pipe(raw);
        if (!pipe) return false;
        const Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        Reply reply{};
        if (!event || !transfer<true>(pipe.get(), event.get(), &request, sizeof(request), deadline) ||
            !transfer<false>(pipe.get(), event.get(), &reply, sizeof(reply), deadline)) return false;
        const auto requested = prefix_bytes ? prefix_bytes : expected_bytes;
        if (reply.signature != kSignature || reply.nonce != request.nonce || reply.section != section ||
            reply.bytes != requested) return false;
        out.resize(reply.bytes);
        if (transfer<false>(pipe.get(), event.get(), out.data(), reply.bytes, deadline)) {
            const auto ack = reply.nonce;
            (void)transfer<true>(pipe.get(), event.get(), &ack, sizeof(ack), deadline);
            return true;
        }
        out.clear();
    } catch (...) {
        out.clear();
    }
    return false;
}

} // namespace w2c_page_ipc
