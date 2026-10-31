#ifndef SPRINTATHON_SURFACE_CONTACT_H
#define SPRINTATHON_SURFACE_CONTACT_H
#include "SprintathonZeroGMath.h"

// Transient collision data, never saved or shared across simulation ticks.
// Distance is in world units. Normals point away from the blocking surface.
namespace SprintathonSurfaceContact {
using SprintathonZeroGMath::V;
enum class Kind { Wall, Object, Floor, Ceiling };
struct Contact {
    V normal{};
    double distance=0;
    Kind kind=Kind::Wall;
    short index=-1;
    bool valid() const { return normal.dot(normal)>.5; }
};
inline Contact make(V normal, double distance, Kind kind, short index=-1) {
    Contact c;
    const double length=normal.length();
    if(!std::isfinite(length) || length<1e-9 || !std::isfinite(distance)) return c;
    c.normal=normal*(1.0/length);c.distance=distance;c.kind=kind;c.index=index;
    return c;
}
inline bool nearerFacing(const Contact& candidate, const Contact& current, V toward, double reach) {
    return candidate.valid() && candidate.distance>=-2 && candidate.distance<=reach &&
        candidate.normal.dot(toward)<-.65 && (!current.valid() || candidate.distance<current.distance);
}
// Remove only inward velocity. Tangential and separating movement survive.
inline V slide(V velocity, const Contact& contact) {
    if(!contact.valid())return velocity;
    const double inward=velocity.dot(contact.normal);
    return inward<0 ? velocity-contact.normal*inward : velocity;
}
}
#endif
