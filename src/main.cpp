// ============================================================
// 1 Noite no Frederico
//
// Requisitos academicos cobertos (ver comentarios "[Requisito X]"
// espalhados pelo codigo e nos outros arquivos do projeto):
//   A - Modelagem de objetos 3D com primitivas       -> scene_builder.*, enemy.*
//   B - Transformacoes geometricas (hierarquia)      -> enemy.cpp, scene_builder.cpp (ventilador)
//   C - Animacoes (controle de tempo)                -> timerFunc() abaixo
//   D - Controle de mouse e teclado                  -> passiveMotion(), keyboard()
//   E - Camera e perspectiva                         -> display() (gluPerspective/gluLookAt)
//   F - Iluminacao                                   -> lighting.*, luz do monitor em scene_builder.cpp
//   G - Curvas parametricas (Bezier)                 -> bezier.*, usado em currentMonsterPosition()
//
// ------------------------------------------------------------
// COMO O PROGRAMA FUNCIONA (visao geral)
// ------------------------------------------------------------
// O GLUT roda um laco de eventos (glutMainLoop). Nos registramos
// "callbacks", funcoes que o GLUT chama quando algo acontece:
//
//   glutTimerFunc(16ms) -> timerFunc()  : ATUALIZA o jogo (logica, sem desenhar)
//   glutDisplayFunc     -> display()    : DESENHA o estado atual na tela
//   glutKeyboardFunc    -> keyboard()   : tecla apertada
//   glutPassiveMotionFunc -> passiveMotion() : mouse se mexeu
//   glutReshapeFunc     -> reshape()    : janela mudou de tamanho
//
// A cada ~16 ms o timerFunc() calcula quanto tempo passou (dt), move
// o monstro, gasta a bateria etc., e chama glutPostRedisplay() pedindo
// pro GLUT chamar display(). Separar "atualizar" de "desenhar" e' o
// padrao de qualquer jogo, e usar "dt" (tempo real decorrido) em vez
// de "andar X por frame" faz o jogo rodar na mesma velocidade em
// qualquer computador.
//
// O jogo e' uma MAQUINA DE ESTADOS (enum GameState):
//
//   PLAYING --(monstro chegou)---> JUMPSCARE --(susto acabou)--> GAME_OVER --(R)--> PLAYING
//   PLAYING --(cronometro zerou)-> WON ------------------------------------(R)--> PLAYING
//
// Cada estado decide o que atualizar (timerFunc), o que desenhar
// (drawHud) e quais teclas valem (keyboard).
//
// ------------------------------------------------------------
// MAPA DO ARQUIVO
// ------------------------------------------------------------
//   1. Constantes de ajuste (balanceamento do jogo)
//   2. Estado global
//   3. Utilitarios e regras de dificuldade (sprint, reset)
//   4. Camera e posicao do monstro
//   5. HUD 2D (barra de energia, cronometro, telas de fim)
//   6. display()  - desenho da cena 3D + HUD
//   7. Entrada    - teclado e mouse
//   8. timerFunc()- logica do jogo a cada frame
//   9. main()     - inicializacao
// ============================================================

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "bezier.h"
#include "lighting.h"
#include "scene_builder.h"
#include "enemy.h"

// Tudo dentro do namespace anonimo so' existe neste arquivo (nao vaza
// pra outros .cpp), o equivalente moderno de declarar tudo "static".
namespace {

// ===============================================================
// 1. CONSTANTES DE AJUSTE
// Mexa aqui pra balancear a dificuldade sem tocar na logica.
// ===============================================================
const float DEG2RAD            = 3.14159265f / 180.0f;
const float YAW_LIMIT_DEG      = 75.0f;  // limite de giro da cabeca (esquerda/direita)
const float PITCH_LIMIT_DEG    = 42.0f;  // limite de giro da cabeca (cima/baixo)
const float MOUSE_SENSITIVITY  = 0.15f;  // graus de rotacao por pixel de mouse
const float JUMPSCARE_DURATION = 0.9f;   // duracao do salto final do monstro, em segundos
const float DOOR_SPEED         = 3.0f;   // velocidade de abrir/fechar a porta (unidades/seg)
const float GAMEOVER_LOCK      = 2.5f;   // segundos ate' aceitar "R" na tela de game over
const float WIN_LOCK           = 1.5f;   // idem na tela de vitoria

// Duracao da noite. Sobreviver ate' o fim = vitoria.
const float NIGHT_DURATION = 300.0f; // 5 minutos

// Velocidade do monstro. A unidade e' "t da curva de Bezier por
// segundo": a curva inteira (fundo do corredor ate' a porta) tem t de
// 0 a 1. Ex: 0.07 significa que ele leva 1/0.07 = ~14 s pra atravessar
// tudo caminhando no escuro.
const float ADVANCE_RATE_BASE = 0.07f;  // caminhada normal no escuro
const float RETREAT_RATE      = 0.11f;  // recuo quando a lanterna esta' acesa

// Bateria compartilhada entre lanterna e porta. Quando acaba, as duas
// param de funcionar e nao voltam mais. Os "drains" sao dados como
// MAX/segundos, pra ficar facil ler: lanterna sozinha dura 250 s.
const float POWER_MAX         = 100.0f;
const float POWER_DRAIN_LIGHT = POWER_MAX / 250.0f; // energia por segundo com lanterna ligada
const float POWER_DRAIN_DOOR  = POWER_MAX / 40.0f;  // energia por segundo com porta fechada

// Dificuldade progressiva: o monstro fica mais rapido conforme a noite
// avanca (de 1.0x ate' SPEED_RAMP_END) e a velocidade oscila entre
// JITTER_MIN e JITTER_MAX, sorteada de novo toda vez que ele e'
// empurrado de volta ao fundo do corredor (o "humor" do monstro).
const float SPEED_RAMP_END   = 1.80f;
const float SPEED_JITTER_MIN = 0.85f;
const float SPEED_JITTER_MAX = 1.25f;

// Sprints aleatorios: de vez em quando o monstro dispara. O intervalo
// entre sprints e' sorteado entre GAP_MIN e GAP_MAX e vai encurtando ate'
// ~38% (SPRINT_GAP_SHRINK) conforme a noite avanca. Durante o sprint a lanterna so'
// desacelera o monstro (ele continua avancando).
const float SPRINT_RATE         = 0.30f; // velocidade do sprint (t/seg), ~4x a caminhada
const float SPRINT_DURATION_MIN = 0.9f;  // duracao do sprint, em segundos
const float SPRINT_DURATION_MAX = 1.5f;
const float SPRINT_GAP_MIN      = 8.0f;  // intervalo entre sprints, em segundos
const float SPRINT_GAP_MAX      = 16.0f;
const float SPRINT_GAP_SHRINK   = 0.62f; // quanto o intervalo encurta ate' o fim da noite
const float SPRINT_FIRST_DELAY  = 6.0f;  // folga extra antes do 1o sprint
const float SPRINT_ANIM_BOOST   = 2.2f;  // pernas do monstro mexem mais rapido durante o sprint

// Mais fatores que escalam com o tempo. Todos sao "valor no fim da noite"
// relativo ao valor inicial (1.0 = igual ao comeco):
//  - a lanterna vai perdendo forca (o monstro recua mais devagar);
//  - o sprint fica mais veloz e dura mais;
//  - a porta gasta mais bateria (segurar ele la' fora sai mais caro).
const float RETREAT_RAMP_END      = 0.72f;
const float SPRINT_RATE_RAMP_END  = 1.20f;
const float SPRINT_DUR_RAMP_END   = 1.30f;
const float DOOR_DRAIN_RAMP_END   = 1.50f;

// Formato da curva de dificuldade: >1 deixa o comeco da noite mais
// tranquilo e concentra o aperto na segunda metade (1.0 = linear).
const float DIFFICULTY_CURVE = 1.4f;

// A fonte vetorial (stroke) do GLUT tem ~119 unidades de altura.
// Usamos isso pra escalar o texto pro tamanho em pixels que quisermos.
const float STROKE_FONT_H = 119.05f;

// ===============================================================
// 2. ESTADO GLOBAL
// ===============================================================
enum GameState { STATE_PLAYING, STATE_JUMPSCARE, STATE_GAME_OVER, STATE_WON };

int g_windowW = 1024;
int g_windowH = 768;

// Camera: fica PARADA (o vigia esta' sentado). Quem muda e' a direcao
// pra onde ele olha (yaw = giro horizontal, pitch = vertical), via mouse.
Vector3 g_eye(0.0f, 1.7f, Scene::ROOM_BACK_Z - 1.0f);
float g_yawDeg   = 0.0f;
float g_pitchDeg = 0.0f;
bool  g_warping  = false; // evita que o glutWarpPointer gere um evento fantasma de movimento

// Recursos do jogador
bool  g_flashlightOn = false; // comeca desligada: economiza bateria e deixa o corredor escuro
float g_power        = POWER_MAX;

// Porta: g_doorClosing e' o que o jogador PEDIU; g_doorOffsetY e' onde a
// porta REALMENTE esta' (ela anima suavemente ate' o alvo).
bool  g_doorClosing = false;
float g_doorOffsetY = 0.0f; // 0 = aberta, DOORWAY_HEIGHT = fechada

// Monstro
float g_monsterT = 0.0f; // parametro t da curva de Bezier, [0,1] (0 = fundo, 1 = porta)
float g_walkTime = 0.0f; // tempo acumulado que alimenta a animacao de caminhada

// Fluxo do jogo
GameState g_state       = STATE_PLAYING;
float     g_stateTimer  = 0.0f; // segundos desde que entrou em GAME_OVER / WON (anima as telas)
float     g_nightTime   = 0.0f; // segundos de noite sobrevividos (alimenta o cronometro)
float     g_uiTime      = 0.0f; // relogio que nunca para: usado em efeitos visuais (piscar, ventilador)
float     g_jumpscareProgress = 0.0f; // 0..1 durante o salto do monstro

// Humor / sprint do monstro
float g_speedJitter   = 1.0f;  // multiplicador sorteado (ver SPEED_JITTER_*)
bool  g_repelled      = true;  // true enquanto o monstro esta' encostado no fundo do corredor
bool  g_sprinting      = false; // esta' em sprint agora?
float g_sprintTimeLeft = 0.0f;  // quanto falta pro sprint atual acabar
float g_nextSprintIn   = 10.0f; // quanto falta pro proximo sprint comecar

int g_lastTimeMs = 0; // instante do frame anterior, pra calcular o dt

// ===============================================================
// 3. UTILITARIOS E REGRAS DE DIFICULDADE
// ===============================================================

// Limita x ao intervalo [0,1].
float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Interpolacao linear: t=0 devolve a, t=1 devolve b, valores no meio
// misturam proporcionalmente. Usada nas cores e nas rampas.
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Numero aleatorio uniforme em [lo, hi].
float randRange(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

// Quanto da noite ja' passou, de 0 (inicio) a 1 (amanheceu).
float nightProgress() { return clamp01(g_nightTime / NIGHT_DURATION); }

// Nivel de dificuldade de 0 a 1: e' o progresso da noite passado por uma
// curva (DIFFICULTY_CURVE), pra primeira metade ser mais justa.
float difficulty() { return powf(nightProgress(), DIFFICULTY_CURVE); }

// Velocidade de caminhada do monstro AGORA: cresce com a dificuldade e
// e' multiplicada pelo "humor" sorteado.
float currentSpeedMultiplier() {
    return lerpf(1.0f, SPEED_RAMP_END, difficulty()) * g_speedJitter;
}

// Forca com que a lanterna empurra o monstro de volta (cai com o tempo).
float currentRetreatRate() {
    return RETREAT_RATE * lerpf(1.0f, RETREAT_RAMP_END, difficulty());
}

// Velocidade e duracao do sprint (crescem com o tempo).
float currentSprintRate() {
    return SPRINT_RATE * lerpf(1.0f, SPRINT_RATE_RAMP_END, difficulty());
}
float randomSprintDuration() {
    return randRange(SPRINT_DURATION_MIN, SPRINT_DURATION_MAX)
         * lerpf(1.0f, SPRINT_DUR_RAMP_END, difficulty());
}

// Consumo de bateria da porta fechada (cresce com o tempo).
float currentDoorDrain() {
    return POWER_DRAIN_DOOR * lerpf(1.0f, DOOR_DRAIN_RAMP_END, difficulty());
}

// Sorteia quando vem o proximo sprint. O intervalo encurta com o
// progresso da noite (shrink vai de 1.0 ate' 1 - SPRINT_GAP_SHRINK).
void scheduleNextSprint() {
    float shrink = 1.0f - SPRINT_GAP_SHRINK * difficulty();
    g_nextSprintIn = randRange(SPRINT_GAP_MIN, SPRINT_GAP_MAX) * shrink;
}

// Comeca uma noite nova do zero. E' chamada uma vez no inicio do
// programa e toda vez que o jogador aperta R depois de perder/ganhar.
// Restaura TUDO (bateria, porta, monstro, cronometro).
void resetGame() {
    g_state             = STATE_PLAYING;
    g_stateTimer        = 0.0f;
    g_nightTime         = 0.0f;

    g_power             = POWER_MAX;
    g_flashlightOn      = false;
    g_doorClosing       = false;
    g_doorOffsetY       = 0.0f;

    g_monsterT          = 0.0f;
    g_jumpscareProgress = 0.0f;
    g_repelled          = true;
    g_speedJitter       = randRange(SPEED_JITTER_MIN, SPEED_JITTER_MAX);

    g_sprinting         = false;
    g_sprintTimeLeft    = 0.0f;
    scheduleNextSprint();
    g_nextSprintIn     += SPRINT_FIRST_DELAY; // o 1o sprint nunca vem logo de cara
}

// ===============================================================
// 4. CAMERA E POSICAO DO MONSTRO
// ===============================================================

// [Requisito E] Direcao "para frente" da camera a partir de yaw/pitch
// (controlados pelo mouse). Sao coordenadas esfericas convertidas em
// vetor: yaw=0/pitch=0 aponta para -Z (a abertura da sala / corredor).
Vector3 computeForward() {
    float yawRad   = g_yawDeg   * DEG2RAD;
    float pitchRad = g_pitchDeg * DEG2RAD;
    Vector3 f;
    f.x = sinf(yawRad) * cosf(pitchRad);
    f.y = sinf(pitchRad);
    f.z = -cosf(yawRad) * cosf(pitchRad);
    return f;
}

// [Requisito G] Posicao mundial atual do monstro.
//  - Normalmente: o ponto da curva de Bezier no parametro g_monsterT
//    (evaluateBezier em bezier.cpp calcula o polinomio cubico).
//  - No jumpscare: interpolacao linear (lerp de vetores) da porta ate'
//    a camera, com g_jumpscareProgress indo de 0 a 1.
Vector3 currentMonsterPosition() {
    BezierPath path = Enemy::getPath();
    bool lunging = (g_state == STATE_JUMPSCARE || g_state == STATE_GAME_OVER);
    if (!lunging) {
        return evaluateBezier(path, g_monsterT);
    }
    Vector3 doorwayPos = evaluateBezier(path, 1.0f);
    Vector3 lungeTarget(g_eye.x, doorwayPos.y, g_eye.z - 0.6f); // "dentro" do campo de visao da camera
    return doorwayPos + (lungeTarget - doorwayPos) * g_jumpscareProgress;
}

// ===============================================================
// 5. HUD 2D
//
// O HUD (barra de energia, cronometro, telas de fim) e' desenhado DEPOIS
// da cena 3D, por cima. Como o OpenGL classico so' sabe desenhar com
// uma projecao por vez, usamos a tecnica padrao:
//   1. guarda o estado atual (glPushAttrib) e as matrizes (glPushMatrix);
//   2. troca pra projecao ORTOGRAFICA 2D em pixels (gluOrtho2D), onde
//      (0,0) e' o canto inferior esquerdo e (largura,altura) o superior
//      direito, sem perspectiva;
//   3. desliga luz e teste de profundidade (HUD nao e' iluminado nem
//      "atras" de nada) e liga transparencia (blending);
//   4. desenha;
//   5. restaura tudo, e a cena 3D do proximo frame nao percebe nada.
// ===============================================================

// Retangulo preenchido (em pixels, no modo 2D do HUD).
void fillRect(float x0, float y0, float x1, float y1) {
    glBegin(GL_QUADS);
        glVertex2f(x0, y0);
        glVertex2f(x1, y0);
        glVertex2f(x1, y1);
        glVertex2f(x0, y1);
    glEnd();
}

// So' o contorno do retangulo.
void outlineRect(float x0, float y0, float x1, float y1) {
    glBegin(GL_LINE_LOOP);
        glVertex2f(x0, y0);
        glVertex2f(x1, y0);
        glVertex2f(x1, y1);
        glVertex2f(x0, y1);
    glEnd();
}

// Texto com as fontes BITMAP do GLUT: letras prontas, de tamanho fixo em
// pixels, posicionadas com glRasterPos. Boas pra texto pequeno.
// Atencao: o glRasterPos "trava" a cor atual, entao a cor tem que ser
// definida ANTES de chamar esta funcao.
void drawBitmapText(void* font, float x, float y, const char* text) {
    glRasterPos2f(x, y);
    for (const char* c = text; *c != '\0'; ++c) {
        glutBitmapCharacter(font, *c);
    }
}

// Mesma coisa, mas centralizado em cx (mede a largura do texto pra
// saber quanto voltar a esquerda).
void drawBitmapCentered(void* font, float cx, float y, const char* text) {
    int w = glutBitmapLength(font, (const unsigned char*)text);
    drawBitmapText(font, cx - w * 0.5f, y, text);
}

// Se o texto (na altura desejada) nao couber na largura maxima, devolve
// uma altura menor que caiba. Evita o titulo sair da tela em janelas
// pequenas.
float fitStrokeHeight(void* font, const char* text, float desiredH, float maxW) {
    float units = (float)glutStrokeLength(font, (const unsigned char*)text);
    float h = desiredH;
    if (units * (h / STROKE_FONT_H) > maxW) h = maxW * STROKE_FONT_H / units;
    return h;
}

// Texto grande com a fonte VETORIAL (stroke) do GLUT. Cada letra e' um
// conjunto de linhas desenhadas em coordenadas proprias, entao podemos
// ESCALAR livremente (glScalef) pra qualquer tamanho, e a espessura vem
// de glLineWidth. Por isso serve pros titulos e pros digitos do relogio.
// Desenha centralizado em (cx, cy) com a altura "height" em pixels.
// So' ASCII (sem acentos).
void drawStrokeCentered(void* font, const char* text, float cx, float cy,
                        float height, float lineWidth) {
    float s = height / STROKE_FONT_H;                                  // fator de escala
    float w = glutStrokeLength(font, (const unsigned char*)text) * s;  // largura final em pixels
    glLineWidth(lineWidth);
    glPushMatrix();
        glTranslatef(cx - w * 0.5f, cy - height * 0.42f, 0.0f); // 0.42: centraliza pelas maiusculas
        glScalef(s, s, 1.0f);
        for (const char* c = text; *c != '\0'; ++c) {
            glutStrokeCharacter(font, *c); // desenha a letra e avanca a posicao
        }
    glPopMatrix();
}

// Gradiente radial usado nas vinhetas: um "leque" de triangulos saindo
// do centro da tela. Cada vertice tem sua propria cor (RGBA); o OpenGL
// interpola a cor entre os vertices (shading suave), entao o centro fica
// com a cor "c" e as bordas com a cor "e". Usamos com alpha 0 no centro
// pra escurecer so' as bordas.
void drawRadial(const float c[4], const float e[4]) {
    float W = (float)g_windowW, H = (float)g_windowH;
    glBegin(GL_TRIANGLE_FAN);
        glColor4f(c[0], c[1], c[2], c[3]);
        glVertex2f(W * 0.5f, H * 0.5f);   // centro do leque
        glColor4f(e[0], e[1], e[2], e[3]);
        glVertex2f(0.0f, 0.0f);           // percorre os 4 cantos e fecha
        glVertex2f(W,    0.0f);
        glVertex2f(W,    H);
        glVertex2f(0.0f, H);
        glVertex2f(0.0f, 0.0f);
    glEnd();
}

// Cor da barra de energia em funcao do nivel (frac de 0 a 1). Cada
// faixa e' uma interpolacao (lerp) entre duas cores, entao a cor muda
// continuamente, sem "saltos":
//   100% verde -> 50% amarelo -> 0% vermelho.
// (Pra comecar em amarelo, troque GREEN por YELLOW.)
void powerColor(float frac, float& r, float& g, float& b) {
    const float GREEN[3]  = { 0.20f, 0.78f, 0.30f };
    const float YELLOW[3] = { 0.95f, 0.82f, 0.10f };
    const float RED[3]    = { 0.85f, 0.10f, 0.08f };
    const float* lo;
    const float* hi;
    float t;
    if (frac > 0.5f) { lo = YELLOW; hi = GREEN;  t = (frac - 0.5f) / 0.5f; } // metade de cima
    else             { lo = RED;    hi = YELLOW; t = frac / 0.5f; }          // metade de baixo
    r = lerpf(lo[0], hi[0], t);
    g = lerpf(lo[1], hi[1], t);
    b = lerpf(lo[2], hi[2], t);
}

// Passos 1 a 3 da tecnica descrita acima.
void hudBegin() {
    // Guarda os flags que vamos mexer, pra restaurar no hudEnd().
    glPushAttrib(GL_ENABLE_BIT | GL_LINE_BIT | GL_COLOR_BUFFER_BIT |
                 GL_CURRENT_BIT | GL_LIGHTING_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    // Transparencia: cor final = cor_nova * alpha + cor_que_ja_estava * (1 - alpha).
    // E' o que permite paineis semi-transparentes e fades.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);  // linhas (fonte vetorial) sem serrilhado
    glShadeModel(GL_SMOOTH);   // garante a interpolacao de cor dos gradientes

    // Projecao 2D em pixels. Empilhamos (push) as matrizes atuais pra
    // poder devolve-las intactas depois.
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, g_windowW, 0.0, g_windowH);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
}

// Passo 5: desfaz tudo que o hudBegin() fez.
void hudEnd() {
    glPopMatrix(); // MODELVIEW
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

// Barra de energia (canto inferior esquerdo): fundo escuro, preenchimento
// proporcional ao que resta (largura = BAR_W * frac), contorno e o
// numero em porcentagem. A cor vem de powerColor() e a barra pulsa
// (o brilho oscila com um seno) quando esta' abaixo de 20%.
void drawPowerBar() {
    const float MARGIN = 20.0f, BAR_W = 220.0f, BAR_H = 22.0f;
    float frac = clamp01(g_power / POWER_MAX);

    glColor4f(0.08f, 0.08f, 0.08f, 0.85f);
    fillRect(MARGIN, MARGIN, MARGIN + BAR_W, MARGIN + BAR_H);

    float r, g, b;
    powerColor(frac, r, g, b);
    if (frac < 0.2f) {
        float pulse = 0.7f + 0.3f * sinf(g_uiTime * 9.0f); // oscila entre 0.4 e 1.0
        r *= pulse; g *= pulse; b *= pulse;
    }
    glColor3f(r, g, b);
    fillRect(MARGIN, MARGIN, MARGIN + BAR_W * frac, MARGIN + BAR_H);

    glLineWidth(1.5f);
    glColor3f(0.75f, 0.75f, 0.75f);
    outlineRect(MARGIN, MARGIN, MARGIN + BAR_W, MARGIN + BAR_H);

    char label[32];
    std::snprintf(label, sizeof(label), "ENERGIA: %d%%", (int)(frac * 100.0f + 0.5f)); // +0.5 arredonda
    glColor3f(1.0f, 1.0f, 1.0f);
    drawBitmapText(GLUT_BITMAP_HELVETICA_18, MARGIN, MARGIN + BAR_H + 8.0f, label);
}

// Cronometro da noite (canto superior direito): contagem regressiva em
// "digitos de relogio", com uma barra de progresso dividida em 6
// "horas" (12 AM ate' 6 AM). A cor vai de vermelho a dourado conforme
// amanhece (lerp guiado por nightProgress()).
void drawNightClock() {
    const float PANEL_W = 190.0f, PANEL_H = 84.0f, MARGIN = 20.0f;
    float x1 = g_windowW - MARGIN, x0 = x1 - PANEL_W;   // ancorado no canto superior direito
    float y1 = g_windowH - MARGIN, y0 = y1 - PANEL_H;
    float cx = (x0 + x1) * 0.5f;

    float progress = nightProgress();
    int remaining = (int)ceilf(NIGHT_DURATION - g_nightTime); // segundos que faltam, arredondando pra cima
    if (remaining < 0) remaining = 0;
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%02d:%02d", remaining / 60, remaining % 60);

    float r = 1.0f;
    float g = lerpf(0.25f, 0.85f, progress);
    float b = lerpf(0.20f, 0.45f, progress);

    // painel translucido com borda
    glColor4f(0.04f, 0.03f, 0.03f, 0.78f);
    fillRect(x0, y0, x1, y1);
    glLineWidth(1.5f);
    glColor3f(r * 0.6f, g * 0.6f, b * 0.6f);
    outlineRect(x0, y0, x1, y1);

    glColor3f(0.72f, 0.72f, 0.72f);
    drawBitmapCentered(GLUT_BITMAP_HELVETICA_12, cx, y1 - 20.0f, "AMANHECE EM");

    // Os digitos usam a fonte MONO (largura fixa), pra o texto nao
    // "dancar" quando um numero fica mais largo que outro. Desenhamos
    // duas vezes: uma camada grossa e translucida (o "brilho", tipo
    // display de LED) e por cima a linha fina e opaca.
    glColor4f(r, g, b, 0.25f);
    drawStrokeCentered(GLUT_STROKE_MONO_ROMAN, digits, cx, y1 - 50.0f, 34.0f, 6.0f);
    glColor4f(r, g, b, 1.0f);
    drawStrokeCentered(GLUT_STROKE_MONO_ROMAN, digits, cx, y1 - 50.0f, 34.0f, 2.2f);

    // barra de progresso: fundo, preenchimento (largura proporcional ao
    // progresso) e 5 riscos escuros que a dividem em 6 "horas"
    float bx0 = x0 + 14.0f, bx1 = x1 - 14.0f, by0 = y0 + 9.0f, by1 = y0 + 14.0f;
    glColor4f(0.2f, 0.2f, 0.2f, 0.9f);
    fillRect(bx0, by0, bx1, by1);
    glColor3f(r, g, b);
    fillRect(bx0, by0, bx0 + (bx1 - bx0) * progress, by1);
    glColor3f(0.04f, 0.03f, 0.03f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
        for (int i = 1; i < 6; ++i) {
            float tx = bx0 + (bx1 - bx0) * (float)i / 6.0f;
            glVertex2f(tx, by0);
            glVertex2f(tx, by1);
        }
    glEnd();
}

// Tela de game over. Tudo e' funcao do tempo "t" desde que o estado
// comecou (g_stateTimer), o que da' uma pequena sequencia animada:
//   0.00 a 0.25 s : flash vermelho que some
//   0.00 a 0.70 s : a tela escurece (fade) e a vinheta vermelha aparece
//   a partir de 0.5 s : o titulo aparece tremendo e piscando
//   a partir de 1.2 s : subtitulo e tempo sobrevivido surgem (fade-in)
//   a partir de GAMEOVER_LOCK : aparece o "R - tentar de novo" piscando
// O efeito de "tubo de TV com defeito" e' feito com numeros aleatorios:
// desloca o titulo alguns pixels a cada frame, e de vez em quando faz
// uma "rajada" de tremor forte e apaga o brilho por um instante.
void drawGameOverScreen() {
    const float t = g_stateTimer;
    const float W = (float)g_windowW, H = (float)g_windowH;
    float fade = clamp01(t / 0.7f);

    glColor4f(0.0f, 0.0f, 0.0f, 0.94f * fade);
    fillRect(0.0f, 0.0f, W, H);

    const float vc[4] = { 0.5f, 0.0f, 0.0f, 0.0f };          // centro: transparente
    const float ve[4] = { 0.5f, 0.0f, 0.0f, 0.85f * fade };  // bordas: vermelho escuro
    drawRadial(vc, ve);

    if (t < 0.25f) {
        glColor4f(0.85f, 0.0f, 0.0f, 0.7f * (1.0f - t / 0.25f));
        fillRect(0.0f, 0.0f, W, H);
    }

    if (t > 0.5f) {
        float tt = t - 0.5f;
        bool  burst = fmodf(tt, 1.7f) < 0.12f;       // 0.12 s de rajada a cada 1.7 s
        float shake = burst ? 10.0f : 1.5f;          // amplitude do tremor em pixels
        float ox = randRange(-shake, shake);
        float oy = randRange(-shake, shake);
        float alpha = 0.8f + 0.2f * sinf(tt * 25.0f);
        if (rand() % 14 == 0) alpha *= 0.15f;        // ~1 frame em 14 quase apaga

        const char* title = "GAME OVER";
        float titleH = fitStrokeHeight(GLUT_STROKE_ROMAN, title, H * 0.20f, W * 0.85f);
        float cx = W * 0.5f + ox, cy = H * 0.58f + oy;

        glColor4f(0.6f, 0.0f, 0.0f, 0.30f * alpha);   // brilho (linha grossa translucida)
        drawStrokeCentered(GLUT_STROKE_ROMAN, title, cx, cy, titleH, 9.0f);
        glColor4f(0.95f, 0.05f, 0.05f, alpha);        // titulo em si
        drawStrokeCentered(GLUT_STROKE_ROMAN, title, cx, cy, titleH, 4.0f);
        if (burst) {                                  // "fantasma" branco deslocado nas rajadas
            glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
            drawStrokeCentered(GLUT_STROKE_ROMAN, title, cx - ox * 2.0f, cy + oy * 2.0f, titleH, 2.0f);
        }

        float subA = clamp01((t - 1.2f) / 1.0f);      // fade-in do subtitulo
        const char* sub = "O FREDERICO TE PEGOU...";
        float subH = fitStrokeHeight(GLUT_STROKE_ROMAN, sub, 30.0f, W * 0.8f);
        glColor4f(0.80f, 0.74f, 0.74f, subA);
        drawStrokeCentered(GLUT_STROKE_ROMAN, sub, W * 0.5f, H * 0.42f, subH, 1.6f);

        char stat[64];
        int total = (int)NIGHT_DURATION, s = (int)g_nightTime;
        std::snprintf(stat, sizeof(stat), "Voce aguentou %02d:%02d de %02d:%02d",
                      s / 60, s % 60, total / 60, total % 60);
        glColor4f(0.6f, 0.6f, 0.6f, subA);
        drawBitmapCentered(GLUT_BITMAP_HELVETICA_18, W * 0.5f, H * 0.33f, stat);

        if (t >= GAMEOVER_LOCK) {
            float blink = 0.55f + 0.45f * sinf(g_uiTime * 4.0f);
            glColor4f(0.9f, 0.9f, 0.9f, blink);
            drawBitmapCentered(GLUT_BITMAP_HELVETICA_18, W * 0.5f, H * 0.17f,
                               "R - tentar de novo     |     ESC - sair");
        }
    }
}

// Tela de vitoria: mesma ideia do game over, mas com brilho dourado
// no centro (o gradiente radial ao contrario: opaco no meio, transparente
// nas bordas), simulando o sol nascendo.
void drawWinScreen() {
    const float t = g_stateTimer;
    const float W = (float)g_windowW, H = (float)g_windowH;
    float fade = clamp01(t / 1.5f);

    glColor4f(0.02f, 0.02f, 0.03f, 0.88f * fade);
    fillRect(0.0f, 0.0f, W, H);

    const float gc[4] = { 1.0f, 0.8f, 0.35f, 0.35f * fade };
    const float ge[4] = { 1.0f, 0.8f, 0.35f, 0.0f };
    drawRadial(gc, ge);

    if (t > 0.6f) {
        float a = clamp01((t - 0.6f) / 0.8f);

        const char* title = "6 AM";
        float titleH = fitStrokeHeight(GLUT_STROKE_ROMAN, title, H * 0.30f, W * 0.6f);
        glColor4f(1.0f, 0.75f, 0.25f, 0.30f * a);
        drawStrokeCentered(GLUT_STROKE_ROMAN, title, W * 0.5f, H * 0.60f, titleH, 10.0f);
        glColor4f(1.0f, 0.88f, 0.45f, a);
        drawStrokeCentered(GLUT_STROKE_ROMAN, title, W * 0.5f, H * 0.60f, titleH, 4.0f);

        const char* sub = "VOCE SOBREVIVEU A NOITE";
        float subH = fitStrokeHeight(GLUT_STROKE_ROMAN, sub, 40.0f, W * 0.85f);
        glColor4f(0.95f, 0.95f, 0.90f, a);
        drawStrokeCentered(GLUT_STROKE_ROMAN, sub, W * 0.5f, H * 0.40f, subH, 2.0f);

        glColor4f(0.7f, 0.7f, 0.7f, a);
        drawBitmapCentered(GLUT_BITMAP_HELVETICA_18, W * 0.5f, H * 0.31f,
                           "O Frederico foi embora... por enquanto.");

        if (t >= WIN_LOCK) {
            float blink = 0.55f + 0.45f * sinf(g_uiTime * 4.0f);
            glColor4f(0.95f, 0.95f, 0.95f, blink);
            drawBitmapCentered(GLUT_BITMAP_HELVETICA_18, W * 0.5f, H * 0.17f,
                               "R - jogar de novo     |     ESC - sair");
        }
    }
}

// Decide o que o HUD mostra conforme o estado do jogo.
void drawHud() {
    hudBegin();
    switch (g_state) {
        case STATE_PLAYING:
            drawPowerBar();
            drawNightClock();
            break;
        case STATE_JUMPSCARE:
            break; // tela limpa, so' o monstro na cara: mais impacto
        case STATE_GAME_OVER:
            drawGameOverScreen();
            break;
        case STATE_WON:
            drawWinScreen();
            break;
    }
    hudEnd();
}

// ===============================================================
// 6. DISPLAY
// [Requisito C] Chamado pelo GLUT sempre que o timer pede um redesenho
// (glutPostRedisplay). Monta a projecao, posiciona a camera e desenha
// tudo, de tras pra frente: cena 3D primeiro, HUD 2D por cima.
// ===============================================================
void display() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // [Requisito E] Projecao em PERSPECTIVA: objetos longe ficam menores.
    // 70 graus de campo de visao vertical, proporcao da janela, e planos
    // de corte perto=0.1 / longe=200 (nada fora disso e' desenhado).
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70.0, (double)g_windowW / (double)g_windowH, 0.1, 200.0);

    // [Requisito E] Camera em primeira pessoa. gluLookAt recebe onde o
    // olho esta', pra que ponto ele olha e qual direcao e' "pra cima".
    // O ponto alvo = olho + direcao do olhar (controlada pelo mouse).
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    Vector3 forward = computeForward();
    Vector3 center(g_eye.x + forward.x, g_eye.y + forward.y, g_eye.z + forward.z);
    gluLookAt(g_eye.x, g_eye.y, g_eye.z,
              center.x, center.y, center.z,
              0.0, 1.0, 0.0);

    // [Requisito D/F] A lanterna e' atualizada logo apos a camera (ver
    // lighting.cpp): a posicao de uma luz e' transformada pela matriz
    // ATUAL, entao precisa ser definida depois do gluLookAt pra "grudar"
    // na camera.
    updateFlashlight(g_flashlightOn);

    // Moveis da sala. Vem ANTES das paredes porque tambem configuram a
    // luz do monitor, que precisa estar ativa quando as paredes forem
    // desenhadas. g_uiTime anima o ventilador e a tela do monitor.
    drawRoomProps(g_uiTime);

    drawSecurityRoom();
    drawCorridor();
    drawDoor(g_doorOffsetY);

    // O monstro: posicao vem da curva de Bezier (ou do salto do susto),
    // g_walkTime anima as pernas/bracos e mouthOpen abre a boca no susto.
    bool  lunging   = (g_state == STATE_JUMPSCARE || g_state == STATE_GAME_OVER);
    Vector3 monsterPos = currentMonsterPosition();
    float monsterYaw   = 180.0f; // o monstro sempre encara quem esta' olhando pra ele
    float mouthOpen    = lunging ? g_jumpscareProgress : 0.05f;
    Enemy::draw(monsterPos, monsterYaw, g_walkTime, mouthOpen);

    // Interruptor da porta (LED: verde = aberta, vermelho = fechada).
    // Vem depois das paredes porque o brilho do LED usa transparencia.
    drawDoorSwitch(g_doorClosing, g_power > 0.0f);

    drawHud();

    glutSwapBuffers(); // double buffering: mostra o quadro pronto de uma vez
}

// Chamado quando a janela muda de tamanho: guarda as dimensoes (usadas
// na proporcao da perspectiva e no HUD) e ajusta a area de desenho.
void reshape(int w, int h) {
    g_windowW = w;
    g_windowH = (h == 0) ? 1 : h; // evita divisao por zero no aspecto
    glViewport(0, 0, g_windowW, g_windowH);
}

// ===============================================================
// 7. ENTRADA (TECLADO E MOUSE)
// ===============================================================

// [Requisito D] Teclado. F liga/desliga a lanterna (GL_LIGHT0), D
// abre/fecha a porta (translacao animada) e R reinicia a partida nas
// telas de game over / vitoria. O comportamento de cada tecla depende
// do estado atual do jogo.
void keyboard(unsigned char key, int, int) {
    if (key == 27) { // ESC vale em qualquer estado
        exit(0);
    }

    // Nas telas de fim, so' o R (ou Enter) importa, e so' depois do
    // tempo de "trava", pra ninguem pular a tela sem querer.
    if (g_state == STATE_GAME_OVER || g_state == STATE_WON) {
        float lock = (g_state == STATE_GAME_OVER) ? GAMEOVER_LOCK : WIN_LOCK;
        if ((key == 'r' || key == 'R' || key == 13) && g_stateTimer >= lock) {
            resetGame();
        }
        return;
    }
    if (g_state != STATE_PLAYING) return; // durante o jumpscare nao tem o que fazer

    switch (key) {
        case 'f':
        case 'F':
            // So' liga se ainda tiver energia; desligar sempre pode.
            if (g_flashlightOn)      g_flashlightOn = false;
            else if (g_power > 0.0f) g_flashlightOn = true;
            break;
        case 'd':
        case 'D':
            if (g_doorClosing)       g_doorClosing = false;
            else if (g_power > 0.0f) g_doorClosing = true;
            break;
        default:
            break;
    }
}

// [Requisito D] Mouse: olhar ao redor. Usa a tecnica classica de FPS:
// o cursor e' sempre recentralizado (glutWarpPointer) e o quanto ele
// se afastou do centro (dx, dy) vira rotacao de camera (yaw/pitch).
// Os limites (clamp) simulam o limite de movimento do pescoco humano.
void passiveMotion(int x, int y) {
    // Quando NOS movemos o cursor de volta ao centro, o GLUT gera um
    // evento de movimento falso. A flag serve pra ignora-lo.
    if (g_warping) {
        g_warping = false;
        return;
    }

    int dx = x - g_windowW / 2;
    int dy = y - g_windowH / 2;

    g_yawDeg   += dx * MOUSE_SENSITIVITY;
    g_pitchDeg -= dy * MOUSE_SENSITIVITY; // invertido: y da janela cresce pra baixo

    if (g_yawDeg > YAW_LIMIT_DEG)      g_yawDeg = YAW_LIMIT_DEG;
    if (g_yawDeg < -YAW_LIMIT_DEG)     g_yawDeg = -YAW_LIMIT_DEG;
    if (g_pitchDeg > PITCH_LIMIT_DEG)  g_pitchDeg = PITCH_LIMIT_DEG;
    if (g_pitchDeg < -PITCH_LIMIT_DEG) g_pitchDeg = -PITCH_LIMIT_DEG;

    g_warping = true;
    glutWarpPointer(g_windowW / 2, g_windowH / 2);
}

// ===============================================================
// 8. TIMER (LOGICA DO JOGO)
// [Requisito C] Roda a cada ~16 ms (glutTimerFunc), independente de
// input ou de desenho. Faz TODA a atualizacao do jogo, na ordem:
//   1. calcula dt (tempo real desde o frame anterior)
//   2. anima a porta
//   3. conforme o estado:
//      PLAYING   -> cronometro, bateria, sprint, monstro, checa fim
//      JUMPSCARE -> anima o salto do monstro
//      GAME_OVER / WON -> so' avanca o relogio da tela
//   4. pede redesenho e se reagenda
// ===============================================================
void timerFunc(int) {
    // --- 1. delta time ---
    int now = glutGet(GLUT_ELAPSED_TIME);   // milissegundos desde o inicio do programa
    float dt = (now - g_lastTimeMs) / 1000.0f;
    g_lastTimeMs = now;
    if (dt > 0.1f) dt = 0.1f; // evita saltos grandes (ex: janela minimizada/arrastada)
    if (dt < 0.0f) dt = 0.0f;

    g_uiTime += dt;

    // --- 2. Porta: anima suavemente ate' a posicao alvo ---
    // Em vez de teleportar, move DOOR_SPEED * dt por frame na direcao do
    // alvo, sem passar dele (fminf/fmaxf).
    float doorTarget = g_doorClosing ? Scene::DOORWAY_HEIGHT : 0.0f;
    if (g_doorOffsetY < doorTarget)
        g_doorOffsetY = fminf(g_doorOffsetY + DOOR_SPEED * dt, doorTarget);
    else if (g_doorOffsetY > doorTarget)
        g_doorOffsetY = fmaxf(g_doorOffsetY - DOOR_SPEED * dt, doorTarget);
    // A porta so' "veda" quando chegou ao chao (o 0.01 e' folga numerica).
    bool doorSealed = g_doorOffsetY >= (Scene::DOORWAY_HEIGHT - 0.01f);

    // --- 3. Logica por estado ---
    if (g_state == STATE_PLAYING) {
        g_nightTime += dt;
        g_walkTime  += dt * (g_sprinting ? SPRINT_ANIM_BOOST : 1.0f);

        // Bateria: a lanterna e a porta fechada consomem energia. Ao
        // zerar, as duas param de funcionar e nao voltam mais.
        float drain = 0.0f;
        if (g_flashlightOn) drain += POWER_DRAIN_LIGHT;
        if (g_doorClosing)  drain += currentDoorDrain();
        g_power -= drain * dt;
        if (g_power <= 0.0f) {
            g_power        = 0.0f;
            g_flashlightOn = false;
            g_doorClosing  = false; // a porta comeca a abrir sozinha
        }

        // Sprints aleatorios: dois cronometros. Enquanto NAO esta' em
        // sprint, conta ate' o proximo comecar; enquanto esta', conta
        // ate' acabar e ja' sorteia o proximo.
        if (g_sprinting) {
            g_sprintTimeLeft -= dt;
            if (g_sprintTimeLeft <= 0.0f) {
                g_sprinting = false;
                scheduleNextSprint();
            }
        } else {
            g_nextSprintIn -= dt;
            if (g_nextSprintIn <= 0.0f) {
                g_sprinting      = true;
                g_sprintTimeLeft = randomSprintDuration();
            }
        }

        // Movimento do monstro. "rate" e' a variacao do parametro t da
        // curva por segundo: positivo = se aproxima, negativo = recua.
        //   porta vedada        -> parado (contido do lado de fora)
        //   lanterna acesa      -> recua (RETREAT_RATE)
        //   lanterna apagada    -> avanca (caminhada normal)
        //   sprint              -> avanca rapido; a lanterna so' desconta
        //                          a forca de recuo dessa velocidade
        float rate;
        if (doorSealed) {
            rate = 0.0f;
        } else {
            float advance = g_sprinting ? currentSprintRate()
                                        : ADVANCE_RATE_BASE * currentSpeedMultiplier();
            float retreat = currentRetreatRate();
            if (g_flashlightOn) rate = g_sprinting ? (advance - retreat) : -retreat;
            else                rate = advance;
        }
        g_monsterT += rate * dt;

        // Mantem t em [0,1]. Quando o monstro e' empurrado ate' o fundo
        // (t < 0), ele "volta com outro humor": sorteia uma nova
        // velocidade. A flag g_repelled garante que isso so' acontece
        // UMA vez por empurrao, nao a cada frame.
        if (g_monsterT < 0.0f) {
            g_monsterT = 0.0f;
            if (!g_repelled) {
                g_repelled    = true;
                g_speedJitter = randRange(SPEED_JITTER_MIN, SPEED_JITTER_MAX);
            }
        } else if (g_monsterT > 0.05f) {
            g_repelled = false;
        }
        if (g_monsterT > 1.0f) g_monsterT = 1.0f;

        // Condicoes de fim. Perder tem prioridade sobre ganhar: se o
        // monstro chegou no mesmo frame em que amanheceu, voce perde.
        if (g_monsterT >= 1.0f && !doorSealed) {
            g_state             = STATE_JUMPSCARE;
            g_jumpscareProgress = 0.0f;
        } else if (g_nightTime >= NIGHT_DURATION) {
            g_nightTime  = NIGHT_DURATION;
            g_state      = STATE_WON;
            g_stateTimer = 0.0f;
        }
    } else if (g_state == STATE_JUMPSCARE) {
        // O progresso vai de 0 a 1 em JUMPSCARE_DURATION segundos; a
        // posicao do monstro e a boca abrindo usam esse valor (ver display).
        g_walkTime          += dt;
        g_jumpscareProgress += dt / JUMPSCARE_DURATION;
        if (g_jumpscareProgress >= 1.0f) {
            g_jumpscareProgress = 1.0f;
            g_state             = STATE_GAME_OVER;
            g_stateTimer        = 0.0f;
        }
    } else {
        g_stateTimer += dt; // GAME_OVER / WON: so' avanca o relogio da animacao da tela
    }

    // --- 4. Redesenha e se reagenda (16 ms ~ 60 quadros por segundo) ---
    glutPostRedisplay();
    glutTimerFunc(16, timerFunc, 0);
}

void initGL() {
    glEnable(GL_DEPTH_TEST); // z-buffer: objetos mais proximos escondem os mais distantes
    initLighting();
}

} // namespace

// ===============================================================
// 9. MAIN
// Cria a janela, registra os callbacks e entrega o controle ao GLUT.
// ===============================================================
int main(int argc, char** argv) {
    glutInit(&argc, argv);
    // DOUBLE = dois buffers (desenha num, mostra o outro: sem flicker);
    // RGB = cores; DEPTH = z-buffer pra esconder o que esta' atras.
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(g_windowW, g_windowH);
    glutCreateWindow("1 Noite no Frederico");

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutPassiveMotionFunc(passiveMotion);
    glutIgnoreKeyRepeat(1);              // segurar a tecla nao repete o evento
    glutSetCursor(GLUT_CURSOR_NONE);     // esconde o cursor (o mouse so' gira a camera)

    // Centraliza o cursor antes do primeiro frame para nao gerar um
    // "pulo" na camera assim que a janela abre.
    g_warping = true;
    glutWarpPointer(g_windowW / 2, g_windowH / 2);

    initGL();

    std::srand((unsigned int)std::time(0)); // semente aleatoria: cada partida e' diferente
    resetGame();

    g_lastTimeMs = glutGet(GLUT_ELAPSED_TIME);
    glutTimerFunc(16, timerFunc, 0);     // dispara o primeiro tick do laco do jogo

    std::printf("=== 1 Noite no Frederico ===\n");
    std::printf("Sobreviva ate' as 6 da manha (5 minutos).\n\n");
    std::printf("Controles:\n");
    std::printf("  Mouse - olhar ao redor (limitado, como um pescoco humano)\n");
    std::printf("  F     - ligar/desligar a lanterna (gasta energia)\n");
    std::printf("  D     - fechar/abrir a porta de seguranca (gasta energia bem mais rapido)\n");
    std::printf("  R     - reiniciar (nas telas de game over / vitoria)\n");
    std::printf("  ESC   - sair\n\n");
    std::printf("A bateria e' compartilhada entre a lanterna e a porta. Quando\n");
    std::printf("acaba, as duas param de funcionar de vez. Fique de olho: de vez\n");
    std::printf("em quando ele dispara.\n");

    glutMainLoop(); // entrega o controle ao GLUT; nunca retorna
    return 0;
}