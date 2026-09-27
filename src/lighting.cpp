#include "lighting.h"

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

void initLighting() {
    glShadeModel(GL_SMOOTH);
    glEnable(GL_NORMALIZE); // corrige o tamanho das normais depois de glScalef

    // Luz ambiente global fraca -- da' o clima de terror sem deixar
    // tudo completamente ilegivel.
    GLfloat luzAmbiente[] = { 0.12f, 0.12f, 0.15f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, luzAmbiente);

    // A "lanterna": uma luz pontual normal (GL_LIGHT0), igual a' da
    // pratica de iluminacao. Aqui ela so' fica mais fraca com a
    // distancia (atenuacao linear simples) pra dar a sensacao de
    // corredor escuro.
    GLfloat luzDifusa[]    = { 0.9f, 0.85f, 0.7f, 1.0f };
    GLfloat luzEspecular[] = { 0.4f, 0.4f, 0.35f, 1.0f };
    glLightfv(GL_LIGHT0, GL_AMBIENT,  luzAmbiente);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  luzDifusa);
    glLightfv(GL_LIGHT0, GL_SPECULAR, luzEspecular);
    glLightf (GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf (GL_LIGHT0, GL_LINEAR_ATTENUATION,   0.05f);

    // Cor do material vem direto do glColor() -- e' o que cada
    // set*Material() abaixo faz.
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
}

void updateFlashlight(bool flashlightOn) {
    if (!flashlightOn) {
        glDisable(GL_LIGHT0);
        return;
    }
    glEnable(GL_LIGHT0);

    // Chamada logo apos gluLookAt(): a MODELVIEW corrente e' a transformacao
    // camera->mundo, entao (0,0,0) e' a propria posicao do olho da camera.
    GLfloat posicao[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, posicao);
}

void setRoomMaterial()     { glColor3f(0.32f, 0.32f, 0.36f); }
void setCorridorMaterial() { glColor3f(0.16f, 0.16f, 0.18f); }
void setMonsterMaterial()  { glColor3f(0.55f, 0.08f, 0.07f); } // vermelho/ferrugem sinistro
void setDoorMaterial()     { glColor3f(0.22f, 0.22f, 0.25f); }