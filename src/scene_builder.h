#ifndef SCENE_BUILDER_H
#define SCENE_BUILDER_H
#include "bezier.h"

// ============================================================
// [Requisito A] Modelagem de Objetos 3D com Primitivas
// Cenario estatico desenhado manualmente com GL_QUADS e normais
// (glNormal3f) explicitas para que a iluminacao funcione.
// ============================================================

// Dimensoes globais do cenario (compartilhadas com main.cpp / enemy.cpp
// para posicionar camera, luzes e pontos de controle da Bezier).
namespace Scene {
    const float ROOM_HALF_WIDTH   = 3.0f;   // sala: x em [-3, 3]
    const float ROOM_HEIGHT       = 4.0f;   // sala: y em [0, 4]
    const float ROOM_BACK_Z       = 8.0f;   // parede de tras da sala (onde o vigia fica encostado)
    const float ROOM_FRONT_Z      = 2.0f;   // parede da frente, com a janela/abertura para o corredor
    const float DOORWAY_HALF_W    = 1.5f;   // metade da largura da abertura/porta
    const float DOORWAY_HEIGHT    = 3.2f;   // altura da abertura (abaixo do lintel)

    const float CORRIDOR_HALF_W   = 1.5f;   // corredor: x em [-1.5, 1.5]
    const float CORRIDOR_HEIGHT   = 3.2f;   // igual a DOORWAY_HEIGHT, para nao haver "degrau" no teto na juncao com a sala
    const float CORRIDOR_FAR_Z    = -55.0f; // ponto mais distante do corredor (spawn do monstro)
}

// Desenha o chao, teto, parede de tras e paredes laterais da sala de
// seguranca, alem da parede frontal com a abertura para o corredor.
void drawSecurityRoom();

// Desenha o chao, teto e paredes laterais do corredor escuro que liga
// a sala ao ponto de spawn do monstro.
void drawCorridor();

// Desenha o bloco da porta de emergencia na abertura da sala.
// doorOffsetY vai de 0 (porta totalmente aberta/recolhida no teto)
// ate DOORWAY_HEIGHT (porta totalmente fechada, tocando o chao).
void drawDoor(float doorOffsetY);

// Desenha os moveis da sala de seguranca (tapete, moldura da porta, mesa
// com computador, ventilador e poster, e armario). time e' o tempo em
// segundos e anima o ventilador e a tela do monitor. Tambem configura a
// luz do monitor, entao deve ser chamada antes das paredes.
void drawRoomProps(float time, bool monitorOn, const Vector3& monsterPos);

// Desenha o interruptor da porta, ao lado da abertura. doorClosing indica
// se a porta esta' fechada (define a posicao da alavanca e a cor do LED:
// verde = aberta, vermelho = fechada). hasPower false apaga o LED
// (bateria acabou). Deve ser chamada depois das paredes.
void drawDoorSwitch(bool doorClosing, bool hasPower);

#endif // SCENE_BUILDER_H