#include "core/cache_store.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "common/temp_dir.h"
#include "common/xxhash.h"

namespace vide {
namespace {

std::string hex64(uint64_t v) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(v));
    return buffer;
}

const char* codecTag(CodecId codec) {
    switch (codec) {
        case CodecId::H264: return "avc";
        case CodecId::HEVC: return "hevc";
        case CodecId::VVC: return "vvc";
        case CodecId::VP9: return "vp9";
        case CodecId::AV1: return "av1";
        case CodecId::JPEG: return "jpeg";
        case CodecId::PNG: return "png";
        case CodecId::RawVideo: return "raw";
        default: return "unk";
    }
}

}  // namespace

std::string SourceFingerprint::baseName(CodecId codec) const {
    return std::string(codecTag(codec)) + "_" + hex64(hash);
}

SourceFingerprint fingerprintFile(const std::string& path, bool full_hash) {
    SourceFingerprint fp;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return fp;
    }
    fp.size = size;

    const auto mtime = std::filesystem::last_write_time(path, ec);
    if (!ec) {
        fp.mtime = static_cast<int64_t>(mtime.time_since_epoch().count());
    }

    if (full_hash) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return fp;
        }
        XxHash64 hasher;
        std::vector<char> buffer(1 << 20);
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto got = file.gcount();
            if (got > 0) {
                hasher.update(buffer.data(), static_cast<std::size_t>(got));
            }
        }
        fp.hash = hasher.digest();
        fp.valid = true;
    }

    return fp;
}

// ---- VpkWriter -------------------------------------------------------------

struct VpkWriter::Impl {
    std::ofstream file;
    std::string path;
    VpkHeader header{};
    VpkFooter footer{};
    std::vector<VpkFrameEntry> index;
    uint64_t index_offset = sizeof(VpkHeader);  // current index location
};

VpkWriter::VpkWriter() = default;
VpkWriter::~VpkWriter() = default;

Status VpkWriter::begin(const std::string& path, CodecId codec, const SourceFingerprint& fp,
                        int width, int height, BitDepth bit_depth, uint32_t frame_count) {
    (void)frame_count;  // count is derived from appended frames
    impl_ = std::make_unique<Impl>();
    impl_->path = path;
    impl_->file.open(path, std::ios::binary | std::ios::trunc);
    if (!impl_->file) {
        return Status::error(StatusCode::IoError, "cannot create cache '" + path + "'");
    }

    impl_->header.codec = static_cast<uint8_t>(codec);
    impl_->header.bit_depth = static_cast<uint8_t>(bit_depth);
    impl_->header.width = width;
    impl_->header.height = height;
    impl_->header.frame_count = 0;
    impl_->header.source_hash = fp.hash;
    impl_->header.source_size = fp.size;
    impl_->header.source_mtime = fp.mtime;
    impl_->header.data_offset = static_cast<uint32_t>(sizeof(VpkHeader));
    impl_->header.index_offset = static_cast<uint32_t>(sizeof(VpkHeader));
    impl_->index_offset = sizeof(VpkHeader);

    impl_->file.write(reinterpret_cast<const char*>(&impl_->header), sizeof(VpkHeader));
    // Empty index => footer sits right after the header.
    impl_->file.write(reinterpret_cast<const char*>(&impl_->footer), sizeof(VpkFooter));
    impl_->file.flush();
    return impl_->file.good() ? Status::success()
                              : Status::error(StatusCode::IoError, "cache write failed");
}

Status VpkWriter::appendFrame(const FrameMeta& meta, const SyntaxTree* syntax,
                              const BlockGrid* blocks, const DecodedImage* reco,
                              const DecodedImage* pred, const DecodedImage* resi) {
    if (!impl_ || !impl_->file) {
        return Status::error(StatusCode::Internal, "writer not open");
    }

    VpkFrameEntry entry;
    entry.poc = static_cast<int32_t>(meta.poc);
    entry.is_keyframe = meta.is_keyframe ? 1 : 0;
    entry.is_ready = 1;
    entry.reserved = static_cast<uint16_t>(meta.frame_type);  // low byte = frame type

    // The new frame's data overwrites the previous index region (which always
    // starts at the current end of data).
    uint64_t offset = impl_->index_offset;
    impl_->file.seekp(static_cast<std::streamoff>(offset));

    if (syntax != nullptr) {
        const auto blob = encodeSyntaxTree(*syntax);
        entry.syntax_offset = offset;
        entry.syntax_size = static_cast<uint32_t>(blob.size());
        impl_->file.write(reinterpret_cast<const char*>(blob.data()),
                          static_cast<std::streamsize>(blob.size()));
        offset += blob.size();
    }
    if (blocks != nullptr) {
        const auto blob = encodeBlockGrid(*blocks);
        entry.block_offset = offset;
        entry.block_size = static_cast<uint32_t>(blob.size());
        impl_->file.write(reinterpret_cast<const char*>(blob.data()),
                          static_cast<std::streamsize>(blob.size()));
        offset += blob.size();
    }
    if (reco != nullptr && reco->valid()) {
        entry.reco_offset = offset;
        entry.reco_size = static_cast<uint32_t>(reco->data.size());
        impl_->file.write(reinterpret_cast<const char*>(reco->data.data()),
                          static_cast<std::streamsize>(reco->data.size()));
        offset += reco->data.size();
    }
    if (pred != nullptr && pred->valid()) {
        entry.pred_offset = offset;
        entry.pred_size = static_cast<uint32_t>(pred->data.size());
        impl_->file.write(reinterpret_cast<const char*>(pred->data.data()),
                          static_cast<std::streamsize>(pred->data.size()));
        offset += pred->data.size();
    }
    if (resi != nullptr && resi->valid()) {
        entry.resi_offset = offset;
        entry.resi_size = static_cast<uint32_t>(resi->data.size());
        impl_->file.write(reinterpret_cast<const char*>(resi->data.data()),
                          static_cast<std::streamsize>(resi->data.size()));
        offset += resi->data.size();
    }

    impl_->index.push_back(entry);
    impl_->header.frame_count = static_cast<uint32_t>(impl_->index.size());
    impl_->footer.ready_count = impl_->header.frame_count;

    // Write the index after the new data, then the header (which advertises the
    // new index location) and the footer.
    impl_->file.seekp(static_cast<std::streamoff>(offset));
    impl_->file.write(reinterpret_cast<const char*>(impl_->index.data()),
                      static_cast<std::streamsize>(impl_->index.size() * sizeof(VpkFrameEntry)));
    impl_->file.write(reinterpret_cast<const char*>(&impl_->footer), sizeof(VpkFooter));

    impl_->header.index_offset = static_cast<uint32_t>(offset);
    impl_->file.seekp(0);
    impl_->file.write(reinterpret_cast<const char*>(&impl_->header), sizeof(VpkHeader));
    impl_->index_offset = offset;

    // Flush so a concurrent reader can observe the completed append.
    impl_->file.flush();
    return impl_->file.good() ? Status::success()
                              : Status::error(StatusCode::IoError, "cache write failed");
}

Status VpkWriter::finish() {
    if (!impl_) {
        return Status::error(StatusCode::Internal, "writer not open");
    }
    impl_->footer.complete = 1;
    impl_->footer.ready_count = static_cast<uint32_t>(impl_->index.size());
    const uint64_t footer_offset =
        impl_->index_offset + impl_->index.size() * sizeof(VpkFrameEntry);
    impl_->file.seekp(static_cast<std::streamoff>(footer_offset));
    impl_->file.write(reinterpret_cast<const char*>(&impl_->footer), sizeof(VpkFooter));
    impl_->file.flush();
    impl_->file.close();
    return Status::success();
}

// ---- VpkReader -------------------------------------------------------------

Status VpkReader::open(const std::string& path) {
    path_ = path;
    header_ = VpkHeader{};
    footer_ = VpkFooter{};
    index_.clear();
    return refresh();
}

Status VpkReader::refresh() {
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        return Status::error(StatusCode::IoError, "cannot open cache '" + path_ + "'");
    }
    // Determine the current size from our own handle: std::filesystem::file_size
    // can be stale for a file that is still being written on some platforms.
    file.seekg(0, std::ios::end);
    const std::streamoff end_pos = file.tellg();
    if (end_pos < 0) {
        return Status::error(StatusCode::IoError, "cannot size cache '" + path_ + "'");
    }
    const uint64_t file_size = static_cast<uint64_t>(end_pos);

    // The writer rewrites the header last, so re-read it after the index and
    // only accept a snapshot whose header did not change underneath us.
    for (int attempt = 0; attempt < 4; ++attempt) {
        file.clear();
        file.seekg(0);
        VpkHeader header;
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(VpkHeader))) {
            return Status::error(StatusCode::ParseError, "truncated pack header");
        }
        if (header.magic != kVpkMagic || header.version != kVpkVersion) {
            return Status::error(StatusCode::ParseError, "bad pack header");
        }

        // Reject corrupt headers before allocating (a truncated/garbage pack
        // must not trigger a huge allocation).
        const uint64_t index_bytes =
            static_cast<uint64_t>(header.frame_count) * sizeof(VpkFrameEntry);
        if (header.index_offset < header.data_offset ||
            static_cast<uint64_t>(header.index_offset) + index_bytes > file_size) {
            return Status::error(StatusCode::ParseError, "corrupt pack index");
        }

        std::vector<VpkFrameEntry> index(header.frame_count);
        if (header.frame_count > 0) {
            file.clear();
            file.seekg(static_cast<std::streamoff>(header.index_offset));
            if (!file.read(reinterpret_cast<char*>(index.data()),
                           static_cast<std::streamsize>(index.size() * sizeof(VpkFrameEntry)))) {
                continue;  // index not fully written yet
            }
        }

        file.clear();
        file.seekg(0);
        VpkHeader check;
        if (!file.read(reinterpret_cast<char*>(&check), sizeof(VpkHeader)) ||
            check.frame_count != header.frame_count ||
            check.index_offset != header.index_offset) {
            continue;  // writer moved on; retry
        }

        // Validate entries against the advertised data region; a torn snapshot
        // (index region already overwritten by newer data) fails here.
        bool valid = true;
        for (const VpkFrameEntry& entry : index) {
            auto in_region = [&](uint64_t offset, uint32_t size) {
                if (size == 0) {
                    return true;
                }
                return offset >= header.data_offset &&
                       offset + size <= header.index_offset;
            };
            if (!in_region(entry.syntax_offset, entry.syntax_size) ||
                !in_region(entry.block_offset, entry.block_size) ||
                !in_region(entry.reco_offset, entry.reco_size) ||
                !in_region(entry.pred_offset, entry.pred_size) ||
                !in_region(entry.resi_offset, entry.resi_size)) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            continue;
        }

        // The footer sits right after the index and, in a consistent pack, is
        // the last thing in the file. A torn pack (killed mid-append) leaves the
        // header describing an older state; require the footer to be exactly at
        // the end and to carry the footer magic before trusting `complete`.
        VpkFooter footer;
        const uint64_t footer_offset =
            header.index_offset + index.size() * sizeof(VpkFrameEntry);
        footer.complete = 0;  // default to "incomplete" on any anomaly
        file.clear();
        file.seekg(static_cast<std::streamoff>(footer_offset));
        if (file.read(reinterpret_cast<char*>(&footer), sizeof(VpkFooter)) &&
            footer.magic == kVpkFooterMagic &&
            footer_offset + sizeof(VpkFooter) == file_size) {
            // consistent
        } else {
            footer = VpkFooter{};
            footer.complete = 0;
        }

        header_ = header;
        footer_ = footer;
        index_ = std::move(index);
        return Status::success();
    }
    return Status::error(StatusCode::ParseError, "pack is being rewritten");
}

Result<FrameMeta> VpkReader::frameMeta(uint32_t index) const {
    if (index >= index_.size()) {
        return Status::error(StatusCode::InvalidArgument, "frame index out of range");
    }
    FrameMeta meta;
    meta.index = index;
    meta.poc = index_[index].poc;
    meta.width = header_.width;
    meta.height = header_.height;
    meta.bit_depth = static_cast<BitDepth>(header_.bit_depth);
    meta.codec = static_cast<CodecId>(header_.codec);
    meta.is_keyframe = index_[index].is_keyframe != 0;
    meta.frame_type = static_cast<FrameType>(index_[index].reserved & 0xFF);
    return meta;
}

Result<SyntaxTree> VpkReader::syntax(uint32_t index) const {
    if (index >= index_.size() || index_[index].syntax_size == 0) {
        return Status::error(StatusCode::InvalidArgument, "no syntax for frame");
    }
    std::ifstream file(path_, std::ios::binary);
    std::vector<uint8_t> buffer(index_[index].syntax_size);
    file.seekg(static_cast<std::streamoff>(index_[index].syntax_offset));
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    return decodeSyntaxTree(buffer.data(), buffer.size());
}

Result<BlockGrid> VpkReader::blocks(uint32_t index) const {
    if (index >= index_.size() || index_[index].block_size == 0) {
        return Status::error(StatusCode::InvalidArgument, "no blocks for frame");
    }
    std::ifstream file(path_, std::ios::binary);
    std::vector<uint8_t> buffer(index_[index].block_size);
    file.seekg(static_cast<std::streamoff>(index_[index].block_offset));
    file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    return decodeBlockGrid(buffer.data(), buffer.size());
}

namespace {

Result<DecodedImage> readImageAt(const std::string& path, const VpkHeader& header,
                                 uint64_t offset, uint32_t size, const char* what) {
    if (size == 0) {
        return Status::error(StatusCode::InvalidArgument, std::string("no ") + what + " for frame");
    }
    std::ifstream file(path, std::ios::binary);
    DecodedImage image;
    image.width = header.width;
    image.height = header.height;
    image.format = PixelFormat::RGBA8888;
    image.bit_depth = static_cast<BitDepth>(header.bit_depth);
    image.data.resize(size);
    file.seekg(static_cast<std::streamoff>(offset));
    if (!file.read(reinterpret_cast<char*>(image.data.data()),
                   static_cast<std::streamsize>(image.data.size()))) {
        return Status::error(StatusCode::ParseError, std::string("truncated ") + what);
    }
    return image;
}

}  // namespace

Result<DecodedImage> VpkReader::reco(uint32_t index) const {
    if (index >= index_.size()) {
        return Status::error(StatusCode::InvalidArgument, "frame index out of range");
    }
    return readImageAt(path_, header_, index_[index].reco_offset, index_[index].reco_size, "reco");
}

Result<DecodedImage> VpkReader::pred(uint32_t index) const {
    if (index >= index_.size()) {
        return Status::error(StatusCode::InvalidArgument, "frame index out of range");
    }
    return readImageAt(path_, header_, index_[index].pred_offset, index_[index].pred_size, "pred");
}

Result<DecodedImage> VpkReader::resi(uint32_t index) const {
    if (index >= index_.size()) {
        return Status::error(StatusCode::InvalidArgument, "frame index out of range");
    }
    return readImageAt(path_, header_, index_[index].resi_offset, index_[index].resi_size, "resi");
}

// ---- CacheStore ------------------------------------------------------------

CacheStore::CacheStore() {
    // Precedence: explicit VIDE_CACHE_DIR, then VIDE_TMP_DIR (tests), then the
    // directory next to the executable (portable), then the temp dir.
    if (const char* cache = std::getenv("VIDE_CACHE_DIR");
        cache != nullptr && cache[0] != '\0') {
        root_ = cache;
    } else if (const char* tmp = std::getenv("VIDE_TMP_DIR");
               tmp != nullptr && tmp[0] != '\0') {
        root_ = (std::filesystem::path(tmp) / "vide_cache").string();
    } else {
        const std::filesystem::path exe_dir = executableDirectory();
        if (!exe_dir.empty()) {
            root_ = (exe_dir / "vide_cache").string();
        } else {
            root_ = (tempDirectory() / "vide_cache").string();
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    // Remove leftovers from a crashed helper before the first use.
    cleanStale();
}

CacheStore& CacheStore::instance() {
    static CacheStore store;
    return store;
}

void CacheStore::refresh() {}

uint64_t CacheStore::totalBytes() const {
    uint64_t total = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (ext == ".vpk") {
            total += entry.file_size(ec);
        }
    }
    return total;
}

uint64_t CacheStore::evictOffline(const std::vector<std::string>& in_use_names) {
    const uint64_t limit = max_total_bytes_;
    const uint64_t target = limit - limit / 10;  // 90%

    struct Pack {
        std::filesystem::path path;
        uint64_t size = 0;
        std::filesystem::file_time_type mtime;
    };
    std::vector<Pack> packs;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".vpk") {
            continue;
        }
        const std::string name = entry.path().filename().string();
        bool in_use = false;
        for (const auto& n : in_use_names) {
            if (n == name) {
                in_use = true;
                break;
            }
        }
        if (in_use) {
            continue;
        }
        packs.push_back(Pack{entry.path(), entry.file_size(ec),
                             std::filesystem::last_write_time(entry.path(), ec)});
    }

    uint64_t total = totalBytes();
    if (total <= limit) {
        return total;
    }
    // Oldest first.
    std::sort(packs.begin(), packs.end(),
              [](const Pack& a, const Pack& b) { return a.mtime < b.mtime; });
    for (const Pack& pack : packs) {
        if (total <= target) {
            break;
        }
        std::error_code rm;
        if (std::filesystem::remove(pack.path, rm)) {
            total -= pack.size;
        }
    }
    return total;
}

void CacheStore::markInUse(const std::string& pack_name) {
    for (const auto& name : in_use_) {
        if (name == pack_name) {
            return;
        }
    }
    in_use_.push_back(pack_name);
}

void CacheStore::markOffline(const std::string& pack_name) {
    in_use_.erase(std::remove(in_use_.begin(), in_use_.end(), pack_name), in_use_.end());
}

bool CacheStore::enforceLimit() {
    if (totalBytes() <= max_total_bytes_) {
        return true;
    }
    // Evict offline packs first (LRU to 90%).
    const uint64_t remaining = evictOffline(in_use_);
    // If only in-use packs remain and we are still over the limit, the caller
    // must abort the new analysis (ADR D5, plan B).
    return remaining <= max_total_bytes_;
}

uint64_t CacheStore::clearOffline() {
    uint64_t freed = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".vpk") {
            continue;
        }
        const std::string name = entry.path().filename().string();
        bool in_use = false;
        for (const auto& n : in_use_) {
            if (n == name) {
                in_use = true;
                break;
            }
        }
        if (in_use) {
            continue;
        }
        const uint64_t size = entry.file_size(ec);
        std::error_code rm;
        if (std::filesystem::remove(entry.path(), rm)) {
            freed += size;
        }
    }
    return freed;
}

uint64_t CacheStore::clearAll() {
    uint64_t freed = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".vpk") {
            continue;
        }
        const uint64_t size = entry.file_size(ec);
        std::error_code rm;
        if (std::filesystem::remove(entry.path(), rm)) {
            freed += size;
        }
    }
    return freed;
}

std::vector<std::pair<std::string, uint64_t>> CacheStore::list() const {
    std::vector<std::pair<std::string, uint64_t>> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".vpk") {
            continue;
        }
        out.emplace_back(entry.path().filename().string(), entry.file_size(ec));
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    return out;
}

bool CacheStore::removePack(const std::string& pack_name) {
    std::error_code rm;
    return std::filesystem::remove(std::filesystem::path(root_) / pack_name, rm);
}

std::vector<CacheStore::Entry> CacheStore::entries() const {
    std::vector<Entry> out;
    std::error_code ec;
    for (const auto& dir : std::filesystem::directory_iterator(root_, ec)) {
        if (!dir.is_regular_file(ec) || dir.path().extension() != ".vpk") {
            continue;
        }
        Entry e;
        e.name = dir.path().filename().string();
        const std::size_t us = e.name.find('_');
        e.codec = us == std::string::npos ? std::string("?") : e.name.substr(0, us);
        e.size = dir.file_size(ec);
        e.mtime = static_cast<int64_t>(dir.last_write_time(ec).time_since_epoch().count() / 10000000LL);
        for (const auto& n : in_use_) {
            if (n == e.name) {
                e.in_use = true;
                break;
            }
        }
        out.push_back(std::move(e));
    }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return a.size > b.size; });
    return out;
}

void CacheStore::cleanStale() {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(root_, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (ext == ".tmp" || ext == ".lock") {
            std::error_code rm;
            std::filesystem::remove(entry.path(), rm);
        }
    }
}

}  // namespace vide
