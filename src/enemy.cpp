#include "enemy.h"
#include "lighting.h"
#include "scene_builder.h"

#include <cmath>

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

namespace {
    // Dimensoes do corpo (metros aproximados)
    const float UPPER_LEG_LEN = 0.55f;
    const float LOWER_LEG_LEN = 0.55f;
    const float HIP_Y         = UPPER_LEG_LEN + LOWER_LEG_LEN;      // altura do quadril
    const float TORSO_HEIGHT  = 1.15f;
    const float TORSO_WIDTH   = 0.85f;
    const float TORSO_DEPTH   = 0.45f;
    const float SHOULDER_Y    = HIP_Y + TORSO_HEIGHT;               // topo do tronco
    const float UPPER_ARM_LEN = 0.5f;
    const float LOWER_ARM_LEN = 0.5f;
    const float HEAD_RADIUS   = 0.32f;

    const float DEG2RAD = 3.14159265f / 180.0f;

    // Desenha um segmento de membro (cubo alongado) "pendurado" a
    // partir da origem corrente da matriz, estendendo-se para -Y.
    void drawLimbSegment(float length, float thickness) {
        glPushMatrix();
            glTranslatef(0.0f, -length * 0.5f, 0.0f);
            glPushMatrix();
                glScalef(thickness, length, thickness);
                glutSolidCube(1.0);
            glPopMatrix();
        glPopMatrix();
    }

    // Uma perna completa: quadril -> coxa -> joelho -> canela.
    // hipSwingDeg / kneeBendDeg controlam a caminhada mecanica.
    void drawLeg(float hipOffsetX, float hipSwingDeg, float kneeBendDeg) {
        glPushMatrix();
            glTranslatef(hipOffsetX, HIP_Y, 0.0f);
            glRotatef(hipSwingDeg, 1.0f, 0.0f, 0.0f);       // balanco da coxa (frente/tras)
            drawLimbSegment(UPPER_LEG_LEN, 0.30f);

            glTranslatef(0.0f, -UPPER_LEG_LEN, 0.0f);       // desce ate o joelho
            glRotatef(kneeBendDeg, 1.0f, 0.0f, 0.0f);        // dobra do joelho (so' para frente)
            drawLimbSegment(LOWER_LEG_LEN, 0.25f);
        glPopMatrix();
    }

    // Um braco completo: ombro -> braco -> cotovelo -> antebraco.
    void drawArm(float shoulderOffsetX, float shoulderSwingDeg, float elbowBendDeg) {
        glPushMatrix();
            glTranslatef(shoulderOffsetX, SHOULDER_Y - 0.1f, 0.0f);
            glRotatef(shoulderSwingDeg, 1.0f, 0.0f, 0.0f);
            drawLimbSegment(UPPER_ARM_LEN, 0.24f);

            glTranslatef(0.0f, -UPPER_ARM_LEN, 0.0f);
            glRotatef(elbowBendDeg, 1.0f, 0.0f, 0.0f);
            drawLimbSegment(LOWER_ARM_LEN, 0.20f);
        glPopMatrix();
    }
}

namespace Enemy {

BezierPath getPath() {
    BezierPath path;
    // P0: ponto de spawn, no fundo do corredor.
    path.p0 = Vector3(0.6f,  0.0f, Scene::CORRIDOR_FAR_Z);
    // P1, P2: pontos intermediarios fora do eixo central, criando o
    // movimento "serpenteante" (zig-zag) ao longo do corredor.
    path.p1 = Vector3(-1.1f, 0.0f, Scene::CORRIDOR_FAR_Z * 0.66f);
    path.p2 = Vector3(1.1f,  0.0f, Scene::CORRIDOR_FAR_Z * 0.28f);
    // P3: entrada da sala (abertura), de onde o jumpscare comeca.
    path.p3 = Vector3(0.0f,  0.0f, Scene::ROOM_FRONT_Z + 0.3f);
    return path;
}

void draw(const Vector3& position, float facingYawDeg, float walkTime, float mouthOpen) {
    setMonsterMaterial();

    // Caminhada mecanica: pernas/bracos em contra-fase, como pedido no
    // enunciado (senoides simples aplicadas em glRotatef).
    float legSwing = 28.0f * sinf(walkTime * 3.2f);
    float kneeBend = 18.0f * (0.5f + 0.5f * sinf(walkTime * 3.2f + 1.2f)); // so' dobra p/ frente
    float armSwing = 22.0f * sinf(walkTime * 3.2f + 3.14159f); // bracos opostos as pernas
    float bodySway = 4.0f  * sinf(walkTime * 1.6f);            // balanco sinistro do tronco

    glPushMatrix();
        glTranslatef(position.x, position.y, position.z);
        glRotatef(facingYawDeg, 0.0f, 1.0f, 0.0f);
        glRotatef(bodySway, 0.0f, 0.0f, 1.0f); // leve inclinacao lateral, "serpenteante"

        // --- Tronco ---
        glPushMatrix();
            glTranslatef(0.0f, HIP_Y + TORSO_HEIGHT * 0.5f, 0.0f);
            glPushMatrix();
                glScalef(TORSO_WIDTH, TORSO_HEIGHT, TORSO_DEPTH);
                glutSolidCube(1.0);
            glPopMatrix();
        glPopMatrix();

        // --- Cabeca + mandibula (jumpscare abre a boca) ---
        glPushMatrix();
            glTranslatef(0.0f, SHOULDER_Y + HEAD_RADIUS * 0.9f, 0.0f);
            glutSolidSphere(HEAD_RADIUS, 16, 12);

            // Mandibula: gira para baixo/frente conforme mouthOpen
            glPushMatrix();
                glTranslatef(0.0f, -HEAD_RADIUS * 0.35f, HEAD_RADIUS * 0.55f);
                glRotatef(mouthOpen * 55.0f, 1.0f, 0.0f, 0.0f);
                glTranslatef(0.0f, -HEAD_RADIUS * 0.3f, HEAD_RADIUS * 0.1f);
                glScalef(HEAD_RADIUS * 1.3f, HEAD_RADIUS * 0.5f, HEAD_RADIUS * 0.9f);
                glutSolidCube(1.0);
            glPopMatrix();
        glPopMatrix();

        // --- Pernas (em contra-fase entre si) ---
        drawLeg(-0.22f,  legSwing, kneeBend);
        drawLeg( 0.22f, -legSwing, -kneeBend + 36.0f); // fase oposta

        // --- Bracos (em contra-fase com as pernas do mesmo lado) ---
        drawArm(-TORSO_WIDTH * 0.5f - 0.05f, -armSwing, 15.0f);
        drawArm( TORSO_WIDTH * 0.5f + 0.05f,  armSwing, 15.0f);

    glPopMatrix();
}

} // namespace Enemy