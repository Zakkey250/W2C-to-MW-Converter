#pragma once
#include "BoundedDisk.h"

namespace w2geometry {
namespace d = w2v2disk;

struct Page {
    std::filesystem::path path;
    d::U storedBytes = 0;
    std::string digest;
    d::PageSummary summary;
    std::array<float, 6> bounds{};

    // Revalidate on every read: startup validation must not authorize a file
    // replaced later. Only the requested page occupies the streaming read budget.
    d::B Read() const {
        d::File file(path);
        d::need(file.size == storedBytes, "geometry addon page changed size");
        auto bytes = file.read(0, storedBytes, 16 * d::MiB);
        d::need(d::sha(bytes) == digest, "geometry addon page changed SHA-256");
        return bytes;
    }
};

// Section replacements are independently bound to the original packaged page,
// the material bank and the canonical section number. Asset paths are derived
// from the section number, never from arbitrary manifest strings.
template<class BaseHash>
std::map<unsigned, Page> read(const d::J& patch, const std::filesystem::path& directory,
                              const std::string& bankSha, BaseHash originalSha) {
    std::map<unsigned, Page> pages;
    if (d::uintv(patch.at("schema")) != 3) return pages;
    const auto& rows = patch.at("geometryPages");
    d::need(rows.is_array() && rows.size() <= 65535, "geometry addon page count");
    for (const auto& row : rows) {
        d::need(row.is_array() && row.size() == 3, "geometry addon page fields");
        const unsigned section = unsigned(d::uintv(row[0], 65535));
        d::need(section != 26000 && !pages.count(section), "geometry addon section");
        d::need(originalSha(section) == d::str(row[1], 64), "geometry addon original SHA-256 mismatch");
        const auto filePath = directory / ("section-" + std::to_string(section) + ".w2p");
        d::File file(filePath);
        d::need(file.size > 256 && file.size <= 16 * d::MiB, "geometry addon page budget");
        auto bytes = file.read(0, file.size, 16 * d::MiB);
        d::need(d::sha(bytes) == d::str(row[2], 64), "geometry addon replacement SHA-256 mismatch");
        auto summary = d::validateGeometryPage(bytes);
        d::need(summary.section == section && summary.bankSha == bankSha && summary.clusters &&
                summary.decodedBytes <= 32 * d::MiB, "geometry addon page identity or decoded size");
        Page page;
        page.path = std::filesystem::absolute(filePath);
        page.storedBytes = file.size;
        page.digest = d::str(row[2], 64);
        page.summary = std::move(summary);
        for (unsigned i = 0; i < 6; ++i) page.bounds[i] = d::rf(bytes, 112 + 4 * i);
        pages.emplace(section, std::move(page));
    }
    return pages;
}
}
