#ifndef SPRINTATHON_ZERO_G_CONTROL_H
#define SPRINTATHON_ZERO_G_CONTROL_H
// Per-level script override, independent of saved user preferences.
namespace SprintathonZeroGControl {
inline int& overrideValue() { static int value = -1; return value; }
inline void clear() { overrideValue() = -1; }
inline void set(bool enabled) { overrideValue() = enabled ? 1 : 0; }
inline bool effective(bool preference) {
    return overrideValue() < 0 ? preference : overrideValue() != 0;
}
}
#endif
