#include "scene_builder.h"

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

using namespace Scene;

// ---------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------

// Um retangulo (quad) em cada um dos 3 tipos de plano. O parametro "n"
// e' o sentido da NORMAL (+1 ou -1): o vetor perpendicular a' superficie,
// que a iluminacao usa pra saber o quanto a face esta' virada pra luz.
// As paredes apontam a normal PARA DENTRO, pro lado de quem olha.

// plano z = z (paredes de frente e de fundo)
static void wallZ(float x0, float x1, float y0, float y1, float z, float n) {
    glBegin(GL_QUADS);
        glNormal3f(0.0f, 0.0f, n);
        glVertex3f(x0, y0, z); glVertex3f(x1, y0, z);
        glVertex3f(x1, y1, z); glVertex3f(x0, y1, z);
    glEnd();
}

// plano x = x (paredes laterais)
static void wallX(float z0, float z1, float y0, float y1, float x, float n) {
    glBegin(GL_QUADS);
        glNormal3f(n, 0.0f, 0.0f);
        glVertex3f(x, y0, z0); glVertex3f(x, y0, z1);
        glVertex3f(x, y1, z1); glVertex3f(x, y1, z0);
    glEnd();
}

// plano y = y (chao e teto)
static void wallY(float x0, float x1, float z0, float z1, float y, float n) {
    glBegin(GL_QUADS);
        glNormal3f(0.0f, n, 0.0f);
        glVertex3f(x0, y, z0); glVertex3f(x1, y, z0);
        glVertex3f(x1, y, z1); glVertex3f(x0, y, z1);
    glEnd();
}

// Um "tubo" retangular: chao, teto e as duas paredes laterais, entre
// zMin e zMax. A sala e o corredor sao o mesmo tubo com medidas diferentes.
static void hall(float halfW, float height, float zMin, float zMax) {
    wallY(-halfW, halfW, zMin, zMax, 0.0f,   +1.0f); // chao: normal pra cima
    wallY(-halfW, halfW, zMin, zMax, height, -1.0f); // teto: normal pra baixo
    wallX(zMin, zMax, 0.0f, height, -halfW,  +1.0f); // parede esquerda: normal pra +X
    wallX(zMin, zMax, 0.0f, height, +halfW,  -1.0f); // parede direita: normal pra -X
}

// Caixa centrada em (cx,cy,cz) com tamanho (sx,sy,sz): um cubo unitario
// do GLUT esticado (glScalef) e movido (glTranslatef).
static void box(float cx, float cy, float cz, float sx, float sy, float sz) {
    glPushMatrix();
        glTranslatef(cx, cy, cz);
        glScalef(sx, sy, sz);
        glutSolidCube(1.0);
    glPopMatrix();
}

// ---------------------------------------------------------------
// Cenario
// ---------------------------------------------------------------

void drawRoom() {
    const float W = ROOM_HALF_WIDTH, H = ROOM_HEIGHT;
    glColor3f(0.32f, 0.32f, 0.36f);
    hall(W, H, ROOM_FRONT_Z, ROOM_BACK_Z);
    wallZ(-W, W, 0.0f, H, ROOM_BACK_Z, -1.0f);            // parede de tras (normal pra -Z)

    // Parede da frente com a abertura no meio: duas colunas + uma viga em cima.
    wallZ(-W, -DOOR_HALF_W, 0.0f, H, ROOM_FRONT_Z, +1.0f);
    wallZ(DOOR_HALF_W, W,   0.0f, H, ROOM_FRONT_Z, +1.0f);
    wallZ(-DOOR_HALF_W, DOOR_HALF_W, DOOR_HEIGHT, H, ROOM_FRONT_Z, +1.0f);
}

void drawCorridor() {
    glColor3f(0.16f, 0.16f, 0.18f);
    hall(DOOR_HALF_W, DOOR_HEIGHT, CORRIDOR_FAR_Z, ROOM_FRONT_Z);
    wallZ(-DOOR_HALF_W, DOOR_HALF_W, 0.0f, DOOR_HEIGHT, CORRIDOR_FAR_Z, +1.0f); // fundo
}

void drawDoor(float closed) {
    // A porta e' um bloco que sobe e desce. Aberta: escondida acima do
    // teto. Fechada: encostada no chao. "closed" (0..1) interpola entre os dois.
    float baseY = (1.0f - closed) * ROOM_HEIGHT;
    glColor3f(0.22f, 0.22f, 0.25f);
    box(0.0f, baseY + DOOR_HEIGHT * 0.5f, ROOM_FRONT_Z + 0.02f,
        DOOR_HALF_W * 2.0f, DOOR_HEIGHT, 0.08f);
}

void drawDesk() {
    glPushMatrix();
    // TRANSFORMACAO: tudo abaixo e' desenhado no sistema de coordenadas
    // da mesa ("+Z e' a frente da mesa"). Um translate + um rotate
    // posicionam o conjunto inteiro, encostado na parede esquerda.
    glTranslatef(-ROOM_HALF_WIDTH + 0.38f, 0.0f, (ROOM_FRONT_Z + ROOM_BACK_Z) * 0.5f);
    glRotatef(90.0f, 0.0f, 1.0f, 0.0f);

    glColor3f(0.30f, 0.20f, 0.12f);                     // madeira
    box(0.0f, 0.725f, 0.0f, 1.5f, 0.05f, 0.7f);         // tampo (topo em y = 0.75)
    for (int i = -1; i <= 1; i += 2)                    // 4 pes, um em cada canto
        for (int j = -1; j <= 1; j += 2)
            box(0.70f * i, 0.35f, 0.30f * j, 0.06f, 0.70f, 0.06f);

    // Monitor e teclado ficam "em cima" da mesa: sao filhos dela, entao
    // se movem junto se a mesa for movida.
    glColor3f(0.60f, 0.58f, 0.50f);
    box(0.0f, 0.80f, -0.12f, 0.10f, 0.10f, 0.10f);      // pe do monitor
    box(0.0f, 1.04f, -0.12f, 0.48f, 0.38f, 0.34f);      // corpo do monitor
    glColor3f(0.10f, 0.10f, 0.11f);
    box(0.0f, 0.765f, 0.22f, 0.42f, 0.03f, 0.14f);      // teclado

    // Tela acesa: desligamos a iluminacao pra ela manter a cor mesmo no
    // escuro (como uma tela de verdade, que emite luz propria).
    glDisable(GL_LIGHTING);
    glColor3f(0.05f, 0.35f, 0.25f);
    glBegin(GL_QUADS);
        glVertex3f(-0.19f, 0.90f, 0.052f); glVertex3f(0.19f, 0.90f, 0.052f);
        glVertex3f( 0.19f, 1.18f, 0.052f); glVertex3f(-0.19f, 1.18f, 0.052f);
    glEnd();
    glEnable(GL_LIGHTING);

    glPopMatrix();
}
