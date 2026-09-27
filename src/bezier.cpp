#include "bezier.h"

Vector3 evaluateBezier(const BezierPath& path, float t) {
    float u   = 1.0f - t;
    float uu  = u * u;
    float uuu = uu * u;
    float tt  = t * t;
    float ttt = tt * t;

    Vector3 result;
    result.x = uuu * path.p0.x + 3.0f * uu * t * path.p1.x
             + 3.0f * u * tt * path.p2.x + ttt * path.p3.x;
    result.y = uuu * path.p0.y + 3.0f * uu * t * path.p1.y
             + 3.0f * u * tt * path.p2.y + ttt * path.p3.y;
    result.z = uuu * path.p0.z + 3.0f * uu * t * path.p1.z
             + 3.0f * u * tt * path.p2.z + ttt * path.p3.z;
    return result;
}