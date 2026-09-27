#ifndef ENEMY_H
#define ENEMY_H

#include "bezier.h"

// ============================================================
// [Requisito A/B/F] O "Animatronico": modelo hierarquico feito de
// primitivas do GLUT, montado com glPushMatrix/glPopMatrix e
// transformacoes (translate/rotate/scale) para caminhada mecanica.
// ============================================================
namespace Enemy {

    // Os 4 pontos de controle (P0..P3) da trajetoria serpenteante do
    // monstro dentro do corredor [Requisito E].
    BezierPath getPath();

    // Desenha o monstro na posicao mundial `position` (pes no chao),
    // virado para `facingYawDeg` graus (0 = olhando para -Z).
    // `walkTime` alimenta os senos/cossenos da caminhada mecanica.
    // `mouthOpen` (0..1) abre a mandibula -- usado no jumpscare final.
    void draw(const Vector3& position, float facingYawDeg, float walkTime, float mouthOpen);

} // namespace Enemy

#endif // ENEMY_H