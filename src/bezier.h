#ifndef BEZIER_H
#define BEZIER_H

// ============================================================
// [Requisito 7] Curvas parametricas (Bezier cubica)
// Matematica pura, sem OpenGL. Usada para calcular a posicao do
// monstro no corredor a partir de um parametro t em [0, 1].
// ============================================================

struct Vector3 {
    float x, y, z;
    Vector3(float x_ = 0.0f, float y_ = 0.0f, float z_ = 0.0f) : x(x_), y(y_), z(z_) {}

    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator*(float s)          const { return Vector3(x * s, y * s, z * s); }
};

// 4 pontos de controle: p0 = inicio da curva, p3 = fim. A curva sai de p0
// em direcao a p1, chega em p3 vindo de p2, mas nao passa por p1 nem p2.
struct BezierPath {
    Vector3 p0, p1, p2, p3;
};

// B(t) = (1-t)^3 P0 + 3 (1-t)^2 t P1 + 3 (1-t) t^2 P2 + t^3 P3
Vector3 evaluateBezier(const BezierPath& path, float t);

#endif // BEZIER_H
