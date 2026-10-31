#ifndef SPRINTATHON_ZERO_G_CONTROL_H
#define SPRINTATHON_ZERO_G_CONTROL_H
// Per-level script/console override, independent of saved user preferences.
// -1: preference, 0: off, 1: thrust, 2: surface kicks and weapon recoil only.
namespace SprintathonZeroGControl {
inline int& overrideValue() { static int value = -1; return value; }
inline void clear() { overrideValue() = -1; }
inline void set(bool enabled) { overrideValue() = enabled ? 1 : 0; }
inline void setMode(int mode) { if(mode>=0 && mode<=2)overrideValue()=mode; }
inline int effectiveMode(bool preference) {
    return overrideValue() < 0 ? (preference ? 1 : 0) : overrideValue();
}
inline bool effective(bool preference) {
    return effectiveMode(preference)!=0;
}
}
#endif
