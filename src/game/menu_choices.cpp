#include "menu_choices.hpp"

#include <algorithm>
#include <cmath>

namespace dw4::game::menu {

std::vector<double> grid(double first, double last, double step) {
    std::vector<double> out;
    if (step <= 0 || last < first) return out;
    const auto count = std::size_t(std::floor((last - first) / step + 1e-9)) + 1;
    out.reserve(count);
    // (Each value from the first and a count, so 0.25 steps stay exact.)
    for (std::size_t i = 0; i < count; ++i) out.push_back(first + double(i) * step);
    return out;
}

namespace {
bool same(double a, double b) { return std::abs(a - b) < 1e-6; }
} // namespace

Placed with_foreign(const std::vector<double>& values, double current) {
    Placed placed;
    placed.values = values;
    for (std::size_t i = 0; i < values.size(); ++i)
        if (same(values[i], current)) {
            placed.index = i;
            return placed;
        }
    const auto at = std::size_t(std::lower_bound(values.begin(), values.end(), current) - values.begin());
    placed.values.insert(placed.values.begin() + std::ptrdiff_t(at), current);
    placed.index = at;
    placed.foreign = true;
    return placed;
}

double step_value(const std::vector<double>& values, double current, int steps) {
    if (values.empty() || steps == 0) return current;
    const Placed placed = with_foreign(values, current);
    long long index = (long long)placed.index + steps;
    index = std::clamp(index, 0LL, (long long)placed.values.size() - 1);
    // (A foreign value is no stop: stepping lands on a listed one.)
    if (placed.foreign && std::size_t(index) == placed.index) index += steps > 0 ? 1 : -1;
    index = std::clamp(index, 0LL, (long long)placed.values.size() - 1);
    if (placed.foreign && std::size_t(index) == placed.index) return current; // (only the foreign value: nothing to step to)
    return placed.values[std::size_t(index)];
}

std::size_t nearest_index(const std::vector<double>& values, double current) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < values.size(); ++i)
        if (std::abs(values[i] - current) < std::abs(values[best] - current) - 1e-9) best = i;
    return best;
}

} // namespace dw4::game::menu
