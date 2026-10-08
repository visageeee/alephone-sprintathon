// Saved-map object reference maintenance. GPL-3.0-or-later.
#ifndef SPRINTATHON_SURFACE_EDITOR_OBJECTS_H
#define SPRINTATHON_SURFACE_EDITOR_OBJECTS_H
#include <cstdint>
#include <set>
#include <vector>

namespace surface_editor_objects {
inline int light_level(int step, int full)
{
    return int((int64_t(full)*(9-step)+4)/9);
}

// Validate the complete chain before the engine's unchecked unlink traversal.
// Malformed map/script objects must not turn an editor click into an assertion.
template<class Valid, class Next>
bool removable(int first, int target, int count, Valid valid, Next next)
{
    bool found = false;
    for (int hops = 0; first != -1; ++hops) {
        if (hops >= count || first < 0 || first >= count || !valid(first)) return false;
        if (first == target) found = true;
        first = next(first);
    }
    return found;
}

// The geometry and sound-source lists share an index buffer. Only visit the
// sentinel-terminated sound lists, and update shared entries exactly once.
template<class Index>
void remap_sound_sources(std::vector<Index>& indexes, const std::vector<int>& starts, int removed)
{
    std::set<size_t> touched;
    for (const int start : starts) {
        if (start == -1) continue;
        for (size_t k = static_cast<uint16_t>(start); k < indexes.size() && indexes[k] != -1; ++k)
            if (touched.insert(k).second && indexes[k] > removed) --indexes[k];
    }
}
}
#endif
