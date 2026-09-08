#include "dw4/player_placement.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/meshes.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>
#include <limits>
#include <set>
#include <string_view>

namespace dw4 {
namespace {
constexpr std::uint32_t max_exports = 4096;
constexpr std::uint32_t max_groups = 4096;
constexpr std::uint32_t players_per_group = 4;
constexpr std::size_t max_string_bytes = 4u * 1024u * 1024u;
[[noreturn]] void fail(const std::string& reason) { throw PlayerPlacementError("Player placement: " + reason); }
void affine(const PlayerPlacementMatrix& value) {
    for (const auto element : value) if (!std::isfinite(element)) fail("point matrix contains a non-finite value");
    if (value[3] != 0 || value[7] != 0 || value[11] != 0 || value[15] != 1)
        fail("point matrix is outside the affine profile");
}
const IgbObject& exact(const IgbDocument& doc, std::uint32_t ref, std::string_view expected) {
    const auto& object = doc.object(ref);
    if (doc.metadata.object_types.at(object.type_index).name != expected)
        fail("expected exact " + std::string(expected) + " at ref " + std::to_string(ref));
    return object;
}
void field(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot,
    std::string_view expected, std::size_t size = 4) {
    const auto* value = doc.field(object,slot);
    if (!value || value->size != size || doc.metadata.field_types.at(value->type_index).name != expected)
        fail("missing or unsupported field at slot " + std::to_string(slot));
}
std::uint32_t reference(const IgbDocument& doc, const IgbObject& object, std::uint16_t slot) {
    field(doc,object,slot,"igObjectRefMetaField");
    const auto ref = doc.field_reference(object,slot);
    if (!ref) fail("required object reference is null");
    return *ref;
}
std::vector<std::optional<std::uint32_t>> list(const IgbDocument& doc, std::uint32_t ref,
    std::string_view expected, std::uint32_t limit) {
    const auto& object = exact(doc,ref,expected);
    field(doc,object,2,"igIntMetaField"); field(doc,object,3,"igIntMetaField");
    field(doc,object,4,"igMemoryRefMetaField");
    if (doc.field_u32(object,2) > limit || doc.field_u32(object,3) > limit)
        fail("list count or capacity exceeds its limit");
    return doc.object_list(ref);
}
struct Budget { std::uint32_t groups{}; std::size_t string_bytes{}; };
std::string name(const IgbDocument& doc, const IgbObject& object, Budget& budget) {
    const auto* value = doc.field(object,2);
    if (!value || doc.metadata.field_types.at(value->type_index).name != "igStringMetaField")
        fail("name has an unsupported field type");
    // Charge the stored bytes before field_string scans and trims NUL padding.
    // Allow the length word, 4,096 label bytes, terminator and alignment padding.
    if (value->size < 4 || value->size > 4104 || value->size > max_string_bytes-budget.string_bytes)
        fail("serialized name exceeds the metadata string budget");
    budget.string_bytes += value->size;
    const auto text = doc.field_string(object,2);
    if (text.size() > 4096) fail("decoded name exceeds 4,096 bytes");
    return std::string(text);
}
PlayerPlacementMatrix matrix(const IgbDocument& doc, const IgbObject& point) {
    field(doc,point,5,"igIntMetaField");
    if (doc.field_u32(point,5) & ~0x1fu) fail("point flags are outside the static profile");
    field(doc,point,9,"igIntMetaField"); field(doc,point,11,"igObjectRefMetaField");
    if (doc.field_i32(point,9) != 0 || doc.field_reference(point,11))
        fail("point transform has a nonzero target or dynamic input");
    field(doc,point,8,"igMatrix44fMetaField",64);
    const auto bytes = doc.field_bytes(*doc.field(point,8));
    PlayerPlacementMatrix result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto at = i*4;
        const auto bits = std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at+1]) << 8) |
            (std::uint32_t(bytes[at+2]) << 16) | (std::uint32_t(bytes[at+3]) << 24);
        result[i] = std::bit_cast<float>(bits);
    }
    affine(result);
    return result;
}
struct Validated {
    PlayerPlacementInfo info;
    std::vector<PlayerPlacement> points;
};
Validated descriptor(const IgbDocument& doc, std::uint32_t ref, Budget& budget) {
    Validated result; result.info.reference_index = ref;
    try {
        if (doc.metadata.header.byte_order != IgbByteOrder::little)
            fail("big-endian placement storage is unverified");
        const auto& info = exact(doc,ref,"bePoint01Info");
        result.info.name = name(doc,info,budget);
        result.info.polygon_switch_reference = reference(doc,info,7);
        exact(doc,result.info.polygon_switch_reference,"igSwitch");
        const auto groups = list(doc,reference(doc,info,8),"beGroupKeepList",max_groups-budget.groups);
        result.info.group_count = static_cast<std::uint32_t>(groups.size());
        budget.groups += result.info.group_count;
        if (groups.empty()) fail("placement descriptor has no groups");
        result.points.reserve(groups.size()*players_per_group);
        for (std::uint32_t g = 0; g < groups.size(); ++g) {
            if (!groups[g]) fail("placement group is null");
            const auto& group = exact(doc,*groups[g],"beGroupKeep");
            const auto points = list(doc,reference(doc,group,7),"igNodeList",players_per_group);
            if (points.size() != players_per_group) fail("static placement group must contain four player points");
            for (std::uint32_t p = 0; p < points.size(); ++p) {
                if (!points[p]) fail("player point is null");
                const auto& point = exact(doc,*points[p],"igTransform");
                result.points.push_back({ref,*groups[g],*points[p],g,p,name(doc,point,budget),matrix(doc,point)});
            }
        }
        result.info.supported = true;
    } catch (const std::exception& error) { result.info.diagnostic = error.what(); result.points.clear(); }
    return result;
}
std::vector<Validated> validate(const IgbDocument& doc) {
    try {
        std::vector<Validated> results;
        if (!doc.info_list_reference) return results;
        const auto exports = list(doc,*doc.info_list_reference,"igInfoList",max_exports);
        Budget budget; std::set<std::uint32_t> seen;
        for (const auto ref : exports) {
            if (!ref || !doc.is_a(doc.object(*ref),"igInfo")) fail("info list has a null or non-info export");
            if (!doc.is_a(doc.object(*ref),"bePoint01Info")) continue;
            if (!seen.insert(*ref).second) fail("info list repeats a placement descriptor");
            results.push_back(descriptor(doc,*ref,budget));
        }
        return results;
    } catch (const PlayerPlacementError&) { throw; }
    catch (const std::exception& error) { fail(error.what()); }
}
} // namespace
std::vector<PlayerPlacementInfo> list_igb_player_placements(const IgbDocument& document) {
    auto validated = validate(document);
    std::vector<PlayerPlacementInfo> result; result.reserve(validated.size());
    for (auto& item : validated) result.push_back(std::move(item.info));
    return result;
}
PlayerPlacement decode_igb_player_placement(const IgbDocument& document,
    std::uint32_t info_reference, std::uint32_t group_index, std::uint32_t player_index) {
    const auto validated = validate(document);
    const auto found = std::find_if(validated.begin(),validated.end(),[&](const auto& item) {
        return item.info.reference_index == info_reference;
    });
    if (found == validated.end()) fail("requested placement descriptor is not an explicit export");
    if (!found->info.supported) fail(found->info.diagnostic);
    if (group_index >= found->info.group_count || player_index >= players_per_group)
        fail("group or player index is outside the validated placement list");
    return found->points[group_index*players_per_group+player_index];
}
std::vector<Vec3> apply_player_placement(const PlayerPlacement& placement, std::span<const Vec3> positions) {
    if (positions.size() > 4u*1024u*1024u) fail("position count exceeds 4,194,304");
    affine(placement.matrix);
    std::vector<Vec3> result; result.reserve(positions.size());
    for (const auto position : positions) {
        const double input[]{position.x,position.y,position.z};
        for (const auto value : input) if (!std::isfinite(value)) fail("input position contains a non-finite value");
        float output[3]{};
        for (std::size_t row = 0; row < 3; ++row) {
            double value = placement.matrix[12+row];
            for (std::size_t col = 0; col < 3; ++col) value += placement.matrix[col*4+row]*input[col];
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                fail("placed position exceeds finite float range");
            output[row] = static_cast<float>(value);
        }
        result.push_back({output[0],output[1],output[2]});
    }
    return result;
}
} // namespace dw4
