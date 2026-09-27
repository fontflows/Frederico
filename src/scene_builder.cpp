#include "scene_builder.h"
#include "lighting.h"
#include "bezier.h" // Vector3

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

using namespace Scene;

// Helper: um quad generico com normal explicita (evita repetir os 4
// glVertex3f + glNormal3f em toda parede/piso).
static void quad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d,
                  const Vector3& normal) {
    glNormal3f(normal.x, normal.y, normal.z);
    glVertex3f(a.x, a.y, a.z);
    glVertex3f(b.x, b.y, b.z);
    glVertex3f(c.x, c.y, c.z);
    glVertex3f(d.x, d.y, d.z);
}

void drawSecurityRoom() {
    setRoomMaterial();
    glBegin(GL_QUADS);

    // --- Chao (normal para cima) ---
    quad(Vector3(-ROOM_HALF_WIDTH, 0, ROOM_FRONT_Z),
         Vector3( ROOM_HALF_WIDTH, 0, ROOM_FRONT_Z),
         Vector3( ROOM_HALF_WIDTH, 0, ROOM_BACK_Z),
         Vector3(-ROOM_HALF_WIDTH, 0, ROOM_BACK_Z),
         Vector3(0, 1, 0));

    // --- Teto (normal para baixo) ---
    quad(Vector3(-ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_BACK_Z),
         Vector3( ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_BACK_Z),
         Vector3( ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_FRONT_Z),
         Vector3(-ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_FRONT_Z),
         Vector3(0, -1, 0));

    // --- Parede de tras (onde o vigia fica encostado; normal para -Z) ---
    quad(Vector3(-ROOM_HALF_WIDTH, 0,           ROOM_BACK_Z),
         Vector3( ROOM_HALF_WIDTH, 0,           ROOM_BACK_Z),
         Vector3( ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_BACK_Z),
         Vector3(-ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_BACK_Z),
         Vector3(0, 0, -1));

    // --- Parede esquerda (normal para +X, apontando para dentro da sala) ---
    quad(Vector3(-ROOM_HALF_WIDTH, 0,           ROOM_BACK_Z),
         Vector3(-ROOM_HALF_WIDTH, 0,           ROOM_FRONT_Z),
         Vector3(-ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_FRONT_Z),
         Vector3(-ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_BACK_Z),
         Vector3(1, 0, 0));

    // --- Parede direita (normal para -X) ---
    quad(Vector3(ROOM_HALF_WIDTH, 0,           ROOM_FRONT_Z),
         Vector3(ROOM_HALF_WIDTH, 0,           ROOM_BACK_Z),
         Vector3(ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_BACK_Z),
         Vector3(ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_FRONT_Z),
         Vector3(-1, 0, 0));

    // --- Parede frontal, EM DUAS COLUNAS + LINTEL, deixando a abertura
    //     central livre para o corredor (a "janela do vigia") ---
    // Coluna esquerda da parede frontal
    quad(Vector3(-ROOM_HALF_WIDTH, 0,           ROOM_FRONT_Z),
         Vector3(-DOORWAY_HALF_W, 0,           ROOM_FRONT_Z),
         Vector3(-DOORWAY_HALF_W, ROOM_HEIGHT, ROOM_FRONT_Z),
         Vector3(-ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_FRONT_Z),
         Vector3(0, 0, 1));
    // Coluna direita da parede frontal
    quad(Vector3(DOORWAY_HALF_W,  0,           ROOM_FRONT_Z),
         Vector3(ROOM_HALF_WIDTH, 0,           ROOM_FRONT_Z),
         Vector3(ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_FRONT_Z),
         Vector3(DOORWAY_HALF_W,  ROOM_HEIGHT, ROOM_FRONT_Z),
         Vector3(0, 0, 1));
    // Lintel acima da abertura
    quad(Vector3(-DOORWAY_HALF_W, DOORWAY_HEIGHT, ROOM_FRONT_Z),
         Vector3( DOORWAY_HALF_W, DOORWAY_HEIGHT, ROOM_FRONT_Z),
         Vector3( DOORWAY_HALF_W, ROOM_HEIGHT,    ROOM_FRONT_Z),
         Vector3(-DOORWAY_HALF_W, ROOM_HEIGHT,    ROOM_FRONT_Z),
         Vector3(0, 0, 1));

    glEnd();
}

void drawCorridor() {
    setCorridorMaterial();
    glBegin(GL_QUADS);

    // --- Chao do corredor ---
    quad(Vector3(-CORRIDOR_HALF_W, 0, CORRIDOR_FAR_Z),
         Vector3( CORRIDOR_HALF_W, 0, CORRIDOR_FAR_Z),
         Vector3( CORRIDOR_HALF_W, 0, ROOM_FRONT_Z),
         Vector3(-CORRIDOR_HALF_W, 0, ROOM_FRONT_Z),
         Vector3(0, 1, 0));

    // --- Teto do corredor ---
    quad(Vector3(-CORRIDOR_HALF_W, CORRIDOR_HEIGHT, ROOM_FRONT_Z),
         Vector3( CORRIDOR_HALF_W, CORRIDOR_HEIGHT, ROOM_FRONT_Z),
         Vector3( CORRIDOR_HALF_W, CORRIDOR_HEIGHT, CORRIDOR_FAR_Z),
         Vector3(-CORRIDOR_HALF_W, CORRIDOR_HEIGHT, CORRIDOR_FAR_Z),
         Vector3(0, -1, 0));

    // --- Parede esquerda do corredor ---
    quad(Vector3(-CORRIDOR_HALF_W, 0,               ROOM_FRONT_Z),
         Vector3(-CORRIDOR_HALF_W, 0,               CORRIDOR_FAR_Z),
         Vector3(-CORRIDOR_HALF_W, CORRIDOR_HEIGHT, CORRIDOR_FAR_Z),
         Vector3(-CORRIDOR_HALF_W, CORRIDOR_HEIGHT, ROOM_FRONT_Z),
         Vector3(1, 0, 0));

    // --- Parede direita do corredor ---
    quad(Vector3(CORRIDOR_HALF_W, 0,               CORRIDOR_FAR_Z),
         Vector3(CORRIDOR_HALF_W, 0,               ROOM_FRONT_Z),
         Vector3(CORRIDOR_HALF_W, CORRIDOR_HEIGHT, ROOM_FRONT_Z),
         Vector3(CORRIDOR_HALF_W, CORRIDOR_HEIGHT, CORRIDOR_FAR_Z),
         Vector3(-1, 0, 0));

    // --- Parede de fundo do corredor (de onde o monstro "surge") ---
    quad(Vector3(-CORRIDOR_HALF_W, 0,               CORRIDOR_FAR_Z),
         Vector3( CORRIDOR_HALF_W, 0,               CORRIDOR_FAR_Z),
         Vector3( CORRIDOR_HALF_W, CORRIDOR_HEIGHT, CORRIDOR_FAR_Z),
         Vector3(-CORRIDOR_HALF_W, CORRIDOR_HEIGHT, CORRIDOR_FAR_Z),
         Vector3(0, 0, 1));

    glEnd();
}

void drawDoor(float doorOffsetY) {
    // A porta e' um bloco de altura fixa (DOORWAY_HEIGHT). A base dela
    // interpola entre "escondida acima do teto" (aberta) e "encostada no
    // chao" (fechada), conforme o progresso do fechamento (0 a 1).
    setDoorMaterial();

    float progresso    = doorOffsetY / DOORWAY_HEIGHT;       // 0 = aberta, 1 = fechada
    float baseAberta   = ROOM_HEIGHT;                        // some acima do teto
    float baseFechada  = 0.0f;                                // encosta no chao
    float base         = baseAberta + (baseFechada - baseAberta) * progresso;
    float centerY       = base + DOORWAY_HEIGHT * 0.5f;

    glPushMatrix();
        glTranslatef(0.0f, centerY, ROOM_FRONT_Z + 0.02f);
        glScalef(DOORWAY_HALF_W * 2.0f, DOORWAY_HEIGHT, 0.08f);
        glutSolidCube(1.0);
    glPopMatrix();
}