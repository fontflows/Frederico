#include "bezier.h"

Vector3 evaluateBezier(const BezierPath& path, float t) {
    float u = 1.0f - t;
    return path.p0 * (u * u * u)
         + path.p1 * (3.0f * u * u * t)
         + path.p2 * (3.0f * u * t * t)
         + path.p3 * (t * t * t);
}
