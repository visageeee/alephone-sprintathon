#ifndef SPRINTATHON_FLASHLIGHT_STATE_H
#define SPRINTATHON_FLASHLIGHT_STATE_H
// Session state is deliberately separate from the saved availability preference.
namespace SprintathonFlashlightState {
inline bool& switchedOn() { static bool value=false; return value; }
inline void reset() { switchedOn()=false; }
inline bool toggle() { switchedOn()=!switchedOn(); return switchedOn(); }
}
#endif
