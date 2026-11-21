#pragma once
#include "w2c_page_ipc.h"

#include <cstring>
#include <limits>
#include <span>
#include <utility>

namespace w2c_resident {

constexpr std::uint32_t kSignature = 0x32435257; // WRC2
constexpr std::uint64_t kSegmentBytes = 256ull * 1024 * 1024;
constexpr std::uint64_t kAlignment = 65536;
constexpr std::uint32_t kTerrain = 21;
constexpr std::uint32_t kTexture = 32;

struct Entry {
    std::uint64_t offset{};
    std::uint32_t bytes{}, kind{}, key{}, reserved{};
    std::array<char, 64> source_sha{};
};
struct Header {
    std::uint32_t signature{}, entry_count{};
    std::uint64_t total_bytes{};
    std::array<char, 64> generation{};
    std::uint32_t segment_count{}, reserved{};
};
static_assert(sizeof(Entry) == 88 && sizeof(Header) == 88);

inline std::wstring name(std::wstring_view token) {
    return w2c_page_ipc::is_hex(token, 32) ? L"Local\\W2C64ResidentV2-" + std::wstring(token) : std::wstring{};
}

inline std::wstring segment_name(std::wstring_view token, std::uint64_t index) {
    auto path = name(token);
    return path.empty() ? path : path + L"-" + std::to_wstring(index);
}

inline std::uint64_t directory_bytes(std::uint32_t entries) {
    return sizeof(Header) + std::uint64_t{entries} * sizeof(Entry);
}

inline std::uint64_t aligned(std::uint64_t value) {
    return (value + kAlignment - 1) & ~(kAlignment - 1);
}

inline bool less(const Entry& left, const Entry& right) {
    return left.kind != right.kind ? left.kind < right.kind : left.key < right.key;
}

class View {
public:
    View() = default;
    View(void* address, std::size_t bytes) : address_(address), bytes_(bytes) {}
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    View(View&& other) noexcept : address_(std::exchange(other.address_, nullptr)), bytes_(other.bytes_) {}
    ~View() { if (address_) UnmapViewOfFile(address_); }
    explicit operator bool() const { return address_ != nullptr; }
    std::span<const unsigned char> span() const {
        return address_ ? std::span(static_cast<const unsigned char*>(address_), bytes_) : std::span<const unsigned char>{};
    }
private:
    void* address_{};
    std::size_t bytes_{};
};

// The host is launched by our lab harness with a single-use random name.
// Readers map the immutable directory once and just the selected asset view.
class Reader {
public:
    explicit Reader(std::wstring_view token) : mapping_(open(token)) {
        if (!mapping_) return;
        const View prefix(MapViewOfFile(mapping_.get(), FILE_MAP_READ, 0, 0, sizeof(Header)), sizeof(Header));
        if (!prefix) return;
        std::memcpy(&header_, prefix.span().data(), sizeof(Header));
        if (header_.signature != kSignature || header_.reserved || !header_.entry_count ||
            !header_.segment_count || header_.segment_count > header_.entry_count ||
            header_.total_bytes > std::uint64_t{header_.segment_count} * kSegmentBytes ||
            header_.total_bytes <= std::uint64_t{header_.segment_count - 1} * kSegmentBytes ||
            !w2c_page_ipc::is_hex(std::string_view(header_.generation.data(), 64), 64)) return;
        const auto bytes = directory_bytes(header_.entry_count);
        if (bytes > (std::numeric_limits<std::size_t>::max)()) return;
        const auto directory_size = static_cast<std::size_t>(bytes);
        const View directory(MapViewOfFile(mapping_.get(), FILE_MAP_READ, 0, 0, directory_size), directory_size);
        if (!directory) return;
        entries_.resize(header_.entry_count);
        std::memcpy(entries_.data(), directory.span().data() + sizeof(Header), entries_.size() * sizeof(Entry));
        const Entry* previous = nullptr;
        for (const auto& entry : entries_) {
            if ((previous && !less(*previous, entry)) || !entry.bytes ||
                entry.bytes > w2c_page_ipc::kMaximumBytes || entry.reserved ||
                (entry.kind != kTerrain && entry.kind != kTexture) ||
                entry.offset % kAlignment || entry.bytes > kSegmentBytes - entry.offset % kSegmentBytes ||
                entry.offset > header_.total_bytes || entry.bytes > header_.total_bytes - entry.offset) {
                entries_.clear();
                return;
            }
            previous = &entry;
        }
        for (std::uint32_t index = 0; index < header_.segment_count; ++index) {
            const auto path = segment_name(token, index);
            w2c_page_ipc::Handle segment(OpenFileMappingW(FILE_MAP_READ, FALSE, path.c_str()));
            if (!segment) {
                entries_.clear();
                segments_.clear();
                return;
            }
            segments_.push_back(std::move(segment));
        }
    }

    View map(std::string_view generation, std::uint32_t kind, std::uint32_t key, std::string_view sha) const {
        if (entries_.empty() || generation != std::string_view(header_.generation.data(), 64)) return {};
        Entry needle{};
        needle.kind = kind;
        needle.key = key;
        const auto entry = std::lower_bound(entries_.begin(), entries_.end(), needle, less);
        if (entry == entries_.end() || entry->kind != kind || entry->key != key ||
            std::string_view(entry->source_sha.data(), 64) != sha) return {};
        const auto& segment = segments_[static_cast<std::size_t>(entry->offset / kSegmentBytes)];
        const auto offset = static_cast<DWORD>(entry->offset % kSegmentBytes);
        return View(MapViewOfFile(segment.get(), FILE_MAP_READ, 0, offset, entry->bytes), entry->bytes);
    }

private:
    static HANDLE open(std::wstring_view token) {
        const auto path = name(token);
        return path.empty() ? nullptr : OpenFileMappingW(FILE_MAP_READ, FALSE, path.c_str());
    }
    w2c_page_ipc::Handle mapping_;
    Header header_{};
    std::vector<Entry> entries_;
    std::vector<w2c_page_ipc::Handle> segments_;
};

inline const Reader& reader() {
    static const Reader instance(w2c_page_ipc::token_from_environment());
    return instance;
}

} // namespace w2c_resident
