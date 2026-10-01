#include "enemy.h"
#include "scene_builder.h"

#include <cmath>

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

// ============================================================
// O FREDERICO
//
// Um urso animatronico velho e estragado, montado so' com primitivas do
// GLUT (cubos, esferas achatadas, cones, cilindros). Detalhes:
//   - cartola torta, gravata borboleta, orelhas, sobrancelhas furiosas
//   - olhos fundos em orbitas escuras, com pupilas vermelhas
//   - boca cheia de dentes afiados e presas (cones), lingua, mandibula
//     que nunca fecha direito (tremor de dentes)
//   - sangue: manchas no peito, rastros escorrendo da boca, gotas
//     animadas pingando do queixo, garras e maos sujas
//   - o corpo "rasgado": costelas e coluna de metal expostas no peito
//     com fios balancando, e um braco e uma perna sem pele (endoesqueleto)
//   - animacao: caminhada com balanco, corcunda, cabeca que tremula e
//     da' "tranco" de vez em quando; no jumpscare ele se joga pra frente
//     com os bracos esticados e a cabeca encarando a camera
//
// A hierarquia de transformacoes (push/pop) segue a estrutura do corpo:
//   raiz -> pernas
//        -> tronco inclinado -> (tronco, braços, cabeca -> mandibula, chapeu)
// ============================================================

namespace {
    // ---------------------------------------------------------------
    // Medidas do corpo (aproximadamente em metros)
    // ---------------------------------------------------------------
    const float LEG_LEN      = 0.55f;                 // coxa e canela
    const float ARM_LEN      = 0.50f;                 // braco e antebraco
    const float HIP_Y        = 2.0f * LEG_LEN;        // altura do quadril
    const float TORSO_HEIGHT = 1.15f;
    const float TORSO_WIDTH  = 0.85f;
    const float TORSO_DEPTH  = 0.45f;
    const float SHOULDER_Y   = HIP_Y + TORSO_HEIGHT;  // topo do tronco
    const float HEAD_RADIUS  = 0.32f;
    const float HAT_HEIGHT   = 0.26f;

    const float DEG2RAD           = 3.14159265f / 180.0f;
    const float JAW_OPEN_MAX_DEG  = 50.0f;  // abertura da mandibula com a boca toda aberta
    const float JAW_CHATTER       = 0.12f;  // o quanto a mandibula "treme" mesmo com a boca fechada

    // Ajustes rapidos de visual
    const float MONSTER_SCALE     = 1.0f;   // escala geral (diminua se a cartola bater no teto)
    // Brilho dos olhos no escuro, de 0 a 1. Com 0 os olhos so' acendem
    // no jumpscare (o jogador nao enxerga o monstro no escuro, o que
    // mantem a mecanica de "vigiar com a lanterna"). Com 1 os olhos ficam
    // sempre brilhando e entregam onde ele esta'.
    const float EYE_GLOW_IN_DARK  = 0.0f;

    // ---------------------------------------------------------------
    // Paleta
    // ---------------------------------------------------------------
    struct Color { float r, g, b; };

    const Color FUR         = { 0.36f, 0.19f, 0.10f }; // pelo marrom encardido
    const Color FUR_DARK    = { 0.22f, 0.11f, 0.07f };
    const Color FUR_BLOODY  = { 0.30f, 0.08f, 0.06f }; // pelo empapado de sangue
    const Color BELLY       = { 0.55f, 0.42f, 0.28f };
    const Color MUZZLE      = { 0.50f, 0.36f, 0.22f };
    const Color BLOOD       = { 0.32f, 0.00f, 0.01f }; // sangue seco
    const Color BLOOD_FRESH = { 0.65f, 0.02f, 0.03f }; // sangue fresco
    const Color BONE        = { 0.85f, 0.82f, 0.68f }; // dentes e garras
    const Color TOOTH_BLOOD = { 0.62f, 0.30f, 0.25f };
    const Color METAL       = { 0.55f, 0.58f, 0.62f };
    const Color DARK        = { 0.03f, 0.00f, 0.00f };
    const Color HAT         = { 0.07f, 0.06f, 0.06f };
    const Color HAT_BAND    = { 0.40f, 0.03f, 0.05f };
    const Color TIE         = { 0.12f, 0.04f, 0.16f };
    const Color EYE_WHITE   = { 0.80f, 0.78f, 0.60f };
    const Color PUPIL       = { 0.85f, 0.05f, 0.04f };
    const Color TONGUE      = { 0.50f, 0.05f, 0.08f };
    const Color EAR_INNER   = { 0.40f, 0.15f, 0.14f };

    // ---------------------------------------------------------------
    // Helpers de material e de forma
    // ---------------------------------------------------------------
    float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

    // Define a cor do proximo objeto (glColor e material, pra funcionar
    // com ou sem glColorMaterial).
    void paint(const Color& c) {
        glColor3f(c.r, c.g, c.b);
        const GLfloat m[] = { c.r, c.g, c.b, 1.0f };
        glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, m);
    }

    // Brilho especular (reflexo da lanterna): 0 = fosco. Usado no metal,
    // nos dentes e no nariz.
    void shine(float spec, float exponent) {
        const GLfloat s[] = { spec, spec, spec, 1.0f };
        glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, s);
        glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, exponent);
    }

    // Emissao: o objeto "brilha" por conta propria, mesmo sem luz.
    void glow(float r, float g, float b) {
        const GLfloat e[] = { r, g, b, 1.0f };
        glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, e);
    }

    // Caixa centrada em (cx,cy,cz) com dimensoes (sx,sy,sz).
    void box(float cx, float cy, float cz, float sx, float sy, float sz) {
        glPushMatrix();
            glTranslatef(cx, cy, cz);
            glScalef(sx, sy, sz);
            glutSolidCube(1.0);
        glPopMatrix();
    }

    // Elipsoide: esfera esticada por glScalef (raios rx, ry, rz).
    void ball(float cx, float cy, float cz, float rx, float ry, float rz) {
        glPushMatrix();
            glTranslatef(cx, cy, cz);
            glScalef(rx, ry, rz);
            glutSolidSphere(1.0, 16, 12);
        glPopMatrix();
    }

    // Cone apontando pra baixo (up=false) ou pra cima (up=true) a partir
    // de (x,y,z). O cone do GLUT aponta pra +Z, entao giramos 90 graus
    // em X pra deita-lo. Poucas "fatias" (6) deixam a ponta facetada e
    // mais cruel. Usado nos dentes.
    void tooth(float x, float y, float z, float baseR, float len, bool up) {
        glPushMatrix();
            glTranslatef(x, y, z);
            glRotatef(up ? -90.0f : 90.0f, 1.0f, 0.0f, 0.0f);
            glutSolidCone(baseR, len, 6, 1);
        glPopMatrix();
    }

    // Cilindro em pe, base em y=0 e topo em y=height. Usado na cartola.
    void cylinderY(float radius, float height, int n) {
        glBegin(GL_QUAD_STRIP);
            for (int i = 0; i <= n; ++i) {
                float a = 6.2831853f * (float)i / (float)n;
                float c = cosf(a), s = sinf(a);
                glNormal3f(c, 0.0f, s);
                glVertex3f(radius * c, 0.0f,   radius * s);
                glVertex3f(radius * c, height, radius * s);
            }
        glEnd();
        glNormal3f(0.0f, 1.0f, 0.0f);          // tampa de cima
        glBegin(GL_TRIANGLE_FAN);
            glVertex3f(0.0f, height, 0.0f);
            for (int i = 0; i <= n; ++i) {
                float a = 6.2831853f * (float)i / (float)n;
                glVertex3f(radius * cosf(a), height, radius * sinf(a));
            }
        glEnd();
        glNormal3f(0.0f, -1.0f, 0.0f);         // tampa de baixo
        glBegin(GL_TRIANGLE_FAN);
            glVertex3f(0.0f, 0.0f, 0.0f);
            for (int i = n; i >= 0; --i) {
                float a = 6.2831853f * (float)i / (float)n;
                glVertex3f(radius * cosf(a), 0.0f, radius * sinf(a));
            }
        glEnd();
    }

    // Elipse chata virada pra frente do monstro (-Z). Serve de mancha de
    // sangue, rastro escorrido e "barriga" colada no tronco.
    void ellipseFront(float cx, float cy, float z, float rx, float ry) {
        const int N = 20;
        glNormal3f(0.0f, 0.0f, -1.0f);
        glBegin(GL_TRIANGLE_FAN);
            glVertex3f(cx, cy, z);
            for (int i = 0; i <= N; ++i) {
                float a = 6.2831853f * (float)i / (float)N;
                glVertex3f(cx + rx * cosf(a), cy + ry * sinf(a), z);
            }
        glEnd();
    }

    // Um segmento de membro: cubo esticado, "pendurado" a partir da
    // origem atual e estendendo-se para baixo (-Y).
    void drawSegment(float length, float thickness) {
        glPushMatrix();
            glTranslatef(0.0f, -length * 0.5f, 0.0f);
            glScalef(thickness, length, thickness);
            glutSolidCube(1.0);
        glPopMatrix();
    }

    // ---------------------------------------------------------------
    // Partes do corpo
    // ---------------------------------------------------------------

    // Perna de 2 segmentos (coxa + canela) com pe e garras.
    // E' aqui que esta' a HIERARQUIA: depois de desenhar a coxa, andamos
    // ate' o joelho e giramos; a canela herda tudo e acompanha a coxa.
    // O pe desfaz a rotacao da perna pra continuar apoiado "reto".
    // exposed = true: canela sem pele, so' o metal do endoesqueleto.
    void drawLeg(float x, float swingDeg, float bendDeg, bool exposed) {
        glPushMatrix();
            glTranslatef(x, HIP_Y, 0.0f);
            paint(FUR_DARK);
            ball(0.0f, 0.0f, 0.0f, 0.17f, 0.17f, 0.17f);        // quadril
            glRotatef(swingDeg, 1.0f, 0.0f, 0.0f);               // balanco da perna
            paint(FUR);
            drawSegment(LEG_LEN, 0.30f);                         // coxa

            glTranslatef(0.0f, -LEG_LEN, 0.0f);                  // ate' o joelho
            if (exposed) { shine(0.8f, 60.0f); paint(METAL); } else { paint(FUR_DARK); }
            ball(0.0f, 0.0f, 0.0f, 0.13f, 0.13f, 0.13f);        // joelho
            glRotatef(bendDeg, 1.0f, 0.0f, 0.0f);                // dobra do joelho
            if (exposed) {
                drawSegment(LEG_LEN, 0.10f);                     // canela de metal
                shine(0.0f, 0.0f);
            } else {
                paint(FUR);
                drawSegment(LEG_LEN, 0.255f);
            }

            glTranslatef(0.0f, -LEG_LEN, 0.0f);                  // ate' o tornozelo
            glRotatef(-(swingDeg + bendDeg), 1.0f, 0.0f, 0.0f);  // desfaz a rotacao: pe nivelado
            paint(FUR_DARK);
            box(0.0f, 0.05f, -0.10f, 0.24f, 0.10f, 0.40f);       // pe
            paint(BONE);
            for (int i = 0; i < 3; ++i) {                        // garras do pe
                glPushMatrix();
                    glTranslatef(-0.08f + 0.08f * (float)i, 0.04f, -0.30f);
                    glRotatef(180.0f, 0.0f, 1.0f, 0.0f);         // cone passa a apontar pra -Z
                    glutSolidCone(0.03, 0.12, 6, 1);
                glPopMatrix();
            }
        glPopMatrix();
    }

    // Braco de 2 segmentos terminando numa mao com garras. O antebraco
    // e' sujo de sangue; com exposed = true ele e' so' metal.
    void drawArm(float x, float y, float swingDeg, float bendDeg, bool exposed) {
        glPushMatrix();
            glTranslatef(x, y, 0.0f);
            glRotatef(swingDeg, 1.0f, 0.0f, 0.0f);               // balanco / alcance
            paint(FUR);
            drawSegment(ARM_LEN, 0.24f);                         // braco

            glTranslatef(0.0f, -ARM_LEN, 0.0f);                  // ate' o cotovelo
            glRotatef(bendDeg, 1.0f, 0.0f, 0.0f);
            if (exposed) {
                shine(0.8f, 60.0f); paint(METAL);
                ball(0.0f, 0.0f, 0.0f, 0.08f, 0.08f, 0.08f);    // articulacao
                drawSegment(ARM_LEN, 0.09f);                     // antebraco de metal
                shine(0.0f, 0.0f);
            } else {
                paint(FUR_BLOODY);
                drawSegment(ARM_LEN, 0.20f);
            }

            glTranslatef(0.0f, -ARM_LEN, 0.0f);                  // ate' o pulso
            paint(FUR_BLOODY);
            ball(0.0f, -0.05f, 0.0f, 0.10f, 0.12f, 0.09f);       // mao
            paint(BONE);
            for (int i = 0; i < 4; ++i) {                        // 4 garras em leque
                glPushMatrix();
                    glTranslatef(-0.06f + 0.04f * (float)i, -0.12f, 0.0f);
                    glRotatef(-15.0f + 10.0f * (float)i, 0.0f, 0.0f, 1.0f); // abre pro lado
                    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);                     // aponta pra baixo
                    glutSolidCone(0.02, 0.17, 6, 1);
                glPopMatrix();
            }
        glPopMatrix();
    }

    // Tronco: caixa de pelo, barriga clara, ombros, sangue, rasgo no peito
    // com costelas e fios, marcas de garra e gravata borboleta.
    // "fz" e' a face da frente do tronco (-Z); cada camada fica alguns
    // milimetros na frente da anterior pra evitar z-fighting.
    void drawTorso(float walkTime) {
        const float fz = -TORSO_DEPTH * 0.5f;

        paint(FUR);
        box(0.0f, HIP_Y + TORSO_HEIGHT * 0.5f, 0.0f, TORSO_WIDTH, TORSO_HEIGHT, TORSO_DEPTH);

        paint(FUR_DARK);                                               // ombros
        ball(-(TORSO_WIDTH * 0.5f + 0.02f), SHOULDER_Y - 0.12f, 0.0f, 0.16f, 0.16f, 0.16f);
        ball( (TORSO_WIDTH * 0.5f + 0.02f), SHOULDER_Y - 0.12f, 0.0f, 0.16f, 0.16f, 0.16f);

        paint(BELLY);
        ellipseFront(0.0f, HIP_Y + 0.50f, fz - 0.003f, 0.30f, 0.38f);  // barriga

        // sangue seco espalhado pelo peito e barriga
        paint(BLOOD);
        ellipseFront(-0.18f, HIP_Y + 0.78f, fz - 0.006f, 0.13f, 0.09f);
        ellipseFront( 0.02f, HIP_Y + 0.55f, fz - 0.006f, 0.18f, 0.14f);
        ellipseFront(-0.10f, HIP_Y + 0.25f, fz - 0.006f, 0.10f, 0.07f);
        ellipseFront( 0.20f, HIP_Y + 0.38f, fz - 0.006f, 0.07f, 0.05f);
        // rastros frescos escorrendo da boca
        paint(BLOOD_FRESH);
        ellipseFront(-0.05f, SHOULDER_Y - 0.22f, fz - 0.008f, 0.018f, 0.20f);
        ellipseFront( 0.03f, SHOULDER_Y - 0.28f, fz - 0.008f, 0.015f, 0.26f);
        ellipseFront( 0.10f, SHOULDER_Y - 0.18f, fz - 0.008f, 0.020f, 0.16f);

        // tres marcas de garra na barriga
        paint(DARK);
        for (int i = 0; i < 3; ++i) {
            glPushMatrix();
                glTranslatef(-0.20f + 0.06f * (float)i, HIP_Y + 0.50f, fz - 0.010f);
                glRotatef(-25.0f, 0.0f, 0.0f, 1.0f);
                glScalef(0.025f, 0.42f, 0.004f);
                glutSolidCube(1.0);
            glPopMatrix();
        }

        // rasgo no peito: cavidade escura com costelas e coluna de metal
        paint(DARK);
        box(0.24f, HIP_Y + 0.85f, fz, 0.30f, 0.36f, 0.03f);
        shine(0.8f, 60.0f);
        paint(METAL);
        for (int k = 0; k < 4; ++k) {                                  // costelas
            box(0.24f, HIP_Y + 0.72f + 0.08f * (float)k, fz - 0.030f, 0.28f, 0.025f, 0.03f);
        }
        box(0.24f, HIP_Y + 0.85f, fz - 0.035f, 0.03f, 0.36f, 0.02f);   // coluna
        shine(0.0f, 0.0f);

        // fios soltos balancando (cada um pivota no topo e oscila com o tempo)
        const Color wires[3] = { {0.80f, 0.05f, 0.05f}, {0.10f, 0.20f, 0.80f}, {0.80f, 0.70f, 0.10f} };
        for (int k = 0; k < 3; ++k) {
            float sway = 12.0f * sinf(walkTime * 2.2f + 1.7f * (float)k);
            glPushMatrix();
                glTranslatef(0.17f + 0.07f * (float)k, HIP_Y + 0.68f, fz - 0.02f);
                glRotatef(sway, 0.0f, 0.0f, 1.0f);
                paint(wires[k]);
                drawSegment(0.28f, 0.018f);
            glPopMatrix();
        }

        // gravata borboleta: dois cones com as pontas se encontrando no no'
        const float ty = SHOULDER_Y - 0.06f, tz = fz - 0.03f;
        paint(TIE);
        glPushMatrix();
            glTranslatef(-0.24f, ty, tz);
            glRotatef(90.0f, 0.0f, 1.0f, 0.0f);                        // aponta pra +X
            glutSolidCone(0.11, 0.22, 10, 1);
        glPopMatrix();
        glPushMatrix();
            glTranslatef(0.24f, ty, tz);
            glRotatef(-90.0f, 0.0f, 1.0f, 0.0f);                       // aponta pra -X
            glutSolidCone(0.11, 0.22, 10, 1);
        glPopMatrix();
        ball(0.0f, ty, tz, 0.05f, 0.06f, 0.05f);                       // no'
    }

    // Cabeca (origem no centro do cranio, a frente e' -Z): cranio, orelhas,
    // focinho, nariz, olhos fundos, sobrancelhas, dentes, mandibula com
    // lingua e sangue pingando, e a cartola.
    void drawHead(float walkTime, float mouthOpen) {
        const float R = HEAD_RADIUS;

        // Tique nervoso: a cabeca gira e inclina devagar e de tempos em
        // tempos da' um "tranco" brusco. "calm" some no jumpscare pra ela
        // encarar a camera.
        float calm = 1.0f - clamp01(mouthOpen * 2.0f);
        float yaw  = calm * 10.0f * sinf(walkTime * 0.9f);
        if (fmodf(walkTime, 2.7f) < 0.10f) yaw += calm * 24.0f;
        float roll = calm * 7.0f * sinf(walkTime * 0.6f + 1.0f);
        glRotatef(yaw,  0.0f, 1.0f, 0.0f);
        glRotatef(roll, 0.0f, 0.0f, 1.0f);

        // Mandibula: abre conforme o jumpscare e nunca fecha de todo
        // (tremor dos dentes).
        float jaw    = clamp01(mouthOpen + JAW_CHATTER * (0.5f + 0.5f * sinf(walkTime * 7.0f)));
        float jawDeg = jaw * JAW_OPEN_MAX_DEG;
        float th     = jawDeg * DEG2RAD;
        float gap    = 0.02f + 0.34f * sinf(th);   // quanto a ponta do queixo desceu

        float eyeGlow = EYE_GLOW_IN_DARK;
        if (mouthOpen > 0.15f) eyeGlow = 1.0f;     // olhos acendem no susto

        // --- cranio e orelhas ---
        paint(FUR);
        ball(0.0f, 0.0f, 0.0f, R, R, R);
        for (int side = -1; side <= 1; side += 2) {
            paint(FUR);
            ball(0.28f * side, 0.17f, 0.0f, 0.11f, 0.11f, 0.07f);
            paint(EAR_INNER);
            ball(0.28f * side, 0.17f, -0.05f, 0.07f, 0.07f, 0.03f);
        }

        // --- focinho e nariz ---
        paint(MUZZLE);
        box(0.0f, -0.09f, -0.28f, 0.40f, 0.18f, 0.40f);
        shine(0.9f, 80.0f);
        paint(DARK);
        ball(0.0f, -0.015f, -0.48f, 0.05f, 0.035f, 0.04f);
        shine(0.0f, 0.0f);
        paint(BLOOD);                                                  // sangue no focinho
        box(0.0f, -0.12f, -0.482f, 0.26f, 0.07f, 0.004f);

        // --- olhos: orbita escura, globo sujo e pupila vermelha ---
        for (int side = -1; side <= 1; side += 2) {
            float ex = 0.13f * side;
            paint(DARK);
            ball(ex, 0.09f, -0.235f, 0.085f, 0.085f, 0.085f);         // orbita
            paint(EYE_WHITE);
            ball(ex, 0.09f, -0.270f, 0.062f, 0.062f, 0.062f);         // globo ocular
            paint(PUPIL);
            glow(eyeGlow, eyeGlow * 0.05f, 0.0f);                     // brilha no susto
            ball(ex, 0.09f, -0.318f, 0.030f, 0.030f, 0.030f);         // pupila
            glow(0.0f, 0.0f, 0.0f);

            glPushMatrix();                                            // sobrancelha furiosa
                glTranslatef(ex, 0.185f, -0.27f);
                glRotatef(-25.0f * side, 0.0f, 0.0f, 1.0f);           // ponta de dentro mais baixa
                paint(FUR_DARK);
                box(0.0f, 0.0f, 0.0f, 0.15f, 0.04f, 0.07f);
            glPopMatrix();
        }

        // gotas de sangue na testa e bochechas
        paint(BLOOD);
        ball(-0.20f, -0.03f, -0.24f, 0.05f, 0.07f, 0.02f);
        ball( 0.20f, -0.03f, -0.24f, 0.04f, 0.06f, 0.02f);
        ball( 0.05f,  0.20f, -0.24f, 0.05f, 0.04f, 0.02f);
        ball(-0.16f,  0.18f, -0.20f, 0.04f, 0.04f, 0.02f);

        // --- interior escuro da boca (entre o focinho e a mandibula) ---
        paint(DARK);
        box(0.0f, -0.18f - gap * 0.5f, -0.28f, 0.30f, gap, 0.32f);

        // --- dentes de cima: 8 cones irregulares, 2 presas longas ---
        // Ficam numa curva em "U" (z = -0.42 + 5*x*x) acompanhando o focinho.
        shine(0.4f, 20.0f);
        for (int i = 0; i < 8; ++i) {
            float x   = -0.14f + 0.04f * (float)i;
            float len = 0.06f + 0.025f * (float)((i * 5) % 3);        // alturas irregulares
            if (i == 1 || i == 6) len = 0.15f;                        // presas
            paint((i % 4 == 0) ? TOOTH_BLOOD : BONE);
            tooth(x, -0.17f, -0.42f + 5.0f * x * x, 0.022f, len, false);
        }

        // --- mandibula: gira em torno da dobradica (0,-0.18,-0.10) ---
        // Girar em X com angulo negativo leva o queixo (-Z) pra baixo.
        glPushMatrix();
            glTranslatef(0.0f, -0.18f, -0.10f);
            glRotatef(-jawDeg, 1.0f, 0.0f, 0.0f);

            shine(0.0f, 0.0f);
            paint(MUZZLE);
            box(0.0f, -0.04f, -0.17f, 0.34f, 0.08f, 0.34f);           // osso do queixo
            paint(BLOOD);
            box(0.0f, -0.04f, -0.342f, 0.30f, 0.05f, 0.004f);         // sangue no queixo
            paint(TONGUE);
            ball(0.0f, 0.0f, -0.17f, 0.08f, 0.02f, 0.12f);            // lingua

            shine(0.4f, 20.0f);
            for (int j = 0; j < 7; ++j) {                             // dentes de baixo (apontam pra cima)
                float x   = -0.12f + 0.04f * (float)j;
                float len = 0.05f + 0.025f * (float)((j * 2) % 3);
                paint((j % 3 == 1) ? TOOTH_BLOOD : BONE);
                tooth(x, -0.005f, -0.32f + 5.0f * x * x, 0.020f, len, true);
            }
            shine(0.0f, 0.0f);
        glPopMatrix();

        // --- sangue pingando do queixo ---
        // As gotas ficam no referencial da cabeca (sempre na vertical) e
        // partem da ponta do queixo, cujo lugar calculamos pelo angulo.
        float jy = -0.18f - 0.08f * cosf(th) - 0.34f * sinf(th);
        float jz = -0.10f + 0.08f * sinf(th) - 0.34f * cosf(th);
        paint(BLOOD_FRESH);
        for (int k = 0; k < 3; ++k) {
            float len = 0.10f + 0.07f * sinf(walkTime * 1.7f + 2.0f * (float)k); // cresce e encolhe
            box(-0.10f + 0.10f * (float)k, jy - len * 0.5f, jz + 0.01f, 0.014f, len, 0.014f);
        }

        // --- cartola torta ---
        glPushMatrix();
            glTranslatef(0.02f, 0.27f, 0.0f);
            glRotatef(8.0f, 0.0f, 0.0f, 1.0f);
            shine(0.3f, 30.0f);
            paint(HAT);
            cylinderY(0.26f, 0.03f, 20);                              // aba
            glTranslatef(0.0f, 0.03f, 0.0f);
            cylinderY(0.19f, HAT_HEIGHT, 20);                         // copa
            paint(HAT_BAND);
            cylinderY(0.196f, 0.07f, 20);                             // faixa
            shine(0.0f, 0.0f);
        glPopMatrix();
    }
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

// mouthOpen vale ~0.05 caminhando e sobe de 0 a 1 durante o jumpscare.
// Aproveitamos esse valor pra tudo que muda no susto: boca, olhos que
// acendem, corpo que se joga pra frente e bracos que esticam.
void draw(const Vector3& position, float facingYawDeg, float walkTime, float mouthOpen) {
    // Guarda e restaura o estado de iluminacao/material no fim, pra nada
    // que mudamos aqui (brilho, emissao, normalizacao) vazar pro resto da cena.
    glPushAttrib(GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_CURRENT_BIT);
    glEnable(GL_NORMALIZE);   // refaz o tamanho das normais depois dos glScalef nao uniformes
    shine(0.0f, 0.0f);
    glow(0.0f, 0.0f, 0.0f);

    // Caminhada: senoides do tempo aplicadas nos angulos das articulacoes.
    // Pernas e bracos balancam em sentidos opostos.
    float s        = sinf(walkTime * 3.2f);
    float legSwing = 28.0f * s;
    float knee     = 18.0f * (0.5f + 0.5f * sinf(walkTime * 3.2f + 1.2f)); // so' dobra pra frente
    float arm      = -22.0f * s;
    float bob      = 0.04f * fabsf(s);              // corpo sobe e desce a cada passo

    // Postura: corcunda leve andando; no jumpscare o tronco tomba pra
    // frente e os bracos esticam em direcao a camera.
    float jump  = clamp01(mouthOpen);
    float lean  = -(10.0f + 45.0f * jump);          // graus (negativo = pra frente, -Z)
    float reach = 140.0f * jump;

    glPushMatrix();
        // Raiz da hierarquia: tudo abaixo e' desenhado relativo ao monstro.
        glTranslatef(position.x, position.y + bob, position.z);
        glRotatef(facingYawDeg, 0.0f, 1.0f, 0.0f);
        glScalef(MONSTER_SCALE, MONSTER_SCALE, MONSTER_SCALE);

        // --- Pernas (em fases opostas); a de tras esta' sem pele ---
        drawLeg(-0.22f, legSwing, knee, false);
        drawLeg( 0.22f, -legSwing, -knee + 36.0f, true);

        // --- Parte de cima do corpo: gira em torno do quadril ---
        glPushMatrix();
            glTranslatef(0.0f, HIP_Y, 0.0f);
            glRotatef(lean, 1.0f, 0.0f, 0.0f);
            glTranslatef(0.0f, -HIP_Y, 0.0f);

            drawTorso(walkTime);

            // Cabeca: contra-gira quase o mesmo tanto da inclinacao do
            // tronco, entao o rosto continua olhando pra frente mesmo
            // com o corpo tombado (e fica mais perturbador).
            glPushMatrix();
                glTranslatef(0.0f, SHOULDER_Y + HEAD_RADIUS * 0.9f, 0.0f);
                glRotatef(-lean * 0.9f, 1.0f, 0.0f, 0.0f);
                drawHead(walkTime, mouthOpen);
            glPopMatrix();

            drawArm(-TORSO_WIDTH * 0.5f - 0.05f, SHOULDER_Y - 0.1f, -arm + reach, 15.0f, false);
            drawArm( TORSO_WIDTH * 0.5f + 0.05f, SHOULDER_Y - 0.1f,  arm + reach, 15.0f, true);
        glPopMatrix();
    glPopMatrix();

    glPopAttrib();
}

} // namespace Enemy