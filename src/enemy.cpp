#include "enemy.h"
#include "scene_builder.h"

#include <cmath>

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

namespace {
    // Medidas do corpo (aproximadamente em metros).
    const float LEG_LEN      = 0.55f;                 // coxa e canela
    const float ARM_LEN      = 0.50f;                 // braco e antebraco
    const float HIP_Y        = 2.0f * LEG_LEN;        // altura do quadril
    const float TORSO_HEIGHT = 1.15f;
    const float TORSO_WIDTH  = 0.85f;
    const float TORSO_DEPTH  = 0.45f;
    const float SHOULDER_Y   = HIP_Y + TORSO_HEIGHT;  // topo do tronco
    const float HEAD_RADIUS  = 0.32f;
    const float JAW_OPEN_MAX_DEG = 42.0f;             // abertura da mandibula com mouthOpen = 1

    // Um segmento de membro: cubo esticado, "pendurado" a partir da
    // origem atual e estendendo-se para baixo (-Y).
    void drawSegment(float length, float thickness) {
        glPushMatrix();
            glTranslatef(0.0f, -length * 0.5f, 0.0f);
            glScalef(thickness, length, thickness);
            glutSolidCube(1.0);
        glPopMatrix();
    }

    // Um membro de 2 segmentos (perna = coxa + canela, braco = braco + antebraco).
    // E' aqui que esta' a HIERARQUIA: depois de desenhar o 1o segmento,
    // andamos ate' a ponta dele e giramos a articulacao (joelho/cotovelo);
    // o 2o segmento herda tudo que foi feito antes, entao acompanha o 1o.
    void drawLimb(float x, float y, float swingDeg, float bendDeg,
                  float length, float thickness) {
        glPushMatrix();
            glTranslatef(x, y, 0.0f);                 // articulacao (quadril / ombro)
            glRotatef(swingDeg, 1.0f, 0.0f, 0.0f);    // balanco pra frente e pra tras
            drawSegment(length, thickness);

            glTranslatef(0.0f, -length, 0.0f);        // desce ate' a articulacao seguinte
            glRotatef(bendDeg, 1.0f, 0.0f, 0.0f);     // dobra do joelho / cotovelo
            drawSegment(length, thickness * 0.85f);
        glPopMatrix();
    }

    void setMonsterColor() { glColor3f(0.55f, 0.08f, 0.07f); } // vermelho ferrugem
}

namespace Enemy {

BezierPath getPath() {
    BezierPath path;
    path.p0 = Vector3( 0.6f, 0.0f, Scene::CORRIDOR_FAR_Z);         // inicio: fundo do corredor
    path.p1 = Vector3(-1.1f, 0.0f, Scene::CORRIDOR_FAR_Z * 0.66f); // p1 e p2 ficam fora do eixo central e
    path.p2 = Vector3( 1.1f, 0.0f, Scene::CORRIDOR_FAR_Z * 0.28f); // fazem a curva serpentear (zig-zag)
    path.p3 = Vector3( 0.0f, 0.0f, Scene::ROOM_FRONT_Z + 0.3f);    // fim: a porta da sala
    return path;
}

void draw(const Vector3& position, float facingYawDeg, float walkTime, float mouthOpen) {
    setMonsterColor();

    // Caminhada: senoides do tempo aplicadas nos angulos das articulacoes.
    // Pernas e bracos balancam em sentidos opostos.
    float s    = sinf(walkTime * 3.2f);
    float leg  = 28.0f * s;
    float knee = 18.0f * (0.5f + 0.5f * sinf(walkTime * 3.2f + 1.2f)); // so' dobra pra frente
    float arm  = -22.0f * s;

    glPushMatrix();
        // Raiz da hierarquia: tudo abaixo e' desenhado relativo ao monstro.
        glTranslatef(position.x, position.y, position.z);
        glRotatef(facingYawDeg, 0.0f, 1.0f, 0.0f);

        // --- Tronco ---
        glPushMatrix();
            glTranslatef(0.0f, HIP_Y + TORSO_HEIGHT * 0.5f, 0.0f);
            glScalef(TORSO_WIDTH, TORSO_HEIGHT, TORSO_DEPTH);
            glutSolidCube(1.0);
        glPopMatrix();

        // --- Cabeca + mandibula ---
        glPushMatrix();
            glTranslatef(0.0f, SHOULDER_Y + HEAD_RADIUS * 0.9f, 0.0f);
            glutSolidSphere(HEAD_RADIUS, 16, 12);

            // ATENCAO: o monstro olha pra -Z no referencial local, entao a
            // FRENTE do rosto e' -Z (o +Z e' a nuca).

            // Interior da boca: caixa escura cuja altura acompanha a
            // abertura (fechada = so' um risco fino, a linha da boca).
            float gap = HEAD_RADIUS * (0.02f + 0.42f * mouthOpen);
            glColor3f(0.04f, 0.0f, 0.0f);
            glPushMatrix();
                glTranslatef(0.0f, -HEAD_RADIUS * 0.40f - gap * 0.5f, -HEAD_RADIUS * 0.75f);
                glScalef(HEAD_RADIUS * 0.95f, gap, HEAD_RADIUS * 0.6f);
                glutSolidCube(1.0);
            glPopMatrix();
            setMonsterColor();

            // Mandibula: a dobradica fica embaixo/atras do rosto e o queixo
            // se estende pra frente (-Z). Girar em X com angulo NEGATIVO
            // leva o que esta' em -Z pra baixo, abrindo a boca.
            glPushMatrix();
                glTranslatef(0.0f, -HEAD_RADIUS * 0.45f, -HEAD_RADIUS * 0.15f); // dobradica
                glRotatef(-mouthOpen * JAW_OPEN_MAX_DEG, 1.0f, 0.0f, 0.0f);
                glTranslatef(0.0f, -HEAD_RADIUS * 0.12f, -HEAD_RADIUS * 0.45f); // centro do queixo
                glScalef(HEAD_RADIUS * 1.1f, HEAD_RADIUS * 0.24f, HEAD_RADIUS * 1.0f);
                glutSolidCube(1.0);
            glPopMatrix();
        glPopMatrix();

        // --- Pernas (em fases opostas) e bracos ---
        drawLimb(-0.22f, HIP_Y,  leg, knee,          LEG_LEN, 0.30f);
        drawLimb( 0.22f, HIP_Y, -leg, -knee + 36.0f, LEG_LEN, 0.30f);
        drawLimb(-TORSO_WIDTH * 0.5f - 0.05f, SHOULDER_Y - 0.1f, -arm, 15.0f, ARM_LEN, 0.24f);
        drawLimb( TORSO_WIDTH * 0.5f + 0.05f, SHOULDER_Y - 0.1f,  arm, 15.0f, ARM_LEN, 0.24f);
    glPopMatrix();
}

} // namespace Enemy
