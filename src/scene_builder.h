#ifndef SCENE_BUILDER_H
#define SCENE_BUILDER_H

// ============================================================
// [Requisito 1] Modelagem de objetos 3D com primitivas
// A sala, o corredor, a porta e a mesa sao feitos so' com quads
// (GL_QUADS, com normais explicitas pra iluminacao funcionar) e cubos
// do GLUT (glutSolidCube) esticados com glScalef.
// ============================================================

// Dimensoes do cenario (compartilhadas com main.cpp e enemy.cpp).
namespace Scene {
    const float ROOM_HALF_WIDTH = 3.0f;   // sala: x em [-3, 3]
    const float ROOM_HEIGHT     = 4.0f;   // sala: y em [0, 4]
    const float ROOM_FRONT_Z    = 2.0f;   // parede da frente, com a abertura pro corredor
    const float ROOM_BACK_Z     = 8.0f;   // parede de tras (o vigia fica perto dela)

    const float DOOR_HALF_W     = 1.5f;   // a abertura, a porta e o corredor tem a mesma largura...
    const float DOOR_HEIGHT     = 3.2f;   // ...e a mesma altura
    const float CORRIDOR_FAR_Z  = -55.0f; // fundo do corredor (onde o monstro comeca)
}

void drawRoom();              // chao, teto e paredes da sala (com a abertura na frente)
void drawCorridor();          // chao, teto e paredes do corredor escuro
void drawDesk();              // mesa com monitor e teclado
void drawDoor(float closed);  // porta de emergencia: closed = 0 (aberta) ate' 1 (fechada)

#endif // SCENE_BUILDER_H
