#ifndef ENEMY_H
#define ENEMY_H

#include "bezier.h"

// ============================================================
// O monstro (Frederico).
//  [Requisito 1] modelado so' com primitivas (cubos e uma esfera);
//  [Requisito 2] hierarquia de transformacoes: a canela gira junto
//                com a coxa, o antebraco junto com o braco etc.;
//  [Requisito 7] a trajetoria dele no corredor e' uma curva de Bezier.
// ============================================================
namespace Enemy {

    // Os 4 pontos de controle da curva que ele percorre no corredor.
    BezierPath getPath();

    // Desenha o monstro com os pes em `position`, virado `facingYawDeg`
    // graus em torno de Y (0 = olhando pra -Z, 180 = olhando pra +Z, ou
    // seja, pro vigia). `walkTime` alimenta a animacao da caminhada e
    // `mouthOpen` (0..1) abre a mandibula (usado no susto final).
    void draw(const Vector3& position, float facingYawDeg, float walkTime, float mouthOpen);

} // namespace Enemy

#endif // ENEMY_H
