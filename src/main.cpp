// ============================================================
// 1 Noite com o Frederico
//
// Requisitos academicos cobertos (ver comentarios "[Requisito X]"
// espalhados pelo codigo e nos outros arquivos do projeto):
//   A - Modelagem de objetos 3D com primitivas       -> scene_builder.*, enemy.*
//   B - Transformacoes geometricas (hierarquia)      -> enemy.cpp (push/pop matrix)
//   C - Animacoes (controle de tempo)                -> timerFunc() abaixo
//   D - Controle de mouse e teclado                  -> passiveMotion(), keyboard()
//   E - Camera e perspectiva                         -> display() (gluPerspective/gluLookAt)
//   F - Iluminacao                                   -> lighting.*
//   G - Curvas parametricas (Bezier)                 -> bezier.*, usado em currentMonsterPosition()
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

namespace {

// ---------------------------------------------------------------
// Constantes de ajuste do jogo
// ---------------------------------------------------------------
const float DEG2RAD            = 3.14159265f / 180.0f;
const float YAW_LIMIT_DEG      = 75.0f;  // limite de giro da cabeca (esquerda/direita)
const float PITCH_LIMIT_DEG    = 42.0f;  // limite de giro da cabeca (cima/baixo)
const float MOUSE_SENSITIVITY  = 0.15f;
const float ADVANCE_RATE_BASE  = 0.075f;  // velocidade "normal" de avanco do monstro (t/seg) no escuro
const float RETREAT_RATE       = 0.025f; // velocidade de recuo quando a lanterna esta ligada (so' 1.5x o avanco)
const float JUMPSCARE_DURATION = 0.9f;   // duracao do salto final, em segundos
const float DOOR_SPEED         = 3.0f;   // velocidade de abrir/fechar a porta (unid/seg)
const float RESET_PAUSE        = 1.6f;   // pausa apos o jumpscare antes de reiniciar o encontro

// [Gameplay] Bateria compartilhada entre lanterna e porta, como no
// jogo original: cada uma consome energia enquanto estiver ativa, e
// quando acaba, as duas param de funcionar (e nao voltam mais). A
// porta gasta bem mais rapido -- ela e' o "botao de panico", nao uma
// solucao pra deixar ligada o tempo todo.
const float POWER_MAX         = 100.0f;
const float POWER_DRAIN_LIGHT = POWER_MAX / 50.0f; // lanterna ligada sem parar: acaba em 50s
const float POWER_DRAIN_DOOR  = POWER_MAX / 25.0f; // porta fechada sem parar: acaba em 25s

// [Gameplay] Dificuldade cresce a cada vez que o monstro e' repelido e
// volta a tentar: fica um pouco mais rapido, e a velocidade varia (nao
// da' pra decorar o tempo exato de reacao).
const float SPEED_RAMP_PER_TRY = 0.12f; // +12% de velocidade por tentativa
const float SPEED_RAMP_CAP     = 2.2f;  // nao passa de 2.2x a velocidade base
const float SPEED_JITTER_MIN   = 0.85f;
const float SPEED_JITTER_MAX   = 1.25f;

// ---------------------------------------------------------------
// Estado global da aplicacao
// ---------------------------------------------------------------
int g_windowW = 1024;
int g_windowH = 768;

Vector3 g_eye(0.0f, 1.7f, Scene::ROOM_BACK_Z - 1.0f); // camera fixa, "sentado" contra a parede de tras
float g_yawDeg   = 0.0f;
float g_pitchDeg = 0.0f;
bool  g_warping  = false; // evita que o glutWarpPointer gere um evento fantasma de movimento

bool  g_flashlightOn = false; // comeca desligada -- economiza bateria e deixa o corredor escuro
float g_power         = POWER_MAX;

bool  g_doorClosing = false;
float g_doorOffsetY = 0.0f; // 0 = aberta, DOORWAY_HEIGHT = fechada

float g_monsterT = 0.0f; // parametro t da curva de Bezier, [0,1]
float g_walkTime = 0.0f; // tempo acumulado para animar a caminhada

bool  g_jumpscareActive   = false;
float g_jumpscareProgress = 0.0f;
bool  g_pausing    = false;
float g_pauseTimer = 0.0f;

int   g_encounterCount   = 0;   // quantas vezes o monstro ja' tentou chegar
float g_speedMultiplier  = 1.0f; // recalculado a cada nova tentativa

int g_lastTimeMs = 0;

// [Gameplay] Sorteia um numero em [SPEED_JITTER_MIN, SPEED_JITTER_MAX].
float randomJitter() {
    return SPEED_JITTER_MIN
         + (float)rand() / (float)RAND_MAX * (SPEED_JITTER_MAX - SPEED_JITTER_MIN);
}

// [Gameplay] Prepara uma nova tentativa do monstro: volta pro comeco da
// curva e sorteia uma velocidade nova (base sobe um pouco a cada
// tentativa + variacao aleatoria), pra nao dar pra decorar o tempo
// exato de reacao nem pra ficar mais facil com o tempo.
void startNewEncounter() {
    g_monsterT           = 0.0f;
    g_jumpscareActive    = false;
    g_jumpscareProgress  = 0.0f;

    float ramp = 1.0f + SPEED_RAMP_PER_TRY * g_encounterCount;
    if (ramp > SPEED_RAMP_CAP) ramp = SPEED_RAMP_CAP;
    g_speedMultiplier = ramp * randomJitter();

    g_encounterCount++;
}

// ---------------------------------------------------------------
// [Requisito E] Camera: direcao "para frente" a partir de yaw/pitch
// (controlados pelo mouse). yaw=0/pitch=0 aponta para -Z (a abertura
// da sala / o corredor).
// ---------------------------------------------------------------
Vector3 computeForward() {
    float yawRad   = g_yawDeg   * DEG2RAD;
    float pitchRad = g_pitchDeg * DEG2RAD;
    Vector3 f;
    f.x = sinf(yawRad) * cosf(pitchRad);
    f.y = sinf(pitchRad);
    f.z = -cosf(yawRad) * cosf(pitchRad);
    return f;
}

// [Requisito G] Posicao mundial atual do monstro: ou avancando pela
// curva de Bezier, ou (durante o jumpscare) saltando da entrada da
// sala em direcao a camera.
Vector3 currentMonsterPosition() {
    BezierPath path = Enemy::getPath();
    if (!g_jumpscareActive) {
        return evaluateBezier(path, g_monsterT);
    }
    Vector3 doorwayPos = evaluateBezier(path, 1.0f);
    Vector3 lungeTarget(g_eye.x, doorwayPos.y, g_eye.z - 0.6f); // "dentro" do campo de visao da camera
    return doorwayPos + (lungeTarget - doorwayPos) * g_jumpscareProgress;
}

// [Gameplay] Escreve uma string na tela usando as fontes bitmap do
// GLUT (nao precisa de nenhuma biblioteca extra de texto).
void drawBitmapText(float x, float y, const char* text) {
    glRasterPos2f(x, y);
    for (const char* c = text; *c != '\0'; ++c) {
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, *c);
    }
}

// [Gameplay] Desenha a barra de energia (com a porcentagem em numero)
// no canto da tela. Tecnica padrao de HUD em OpenGL classico: troca
// pra uma projecao ortografica 2D (em pixels), desenha uns quads
// simples sem luz/profundidade, e desfaz a troca no final. Nao mexe
// na projecao/camera 3D usada no resto da cena.
void drawPowerBar() {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, g_windowW, 0.0, g_windowH);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    const float MARGIN = 20.0f, BAR_W = 220.0f, BAR_H = 22.0f;
    float frac = g_power / POWER_MAX;
    if (frac < 0.0f) frac = 0.0f;

    // fundo da barra
    glColor3f(0.12f, 0.12f, 0.12f);
    glBegin(GL_QUADS);
        glVertex2f(MARGIN,         MARGIN);
        glVertex2f(MARGIN + BAR_W, MARGIN);
        glVertex2f(MARGIN + BAR_W, MARGIN + BAR_H);
        glVertex2f(MARGIN,         MARGIN + BAR_H);
    glEnd();

    // preenchimento proporcional a' energia restante; muda de cor
    // conforme fica critica (verde -> amarelo -> vermelho)
    if (frac > 0.5f)      glColor3f(0.2f, 0.75f, 0.3f);
    else if (frac > 0.2f) glColor3f(0.85f, 0.75f, 0.1f);
    else                   glColor3f(0.8f, 0.15f, 0.1f);

    glBegin(GL_QUADS);
        glVertex2f(MARGIN,               MARGIN);
        glVertex2f(MARGIN + BAR_W * frac, MARGIN);
        glVertex2f(MARGIN + BAR_W * frac, MARGIN + BAR_H);
        glVertex2f(MARGIN,               MARGIN + BAR_H);
    glEnd();

    // numero da porcentagem, escrito acima da barra
    char label[32];
    std::snprintf(label, sizeof(label), "ENERGIA: %d%%", (int)(frac * 100.0f + 0.5f));
    glColor3f(1.0f, 1.0f, 1.0f);
    drawBitmapText(MARGIN, MARGIN + BAR_H + 8.0f, label);

    glPopMatrix(); // MODELVIEW
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

// ---------------------------------------------------------------
// [Requisito C] Callback de display: monta projecao, camera e desenha
// tudo. Chamado sempre que glutPostRedisplay() e' disparado pelo timer.
// ---------------------------------------------------------------
void display() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // [Requisito E] Perspectiva
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70.0, (double)g_windowW / (double)g_windowH, 0.1, 200.0);

    // [Requisito E] Camera em primeira pessoa fixa; alvo controlado pelo mouse
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    Vector3 forward = computeForward();
    Vector3 center(g_eye.x + forward.x, g_eye.y + forward.y, g_eye.z + forward.z);
    gluLookAt(g_eye.x, g_eye.y, g_eye.z,
              center.x, center.y, center.z,
              0.0, 1.0, 0.0);

    // [Requisito D/F] A lanterna e' atualizada logo apos a camera (ver lighting.cpp)
    updateFlashlight(g_flashlightOn);

    drawSecurityRoom();
    drawCorridor();
    drawDoor(g_doorOffsetY);

    Vector3 monsterPos = currentMonsterPosition();
    float monsterYaw   = 180.0f; // o monstro sempre encara quem esta' olhando pra ele
    float mouthOpen    = g_jumpscareActive ? g_jumpscareProgress : 0.05f;
    Enemy::draw(monsterPos, monsterYaw, g_walkTime, mouthOpen);

    drawPowerBar();

    glutSwapBuffers();
}

void reshape(int w, int h) {
    g_windowW = w;
    g_windowH = (h == 0) ? 1 : h;
    glViewport(0, 0, g_windowW, g_windowH);
}

// ---------------------------------------------------------------
// [Requisito D] Teclado: liga/desliga lanterna (GL_LIGHT0) e
// abre/fecha a porta de emergencia (translacao animada).
// ---------------------------------------------------------------
void keyboard(unsigned char key, int, int) {
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
        case 27: // ESC
            exit(0);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------
// [Requisito D] Mouse: olhar ao redor. Usa a tecnica classica de FPS:
// o cursor e' sempre recentralizado (glutWarpPointer) e o delta entre
// frames vira rotacao de camera (yaw/pitch), com clamp simulando o
// limite de movimento do pescoco humano.
// ---------------------------------------------------------------
void passiveMotion(int x, int y) {
    if (g_warping) {
        g_warping = false;
        return;
    }

    int dx = x - g_windowW / 2;
    int dy = y - g_windowH / 2;

    g_yawDeg   += dx * MOUSE_SENSITIVITY;
    g_pitchDeg -= dy * MOUSE_SENSITIVITY; // invertido: mouse para cima = olhar para cima

    if (g_yawDeg > YAW_LIMIT_DEG)      g_yawDeg = YAW_LIMIT_DEG;
    if (g_yawDeg < -YAW_LIMIT_DEG)     g_yawDeg = -YAW_LIMIT_DEG;
    if (g_pitchDeg > PITCH_LIMIT_DEG)  g_pitchDeg = PITCH_LIMIT_DEG;
    if (g_pitchDeg < -PITCH_LIMIT_DEG) g_pitchDeg = -PITCH_LIMIT_DEG;

    g_warping = true;
    glutWarpPointer(g_windowW / 2, g_windowH / 2);
}

// ---------------------------------------------------------------
// [Requisito C] Timer: controla o tempo do jogo independente do input
// (glutTimerFunc), atualizando a animacao de caminhada, a curva de
// Bezier do monstro, a porta e a sequencia de jumpscare.
// ---------------------------------------------------------------
void timerFunc(int) {
    int now = glutGet(GLUT_ELAPSED_TIME);
    float dt = (now - g_lastTimeMs) / 1000.0f;
    g_lastTimeMs = now;
    if (dt > 0.1f) dt = 0.1f; // evita saltos grandes (ex: janela minimizada)
    if (dt < 0.0f) dt = 0.0f;

    g_walkTime += dt;

    // --- Porta: anima suavemente ate a posicao alvo ---
    float doorTarget = g_doorClosing ? Scene::DOORWAY_HEIGHT : 0.0f;
    if (g_doorOffsetY < doorTarget)
        g_doorOffsetY = fminf(g_doorOffsetY + DOOR_SPEED * dt, doorTarget);
    else if (g_doorOffsetY > doorTarget)
        g_doorOffsetY = fmaxf(g_doorOffsetY - DOOR_SPEED * dt, doorTarget);
    bool doorSealed = g_doorOffsetY >= (Scene::DOORWAY_HEIGHT - 0.01f);

    // --- Bateria: a lanterna e a porta fechada consomem energia. Ao
    // zerar, as duas param de funcionar e nao voltam mais. ---
    float drain = 0.0f;
    if (g_flashlightOn) drain += POWER_DRAIN_LIGHT;
    if (g_doorClosing)  drain += POWER_DRAIN_DOOR;
    g_power -= drain * dt;
    if (g_power <= 0.0f) {
        g_power        = 0.0f;
        g_flashlightOn = false;
        g_doorClosing  = false; // a porta comeca a abrir sozinha
    }

    if (g_pausing) {
        g_pauseTimer += dt;
        if (g_pauseTimer >= RESET_PAUSE) {
            g_pausing = false;
            startNewEncounter();
        }
    } else if (g_jumpscareActive) {
        g_jumpscareProgress += dt / JUMPSCARE_DURATION;
        if (g_jumpscareProgress >= 1.0f) {
            g_jumpscareProgress = 1.0f;
            g_pausing    = true;
            g_pauseTimer = 0.0f;
        }
    } else {
        // [Gameplay] regra simples: lanterna ligada afasta o monstro,
        // desligada ele se aproxima -- na velocidade sorteada para
        // esta tentativa (ver startNewEncounter).
        if (doorSealed) {
            // Porta trancada: o monstro fica contido do lado de fora do corredor.
        } else if (g_flashlightOn) {
            g_monsterT -= RETREAT_RATE * dt;
        } else {
            g_monsterT += ADVANCE_RATE_BASE * g_speedMultiplier * dt;
        }

        if (g_monsterT < 0.0f) g_monsterT = 0.0f;
        if (g_monsterT > 1.0f) g_monsterT = 1.0f;

        if (g_monsterT >= 1.0f && !doorSealed) {
            g_jumpscareActive   = true;
            g_jumpscareProgress = 0.0f;
        }
    }

    glutPostRedisplay();
    glutTimerFunc(16, timerFunc, 0); // ~60 FPS
}

void initGL() {
    glEnable(GL_DEPTH_TEST);
    initLighting();
}

} // namespace

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(g_windowW, g_windowH);
    glutCreateWindow("1 Noite com o Frederico");

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutPassiveMotionFunc(passiveMotion);
    glutIgnoreKeyRepeat(1);
    glutSetCursor(GLUT_CURSOR_NONE);

    // Centraliza o cursor antes do primeiro frame para nao gerar um
    // "pulo" na camera assim que a janela abre.
    g_warping = true;
    glutWarpPointer(g_windowW / 2, g_windowH / 2);

    initGL();

    std::srand((unsigned int)std::time(nullptr));
    startNewEncounter();

    g_lastTimeMs = glutGet(GLUT_ELAPSED_TIME);
    glutTimerFunc(16, timerFunc, 0);

    std::printf("Controles:\n");
    std::printf("  Mouse - olhar ao redor (limitado, como um pescoco humano)\n");
    std::printf("  F     - ligar/desligar a lanterna (gasta energia)\n");
    std::printf("  D     - fechar/abrir a porta de seguranca (gasta energia bem mais rapido)\n");
    std::printf("  ESC   - sair\n");
    std::printf("A bateria e' compartilhada entre a lanterna e a porta -- quando\n");
    std::printf("acaba, as duas param de funcionar de vez. Use a porta so' quando\n");
    std::printf("precisar de verdade; a lanterna sozinha ja' segura o monstro.\n");

    glutMainLoop();
    return 0;
}