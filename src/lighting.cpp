#include "lighting.h"

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

void initLighting() {
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);       // refaz o tamanho das normais depois de glScalef
    glEnable(GL_COLOR_MATERIAL);  // glColor() passa a definir o material do objeto

    // Luz ambiente global: bem fraca, so' pra cena nao ficar 100% preta.
    const GLfloat ambiente[] = { 0.12f, 0.12f, 0.15f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambiente);

    // Lanterna: luz pontual amarelada que perde forca com a distancia
    // (atenuacao = 1 / (constante + linear * distancia)).
    const GLfloat difusa[]    = { 0.9f, 0.85f, 0.7f, 1.0f };
    const GLfloat especular[] = { 0.4f, 0.4f, 0.35f, 1.0f };
    glLightfv(GL_LIGHT0, GL_AMBIENT,  ambiente);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  difusa);
    glLightfv(GL_LIGHT0, GL_SPECULAR, especular);
    glLightf (GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
    glLightf (GL_LIGHT0, GL_LINEAR_ATTENUATION,   0.05f);
}

void updateFlashlight(bool on) {
    if (!on) {
        glDisable(GL_LIGHT0);
        return;
    }
    glEnable(GL_LIGHT0);
    const GLfloat posicao[] = { 0.0f, 0.0f, 0.0f, 1.0f }; // w = 1: luz pontual
    glLightfv(GL_LIGHT0, GL_POSITION, posicao);
}
