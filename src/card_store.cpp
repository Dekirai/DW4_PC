#include "dw4/card_store.hpp"

#include "dw4/save_data.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>

namespace dw4::save {
namespace {

std::size_t slot_index(int slot) { return std::size_t(slot & 1); }

std::string path_text(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// The block count of a GCI directory entry (+0x38).
std::uint32_t gci_blocks(std::span<const std::uint8_t> head) {
    return head.size() >= kGciEntrySize ? std::uint32_t(head[0x38] << 8 | head[0x39]) : 0;
}

} // namespace

CardStore CardStore::folder(std::filesystem::path root) {
    CardStore store;
    store.kind_ = Kind::folder;
    store.root_ = std::move(root);
    return store;
}

CardStore CardStore::single_file(std::filesystem::path slot_a, std::filesystem::path slot_b) {
    CardStore store;
    store.kind_ = Kind::single_file;
    store.root_ = slot_a.parent_path();
    store.files_ = {std::move(slot_a), std::move(slot_b)};
    return store;
}

std::filesystem::path CardStore::slot_folder(int slot) const {
    if (kind_ == Kind::folder) return root_ / (slot_index(slot) == 0 ? "Card A" : "Card B");
    return files_[slot_index(slot)].parent_path();
}

std::filesystem::path CardStore::save_path(int slot) const {
    if (kind_ == Kind::folder) return slot_folder(slot) / std::string(kSaveFileName);
    return files_[slot_index(slot)];
}

bool CardStore::slot_exists(int slot) const {
    std::error_code error;
    if (kind_ == Kind::folder) return std::filesystem::is_directory(slot_folder(slot), error);
    return std::filesystem::is_regular_file(files_[slot_index(slot)], error);
}

std::optional<std::vector<std::uint8_t>> CardStore::read(int slot) const {
    std::ifstream in(save_path(slot), std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.size() < kGciEntrySize + kFileSize) return std::nullopt;
    return bytes;
}

bool CardStore::write(int slot, std::span<const std::uint8_t> gci, std::string* error) const {
    if (kind_ == Kind::folder) {
        std::error_code ec;
        std::filesystem::create_directories(slot_folder(slot), ec);
    }
    return write_file_atomic(save_path(slot), gci, kind_ == Kind::single_file, error);
}

CardUsage CardStore::usage(int slot) const {
    CardUsage usage;
    std::error_code error;
    if (kind_ == Kind::single_file) {
        std::ifstream in(save_path(slot), std::ios::binary);
        std::array<std::uint8_t, kGciEntrySize> head{};
        if (in && in.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()))) {
            usage.blocks = gci_blocks(head);
            usage.files = 1;
        }
        return usage;
    }
    // Dolphin's GCI folder card: every *.gci is a file.
    for (std::filesystem::directory_iterator it(slot_folder(slot), error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        auto extension = it->path().extension().string();
        for (auto& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (extension != ".gci") continue;
        std::ifstream in(it->path(), std::ios::binary);
        std::array<std::uint8_t, kGciEntrySize> head{};
        if (!in.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()))) continue;
        usage.blocks += gci_blocks(head);
        ++usage.files;
    }
    return usage;
}

std::string CardStore::describe(int slot) const {
    if (kind_ == Kind::folder) return path_text(slot_folder(slot)) + " (GCI folder)";
    return path_text(save_path(slot));
}

std::optional<std::vector<std::uint8_t>> read_whole_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

bool write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> bytes, bool keep_backup, std::string* error) {
    auto fail = [&](const std::string& text) {
        if (error) *error = text;
        return false;
    };
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot create " + path_text(temporary));
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!out) return fail("cannot write " + path_text(temporary));
    }
    if (keep_backup && std::filesystem::is_regular_file(path, ec)) {
        auto backup = path;
        backup += ".bak";
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::filesystem::remove(temporary, ec);
            return fail("cannot keep the old save as " + path_text(backup));
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return fail("cannot replace " + path_text(path) + ": " + ec.message());
    }
    return true;
}

bool valid_save_gci(std::span<const std::uint8_t> gci) {
    return gci.size() >= kGciEntrySize + kFileSize && read_file(gci.subspan(kGciEntrySize, kFileSize)).has_value();
}

Migration migrate_gci(const std::filesystem::path& legacy_gci, const std::filesystem::path& target, std::string* error) {
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) return Migration::target_exists;
    if (!std::filesystem::is_regular_file(legacy_gci, ec)) return Migration::no_source;
    const auto bytes = read_whole_file(legacy_gci);
    if (!bytes || !valid_save_gci(*bytes)) return Migration::invalid_source;
    if (!write_file_atomic(target, *bytes, false, error)) return Migration::failed;
    return Migration::copied;
}

const char* migration_text(Migration result) {
    switch (result) {
    case Migration::copied: return "copied";
    case Migration::target_exists: return "the save file already exists";
    case Migration::no_source: return "no old save";
    case Migration::invalid_source: return "the old save is damaged";
    case Migration::failed: return "the copy failed";
    }
    return "?";
}

std::filesystem::path backup_folder(const CardStore& store) {
    if (store.kind() == CardStore::Kind::folder) return store.root() / "backups";
    return store.save_path(0).parent_path() / "backups";
}

std::optional<std::filesystem::path> backup_save(const CardStore& store, int slot, const std::filesystem::path& folder, std::string* error) {
    const auto source = store.save_path(slot);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec)) {
        if (error) *error = "no save to back up";
        return std::nullopt;
    }
    std::filesystem::create_directories(folder, ec);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "-%04d%02d%02d-%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                  local.tm_min, local.tm_sec);
    const auto stem = source.stem().string() + (slot_index(slot) == 1 && store.kind() == CardStore::Kind::folder ? "-B" : "");
    const auto extension = source.extension().string();
    auto target = folder / (stem + stamp + extension);
    for (int n = 2; std::filesystem::exists(target, ec) && n < 100; ++n) target = folder / (stem + stamp + "-" + std::to_string(n) + extension);
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, ec);
    if (ec) {
        if (error) *error = "cannot write " + path_text(target) + ": " + ec.message();
        return std::nullopt;
    }
    return target;
}

} // namespace dw4::save
