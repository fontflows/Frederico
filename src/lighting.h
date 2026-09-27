#ifndef LIGHTING_H
#define LIGHTING_H

// ============================================================
// [Requisito D] Iluminacao e Materiais
// Praticamente o mesmo esquema da pratica de iluminacao da
// disciplina: luz ambiente global + uma luz (GL_LIGHT0), materiais
// via GL_COLOR_MATERIAL. A unica diferenca e' que aqui a posicao da
// luz e' atualizada a cada frame para acompanhar a camera (efeito
// de lanterna/tocha que anda junto com o vigia).
// ============================================================

// Configura luz ambiente global e os parametros base da lanterna.
void initLighting();

// Atualiza a posicao da lanterna a cada frame. Deve ser chamada logo
// apos gluLookAt() e ANTES de desenhar a cena: nesse ponto a matriz
// MODELVIEW e' exatamente a transformacao camera->mundo, entao (0,0,0)
// e' a propria posicao da camera -- a luz "gruda" na camera de graca.
void updateFlashlight(bool flashlightOn);

// Cada funcao abaixo so' troca a cor "corrente" (glColor); como
// GL_COLOR_MATERIAL esta' habilitado, isso e' o suficiente pra mudar
// o material usado na iluminacao dos proximos objetos desenhados.
void setRoomMaterial();
void setCorridorMaterial();
void setMonsterMaterial();
void setDoorMaterial();

#endif // LIGHTING_H