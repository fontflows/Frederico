// ============================================================
// 1 Noite no Frederico  -  Trabalho 1 de Computacao Grafica
//
// Onde cada requisito aparece no codigo:
//   1. Modelagem com primitivas   -> scene_builder.cpp e enemy.cpp (quads, cubos, esfera)
//   2. Transformacoes geometricas -> enemy.cpp (hierarquia), scene_builder.cpp (mesa), porta
//   3. Animacoes (tempo)          -> update(): dt, porta, monstro, bateria, cronometro
//   4. Mouse e teclado            -> onMouseMove() e onKey()
//   5. Camera e perspectiva       -> display(): gluPerspective + gluLookAt
//   6. Iluminacao                 -> lighting.cpp (lanterna + luz ambiente)
//   7. Curvas parametricas        -> bezier.cpp, usada em monsterPosition()
//
// COMO O GLUT FUNCIONA: glutMainLoop() roda um laco de eventos e chama as
// nossas funcoes ("callbacks") quando algo acontece:
//   update()      a cada 16 ms   -> ATUALIZA o jogo (logica, sem desenhar)
//   display()     quando pedimos -> DESENHA o estado atual
//   onKey()       tecla apertada
//   onMouseMove() mouse se mexeu
//   reshape()     janela mudou de tamanho
// Separar "atualizar" de "desenhar" e usar dt (tempo real decorrido) em vez
// de "andar X por quadro" faz o jogo rodar igual em qualquer computador.
//
// O jogo e' uma maquina de 3 estados:
//   PLAYING --(monstro chegou)--> LOST --(R)--> PLAYING
//   PLAYING --(cronometro zerou)-> WON --(R)--> PLAYING
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

// ===============================================================
// 1. CONSTANTES (mexa aqui pra balancear o jogo)
// ===============================================================
const float DEG2RAD        = 3.14159265f / 180.0f;
const float NIGHT_DURATION = 300.0f;   // 5 minutos reais; sobreviver ate' o fim = vitoria
const float LUNGE_DURATION = 0.9f;     // duracao do salto final do monstro (susto), em segundos
const float DOOR_SPEED     = 1.0f;     // a porta leva 1/DOOR_SPEED segundos pra fechar

// Mouse: limites de giro da cabeca (o vigia esta' sentado, nao gira 360 graus)
const float MOUSE_SENSITIVITY = 0.15f; // graus por pixel
const float YAW_LIMIT_DEG     = 75.0f;
const float PITCH_LIMIT_DEG   = 42.0f;

// Bateria: a lanterna e a porta dividem a mesma energia, que nao recarrega.
const float POWER_MAX   = 100.0f;
const float DRAIN_LIGHT = POWER_MAX / 200.0f; // lanterna sozinha dura 200 s
const float DRAIN_DOOR  = POWER_MAX / 40.0f;  // porta fechada gasta 5x mais: e' o "botao de panico"

// Monstro. A velocidade e' medida em "t da curva de Bezier por segundo":
// a curva toda (fundo do corredor ate' a porta) vai de t = 0 a t = 1.
//
// DIFICULDADE PROGRESSIVA: duas coisas pioram conforme a noite avanca,
// ambas ligadas a progress() (0 no inicio, 1 no fim da noite):
//   - o monstro anda mais rapido: de 1x ate' 4x a velocidade inicial;
//   - a lanterna empurra ele de volta com menos forca: ate' 40% mais fraca.
const float MONSTER_SPEED_START = 0.07f; // no escuro, atravessa tudo em ~14 s (no inicio)
const float MONSTER_SPEED_GAIN  = 3.0f;  // velocidade final = START * (1 + GAIN)
const float FLASHLIGHT_PUSH     = 0.11f; // com a lanterna acesa, ele recua nessa velocidade
const float FLASHLIGHT_WEAKEN   = 0.4f;  // quanto a lanterna perde de forca ate' o fim

// ===============================================================
// 2. ESTADO
// ===============================================================
enum State { PLAYING, LOST, WON };

int   g_windowW = 1024;
int   g_windowH = 768;
int   g_lastMs  = 0;              // instante do quadro anterior (pra calcular o dt)

State g_state = PLAYING;
float g_nightTime = 0.0f;         // segundos de noite ja' sobrevividos

// Camera: o vigia fica parado; o mouse so' muda pra onde ele olha.
const Vector3 EYE(0.0f, 1.7f, Scene::ROOM_BACK_Z - 1.0f);
float g_yawDeg   = 0.0f;          // giro horizontal
float g_pitchDeg = 0.0f;          // giro vertical

// Jogador
bool  g_flashlightOn = false;
float g_power        = POWER_MAX;
bool  g_doorClosing  = false;     // o que o jogador PEDIU
float g_door         = 0.0f;      // onde a porta REALMENTE esta': 0 = aberta, 1 = fechada

// Monstro
float g_monsterT = 0.0f;          // posicao na curva de Bezier: 0 = fundo, 1 = porta
float g_walkTime = 0.0f;          // alimenta a animacao das pernas
float g_lunge    = 0.0f;          // 0..1 durante o salto final (estado LOST)

// ===============================================================
// 3. REGRAS DO JOGO
// ===============================================================
float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Quanto da noite ja' passou: 0 (comecou) ate' 1 (amanheceu).
float progress() { return clamp01(g_nightTime / NIGHT_DURATION); }

// As duas regras de dificuldade (ver constantes acima).
float monsterSpeed()   { return MONSTER_SPEED_START * (1.0f + MONSTER_SPEED_GAIN * progress()); }
float flashlightPush() { return FLASHLIGHT_PUSH * (1.0f - FLASHLIGHT_WEAKEN * progress()); }

void resetGame() {
    g_state = PLAYING;
    g_nightTime = 0.0f;
    g_flashlightOn = false;
    g_power = POWER_MAX;
    g_doorClosing = false;
    g_door = 0.0f;
    g_monsterT = 0.0f;
    g_lunge = 0.0f;
}

// [Requisito 7] Posicao do monstro no mundo.
//  - Jogando: e' o ponto da curva de Bezier no parametro g_monsterT.
//  - No susto: interpolacao linear (lerp) da porta ate' a frente da
//    camera, conforme g_lunge vai de 0 a 1.
Vector3 monsterPosition() {
    BezierPath path = Enemy::getPath();
    if (g_state != LOST) return evaluateBezier(path, g_monsterT);

    Vector3 atDoor = evaluateBezier(path, 1.0f);
    Vector3 target(EYE.x, atDoor.y, EYE.z - 0.6f);
    return atDoor + (target - atDoor) * g_lunge;
}

// ===============================================================
// 4. HUD 2D (desenhado por cima da cena 3D)
// O OpenGL so' usa uma projecao por vez. Pro HUD trocamos pra uma
// projecao ORTOGRAFICA em pixels (gluOrtho2D: (0,0) = canto inferior
// esquerdo), desligamos luz e profundidade, desenhamos, e restauramos.
// ===============================================================
void drawText(void* font, float x, float y, const char* text) {
    glRasterPos2f(x, y);   // a cor usada e' a do glColor ANTES desta chamada
    for (const char* c = text; *c != '\0'; ++c) glutBitmapCharacter(font, *c);
}

void drawCentered(void* font, float y, const char* text) {
    float w = (float)glutBitmapLength(font, (const unsigned char*)text);
    drawText(font, (g_windowW - w) * 0.5f, y, text);
}

void fillRect(float x0, float y0, float x1, float y1) {
    glBegin(GL_QUADS);
        glVertex2f(x0, y0); glVertex2f(x1, y0);
        glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

void drawPlayingHud() {
    char text[64];

    // Barra de energia: quanto resta = largura preenchida. A cor vai de
    // verde (cheia) ate' vermelho (vazia).
    float frac = clamp01(g_power / POWER_MAX);
    glColor3f(0.1f, 0.1f, 0.1f);
    fillRect(20, 20, 240, 42);
    glColor3f(1.0f - frac, frac, 0.1f);
    fillRect(20, 20, 20 + 220 * frac, 42);
    glColor3f(1.0f, 1.0f, 1.0f);
    std::snprintf(text, sizeof(text), "ENERGIA: %d%%", (int)(frac * 100.0f + 0.5f));
    drawText(GLUT_BITMAP_HELVETICA_18, 20, 50, text);

    // Estado da lanterna e da porta.
    std::snprintf(text, sizeof(text), "[F] LANTERNA: %s", g_flashlightOn ? "LIGADA" : "DESLIGADA");
    drawText(GLUT_BITMAP_HELVETICA_18, 20, 78, text);
    std::snprintf(text, sizeof(text), "[D] PORTA: %s", g_doorClosing ? "FECHADA" : "ABERTA");
    drawText(GLUT_BITMAP_HELVETICA_18, 20, 102, text);

    // Cronometro (canto superior direito): quanto falta pra amanhecer.
    int remaining = (int)ceilf(NIGHT_DURATION - g_nightTime);
    std::snprintf(text, sizeof(text), "AMANHECE EM %02d:%02d", remaining / 60, remaining % 60);
    float w = (float)glutBitmapLength(GLUT_BITMAP_HELVETICA_18, (const unsigned char*)text);
    drawText(GLUT_BITMAP_HELVETICA_18, g_windowW - w - 20.0f, g_windowH - 30.0f, text);
}

// Tela de fim de jogo: painel escuro translucido (blending) + texto.
void drawEndScreen() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // cor = nova*alpha + antiga*(1-alpha)
    glColor4f(0.0f, 0.0f, 0.0f, 0.75f);
    fillRect(0, 0, (float)g_windowW, (float)g_windowH);
    glDisable(GL_BLEND);

    float h = (float)g_windowH;
    char text[64];
    if (g_state == LOST) {
        glColor3f(0.9f, 0.05f, 0.05f);
        drawCentered(GLUT_BITMAP_TIMES_ROMAN_24, h * 0.60f, "GAME OVER");
        glColor3f(0.8f, 0.8f, 0.8f);
        drawCentered(GLUT_BITMAP_HELVETICA_18, h * 0.50f, "O Frederico te pegou...");
        int s = (int)g_nightTime, total = (int)NIGHT_DURATION;
        std::snprintf(text, sizeof(text), "Voce aguentou %02d:%02d de %02d:%02d",
                      s / 60, s % 60, total / 60, total % 60);
        drawCentered(GLUT_BITMAP_HELVETICA_18, h * 0.43f, text);
        drawCentered(GLUT_BITMAP_HELVETICA_18, h * 0.30f, "R - tentar de novo  |  ESC - sair");
    } else {
        glColor3f(1.0f, 0.85f, 0.4f);
        drawCentered(GLUT_BITMAP_TIMES_ROMAN_24, h * 0.60f, "6 AM - VOCE SOBREVIVEU A NOITE!");
        glColor3f(0.8f, 0.8f, 0.8f);
        drawCentered(GLUT_BITMAP_HELVETICA_18, h * 0.50f, "O Frederico foi embora... por enquanto.");
        drawCentered(GLUT_BITMAP_HELVETICA_18, h * 0.30f, "R - jogar de novo  |  ESC - sair");
    }
}

void drawHud() {
    // Entra no modo 2D (guardando as matrizes 3D pra devolver depois).
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, g_windowW, 0.0, g_windowH);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    if (g_state == PLAYING)            drawPlayingHud();
    else if (g_state == WON || g_lunge >= 1.0f) drawEndScreen(); // no LOST, so' depois do susto

    // Volta ao modo 3D.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

// ===============================================================
// 5. DESENHO
// ===============================================================
void display() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // [Requisito 5] Projecao em PERSPECTIVA: o que esta' longe fica menor.
    // Campo de visao vertical de 70 graus, proporcao da janela, e planos
    // de corte perto = 0.1 e longe = 200 (nada fora disso e' desenhado).
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(70.0, (double)g_windowW / (double)g_windowH, 0.1, 200.0);

    // [Requisito 5] Camera em primeira pessoa. gluLookAt recebe onde o olho
    // esta', pra onde ele olha e onde e' "pra cima". O ponto olhado e' o
    // olho + a direcao do olhar, que vem dos angulos do mouse (coordenadas
    // esfericas: yaw = 0 e pitch = 0 apontam pra -Z, pro corredor).
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float yaw = g_yawDeg * DEG2RAD, pitch = g_pitchDeg * DEG2RAD;
    gluLookAt(EYE.x, EYE.y, EYE.z,
              EYE.x + sinf(yaw) * cosf(pitch),
              EYE.y + sinf(pitch),
              EYE.z - cosf(yaw) * cosf(pitch),
              0.0, 1.0, 0.0);

    // [Requisito 6] A lanterna vem logo depois do gluLookAt (ver lighting.h).
    updateFlashlight(g_flashlightOn);

    drawRoom();
    drawDesk();
    drawCorridor();
    drawDoor(g_door);

    // O monstro sempre encara o vigia (yaw 180). A boca so' abre no susto.
    float mouthOpen = (g_state == LOST) ? g_lunge : 0.05f;
    Enemy::draw(monsterPosition(), 180.0f, g_walkTime, mouthOpen);

    drawHud();
    glutSwapBuffers(); // double buffering: mostra o quadro pronto de uma vez
}

void reshape(int w, int h) {
    g_windowW = w;
    g_windowH = (h == 0) ? 1 : h;   // evita divisao por zero no aspecto
    glViewport(0, 0, g_windowW, g_windowH);
}

// ===============================================================
// 6. ENTRADA
// ===============================================================

// [Requisito 4] Teclado: F = lanterna, D = porta, R = reiniciar, ESC = sair.
void onKey(unsigned char key, int, int) {
    if (key == 27) std::exit(0);

    if (g_state != PLAYING) {   // nas telas de fim, so' o R vale (depois do susto)
        if ((key == 'r' || key == 'R') && (g_state == WON || g_lunge >= 1.0f)) resetGame();
        return;
    }
    // Desligar sempre pode; ligar so' se ainda tem energia.
    if (key == 'f' || key == 'F') g_flashlightOn = !g_flashlightOn && g_power > 0.0f;
    if (key == 'd' || key == 'D') g_doorClosing  = !g_doorClosing  && g_power > 0.0f;
}

// [Requisito 4] Mouse: olhar ao redor. Tecnica classica de FPS: o cursor e'
// sempre devolvido ao centro da janela, e o quanto ele se afastou do centro
// (dx, dy) vira rotacao da camera. Os limites simulam o pescoco.
void onMouseMove(int x, int y) {
    g_yawDeg   += (x - g_windowW / 2) * MOUSE_SENSITIVITY;
    g_pitchDeg -= (y - g_windowH / 2) * MOUSE_SENSITIVITY; // y da janela cresce pra baixo
    g_yawDeg   = clampf(g_yawDeg,   -YAW_LIMIT_DEG,   YAW_LIMIT_DEG);
    g_pitchDeg = clampf(g_pitchDeg, -PITCH_LIMIT_DEG, PITCH_LIMIT_DEG);
    glutWarpPointer(g_windowW / 2, g_windowH / 2);
}

// ===============================================================
// 7. ATUALIZACAO (LOGICA DO JOGO)
// [Requisito 3] Roda a cada ~16 ms. Tudo que se move depende de dt, o
// tempo REAL desde o quadro anterior, e nao do numero de quadros.
// ===============================================================
void update(int) {
    int now = glutGet(GLUT_ELAPSED_TIME);    // ms desde que o programa abriu
    float dt = (now - g_lastMs) / 1000.0f;
    g_lastMs = now;
    if (dt > 0.1f) dt = 0.1f;                // evita saltos (ex: janela arrastada)

    // Porta: anda em direcao ao alvo (0 = aberta, 1 = fechada) sem passar dele.
    float doorTarget = g_doorClosing ? 1.0f : 0.0f;
    if (g_door < doorTarget) g_door = fminf(g_door + DOOR_SPEED * dt, doorTarget);
    else                     g_door = fmaxf(g_door - DOOR_SPEED * dt, doorTarget);
    bool sealed = g_door >= 0.99f;           // so' "veda" quando chegou no chao

    if (g_state == PLAYING) {
        g_nightTime += dt;
        g_walkTime  += dt;

        // Bateria: lanterna e porta fechada gastam energia. Zerou = as duas
        // param de funcionar de vez.
        float drain = 0.0f;
        if (g_flashlightOn) drain += DRAIN_LIGHT;
        if (g_doorClosing)  drain += DRAIN_DOOR;
        g_power -= drain * dt;
        if (g_power <= 0.0f) {
            g_power = 0.0f;
            g_flashlightOn = false;
            g_doorClosing  = false;
        }

        // Monstro: "rate" e' a variacao de t por segundo (positivo = se aproxima).
        float rate;
        if (sealed)              rate = 0.0f;              // porta fechada: contido
        else if (g_flashlightOn) rate = -flashlightPush(); // lanterna: recua
        else                     rate = monsterSpeed();    // escuro: avanca
        g_monsterT = clamp01(g_monsterT + rate * dt);

        // Fim de jogo. Perder tem prioridade: se ele chegou no mesmo quadro
        // em que amanheceu, voce perde.
        if (g_monsterT >= 1.0f && !sealed) {
            g_state = LOST;
            g_lunge = 0.0f;
        } else if (g_nightTime >= NIGHT_DURATION) {
            g_state = WON;
        }
    } else if (g_state == LOST) {
        g_walkTime += dt;
        g_lunge = fminf(g_lunge + dt / LUNGE_DURATION, 1.0f); // o salto do susto
    }

    glutPostRedisplay();          // pede pro GLUT chamar display()
    glutTimerFunc(16, update, 0); // reagenda (16 ms = ~60 quadros por segundo)
}

} // namespace

// ===============================================================
// 8. MAIN: cria a janela, registra os callbacks e entrega o controle ao GLUT
// ===============================================================
int main(int argc, char** argv) {
    glutInit(&argc, argv);
    // DOUBLE = 2 buffers (sem flicker); RGB = cores; DEPTH = z-buffer.
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(g_windowW, g_windowH);
    glutCreateWindow("1 Noite no Frederico");

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(onKey);
    glutPassiveMotionFunc(onMouseMove);
    glutIgnoreKeyRepeat(1);            // segurar a tecla nao repete o evento
    glutSetCursor(GLUT_CURSOR_NONE);   // esconde o cursor
    glutWarpPointer(g_windowW / 2, g_windowH / 2);

    glEnable(GL_DEPTH_TEST);           // z-buffer: o que esta' perto esconde o que esta' longe
    initLighting();

    resetGame();
    g_lastMs = glutGet(GLUT_ELAPSED_TIME);
    glutTimerFunc(16, update, 0);

    std::printf("=== 1 Noite no Frederico ===\n");
    std::printf("Sobreviva ate' as 6 da manha (5 minutos).\n");
    std::printf("Mouse: olhar | F: lanterna | D: porta | R: reiniciar | ESC: sair\n");

    glutMainLoop();                    // nunca retorna
    return 0;
}
