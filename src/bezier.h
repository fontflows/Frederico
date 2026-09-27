#ifndef BEZIER_H
#define BEZIER_H

// ============================================================
// [Requisito E] Curvas Parametricas (Bezier)
// Logica matematica pura, sem qualquer dependencia de OpenGL.
// Usada para calcular a posicao (X, Y, Z) do monstro no corredor
// a partir de um parametro de tempo t em [0.0, 1.0].
// ============================================================

struct Vector3 {
    float x, y, z;
    Vector3(float x_ = 0.0f, float y_ = 0.0f, float z_ = 0.0f)
        : x(x_), y(y_), z(z_) {}

    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator*(float s)          const { return Vector3(x * s, y * s, z * s); }
};

// 4 pontos de controle definindo a trajetoria serpenteante do monstro
// dentro do corredor (P0 = ponto de spawn, P3 = entrada da sala).
struct BezierPath {
    Vector3 p0, p1, p2, p3;
};

// Avalia a curva cubica de Bezier:
// B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3
Vector3 evaluateBezier(const BezierPath& path, float t);

#endif // BEZIER_H