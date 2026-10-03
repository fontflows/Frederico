#include "scene_builder.h"
#include "lighting.h"
#include "bezier.h" // Vector3
#include "textures.h"

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

#include <cmath>

using namespace Scene;

// ===============================================================
// CONFIGURACAO
// ===============================================================

// Luzes do OpenGL usadas aqui. A GL_LIGHT0 e' a lanterna (lighting.cpp).
// Se alguma das outras ja' estiver em uso, troque por GL_LIGHT4, 5...
static const GLenum MONITOR_LIGHT    = GL_LIGHT1;
static const GLenum CORRIDOR_LIGHT_A = GL_LIGHT2;
static const GLenum CORRIDOR_LIGHT_B = GL_LIGHT3;

// Forca das lampadas do corredor (0 = apagadas, so' os suportes ficam).
// Com 0.5 elas deixam o corredor so' "entrevisto" quando piscam; aumentar
// deixa o jogo mais facil (da' pra ver o monstro sem lanterna).
static const float CORRIDOR_LIGHT_LEVEL = 0.5f;

// Altura da faixa de azulejo ("lambril") nas paredes.
static const float WAINSCOT_H = 1.1f;

// Posicao da cabeca do jogador no eixo Z: tem que ser igual a g_eye.z
// da main.cpp (ROOM_BACK_Z - 1.0). Usada pra colocar a mesa na frente dele.
static const float PLAYER_EYE_Z = ROOM_BACK_Z - 1.0f;

// Tintas (R,G,B): MULTIPLICAM a cor da textura. {1,1,1} deixa a textura
// como ela e'; valores menores escurecem / mudam o tom.
static const float C_WHITE[3]     = { 1.00f, 1.00f, 1.00f };
static const float C_ROOM_LOW[3]  = { 1.00f, 1.00f, 1.00f }; // azulejo da sala
static const float C_ROOM_HIGH[3] = { 1.00f, 1.00f, 1.00f }; // reboco da sala
static const float C_CEIL[3]      = { 0.70f, 0.70f, 0.70f }; // forro do corredor, mais escuro
static const float C_COR_LOW[3]   = { 0.75f, 0.95f, 0.85f }; // azulejo do corredor, mais verde
static const float C_COR_HIGH[3]  = { 0.55f, 0.58f, 0.60f }; // reboco do corredor, mais frio

// ===============================================================
// HELPERS DE DESENHO
// ===============================================================

// Um quad com normal explicita. A NORMAL e' o vetor perpendicular a'
// superficie; a iluminacao usa ela pra saber o quanto a face esta'
// virada pra luz.
static void quad(const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d,
                  const Vector3& normal) {
    glNormal3f(normal.x, normal.y, normal.z);
    glVertex3f(a.x, a.y, a.z);
    glVertex3f(b.x, b.y, b.z);
    glVertex3f(c.x, c.y, c.z);
    glVertex3f(d.x, d.y, d.z);
}

// Define a "tinta" do proximo objeto: cor difusa+ambiente, sem brilho
// especular e sem emissao (o objeto so' aparece quando alguma luz bate
// nele). Chama tambem glColor porque, se o projeto usar glColorMaterial,
// e' o glColor que manda no material.
static void setPaint(float r, float g, float b) {
    const GLfloat c[]   = { r, g, b, 1.0f };
    const GLfloat off[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, c);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, off);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, off);
    glColor3f(r, g, b);
}

// So' troca a cor (sem mexer em especular/emissao). Pode ser chamada
// dentro de glBegin/glEnd, entao serve pra pintar ladrilho por ladrilho.
static void tint3(float r, float g, float b) {
    const GLfloat c[] = { r, g, b, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, c);
    glColor3f(r, g, b);
}

// Placa retangular texturizada no plano Z (virada pra +Z), centro (cx,cy,z),
// largura w, altura h. "texSize" = metros por repeticao da textura.
// Usada pra cobrir a face da porta de aco e do armario com metal.
static void texPlateZ(float cx, float cy, float z, float w, float h,
                      TextureId tex, float texSize, float r, float g, float b) {
    setPaint(r, g, b);
    useTexture(tex);
    glBegin(GL_QUADS);
        glNormal3f(0.0f, 0.0f, 1.0f);
        glTexCoord2f(0.0f, 0.0f);                       glVertex3f(cx - w * 0.5f, cy - h * 0.5f, z);
        glTexCoord2f(w / texSize, 0.0f);                glVertex3f(cx + w * 0.5f, cy - h * 0.5f, z);
        glTexCoord2f(w / texSize, h / texSize);         glVertex3f(cx + w * 0.5f, cy + h * 0.5f, z);
        glTexCoord2f(0.0f, h / texSize);                glVertex3f(cx - w * 0.5f, cy + h * 0.5f, z);
    glEnd();
    noTexture();
}

// Idem, mas horizontal (virada pra cima), a' altura y. Cobre o tampo das mesas.
static void texPlateY(float cx, float y, float cz, float w, float d,
                      TextureId tex, float texSize, float r, float g, float b) {
    setPaint(r, g, b);
    useTexture(tex);
    glBegin(GL_QUADS);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glTexCoord2f(0.0f, 0.0f);                       glVertex3f(cx - w * 0.5f, y, cz + d * 0.5f);
        glTexCoord2f(w / texSize, 0.0f);                glVertex3f(cx + w * 0.5f, y, cz + d * 0.5f);
        glTexCoord2f(w / texSize, d / texSize);         glVertex3f(cx + w * 0.5f, y, cz - d * 0.5f);
        glTexCoord2f(0.0f, d / texSize);                glVertex3f(cx - w * 0.5f, y, cz - d * 0.5f);
    glEnd();
    noTexture();
}

// Reflexo (brilho especular) do material atual: serve pro metal.
// Chamar DEPOIS do setPaint (que zera o brilho) e zerar no fim.
static void shiny(float spec, float exponent) {
    const GLfloat s[] = { spec, spec, spec, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, s);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, exponent);
}

// Faz o objeto "brilhar" sozinho (emissao). Zere depois com glowOff().
static void glowOn(float r, float g, float b) {
    const GLfloat e[] = { r, g, b, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, e);
}
static void glowOff() { glowOn(0.0f, 0.0f, 0.0f); }

// Numero pseudo-aleatorio fixo em [0,1] a partir de dois inteiros (hash).
// Sempre devolve o mesmo valor pro mesmo (i,j), entao a "sujeira" de cada
// ladrilho e' estavel entre frames, sem precisar guardar nada.
static float hash2(int i, int j) {
    unsigned int n = (unsigned int)i * 73856093u ^ (unsigned int)j * 19349663u;
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return (float)(n & 0x7fffffffu) / 2147483647.0f;
}

static float clampf01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static float lenOf(const Vector3& a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

// (definida mais abaixo, na parte do corredor; o monitor tambem usa)
static float lampLevel(float t, float phase);

// Quao perto o monstro esta' (0 a 1): as lampadas do corredor falham mais
// (flashes extras de apagao) quando ele se aproxima. Atualizado em drawRoomProps.
static float g_lampDanger = 0.0f;

// Caixa (paralelepipedo) centrada em (cx,cy,cz) com dimensoes
// (sx,sy,sz): um cubo unitario do GLUT esticado por glScalef e movido
// por glTranslatef. Com esse helper montamos quase todos os objetos.
static void box(float cx, float cy, float cz, float sx, float sy, float sz) {
    glPushMatrix();
        glTranslatef(cx, cy, cz);
        glScalef(sx, sy, sz);
        glutSolidCube(1.0);
    glPopMatrix();
}

// Disco no plano XY, virado pra +Z, em (cx,cy,z) (leque de triangulos).
static void disc(float cx, float cy, float z, float radius) {
    const int N = 24;
    glNormal3f(0.0f, 0.0f, 1.0f);
    glBegin(GL_TRIANGLE_FAN);
        glVertex3f(cx, cy, z);
        for (int i = 0; i <= N; ++i) {
            float a = 6.2831853f * (float)i / (float)N;
            glVertex3f(cx + radius * cosf(a), cy + radius * sinf(a), z);
        }
    glEnd();
}

// Cilindro em pe: base em y=0, topo em y=height. Os lados usam normais
// radiais (apontando pra fora), o que da' o sombreamento arredondado.
static void cylinderY(float radius, float height, int n) {
    glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= n; ++i) {
            float a = 6.2831853f * (float)i / (float)n;
            float c = cosf(a), s = sinf(a);
            glNormal3f(c, 0.0f, s);
            glVertex3f(radius * c, 0.0f,   radius * s);
            glVertex3f(radius * c, height, radius * s);
        }
    glEnd();
    glNormal3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLE_FAN);
        glVertex3f(0.0f, height, 0.0f);
        for (int i = 0; i <= n; ++i) {
            float a = 6.2831853f * (float)i / (float)n;
            glVertex3f(radius * cosf(a), height, radius * sinf(a));
        }
    glEnd();
}

// Cilindro deitado ao longo do corredor: comeca em (x,y,z0) e segue
// "len" metros em direcao a -Z (o fundo do corredor). E' o cilindro em
// pe girado -90 graus em X. Usado nos canos.
static void cylinderZ(float x, float y, float z0, float len, float radius) {
    glPushMatrix();
        glTranslatef(x, y, z0);
        glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
        cylinderY(radius, len, 14);
    glPopMatrix();
}

// Superficie plana (parede, piso, teto) SUBDIVIDIDA em varias celulas e
// TEXTURIZADA.
//   p = canto de origem; u e v = vetores das duas arestas (comprimento total)
//   n = normal; cell = tamanho aproximado de cada celula
//   tex / texSize = textura e quantos metros ela cobre por repeticao
//   tint = cor que multiplica a textura; variation = quanto o brilho de
//   cada celula varia ao acaso (quebra a repeticao da textura)
// POR QUE subdividir? O OpenGL classico calcula a luz so' nos VERTICES e
// interpola o resultado. Um quad gigante tem so' 4 vertices, entao a
// lanterna quase nao apareceria nele. Com varias celulas, a luz "cai"
// de forma localizada (mancha de luz).
// COORDENADAS DE TEXTURA: cada vertice recebe (s,t) = distancia em metros
// ao longo de u e de v, dividida por texSize. Com GL_REPEAT, a textura se
// repete a cada texSize metros, mantendo o mesmo tamanho em qualquer parede.
static void gridWall(const Vector3& p, const Vector3& u, const Vector3& v, const Vector3& n,
                     float cell, TextureId tex, float texSize, const float* tint,
                     float variation, int seed) {
    float lu = lenOf(u), lv = lenOf(v);
    int nu = (int)(lu / cell + 0.5f); if (nu < 1) nu = 1;
    int nv = (int)(lv / cell + 0.5f); if (nv < 1) nv = 1;
    setPaint(tint[0], tint[1], tint[2]);
    useTexture(tex);
    glBegin(GL_QUADS);
    for (int j = 0; j < nv; ++j) {
        for (int i = 0; i < nu; ++i) {
            float f = 1.0f - variation * hash2(i + seed * 131, j + seed * 71);
            tint3(tint[0] * f, tint[1] * f, tint[2] * f);
            float u0 = (float)i / nu, u1 = (float)(i + 1) / nu;
            float v0 = (float)j / nv, v1 = (float)(j + 1) / nv;
            float s0 = lu * u0 / texSize, s1 = lu * u1 / texSize;
            float t0 = lv * v0 / texSize, t1 = lv * v1 / texSize;
            Vector3 a = p + u * u0 + v * v0, b = p + u * u1 + v * v0;
            Vector3 c = p + u * u1 + v * v1, d = p + u * u0 + v * v1;
            glNormal3f(n.x, n.y, n.z);
            glTexCoord2f(s0, t0); glVertex3f(a.x, a.y, a.z);
            glTexCoord2f(s1, t0); glVertex3f(b.x, b.y, b.z);
            glTexCoord2f(s1, t1); glVertex3f(c.x, c.y, c.z);
            glTexCoord2f(s0, t1); glVertex3f(d.x, d.y, d.z);
        }
    }
    glEnd();
    noTexture();
}

// Parede em duas faixas: azulejo embaixo (ate' WAINSCOT_H) e reboco em cima.
static void wallBands(const Vector3& p0, const Vector3& u, const Vector3& n, float h,
                      const float* low, const float* high, int seed) {
    gridWall(p0, u, Vector3(0.0f, WAINSCOT_H, 0.0f), n, 0.5f, TEX_WALL_TILE, 0.6f, low, 0.18f, seed);
    gridWall(p0 + Vector3(0.0f, WAINSCOT_H, 0.0f), u, Vector3(0.0f, h - WAINSCOT_H, 0.0f),
             n, 1.0f, TEX_PLASTER, 2.0f, high, 0.18f, seed + 1);
}

// ===============================================================
// ESTRUTURA: SALA, CORREDOR, PORTA
// ===============================================================

void drawSecurityRoom() {
    const float depth = ROOM_BACK_Z - ROOM_FRONT_Z;
    const float W2    = 2.0f * ROOM_HALF_WIDTH;

    // Piso de ladrilhos xadrez (claro/escuro), cada um com sujeira propria.
    gridWall(Vector3(-ROOM_HALF_WIDTH, 0.0f, ROOM_FRONT_Z), Vector3(W2, 0.0f, 0.0f),
             Vector3(0.0f, 0.0f, depth), Vector3(0.0f, 1.0f, 0.0f),
             0.5f, TEX_FLOOR_TILE, 1.0f, C_WHITE, 0.15f, 11);

    // Teto
    gridWall(Vector3(-ROOM_HALF_WIDTH, ROOM_HEIGHT, ROOM_FRONT_Z), Vector3(W2, 0.0f, 0.0f),
             Vector3(0.0f, 0.0f, depth), Vector3(0.0f, -1.0f, 0.0f),
             1.0f, TEX_CEILING, 1.2f, C_WHITE, 0.15f, 12);

    // Parede de tras (normal -Z)
    wallBands(Vector3(-ROOM_HALF_WIDTH, 0.0f, ROOM_BACK_Z), Vector3(W2, 0.0f, 0.0f),
              Vector3(0.0f, 0.0f, -1.0f), ROOM_HEIGHT, C_ROOM_LOW, C_ROOM_HIGH, 21);

    // Parede esquerda (normal +X) e direita (normal -X)
    wallBands(Vector3(-ROOM_HALF_WIDTH, 0.0f, ROOM_FRONT_Z), Vector3(0.0f, 0.0f, depth),
              Vector3(1.0f, 0.0f, 0.0f), ROOM_HEIGHT, C_ROOM_LOW, C_ROOM_HIGH, 31);
    wallBands(Vector3(ROOM_HALF_WIDTH, 0.0f, ROOM_FRONT_Z), Vector3(0.0f, 0.0f, depth),
              Vector3(-1.0f, 0.0f, 0.0f), ROOM_HEIGHT, C_ROOM_LOW, C_ROOM_HIGH, 41);

    // Parede frontal: duas colunas + lintel deixando a abertura livre
    wallBands(Vector3(-ROOM_HALF_WIDTH, 0.0f, ROOM_FRONT_Z),
              Vector3(ROOM_HALF_WIDTH - DOORWAY_HALF_W, 0.0f, 0.0f),
              Vector3(0.0f, 0.0f, 1.0f), ROOM_HEIGHT, C_ROOM_LOW, C_ROOM_HIGH, 51);
    wallBands(Vector3(DOORWAY_HALF_W, 0.0f, ROOM_FRONT_Z),
              Vector3(ROOM_HALF_WIDTH - DOORWAY_HALF_W, 0.0f, 0.0f),
              Vector3(0.0f, 0.0f, 1.0f), ROOM_HEIGHT, C_ROOM_LOW, C_ROOM_HIGH, 61);
    gridWall(Vector3(-DOORWAY_HALF_W, DOORWAY_HEIGHT, ROOM_FRONT_Z),
             Vector3(2.0f * DOORWAY_HALF_W, 0.0f, 0.0f),
             Vector3(0.0f, ROOM_HEIGHT - DOORWAY_HEIGHT, 0.0f),
             Vector3(0.0f, 0.0f, 1.0f), 1.0f, TEX_PLASTER, 2.0f, C_ROOM_HIGH, 0.20f, 71);
}

void drawCorridor() {
    const float L  = ROOM_FRONT_Z - CORRIDOR_FAR_Z;   // comprimento do corredor
    const float W2 = 2.0f * CORRIDOR_HALF_W;

    // Piso de concreto sujo, em lajes de 1 m
    gridWall(Vector3(-CORRIDOR_HALF_W, 0.0f, ROOM_FRONT_Z), Vector3(W2, 0.0f, 0.0f),
             Vector3(0.0f, 0.0f, -L), Vector3(0.0f, 1.0f, 0.0f),
             1.0f, TEX_CONCRETE, 2.0f, C_WHITE, 0.25f, 81);

    // Faixas amarelas de seguranca, tracejadas, nas duas bordas do piso
    setPaint(0.50f, 0.42f, 0.07f);
    glBegin(GL_QUADS);
    for (int side = -1; side <= 1; side += 2) {
        float x = side * (CORRIDOR_HALF_W - 0.22f);
        for (float d = 0.0f; d + 1.0f <= L; d += 2.0f) {
            float f = 0.6f + 0.4f * hash2((int)d, side + 5);   // faixas gastas
            tint3(0.50f * f, 0.42f * f, 0.07f * f);
            float z0 = ROOM_FRONT_Z - d, z1 = ROOM_FRONT_Z - d - 1.0f;
            quad(Vector3(x - 0.04f, 0.003f, z0), Vector3(x + 0.04f, 0.003f, z0),
                 Vector3(x + 0.04f, 0.003f, z1), Vector3(x - 0.04f, 0.003f, z1),
                 Vector3(0.0f, 1.0f, 0.0f));
        }
    }
    glEnd();

    // Teto
    gridWall(Vector3(-CORRIDOR_HALF_W, CORRIDOR_HEIGHT, ROOM_FRONT_Z), Vector3(W2, 0.0f, 0.0f),
             Vector3(0.0f, 0.0f, -L), Vector3(0.0f, -1.0f, 0.0f),
             1.0f, TEX_CEILING, 1.2f, C_CEIL, 0.20f, 82);

    // Paredes laterais e parede de fundo, com azulejo embaixo
    wallBands(Vector3(-CORRIDOR_HALF_W, 0.0f, ROOM_FRONT_Z), Vector3(0.0f, 0.0f, -L),
              Vector3(1.0f, 0.0f, 0.0f), CORRIDOR_HEIGHT, C_COR_LOW, C_COR_HIGH, 91);
    wallBands(Vector3(CORRIDOR_HALF_W, 0.0f, ROOM_FRONT_Z), Vector3(0.0f, 0.0f, -L),
              Vector3(-1.0f, 0.0f, 0.0f), CORRIDOR_HEIGHT, C_COR_LOW, C_COR_HIGH, 101);
    wallBands(Vector3(-CORRIDOR_HALF_W, 0.0f, CORRIDOR_FAR_Z), Vector3(W2, 0.0f, 0.0f),
              Vector3(0.0f, 0.0f, 1.0f), CORRIDOR_HEIGHT, C_COR_LOW, C_COR_HIGH, 111);
}

// Detalhes da face da frente (+Z) da porta de aco, em coordenadas
// locais da porta: origem no centro, W = largura, H = altura, face em
// z = +0.04. Cada camada fica poucos milimetros na frente da anterior.
static void rivet(float x, float y, float z) {
    glPushMatrix();
        glTranslatef(x, y, z);
        glutSolidSphere(0.013, 6, 5);
    glPopMatrix();
}

static void drawDoorDetails(float W, float H) {
    const float z0 = 0.04f;

    // chapa de aco texturizada (escovado + ferrugem + arranhoes) cobrindo
    // a face inteira; os detalhes abaixo ficam por cima dela
    texPlateZ(0.0f, 0.0f, z0 + 0.0015f, W - 0.04f, H - 0.04f, TEX_METAL, 1.0f, 1.0f, 1.0f, 1.0f);

    // moldura interna: 4 barras escuras
    setPaint(0.10f, 0.11f, 0.12f);
    box(0.0f,  H * 0.5f - 0.03f, z0 + 0.006f, W - 0.06f, 0.04f, 0.012f);
    box(0.0f, -H * 0.5f + 0.03f, z0 + 0.006f, W - 0.06f, 0.04f, 0.012f);
    box(-(W * 0.5f - 0.03f), 0.0f, z0 + 0.006f, 0.04f, H - 0.06f, 0.012f);
    box( (W * 0.5f - 0.03f), 0.0f, z0 + 0.006f, 0.04f, H - 0.06f, 0.012f);

    // dois paineis em relevo (cima e baixo)
    const float pw = W - 0.30f, ph = H * 0.34f, py = H * 0.24f;
    setPaint(0.24f, 0.26f, 0.27f);
    box(0.0f,  py, z0 + 0.008f, pw, ph, 0.016f);
    box(0.0f, -py, z0 + 0.008f, pw, ph, 0.016f);

    // rebites em volta de cada painel
    setPaint(0.45f, 0.46f, 0.48f);
    for (int s = -1; s <= 1; s += 2) {
        float cy = s * py;
        for (float x = -pw * 0.5f + 0.06f; x <= pw * 0.5f - 0.05f; x += 0.22f) {
            rivet(x, cy + ph * 0.5f - 0.04f, z0 + 0.018f);
            rivet(x, cy - ph * 0.5f + 0.04f, z0 + 0.018f);
        }
        for (int k = 1; k < 4; ++k) {
            float y = cy - ph * 0.5f + ph * (float)k / 4.0f;
            rivet(-(pw * 0.5f - 0.04f), y, z0 + 0.018f);
            rivet( (pw * 0.5f - 0.04f), y, z0 + 0.018f);
        }
    }

    // roda de trava no centro, tipo escotilha de submarino
    setPaint(0.40f, 0.42f, 0.45f);
    shiny(0.7f, 50.0f);
    glPushMatrix();
        glTranslatef(0.0f, 0.0f, z0 + 0.035f);
        glutSolidTorus(0.014, 0.14, 6, 20);                      // aro
        for (int k = 0; k < 3; ++k) {                            // 3 raios
            glPushMatrix();
                glRotatef(60.0f * (float)k, 0.0f, 0.0f, 1.0f);
                box(0.0f, 0.0f, 0.0f, 0.28f, 0.018f, 0.018f);
            glPopMatrix();
        }
        glutSolidSphere(0.035, 10, 8);                           // cubo central
        box(0.0f, 0.0f, -0.015f, 0.05f, 0.05f, 0.03f);           // eixo
    glPopMatrix();
    shiny(0.0f, 0.0f);

    // faixa de perigo amarela e preta na base
    int n = (int)(W / 0.12f);
    for (int k = 0; k < n; ++k) {
        if (k & 1) setPaint(0.70f, 0.55f, 0.05f); else setPaint(0.04f, 0.04f, 0.04f);
        box(-W * 0.5f + 0.06f + 0.12f * (float)k, -H * 0.5f + 0.10f, z0 + 0.004f,
            0.12f, 0.08f, 0.008f);
    }

    // ferrugem escorrendo de cima dos paineis
    setPaint(0.30f, 0.12f, 0.04f);
    for (int k = 0; k < 5; ++k) {
        float len = 0.25f + 0.12f * (float)(k % 3);
        box(-0.8f + 0.4f * (float)k, py + ph * 0.5f - len * 0.5f, z0 + 0.018f, 0.03f, len, 0.004f);
    }

    // marcas de garra: tinta riscada mostrando o metal claro por baixo
    setPaint(0.72f, 0.72f, 0.74f);
    for (int k = 0; k < 4; ++k) {
        glPushMatrix();
            glTranslatef(0.45f + 0.07f * (float)k, -py, z0 + 0.0185f);
            glRotatef(18.0f, 0.0f, 0.0f, 1.0f);
            box(0.0f, 0.0f, 0.0f, 0.014f, 0.50f, 0.004f);
        glPopMatrix();
    }

    // sangue escorrido abaixo da roda
    setPaint(0.30f, 0.00f, 0.01f);
    box(0.15f, -0.30f, z0 + 0.0185f, 0.045f, 0.34f, 0.004f);
    box(0.19f, -0.55f, z0 + 0.0185f, 0.020f, 0.30f, 0.004f);
}

void drawDoor(float doorOffsetY) {
    // A porta e' um bloco de altura fixa (DOORWAY_HEIGHT). A base dela
    // interpola entre "escondida acima do teto" (aberta) e "encostada no
    // chao" (fechada), conforme o progresso do fechamento (0 a 1).
    float progresso    = doorOffsetY / DOORWAY_HEIGHT;       // 0 = aberta, 1 = fechada
    float baseAberta   = ROOM_HEIGHT;                        // some acima do teto
    float baseFechada  = 0.0f;                                // encosta no chao
    float base         = baseAberta + (baseFechada - baseAberta) * progresso;
    float centerY      = base + DOORWAY_HEIGHT * 0.5f;

    const float W = DOORWAY_HALF_W * 2.0f, H = DOORWAY_HEIGHT;

    glEnable(GL_NORMALIZE); // glScalef nao uniforme: refaz as normais
    glPushMatrix();
        glTranslatef(0.0f, centerY, ROOM_FRONT_Z + 0.02f);
        // Os detalhes sao desenhados no mesmo referencial da laje, entao
        // acompanham a porta quando ela sobe e desce.
        setDoorMaterial();
        glPushMatrix();
            glScalef(W, H, 0.08f);
            glutSolidCube(1.0);                               // a laje de aco
        glPopMatrix();
        drawDoorDetails(W, H);
    glPopMatrix();
    glDisable(GL_NORMALIZE);
}

// ---------------------------------------------------------------
// Interruptor da porta
// ---------------------------------------------------------------
// Um painelzinho na parede, ao lado da abertura, com uma alavanca (sobe
// = porta aberta, desce = fechada) e um LED que brilha no escuro:
// verde = aberta, vermelho = fechada, apagado = sem energia.
// O LED e' EMISSIVO: desligamos a iluminacao pra desenha-lo, entao ele
// mantem a cor mesmo no escuro. O halo em volta usa transparencia
// aditiva (GL_ONE), que soma luz em vez de tampar, e por isso deve ser
// desenhado DEPOIS das paredes.
void drawDoorSwitch(bool doorClosing, bool hasPower) {
    // No meio da coluna direita da parede frontal
    const float x = (DOORWAY_HALF_W + ROOM_HALF_WIDTH) * 0.5f;
    const float y = 1.25f;
    const float z = ROOM_FRONT_Z + 0.03f;

    // caixa do painel e alavanca (com iluminacao normal)
    setPaint(0.14f, 0.14f, 0.16f);
    box(x, y, z, 0.24f, 0.42f, 0.05f);

    setPaint(0.70f, 0.70f, 0.74f);
    float leverY = y - 0.08f + (doorClosing ? -0.035f : 0.035f); // posicao da alavanca
    box(x, leverY, z + 0.05f, 0.05f, 0.11f, 0.05f);

    // cor do LED conforme o estado
    float r, g, b;
    if (!hasPower)        { r = 0.10f; g = 0.02f; b = 0.02f; } // apagado
    else if (doorClosing) { r = 1.00f; g = 0.12f; b = 0.08f; } // fechada: vermelho
    else                  { r = 0.15f; g = 1.00f; b = 0.35f; } // aberta: verde

    // glPushAttrib/glPopAttrib guardam e restauram os flags de estado
    // que mexemos aqui (luz, blending, escrita de profundidade, cor).
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    glColor3f(r, g, b);
    glPushMatrix();
        glTranslatef(x, y + 0.14f, z + 0.03f);
        glutSolidSphere(0.035, 16, 12);
        if (hasPower) { // halo suave em volta do LED
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
            glDepthMask(GL_FALSE);
            glColor4f(r, g, b, 0.20f);
            glutSolidSphere(0.09, 16, 12);
        }
    glPopMatrix();
    glPopAttrib();
}

// ===============================================================
// MOVEIS DA SALA
// Os moveis grandes (mesas e armario) sao desenhados num sistema de
// coordenadas LOCAL: dentro dele pensamos sempre igual ("+Z e' a frente
// do movel, +Y e' pra cima") e um unico glTranslatef/glRotatef posiciona
// o conjunto no mundo. Isso e' a hierarquia de transformacoes: monitor,
// teclado, caneca etc. sao "filhos" da mesa e se movem junto com ela.
// ===============================================================

// Luz do monitor: uma luz pontual azul-esverdeada, fraca e com
// atenuacao (some rapido com a distancia), que tremula. Faz a mesa e o
// teclado receberem um brilho de tela no escuro. Com o monitor desligado
// a intensidade e' zero.
// Formula da atenuacao do OpenGL: 1 / (kc + kl*d + kq*d*d).
// Como o glLight transforma a posicao pela matriz atual, esta funcao
// deve ser chamada depois do gluLookAt (o display ja faz isso).
static void setupMonitorLight(float x, float y, float z, float time, bool on) {
    float flick = (on ? 1.0f : 0.0f) * (0.85f + 0.15f * sinf(time * 37.0f)); // tremulacao rapida
    const GLfloat pos[]     = { x, y, z, 1.0f };       // w=1: luz pontual (nao direcional)
    const GLfloat diffuse[] = { 0.15f * flick, 0.75f * flick, 0.60f * flick, 1.0f };
    const GLfloat none[]    = { 0.0f, 0.0f, 0.0f, 1.0f };
    glLightfv(MONITOR_LIGHT, GL_POSITION, pos);
    glLightfv(MONITOR_LIGHT, GL_DIFFUSE,  diffuse);
    glLightfv(MONITOR_LIGHT, GL_AMBIENT,  none);
    glLightfv(MONITOR_LIGHT, GL_SPECULAR, none);
    glLightf(MONITOR_LIGHT, GL_CONSTANT_ATTENUATION,  1.0f);
    glLightf(MONITOR_LIGHT, GL_LINEAR_ATTENUATION,    0.5f);
    glLightf(MONITOR_LIGHT, GL_QUADRATIC_ATTENUATION, 2.0f);
    glEnable(MONITOR_LIGHT);
}

// Computador antigo (monitor de tubo + teclado + mouse), na origem do
// sistema local, com a tela virada pra +Z.
// Desligado: tela apagada. Ligado: mostra um MAPA do corredor visto de
// cima, com o fundo a' esquerda e a sua porta a' direita:
//   - o ponto vermelho e' o monstro (f = quao perto da porta, 0 a 1;
//     lateral = posicao esquerda/direita no corredor, -1 a 1);
//   - os quadradinhos amarelos sao as lampadas, piscando de verdade;
//   - "chiado" (pontinhos aleatorios) aumenta conforme ele se aproxima;
//   - uma linha de varredura sobe e desce, e o ponto REC pisca.
static void drawComputer(float time, bool on, float f, float lateral) {
    const float top = 0.75f; // altura do tampo da mesa

    setPaint(0.55f, 0.53f, 0.46f);                          // bege gasto
    box(0.0f, top + 0.015f, -0.08f, 0.26f, 0.03f, 0.22f);   // base
    box(0.0f, top + 0.080f, -0.08f, 0.07f, 0.12f, 0.07f);   // pescoco
    setPaint(0.60f, 0.58f, 0.50f);
    box(0.0f, top + 0.300f, -0.12f, 0.48f, 0.38f, 0.34f);   // corpo do monitor (frente em z=+0.05)

    setPaint(0.10f, 0.10f, 0.11f);
    box(0.0f, top + 0.015f, 0.22f, 0.42f, 0.03f, 0.14f);    // teclado
    box(0.30f, top + 0.012f, 0.22f, 0.05f, 0.025f, 0.08f);  // mouse

    // --- tela (emissiva: sem iluminacao) ---
    const float sz = 0.052f;                                 // um pouco a' frente do corpo
    const float sx0 = -0.19f, sx1 = 0.19f, sy0 = 0.93f, sy1 = 1.17f;
    float flick = 0.85f + 0.15f * sinf(time * 37.0f);

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT | GL_POINT_BIT);
    glDisable(GL_LIGHTING);

    if (on) glColor3f(0.03f * flick, 0.16f * flick, 0.12f * flick);  // fundo da tela
    else    glColor3f(0.010f, 0.015f, 0.012f);                       // tela apagada
    glBegin(GL_QUADS);
        glVertex3f(sx0, sy0, sz);
        glVertex3f(sx1, sy0, sz);
        glVertex3f(sx1, sy1, sz);
        glVertex3f(sx0, sy1, sz);
    glEnd();

    if (on) {
        const float lz = sz + 0.002f;                        // camada de cima da tela
        const float mx0 = -0.17f, mx1 = 0.17f;              // fundo do corredor (esq.) e porta (dir.)
        const float my = 1.05f, mh = 0.055f;                // eixo e meia largura do mapa
        glColor3f(0.25f * flick, 0.95f * flick, 0.60f * flick);

        // paredes do corredor (2 linhas), fundo (esq.) e a sua porta (dir., mais grossa)
        glLineWidth(1.5f);
        glBegin(GL_LINES);
            glVertex3f(mx0, my + mh, lz); glVertex3f(mx1, my + mh, lz);
            glVertex3f(mx0, my - mh, lz); glVertex3f(mx1, my - mh, lz);
            glVertex3f(mx0, my - mh, lz); glVertex3f(mx0, my + mh, lz);
        glEnd();
        glLineWidth(3.0f);
        glBegin(GL_LINES);
            glVertex3f(mx1, my - mh, lz); glVertex3f(mx1, my + mh, lz);
        glEnd();

        // lampadas do corredor: o brilho de cada uma segue o nivel real da piscada
        glBegin(GL_QUADS);
        for (int w = 0; w < 2; ++w) {
            float lf = (w == 0) ? 0.67f : 0.30f;             // posicao ao longo do corredor (0 = fundo)
            float lv = lampLevel(time, w == 0 ? 0.0f : 3.1f);
            float x = mx0 + (mx1 - mx0) * lf;
            glColor3f((0.15f + 0.85f * lv) * 0.9f, (0.15f + 0.85f * lv) * 0.8f, 0.05f);
            glVertex3f(x - 0.007f, my + mh + 0.010f, lz);
            glVertex3f(x + 0.007f, my + mh + 0.010f, lz);
            glVertex3f(x + 0.007f, my + mh + 0.024f, lz);
            glVertex3f(x - 0.007f, my + mh + 0.024f, lz);
        }
        glEnd();

        // o monstro: ponto vermelho que pulsa
        float bx = mx0 + (mx1 - mx0) * f;
        float by = my + lateral * mh * 0.8f;
        float bs = 0.011f + 0.003f * sinf(time * 8.0f);
        glColor3f(1.0f, 0.12f, 0.08f);
        glBegin(GL_QUADS);
            glVertex3f(bx - bs, by - bs, lz + 0.001f);
            glVertex3f(bx + bs, by - bs, lz + 0.001f);
            glVertex3f(bx + bs, by + bs, lz + 0.001f);
            glVertex3f(bx - bs, by + bs, lz + 0.001f);
        glEnd();

        // chiado: mais forte quanto mais perto ele esta'. Posicoes vem de
        // hash2 trocado 24 vezes por segundo, entao o chiado "cintila".
        float closeness = clampf01((f - 0.6f) * 2.5f);
        int dots = 8 + (int)(70.0f * closeness);
        int tick = (int)(time * 24.0f);
        glPointSize(2.0f);
        glBegin(GL_POINTS);
        for (int k = 0; k < dots; ++k) {
            float c = 0.3f + 0.7f * hash2(k, tick + 3);
            glColor3f(0.2f * c, 0.9f * c, 0.6f * c);
            glVertex3f(sx0 + (sx1 - sx0) * hash2(k * 7 + 1, tick),
                       sy0 + (sy1 - sy0) * hash2(k * 13 + 5, tick + 99), lz);
        }
        glEnd();

        // linha de varredura percorrendo a tela
        float sy = sy0 + (sy1 - sy0) * fmodf(time * 0.35f, 1.0f);
        glLineWidth(1.0f);
        glColor3f(0.10f * flick, 0.45f * flick, 0.30f * flick);
        glBegin(GL_LINES);
            glVertex3f(sx0, sy, lz); glVertex3f(sx1, sy, lz);
        glEnd();

        // ponto vermelho "REC": aceso 0.7 s a cada 1.2 s (fmodf = resto da divisao)
        if (fmodf(time, 1.2f) < 0.7f) {
            glColor3f(1.0f, 0.1f, 0.1f);
            glBegin(GL_QUADS);
                glVertex3f(-0.175f, 1.135f, lz);
                glVertex3f(-0.150f, 1.135f, lz);
                glVertex3f(-0.150f, 1.160f, lz);
                glVertex3f(-0.175f, 1.160f, lz);
            glEnd();
        }
    }
    glPopAttrib();
}

// Ventilador de mesa, na origem do sistema local. Tem tres niveis de
// hierarquia, cada um herdando a transformacao do anterior:
//   base -> cabeca (oscila girando em Y) -> helice (gira em Z)
// As duas rotacoes dependem do tempo, entao o ventilador se mexe sozinho.
static void drawFan(float time) {
    const float top = 0.75f;

    glPushMatrix();
    glTranslatef(0.0f, top, 0.0f);

    setPaint(0.55f, 0.60f, 0.66f);
    box(0.0f, 0.015f, 0.0f, 0.17f, 0.03f, 0.17f);  // base
    box(0.0f, 0.130f, 0.0f, 0.03f, 0.22f, 0.03f);  // haste

    glTranslatef(0.0f, 0.27f, 0.0f);               // sobe ate' o topo da haste (cabeca)
    glRotatef(35.0f * sinf(time * 0.7f), 0.0f, 1.0f, 0.0f); // oscila +-35 graus, devagar
    glutSolidSphere(0.05, 12, 10);                 // motor

    glTranslatef(0.0f, 0.0f, 0.06f);               // avanca ate' a frente do motor
    glutSolidTorus(0.006, 0.17, 6, 24);            // aro da gaiola

    glRotatef(time * 900.0f, 0.0f, 0.0f, 1.0f);    // helice: 900 graus/s
    for (int i = 0; i < 3; ++i) {                  // 3 pas, defasadas de 120 graus
        glPushMatrix();
            glRotatef(120.0f * (float)i, 0.0f, 0.0f, 1.0f);
            glTranslatef(0.0f, 0.075f, 0.0f);
            glRotatef(20.0f, 0.0f, 1.0f, 0.0f);    // inclina a pa (passo da helice)
            box(0.0f, 0.0f, 0.0f, 0.06f, 0.15f, 0.004f);
        glPopMatrix();
    }
    glutSolidSphere(0.02, 10, 8);                  // cubo central da helice

    glPopMatrix();
}

// Caneca de cafe (cilindro + "cafe" escuro por cima + alca de torus).
static void drawMug() {
    glPushMatrix();
        glTranslatef(0.42f, 0.75f, 0.02f);
        setPaint(0.75f, 0.72f, 0.65f);
        cylinderY(0.045f, 0.10f, 16);
        glTranslatef(0.0f, 0.0985f, 0.0f);
        setPaint(0.08f, 0.04f, 0.02f);
        cylinderY(0.040f, 0.003f, 16);             // superficie do cafe
        glTranslatef(0.05f, -0.0485f, 0.0f);
        setPaint(0.75f, 0.72f, 0.65f);
        glutSolidTorus(0.008, 0.030, 6, 12);       // alca
    glPopMatrix();
}

// Pilha de papeis levemente girados, com uma mancha vermelha escura.
static void drawPapers() {
    glPushMatrix();
        glTranslatef(0.12f, 0.753f, 0.14f);
        glRotatef(12.0f, 0.0f, 1.0f, 0.0f);
        setPaint(0.82f, 0.80f, 0.70f);
        box(0.0f, 0.0f, 0.0f, 0.21f, 0.004f, 0.30f);
        glTranslatef(0.03f, 0.004f, -0.02f);
        glRotatef(-20.0f, 0.0f, 1.0f, 0.0f);
        box(0.0f, 0.0f, 0.0f, 0.21f, 0.004f, 0.30f);
        setPaint(0.40f, 0.03f, 0.03f);
        box(0.04f, 0.0035f, 0.05f, 0.05f, 0.001f, 0.035f);   // mancha
    glPopMatrix();
}

// Telefone antigo com um LED de "recado" piscando.
static void drawPhone(float time) {
    glPushMatrix();
        glTranslatef(0.64f, 0.75f, 0.12f);
        setPaint(0.62f, 0.57f, 0.47f);
        box(0.0f, 0.03f, 0.0f, 0.20f, 0.06f, 0.18f);          // base
        box(0.0f, 0.075f, -0.02f, 0.22f, 0.03f, 0.055f);      // fone
        box(-0.10f, 0.065f, -0.02f, 0.05f, 0.05f, 0.07f);     // ponta do fone
        box( 0.10f, 0.065f, -0.02f, 0.05f, 0.05f, 0.07f);
        setPaint(0.10f, 0.10f, 0.10f);
        box(0.0f, 0.062f, 0.05f, 0.09f, 0.004f, 0.07f);       // disco de discar
        // LED vermelho (emissivo) piscando. O glowOn vem DEPOIS do
        // setPaint, porque o setPaint zera a emissao.
        setPaint(0.5f, 0.05f, 0.05f);
        if (fmodf(time, 1.0f) < 0.5f) glowOn(1.0f, 0.1f, 0.1f);
        glPushMatrix();
            glTranslatef(0.08f, 0.065f, 0.07f);
            glutSolidSphere(0.012, 8, 6);
        glPopMatrix();
        glowOff();
    glPopMatrix();
}

// Radio velho com antena e botao (na mesa lateral).
static void drawRadio() {
    glPushMatrix();
        glTranslatef(0.18f, 0.75f, 0.0f);
        setPaint(0.20f, 0.14f, 0.10f);
        box(0.0f, 0.08f, 0.0f, 0.30f, 0.16f, 0.10f);
        setPaint(0.08f, 0.06f, 0.05f);
        box(-0.05f, 0.08f, 0.052f, 0.14f, 0.11f, 0.004f);     // alto-falante
        setPaint(0.55f, 0.50f, 0.40f);
        glPushMatrix();
            glTranslatef(0.09f, 0.08f, 0.056f);
            glutSolidSphere(0.022, 10, 8);                    // botao
        glPopMatrix();
        setPaint(0.55f, 0.58f, 0.62f);
        glPushMatrix();
            glTranslatef(0.12f, 0.16f, -0.02f);
            glRotatef(-25.0f, 0.0f, 0.0f, 1.0f);              // antena inclinada
            box(0.0f, 0.17f, 0.0f, 0.008f, 0.34f, 0.008f);
        glPopMatrix();
    glPopMatrix();
}

// Duas caixas de papelao empilhadas e tortas (na mesa lateral).
static void drawCardboardBoxes() {
    glPushMatrix();
        glTranslatef(-0.42f, 0.75f, 0.0f);
        glRotatef(10.0f, 0.0f, 1.0f, 0.0f);
        setPaint(0.45f, 0.33f, 0.20f);
        box(0.0f, 0.10f, 0.0f, 0.30f, 0.20f, 0.25f);
        glTranslatef(0.0f, 0.20f, 0.0f);
        glRotatef(-25.0f, 0.0f, 1.0f, 0.0f);
        setPaint(0.40f, 0.29f, 0.17f);
        box(0.0f, 0.07f, 0.0f, 0.22f, 0.14f, 0.20f);
    glPopMatrix();
}

// Poster na parede atras da mesa lateral: o mascote sorridente do lugar.
// Fica no plano z = -0.38 do sistema local da mesa (a parede), com cada
// camada alguns milimetros a' frente da anterior pra evitar o
// "z-fighting" (duas superficies no mesmo plano brigando por quem
// aparece).
static void drawPoster() {
    const float cy = 1.75f; // altura do centro do poster

    setPaint(0.18f, 0.08f, 0.06f);
    box(0.0f, cy, -0.370f, 0.62f, 0.86f, 0.02f);   // moldura

    setPaint(0.85f, 0.80f, 0.62f);                 // papel
    glBegin(GL_QUADS);
        glNormal3f(0.0f, 0.0f, 1.0f);
        glVertex3f(-0.27f, cy - 0.39f, -0.359f);
        glVertex3f( 0.27f, cy - 0.39f, -0.359f);
        glVertex3f( 0.27f, cy + 0.39f, -0.359f);
        glVertex3f(-0.27f, cy + 0.39f, -0.359f);
    glEnd();

    setPaint(0.90f, 0.72f, 0.20f);                 // rosto amarelo
    disc(0.0f, cy + 0.09f, -0.357f, 0.15f);

    setPaint(0.05f, 0.03f, 0.03f);                 // olhos
    disc(-0.060f, cy + 0.14f, -0.355f, 0.028f);
    disc( 0.060f, cy + 0.14f, -0.355f, 0.028f);

    // sorriso largo: faixa curva (arco de 200 a 340 graus, ou seja, a
    // parte de baixo de um circulo) desenhada com GL_QUAD_STRIP
    glBegin(GL_QUAD_STRIP);
        glNormal3f(0.0f, 0.0f, 1.0f);
        const int N = 16;
        for (int i = 0; i <= N; ++i) {
            float a = (200.0f + 140.0f * (float)i / (float)N) * 3.14159265f / 180.0f;
            glVertex3f(0.110f * cosf(a), cy + 0.13f + 0.110f * sinf(a), -0.355f);
            glVertex3f(0.088f * cosf(a), cy + 0.13f + 0.088f * sinf(a), -0.355f);
        }
    glEnd();

    setPaint(0.55f, 0.08f, 0.08f);                 // "texto" do poster: barras vermelhas
    box(0.0f, cy - 0.20f, -0.357f, 0.42f, 0.05f, 0.004f);
    box(0.0f, cy - 0.29f, -0.357f, 0.34f, 0.02f, 0.004f);
    box(0.0f, cy - 0.34f, -0.357f, 0.38f, 0.02f, 0.004f);
}

// Mesa generica na origem do sistema local: tampo (topo em y = 0.75),
// 4 pes e paineis de tras e de frente. w = largura (X), d = fundo (Z).
static void drawDesk(float w, float d) {
    setPaint(0.30f, 0.20f, 0.12f);                       // madeira escura
    box(0.0f, 0.725f, 0.0f, w, 0.05f, d);                // tampo
    texPlateY(0.0f, 0.7505f, 0.0f, w, d, TEX_WOOD, 0.8f, 1.0f, 1.0f, 1.0f);  // madeira com veios por cima
    for (int i = -1; i <= 1; i += 2) {                   // 4 pes, um em cada canto
        for (int j = -1; j <= 1; j += 2) {
            box((w * 0.5f - 0.05f) * i, 0.35f, (d * 0.5f - 0.05f) * j, 0.06f, 0.70f, 0.06f);
        }
    }
    setPaint(0.22f, 0.15f, 0.09f);
    box(0.0f, 0.45f, -d * 0.5f + 0.03f, w - 0.10f, 0.50f, 0.02f);   // painel de tras
    box(0.0f, 0.45f,  d * 0.5f - 0.03f, w - 0.10f, 0.50f, 0.02f);   // painel da frente
}

// A MESA NA FRENTE DO JOGADOR. A camera (olhos) fica a ~0.85 m da borda
// e 45 cm acima do tampo, o que da' a sensacao de estar sentado atras
// dela. O centro da mesa fica livre pra enxergar o corredor: o monitor
// fica a' esquerda (virado um pouco pro jogador) e o ventilador a' direita.
static void drawFrontDesk(float zc, float time, bool monitorOn, float f, float lateral) {
    float w = fminf(2.2f, 2.0f * ROOM_HALF_WIDTH - 0.8f);

    glPushMatrix();
    glTranslatef(0.0f, 0.0f, zc);

    drawDesk(w, 0.7f);

    glPushMatrix();                                  // computador (filho da mesa)
        glTranslatef(-0.65f, 0.0f, -0.05f);
        glRotatef(14.0f, 0.0f, 1.0f, 0.0f);          // gira a tela na direcao do jogador
        drawComputer(time, monitorOn, f, lateral);
    glPopMatrix();

    glPushMatrix();                                  // ventilador (filho da mesa)
        glTranslatef(0.85f, 0.0f, -0.12f);
        glRotatef(-20.0f, 0.0f, 1.0f, 0.0f);
        drawFan(time);
    glPopMatrix();

    drawMug();
    drawPapers();
    drawPhone(time);

    glPopMatrix();
}

// Mesa lateral, encostada na parede esquerda, com radio, caixas e o poster.
// (x, z) e' o ponto do chao sob o centro da mesa.
static void drawSideDesk(float x, float z) {
    glPushMatrix();
    glTranslatef(x, 0.0f, z);
    glRotatef(90.0f, 0.0f, 1.0f, 0.0f); // a "frente" da mesa (+Z local) passa a apontar pro centro da sala (+X)

    drawDesk(1.5f, 0.7f);
    drawRadio();
    drawCardboardBoxes();
    drawPoster();

    glPopMatrix();
}

// Armario de aco (tipo vestiario), encostado na parede direita, virado
// pro centro da sala. Duas portas com uma fresta no meio, venezianas no
// alto e puxadores.
static void drawCabinet(float x, float z) {
    glPushMatrix();
    glTranslatef(x, 0.0f, z);
    glRotatef(-90.0f, 0.0f, 1.0f, 0.0f); // +Z local passa a apontar pra -X (pro centro da sala)

    setPaint(0.22f, 0.28f, 0.25f);                        // verde acinzentado
    box(0.0f, 0.95f, 0.0f, 0.90f, 1.90f, 0.60f);          // corpo

    texPlateZ(0.0f, 0.95f, 0.3003f, 0.90f, 1.90f, TEX_METAL, 1.0f, 0.55f, 0.85f, 0.65f); // metal esverdeado

    setPaint(0.05f, 0.06f, 0.06f);
    box(0.0f, 0.95f, 0.301f, 0.008f, 1.80f, 0.004f);      // fresta entre as portas
    for (int k = 0; k < 5; ++k) {                         // venezianas nas duas portas
        float vy = 1.72f - 0.05f * (float)k;
        box(-0.22f, vy, 0.302f, 0.30f, 0.015f, 0.004f);
        box( 0.22f, vy, 0.302f, 0.30f, 0.015f, 0.004f);
    }

    setPaint(0.70f, 0.70f, 0.72f);
    box(-0.06f, 0.95f, 0.320f, 0.02f, 0.16f, 0.03f);      // puxadores
    box( 0.06f, 0.95f, 0.320f, 0.02f, 0.16f, 0.03f);

    glPopMatrix();
}

// Moldura de seguranca em volta da abertura (amarelo escuro). E' um pouco
// mais grossa que a porta pra que ela "entre" na moldura ao subir/descer.
static void drawDoorFrame() {
    const float t = 0.09f;                 // largura da moldura
    const float z = ROOM_FRONT_Z + 0.045f;
    setPaint(0.45f, 0.36f, 0.04f);
    box(-DOORWAY_HALF_W - t * 0.5f, DOORWAY_HEIGHT * 0.5f, z, t, DOORWAY_HEIGHT, 0.05f);
    box( DOORWAY_HALF_W + t * 0.5f, DOORWAY_HEIGHT * 0.5f, z, t, DOORWAY_HEIGHT, 0.05f);
    box(0.0f, DOORWAY_HEIGHT + t * 0.5f, z, DOORWAY_HALF_W * 2.0f + 2.0f * t, t, 0.05f);
}

// Tapete vermelho escuro no chao, embaixo da mesa e do jogador. Um quad
// 6 mm acima do chao (senao brigaria com ele por profundidade).
static void drawRug() {
    const float hx = ROOM_HALF_WIDTH * 0.45f;
    const float hz = 0.8f;
    const float zc = ROOM_BACK_Z - 1.6f;
    setPaint(0.30f, 0.05f, 0.07f);
    glBegin(GL_QUADS);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glVertex3f(-hx, 0.006f, zc + hz);
        glVertex3f( hx, 0.006f, zc + hz);
        glVertex3f( hx, 0.006f, zc - hz);
        glVertex3f(-hx, 0.006f, zc - hz);
    glEnd();
}

// Rodape e friso (divisa entre o azulejo e a tinta) nas paredes da sala.
static void drawTrim() {
    const float depth = ROOM_BACK_Z - ROOM_FRONT_Z;
    const float zMid  = (ROOM_FRONT_Z + ROOM_BACK_Z) * 0.5f;
    const float hw    = ROOM_HALF_WIDTH;
    const float ys[2] = { 0.04f, WAINSCOT_H };
    const float hs[2] = { 0.08f, 0.05f };
    for (int k = 0; k < 2; ++k) {
        if (k == 0) setPaint(0.06f, 0.06f, 0.06f); else setPaint(0.10f, 0.12f, 0.11f);
        box(-hw + 0.015f, ys[k], zMid, 0.03f, hs[k], depth);                       // esquerda
        box( hw - 0.015f, ys[k], zMid, 0.03f, hs[k], depth);                       // direita
        box(0.0f, ys[k], ROOM_BACK_Z - 0.015f, 2.0f * hw, hs[k], 0.03f);           // fundo
    }
}

// ---------------------------------------------------------------
// CORREDOR: lampadas piscando, canos, fios, avisos, sangue, porta do fundo
// ---------------------------------------------------------------

// Intensidade (0 a 1) de uma lampada de fluorescente quase morrendo, num
// ciclo de 7.3 s: aceso fraco e estavel, depois pisca rapido, depois
// apagao total. "phase" defasa as lampadas pra nao piscarem juntas.
static float lampLevel(float t, float phase) {
    float c = fmodf(t + phase, 7.3f);
    float lv;
    if (c > 6.4f)      lv = 0.0f;                                         // apagao
    else if (c > 5.6f) lv = (fmodf(t * 17.0f, 1.0f) < 0.45f) ? 1.0f : 0.1f; // pisca rapido
    else               lv = 0.30f + 0.05f * sinf(t * 2.0f);                // fraco e estavel
    // Perigo: 20 vezes por segundo sorteia (hash) se a lampada cai pra 10%.
    // Com o monstro colado na porta, ~35% dos instantes viram falha.
    if (g_lampDanger > 0.0f &&
        hash2((int)(t * 20.0f), (int)(phase * 10.0f) + 5) < 0.35f * g_lampDanger) lv *= 0.1f;
    return lv;
}

// Liga uma luz pontual esverdeada no teto do corredor, com a forca
// vinda do nivel da lampada. Atenuacao suave: alcanca uns 6 a 8 metros.
static void setupCorridorLight(GLenum light, float x, float y, float z, float level) {
    float k = CORRIDOR_LIGHT_LEVEL * level;
    const GLfloat pos[]     = { x, y, z, 1.0f };
    const GLfloat diffuse[] = { 0.55f * k, 0.70f * k, 0.55f * k, 1.0f };
    const GLfloat none[]    = { 0.0f, 0.0f, 0.0f, 1.0f };
    glLightfv(light, GL_POSITION, pos);
    glLightfv(light, GL_DIFFUSE,  diffuse);
    glLightfv(light, GL_AMBIENT,  none);
    glLightfv(light, GL_SPECULAR, none);
    glLightf(light, GL_CONSTANT_ATTENUATION,  1.0f);
    glLightf(light, GL_LINEAR_ATTENUATION,    0.2f);
    glLightf(light, GL_QUADRATIC_ATTENUATION, 0.15f);
    glEnable(light);
}

// Posicoes das duas lampadas ao longo do corredor (fracao do comprimento).
static float lampZ(int which) {
    float L = ROOM_FRONT_Z - CORRIDOR_FAR_Z;
    return ROOM_FRONT_Z - L * (which == 0 ? 0.33f : 0.70f);
}

// Suporte de lampada fluorescente: carcaca escura + tubo emissivo cuja
// luz acompanha o nivel de piscada.
static void drawLampFixture(float z, float level) {
    float y = CORRIDOR_HEIGHT - 0.04f;
    setPaint(0.10f, 0.10f, 0.10f);
    box(0.0f, y, z, 0.22f, 0.05f, 1.0f);
    setPaint(0.5f, 0.5f, 0.5f);
    glowOn(0.8f * level, 1.0f * level, 0.8f * level);
    box(0.0f, y - 0.035f, z, 0.12f, 0.02f, 0.85f);
    glowOff();
}

// Fio solto pendurado do teto, balancando de leve. Pivota no topo.
static void drawDangle(float x, float y, float z, float len, float time, float phase) {
    glPushMatrix();
        glTranslatef(x, y, z);
        glRotatef(8.0f * sinf(time * 1.1f + phase), 0.0f, 0.0f, 1.0f);
        setPaint(0.04f, 0.04f, 0.04f);
        box(0.0f, -len * 0.5f, 0.0f, 0.015f, len, 0.015f);
    glPopMatrix();
}

// Gota caindo de um cano ate' o chao, em loop (queda acelerada: frac^2).
static void drawDrip(float x, float yTop, float z, float phase, float time) {
    float frac = fmodf(time * 0.7f + phase, 1.0f);
    float y = yTop - yTop * frac * frac;
    setPaint(0.60f, 0.65f, 0.70f);
    glPushMatrix();
        glTranslatef(x, y, z);
        glutSolidSphere(0.012, 6, 5);
    glPopMatrix();
}

// Aviso colado na parede esquerda do corredor, com rabisco vermelho.
static void drawNotice(float y, float z) {
    setPaint(0.80f, 0.78f, 0.65f);
    box(-CORRIDOR_HALF_W + 0.006f, y, z, 0.004f, 0.40f, 0.30f);
    setPaint(0.45f, 0.03f, 0.03f);
    box(-CORRIDOR_HALF_W + 0.009f, y + 0.05f, z, 0.002f, 0.03f, 0.22f);
    box(-CORRIDOR_HALF_W + 0.009f, y - 0.05f, z, 0.002f, 0.03f, 0.16f);
}

static void drawCorridorProps(float time) {
    const float L  = ROOM_FRONT_Z - CORRIDOR_FAR_Z;
    const float CW = CORRIDOR_HALF_W;
    const float CH = CORRIDOR_HEIGHT;

    // --- lampadas piscando (luz + suporte) ---
    float levelA = lampLevel(time, 0.0f);
    float levelB = lampLevel(time, 3.1f);
    setupCorridorLight(CORRIDOR_LIGHT_A, 0.0f, CH - 0.2f, lampZ(0), levelA);
    setupCorridorLight(CORRIDOR_LIGHT_B, 0.0f, CH - 0.2f, lampZ(1), levelB);
    drawLampFixture(lampZ(0), levelA);
    drawLampFixture(lampZ(1), levelB);

    // --- canos ao longo das paredes e do teto ---
    setPaint(0.28f, 0.16f, 0.10f);                      // cano grosso enferrujado
    cylinderZ(-CW + 0.18f, CH - 0.35f, ROOM_FRONT_Z, L, 0.09f);
    setPaint(0.25f, 0.27f, 0.28f);                      // cano fino cinza
    shiny(0.4f, 20.0f);
    cylinderZ(-CW + 0.12f, CH - 0.70f, ROOM_FRONT_Z, L, 0.05f);
    shiny(0.0f, 0.0f);
    setPaint(0.30f, 0.30f, 0.28f);                      // duto grande no teto
    cylinderZ(CW - 0.35f, CH - 0.12f, ROOM_FRONT_Z, L, 0.12f);
    setPaint(0.22f, 0.20f, 0.18f);                      // cano baixo na parede direita
    cylinderZ(CW - 0.10f, 0.25f, ROOM_FRONT_Z, L, 0.05f);

    // flanges (aneis de emenda) a cada 3 m nos canos grossos
    setPaint(0.18f, 0.12f, 0.08f);
    for (float d = 1.5f; d < L; d += 3.0f) {
        float z = ROOM_FRONT_Z - d;
        cylinderZ(-CW + 0.18f, CH - 0.35f, z + 0.04f, 0.08f, 0.12f);
        cylinderZ( CW - 0.35f, CH - 0.12f, z + 0.04f, 0.08f, 0.15f);
    }

    // valvulas vermelhas penduradas no cano grosso (haste + roda)
    for (int k = 0; k < 2; ++k) {
        float z = ROOM_FRONT_Z - L * (0.25f + 0.35f * (float)k);
        glPushMatrix();
            glTranslatef(-CW + 0.18f, CH - 0.35f - 0.09f, z);
            setPaint(0.25f, 0.25f, 0.26f);
            glPushMatrix(); glRotatef(180.0f, 1.0f, 0.0f, 0.0f); cylinderY(0.015f, 0.18f, 8); glPopMatrix();
            glTranslatef(0.0f, -0.18f, 0.0f);
            glRotatef(90.0f, 1.0f, 0.0f, 0.0f);          // roda horizontal
            setPaint(0.55f, 0.05f, 0.05f);
            glutSolidTorus(0.012, 0.09, 6, 16);
        glPopMatrix();
    }

    // gotas caindo dos canos e fios soltos
    drawDrip(-CW + 0.12f, CH - 0.72f, ROOM_FRONT_Z - L * 0.18f, 0.0f, time);
    drawDrip(-CW + 0.18f, CH - 0.40f, ROOM_FRONT_Z - L * 0.52f, 0.5f, time);
    drawDangle( 0.30f, CH - 0.05f, ROOM_FRONT_Z - L * 0.20f, 0.55f, time, 0.0f);
    drawDangle(-0.25f, CH - 0.05f, ROOM_FRONT_Z - L * 0.45f, 0.80f, time, 2.0f);
    drawDangle( 0.20f, CH - 0.05f, ROOM_FRONT_Z - L * 0.80f, 0.65f, time, 4.0f);

    // avisos rabiscados na parede
    drawNotice(1.55f, ROOM_FRONT_Z - L * 0.15f);
    drawNotice(1.50f, ROOM_FRONT_Z - L * 0.42f);
    drawNotice(1.60f, ROOM_FRONT_Z - L * 0.68f);

    // rastro de sangue arrastado pelo piso, levando ate' a sala
    setPaint(0.28f, 0.00f, 0.01f);
    glBegin(GL_QUADS);
    for (int k = 0; k < 14; ++k) {
        float z = ROOM_FRONT_Z - 1.0f - 1.15f * (float)k;
        if (z < CORRIDOR_FAR_Z + 0.6f) break;
        float x = 0.45f * sinf(0.9f * (float)k);
        float w = 0.12f + 0.06f * (float)(k % 3);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glVertex3f(x - w,        0.004f, z + 0.5f);
        glVertex3f(x + w,        0.004f, z + 0.5f);
        glVertex3f(x + w * 0.7f, 0.004f, z - 0.5f);
        glVertex3f(x - w * 0.7f, 0.004f, z - 0.5f);
    }
    glEnd();

    // porta dupla no fundo do corredor, de onde o monstro sai
    float dy = fminf(CH - 0.2f, 2.3f) * 0.5f;
    float dh = dy * 2.0f;
    setPaint(0.20f, 0.20f, 0.22f);
    box(0.0f, dy, CORRIDOR_FAR_Z + 0.03f, 1.4f, dh, 0.05f);
    setPaint(0.04f, 0.04f, 0.05f);
    box(0.0f, dy, CORRIDOR_FAR_Z + 0.058f, 0.012f, dh, 0.004f);          // juncao das folhas
    setPaint(0.55f, 0.56f, 0.58f);
    box(-0.07f, dy, CORRIDOR_FAR_Z + 0.065f, 0.025f, 0.45f, 0.02f);      // barras de empurrar
    box( 0.07f, dy, CORRIDOR_FAR_Z + 0.065f, 0.025f, 0.45f, 0.02f);
    setPaint(0.5f, 0.05f, 0.05f);                                        // lampadinha vermelha de saida
    glowOn(0.9f * (0.4f + 0.6f * levelB), 0.05f, 0.05f);
    glPushMatrix();
        glTranslatef(0.0f, dh + 0.12f, CORRIDOR_FAR_Z + 0.06f);
        glutSolidSphere(0.05, 10, 8);
    glPopMatrix();
    glowOff();
}

// ===============================================================
// PONTO DE ENTRADA DOS MOVEIS E DAS LUZES
// ===============================================================
// Mobilia a sala e o corredor e configura as luzes extras (monitor e
// lampadas do corredor). E' chamada ANTES das paredes no display, porque
// as luzes precisam estar ligadas quando as paredes forem desenhadas.
// monitorOn = monitor ligado; monsterPos = onde o monstro esta' (o mapa
// da tela mostra isso). Todas as posicoes saem das constantes da sala
// (ROOM_*), entao os moveis acompanham se as dimensoes mudarem.
void drawRoomProps(float time, bool monitorOn, const Vector3& monsterPos) {
    // O GL_NORMALIZE faz o OpenGL refazer o comprimento das normais
    // depois de glScalef nao uniformes (as caixas esticadas); sem ele a
    // iluminacao dessas faces sairia errada.
    glEnable(GL_NORMALIZE);

    const float zMid       = (ROOM_FRONT_Z + ROOM_BACK_Z) * 0.5f;
    const float sideDeskX  = -ROOM_HALF_WIDTH + 0.38f;  // mesa lateral quase encostada na parede
    const float frontDeskZ = PLAYER_EYE_Z - 1.2f;       // borda da mesa a ~0.85 m dos olhos

    // posicao do monstro no mapa do monitor: f = 0 no fundo do corredor,
    // 1 na porta; lateral = -1 (parede esquerda) a 1 (direita)
    float f = clampf01((monsterPos.z - CORRIDOR_FAR_Z) / (ROOM_FRONT_Z - CORRIDOR_FAR_Z));
    float lateral = monsterPos.x / CORRIDOR_HALF_W;
    if (lateral < -1.0f) lateral = -1.0f;
    if (lateral >  1.0f) lateral =  1.0f;

    g_lampDanger = clampf01((f - 0.5f) * 2.0f);   // 0 ate' a metade do corredor, 1 colado na porta

    // brilho do monitor: um pouco a' frente da tela, do lado do jogador
    setupMonitorLight(-0.50f, 1.05f, frontDeskZ + 0.35f, time, monitorOn);

    drawRug();
    drawTrim();
    drawDoorFrame();
    drawFrontDesk(frontDeskZ, time, monitorOn, f, lateral);
    drawSideDesk(sideDeskX, zMid);
    drawCabinet(ROOM_HALF_WIDTH - 0.32f, ROOM_FRONT_Z + 1.5f);
    drawCorridorProps(time);

    glDisable(GL_NORMALIZE);
}

// ===============================================================
// DECALQUES DE SANGUE
// Um "decalque" e' um quad com uma textura que tem transparencia (canal
// alfa), colado um milimetro acima de uma superficie. Com blending
// ligado, so' a mancha aparece e o piso/parede continua visivel em volta.
// Escrita de profundidade desligada: o decalque nao esconde nada.
// Deve ser chamado DEPOIS das paredes e do piso (senao seriam cobertos).
// ===============================================================

// Decalque no chao, centro (x,z), meio-lado "half", girado "angleDeg".
static void floorDecal(float x, float z, float half, float angleDeg) {
    float a = angleDeg * 3.14159265f / 180.0f;
    float ux = cosf(a) * half, uz = sinf(a) * half;       // eixos do decalque girados
    float vx = -sinf(a) * half, vz = cosf(a) * half;
    const float y = 0.006f;
    setPaint(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(x - ux - vx, y, z - uz - vz);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(x + ux - vx, y, z + uz - vz);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(x + ux + vx, y, z + uz + vz);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(x - ux + vx, y, z - uz + vz);
    glEnd();
}

// Decalque numa parede perpendicular a X, em (x,y,z). nx = +1 se a parede
// olha pra +X (parede esquerda) ou -1 se olha pra -X (parede direita).
static void wallDecalX(float x, float y, float z, float half, float nx) {
    setPaint(1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
        glNormal3f(nx, 0.0f, 0.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(x, y - half, z - half);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(x, y - half, z + half);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(x, y + half, z + half);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(x, y + half, z - half);
    glEnd();
}

void drawDecals() {
    const float F  = ROOM_FRONT_Z;
    const float CW = CORRIDOR_HALF_W;

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    useTexture(TEX_BLOOD);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // pocas no chao do corredor, perto da porta e mais ao fundo
    floorDecal( 0.20f, F - 1.8f, 1.30f,  20.0f);
    floorDecal(-0.50f, F - 6.8f, 0.90f, 110.0f);
    floorDecal( 0.40f, F - 12.5f, 1.10f, 200.0f);
    // respingos nas paredes do corredor
    wallDecalX(-CW + 0.004f, 1.55f, F - 4.4f, 0.75f,  1.0f);
    wallDecalX( CW - 0.004f, 1.20f, F - 9.5f, 0.65f, -1.0f);
    // sangue na sala, junto da abertura
    floorDecal( 0.50f, F + 1.2f, 0.90f, 65.0f);

    glPopAttrib();
}