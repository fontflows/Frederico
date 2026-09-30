#ifndef LIGHTING_H
#define LIGHTING_H

// ============================================================
// [Requisito 6] Iluminacao
// Luz ambiente global fraca + uma luz pontual (GL_LIGHT0) que faz o
// papel de lanterna. A cor de cada objeto vem de glColor(), porque
// GL_COLOR_MATERIAL esta' ligado (glColor vira o material).
// ============================================================

// Configura a luz ambiente e os parametros da lanterna. Chamar uma vez.
void initLighting();

// Liga/desliga a lanterna e a coloca na posicao da camera. Deve ser
// chamada logo depois do gluLookAt() e antes de desenhar a cena: a
// posicao de uma luz e' transformada pela matriz MODELVIEW atual, e
// nesse ponto (0,0,0) e' o proprio olho da camera.
void updateFlashlight(bool on);

#endif // LIGHTING_H
