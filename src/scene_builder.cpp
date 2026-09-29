#include "scene_builder.h"
#include "lighting.h"
#include "bezier.h" // Vector3

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

#include <cmath>

using namespace Scene;

// Qual luz do OpenGL representa o brilho do monitor. A GL_LIGHT0 e' a
// lanterna (ver lighting.cpp). Se a GL_LIGHT1 ja' estiver em uso em
// outro lugar, troque aqui por GL_LIGHT2.
static const GLenum MONITOR_LIGHT = GL_LIGHT1;

// ---------------------------------------------------------------
// Helpers de desenho
// ---------------------------------------------------------------

// Helper: um quad generico com normal explicita (evita repetir os 4
// glVertex3f + glNormal3f em toda parede/piso). A NORMAL e' o vetor
// perpendicular a' superficie; a iluminacao usa ela pra saber o quanto
// a face esta' virada pra luz.
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
// nele). Chama tambem glColor porque, se o projeto usar
// glColorMaterial, e' o glColor que manda no material.
static void setPaint(float r, float g, float b) {
    const GLfloat c[]   = { r, g, b, 1.0f };
    const GLfloat off[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, c);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, off);
    glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, off);
    glColor3f(r, g, b);
}

// Caixa (paralelepipedo) centrada em (cx,cy,cz) com dimensoes
// (sx,sy,sz). E' um cubo unitario do GLUT esticado por glScalef e
// movido por glTranslatef: com esse unico helper montamos mesa,
// armario, monitor, teclado etc.
static void box(float cx, float cy, float cz, float sx, float sy, float sz) {
    glPushMatrix();
        glTranslatef(cx, cy, cz);
        glScalef(sx, sy, sz);
        glutSolidCube(1.0);
    glPopMatrix();
}

// Disco (circulo preenchido) no plano XY, virado pra +Z, em (cx,cy,z).
// Leque de triangulos: um vertice no centro e os demais na borda.
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

// ---------------------------------------------------------------
// Estrutura da sala
// ---------------------------------------------------------------
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
// Cada movel e' uma funcao separada, montada com box()/disc(). Os
// moveis grandes (mesa e armario) sao desenhados num sistema de
// coordenadas LOCAL, girado pra ficar encostado na parede: dentro dele
// pensamos sempre igual ("+Z e' a frente do movel, +Y e' pra cima") e
// um unico glTranslatef/glRotatef posiciona o conjunto no mundo. Isso e'
// a hierarquia de transformacoes: o monitor, o teclado, o ventilador e
// o poster sao "filhos" da mesa e se movem junto com ela.
// ===============================================================

// Luz do monitor: uma luz pontual azul-esverdeada, fraca e com
// atenuacao (some rapido com a distancia), que tremula. Faz a mesa, o
// poster e a parede ao lado receberem um brilho de tela no escuro.
// Formula da atenuacao do OpenGL: 1 / (kc + kl*d + kq*d*d).
// Como o glLight transforma a posicao pela matriz atual, esta funcao
// deve ser chamada depois do gluLookAt (o display ja faz isso).
static void setupMonitorLight(float x, float y, float z, float time) {
    float flick = 0.85f + 0.15f * sinf(time * 37.0f); // tremulacao rapida
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

// Computador antigo (monitor de tubo + teclado), em coordenadas locais
// da mesa. A tela e' emissiva e mostra uma "camera de seguranca": um
// corredor em perspectiva desenhado com linhas, mais um ponto vermelho
// de gravacao (REC) que pisca.
static void drawComputer(float time) {
    const float top = 0.75f; // altura do tampo da mesa

    glPushMatrix();
    glTranslatef(-0.15f, 0.0f, 0.0f); // desloca o computador pro lado esquerdo do tampo

    setPaint(0.55f, 0.53f, 0.46f);                          // bege gasto
    box(0.0f, top + 0.015f, -0.08f, 0.26f, 0.03f, 0.22f);   // base
    box(0.0f, top + 0.080f, -0.08f, 0.07f, 0.12f, 0.07f);   // pescoco
    setPaint(0.60f, 0.58f, 0.50f);
    box(0.0f, top + 0.300f, -0.12f, 0.48f, 0.38f, 0.34f);   // corpo do monitor (frente em z=+0.05)

    setPaint(0.10f, 0.10f, 0.11f);
    box(0.0f, top + 0.015f, 0.22f, 0.42f, 0.03f, 0.14f);    // teclado

    // --- tela (emissiva: sem iluminacao) ---
    const float sz = 0.052f;                                 // um pouco a' frente do corpo
    const float sx0 = -0.19f, sx1 = 0.19f, sy0 = 0.93f, sy1 = 1.17f;
    float flick = 0.85f + 0.15f * sinf(time * 37.0f);

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT | GL_LINE_BIT);
    glDisable(GL_LIGHTING);

    glColor3f(0.03f * flick, 0.16f * flick, 0.12f * flick);  // fundo da tela
    glBegin(GL_QUADS);
        glVertex3f(sx0, sy0, sz);
        glVertex3f(sx1, sy0, sz);
        glVertex3f(sx1, sy1, sz);
        glVertex3f(sx0, sy1, sz);
    glEnd();

    // Corredor em perspectiva: retangulo pequeno no meio ligado aos
    // cantos da tela. (Fuga central = sensacao de profundidade.)
    const float lz = sz + 0.002f;
    const float ix0 = -0.05f, ix1 = 0.05f, iy0 = 1.015f, iy1 = 1.085f;
    glLineWidth(1.5f);
    glColor3f(0.25f * flick, 0.95f * flick, 0.60f * flick);
    glBegin(GL_LINES);
        glVertex3f(sx0, sy0, lz); glVertex3f(ix0, iy0, lz);
        glVertex3f(sx1, sy0, lz); glVertex3f(ix1, iy0, lz);
        glVertex3f(sx1, sy1, lz); glVertex3f(ix1, iy1, lz);
        glVertex3f(sx0, sy1, lz); glVertex3f(ix0, iy1, lz);
    glEnd();
    glBegin(GL_LINE_LOOP);
        glVertex3f(ix0, iy0, lz);
        glVertex3f(ix1, iy0, lz);
        glVertex3f(ix1, iy1, lz);
        glVertex3f(ix0, iy1, lz);
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
    glPopAttrib();

    glPopMatrix();
}

// Ventilador de mesa, em coordenadas locais da mesa. Tem tres niveis
// de hierarquia, cada um herdando a transformacao do anterior:
//   base -> cabeca (oscila girando em Y) -> helice (gira em Z)
// As duas rotacoes dependem do tempo, entao o ventilador se mexe sozinho.
static void drawFan(float time) {
    const float top = 0.75f;

    glPushMatrix();
    glTranslatef(0.55f, top, -0.02f);

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

// Poster na parede atras da mesa: o mascote sorridente do lugar. Fica
// no plano z = -0.38 do sistema local da mesa (a parede), com cada
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

// Mesa + tudo que esta' em cima/atras dela, encostada na parede esquerda.
// (x, z) e' o ponto do chao sob o centro da mesa.
static void drawDeskSet(float x, float z, float time) {
    glPushMatrix();
    glTranslatef(x, 0.0f, z);
    glRotatef(90.0f, 0.0f, 1.0f, 0.0f); // gira o sistema local: a "frente" da mesa (+Z local) passa a apontar pro centro da sala (+X)

    setPaint(0.30f, 0.20f, 0.12f);                       // madeira escura
    box(0.0f, 0.725f, 0.0f, 1.5f, 0.05f, 0.7f);          // tampo (topo em y=0.75)
    for (int i = -1; i <= 1; i += 2) {                   // 4 pes, um em cada canto
        for (int j = -1; j <= 1; j += 2) {
            box(0.70f * i, 0.35f, 0.30f * j, 0.06f, 0.70f, 0.06f);
        }
    }
    setPaint(0.22f, 0.15f, 0.09f);
    box(0.0f, 0.45f, -0.32f, 1.4f, 0.50f, 0.02f);        // painel de tras

    drawComputer(time);
    drawFan(time);
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

// Tapete vermelho escuro no chao, na frente de onde o vigia senta. Um
// quad um milimetro acima do chao (senao brigaria com ele por
// profundidade).
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

// Ponto de entrada: mobilia a sala. Todas as posicoes sao calculadas a
// partir das constantes da sala (ROOM_*), entao os moveis acompanham se
// as dimensoes mudarem.
void drawRoomProps(float time) {
    // O GL_NORMALIZE faz o OpenGL refazer o comprimento das normais
    // depois de glScalef nao uniformes (as caixas esticadas); sem ele a
    // iluminacao dessas faces sairia errada.
    glEnable(GL_NORMALIZE);

    const float zMid  = (ROOM_FRONT_Z + ROOM_BACK_Z) * 0.5f;
    const float deskX = -ROOM_HALF_WIDTH + 0.38f; // mesa quase encostada na parede esquerda

    // brilho do monitor: um pouco a' frente da tela (a tela fica em zMid + 0.15)
    setupMonitorLight(deskX + 0.30f, 1.05f, zMid + 0.15f, time);

    drawRug();
    drawDoorFrame();
    drawDeskSet(deskX, zMid, time);
    drawCabinet(ROOM_HALF_WIDTH - 0.32f, ROOM_FRONT_Z + 1.5f);

    glDisable(GL_NORMALIZE);
}