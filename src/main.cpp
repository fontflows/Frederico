// ============================================================
// 1 Noite no Frederico
//
// Requisitos academicos cobertos (ver comentarios "[Requisito X]"
// espalhados pelo codigo e nos outros arquivos do projeto):
//   A - Modelagem de objetos 3D com primitivas       -> scene_builder.*, enemy.*
//   B - Transformacoes geometricas (hierarquia)      -> enemy.cpp, scene_builder.cpp (ventilador)
//   C - Animacoes (controle de tempo)                -> timerFunc() abaixo
//   D - Controle de mouse e teclado                  -> passiveMotion(), keyboard(), mouse()
//   E - Camera e perspectiva                         -> display() (gluPerspective/gluLookAt)
//   F - Iluminacao                                   -> lighting.*, lanterna (spot), nevoa, bateria baixa aqui
//   H - Texturas (procedurais)                       -> textures.*, aplicadas em scene_builder.cpp
//   I - Atmosfera (vinheta, granulado, balanco de camera) -> computeSway(), drawAtmosphere()
//   J - Audio procedural (sintese em codigo)         -> audio.*, eventos e parametros em timerFunc()
//   G - Curvas parametricas (Bezier)                 -> bezier.*, usado em currentMonsterPosition()
//
// ------------------------------------------------------------
// COMO O PROGRAMA FUNCIONA (visao geral)
// ------------------------------------------------------------
// O GLUT roda um laco de eventos (glutMainLoop). Nos registramos
// "callbacks", funcoes que o GLUT chama quando algo acontece:
//
//   glutTimerFunc(16ms)   -> timerFunc()    : ATUALIZA o jogo (logica, sem desenhar)
//   glutDisplayFunc       -> display()      : DESENHA o estado atual na tela
//   glutKeyboardFunc      -> keyboard()     : tecla comum apertada
//   glutSpecialFunc       -> special()      : setas e teclas F1..F12
//   glutMouseFunc         -> mouse()        : clique do mouse (menus)
//   glutPassiveMotionFunc -> passiveMotion(): mouse se mexeu
//   glutReshapeFunc       -> reshape()      : janela mudou de tamanho
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
//   MENU --(Jogar)--> PLAYING <--(P / Esc)--> PAUSED
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
//   4. Camera, lanterna (feixe e mira) e posicao do monstro
//   5. Captura de tela (gravador de PNG) e modo foto
//   6. HUD 2D (barra, cronometro, menus, telas de fim)
//   7. display()  - desenho da cena 3D + HUD
//   8. Entrada    - teclado, setas, mouse
//   9. timerFunc()- logica do jogo a cada frame
//  10. main()     - inicializacao
// ============================================================

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#ifdef _WIN32
    #include <direct.h>
    #define MAKE_DIR(p) _mkdir(p)
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #define MAKE_DIR(p) mkdir(p, 0755)
#endif

#include "bezier.h"
#include "lighting.h"
#include "scene_builder.h"
#include "enemy.h"
#include "textures.h"
#include "audio.h"

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
const float RETREAT_RATE      = 0.11f;  // recuo quando a lanterna esta' acesa E apontada pra ele

// Bateria compartilhada entre lanterna, porta e monitor. Quando acaba,
// tudo para de funcionar e nao volta mais. Os "drains" sao dados como
// MAX/segundos, pra ficar facil ler: lanterna sozinha dura 250 s.
const float POWER_MAX           = 100.0f;
const float POWER_DRAIN_LIGHT   = POWER_MAX / 250.0f; // energia por segundo com lanterna ligada
const float POWER_DRAIN_DOOR    = POWER_MAX / 40.0f;  // com a porta fechada
const float POWER_DRAIN_MONITOR = POWER_MAX / 200.0f; // com o monitor ligado

// Feixe da lanterna. A lanterna e' um cone: so' afasta o monstro se o
// corpo dele estiver DENTRO do cone. BEAM_HALF_ANGLE_DEG e' a meia
// abertura (graus); o tamanho do corpo do monstro conta como folga.
// Aumente pra deixar o jogo mais facil, diminua pra exigir mira.
const float BEAM_HALF_ANGLE_DEG = 13.0f;
const float BEAM_RANGE          = 26.0f;  // alcance maximo (m)
const float MONSTER_HIT_RADIUS  = 0.9f;   // raio aproximado do corpo do monstro (m)
const float MONSTER_CENTER_Y    = 1.5f;   // altura do centro do corpo do monstro (m)

// Lanterna com bateria baixa: abaixo de FLASH_LOW_FRAC da bateria (25%) ela
// vai piorando aos poucos: escurece e fica amarelada, o feixe estreita e
// encurta, e ela falha (pisca e apaga por instantes). Quanto mais perto
// de 0%, pior. Quando falha, nao afasta o monstro.
const float FLASH_LOW_FRAC = 0.25f;

// Cor e forca da luz da lanterna. O main.cpp define TUDO da GL_LIGHT0 (nao
// depende do que o lighting.cpp deixou configurado): cor quente, intensidade
// acima de 1 pra iluminar bem o monstro mesmo de longe, e uma atenuacao
// suave (a 8 m resta ~67% da luz, a 15 m ~43%, a 22 m ~28%).
const float FLASH_R = 1.00f, FLASH_G = 0.96f, FLASH_B = 0.88f;
const float FLASH_INTENSITY = 1.5f;

// Nevoa: densidade da nevoa exponencial (GL_EXP2). A visibilidade cai com
// a distancia como exp(-(densidade*distancia)^2): com 0.045, a 10 m
// resta ~80%, a 20 m ~45% e no fundo do corredor (~22 m) ~37%.
const float FOG_DENSITY = 0.045f;

// Atmosfera. O "perigo" (g_danger, 0 a 1) sobe quando o monstro se
// aproxima, quando ele esta' em sprint e quando a bateria esta' acabando;
// e' ele que intensifica a vinheta, o granulado, o balanco da camera e a
// nevoa. VIGNETTE_BASE e GRAIN_BASE sao os valores com perigo zero.
const float VIGNETTE_BASE = 0.30f;  // escurecimento das bordas da tela (0 a 1)
const float GRAIN_BASE    = 0.045f; // forca do granulado de filme (0 a 1)

// Dificuldade progressiva: o monstro fica mais rapido conforme a noite
// avanca (de 1.0x ate' SPEED_RAMP_END) e a velocidade oscila entre
// JITTER_MIN e JITTER_MAX, sorteada de novo toda vez que ele e'
// empurrado de volta ao fundo do corredor (o "humor" do monstro).
const float SPEED_RAMP_END   = 1.45f;
const float SPEED_JITTER_MIN = 0.85f;
const float SPEED_JITTER_MAX = 1.25f;

// Sprints aleatorios: de vez em quando o monstro dispara. O intervalo
// entre sprints e' sorteado entre GAP_MIN e GAP_MAX e vai encurtando ate'
// ~45% conforme a noite avanca. Durante o sprint a lanterna so'
// desacelera o monstro (ele continua avancando).
const float SPRINT_RATE         = 0.30f; // velocidade do sprint (t/seg), ~4x a caminhada
const float SPRINT_DURATION_MIN = 0.9f;  // duracao do sprint, em segundos
const float SPRINT_DURATION_MAX = 1.5f;
const float SPRINT_GAP_MIN      = 8.0f;  // intervalo entre sprints, em segundos
const float SPRINT_GAP_MAX      = 16.0f;
const float SPRINT_GAP_SHRINK   = 0.45f; // quanto o intervalo encurta ate' o fim da noite
const float SPRINT_FIRST_DELAY  = 6.0f;  // folga extra antes do 1o sprint
const float SPRINT_ANIM_BOOST   = 2.2f;  // pernas do monstro mexem mais rapido durante o sprint

// A fonte vetorial (stroke) do GLUT tem ~119 unidades de altura.
// Usamos isso pra escalar o texto pro tamanho em pixels que quisermos.
const float STROKE_FONT_H = 119.05f;

// ===============================================================
// 2. ESTADO GLOBAL
// ===============================================================
enum GameState { STATE_MENU, STATE_PLAYING, STATE_PAUSED,
                 STATE_JUMPSCARE, STATE_GAME_OVER, STATE_WON };

int g_windowW = 1024;
int g_windowH = 768;

// Camera: fica PARADA (o vigia esta' sentado: olhos a 1.2 m do chao, 45 cm
// acima do tampo da mesa). Quem muda e' a direcao pra onde ele olha (yaw =
// giro horizontal, pitch = vertical), via mouse.
// ATENCAO: o Z (ROOM_BACK_Z - 1.0) tem que ser o mesmo PLAYER_EYE_Z do
// scene_builder.cpp, que usa esse valor pra posicionar a mesa na frente.
Vector3 g_eye(0.0f, 1.2f, Scene::ROOM_BACK_Z - 1.0f);
float g_yawDeg   = 0.0f;
float g_pitchDeg = 0.0f;
bool  g_warping  = false; // evita que o glutWarpPointer gere um evento fantasma de movimento
bool  g_cursorShown = false; // o cursor so' aparece nos menus

// Recursos do jogador
bool  g_flashlightOn = false; // comeca desligada: economiza bateria e deixa o corredor escuro
bool  g_monitorOn    = false; // monitor da mesa (mostra onde o monstro esta')
float g_power        = POWER_MAX;
bool  g_beamHit      = false; // o feixe da lanterna esta' acertando o monstro agora?
bool  g_debug        = false; // painel de depuracao (F3)
float g_flashLevel   = 1.0f;  // 0..1: quanto a lanterna esta' funcionando (cai com bateria baixa)
float g_danger       = 0.0f;  // 0 = calmo, 1 = perigo maximo (alimenta a atmosfera)
GLuint g_grainTex    = 0;     // textura de ruido do granulado
// Fases INTEGRADAS (somadas dt a dt) do batimento e da respiracao. Nao da'
// pra usar "tempo * velocidade": a velocidade muda com o perigo, e a fase
// "pularia" a cada mudanca. Somando passo a passo, so' a velocidade muda.
float g_heartPhase   = 0.0f;  // radianos; alimenta a vinheta pulsante e o som do coracao
float g_breathPhase  = 0.0f;  // radianos; alimenta a respiracao da camera
int   g_lastHour     = 0;     // ultima "hora" da noite que ja tocou o sino
bool  g_monsterMoved = false; // o monstro andou neste frame? (so' entao ha' passos)
bool  g_doorMoving   = false; // a porta esta' em movimento? (motor + pancada ao parar)

// Porta: g_doorClosing e' o que o jogador PEDIU; g_doorOffsetY e' onde a
// porta REALMENTE esta' (ela anima suavemente ate' o alvo).
bool  g_doorClosing = false;
float g_doorOffsetY = 0.0f; // 0 = aberta, DOORWAY_HEIGHT = fechada

// Monstro
float g_monsterT = 0.0f; // parametro t da curva de Bezier, [0,1] (0 = fundo, 1 = porta)
float g_walkTime = 0.0f; // tempo acumulado que alimenta a animacao de caminhada

// Fluxo do jogo
GameState g_state       = STATE_MENU;
float     g_stateTimer  = 0.0f; // segundos desde que entrou no estado (anima menus e telas de fim)
float     g_nightTime   = 0.0f; // segundos de noite sobrevividos (alimenta o cronometro)
float     g_uiTime      = 0.0f; // relogio que nunca para: usado em efeitos visuais (piscar, ventilador)
float     g_jumpscareProgress = 0.0f; // 0..1 durante o salto do monstro
int       g_menuSel     = 0;    // item selecionado no menu

// Humor / sprint do monstro
float g_speedJitter   = 1.0f;  // multiplicador sorteado (ver SPEED_JITTER_*)
bool  g_repelled      = true;  // true enquanto o monstro esta' encostado no fundo do corredor
bool  g_sprinting      = false; // esta' em sprint agora?
float g_sprintTimeLeft = 0.0f;  // quanto falta pro sprint atual acabar
float g_nextSprintIn   = 10.0f; // quanto falta pro proximo sprint comecar

int g_lastTimeMs = 0; // instante do frame anterior, pra calcular o dt

// Captura de tela e modo foto (secao 5)
bool  g_screenshotRequested = false;
int   g_screenshotCount     = 0;
bool  g_photoActive   = false;   // rodando a sequencia automatica de fotos?
bool  g_photoExitAtEnd = false;
int   g_photoIndex    = 0;
int   g_photoFrames   = 0;
bool  g_camOverride   = false;   // camera livre (usada so' nas fotos)
Vector3 g_ovEye, g_ovCenter;
float g_photoAmbient  = -1.0f;   // luz ambiente extra na foto (<0 = nao mexe)
float g_photoMouth    = -1.0f;   // boca do monstro forcada (<0 = normal)
bool  g_photoHud      = false;
float g_photoTime     = 2.0f;    // "relogio" fixo da foto (controla piscadas e ventilador)

// ===============================================================
// 3. UTILITARIOS E REGRAS DE DIFICULDADE
// ===============================================================

// Limita x ao intervalo [0,1].
float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Degrau suave: 0 antes de a, 1 depois de b, curva em S no meio.
float smooth01(float a, float b, float x) {
    float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Interpolacao linear: t=0 devolve a, t=1 devolve b, valores no meio
// misturam proporcionalmente. Usada nas cores e nas rampas.
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Numero aleatorio uniforme em [lo, hi].
float randRange(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

// "Hash": numero fixo em [0,1] a partir de dois inteiros. Sempre da' o
// mesmo valor pra mesma entrada, entao serve de "aleatorio estavel"
// (as particulas de poeira nao ficam trocando de lugar a cada frame).
float hash01(int a, int b) {
    unsigned int n = (unsigned int)a * 73856093u ^ (unsigned int)b * 19349663u;
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return (float)(n & 0x7fffffffu) / 2147483647.0f;
}

// Quanto da noite ja' passou, de 0 (inicio) a 1 (amanheceu).
float nightProgress() { return clamp01(g_nightTime / NIGHT_DURATION); }

// Velocidade de caminhada do monstro AGORA: cresce linearmente com o
// progresso da noite e e' multiplicada pelo "humor" sorteado.
float currentSpeedMultiplier() {
    return (1.0f + (SPEED_RAMP_END - 1.0f) * nightProgress()) * g_speedJitter;
}

// Sorteia quando vem o proximo sprint. O intervalo encurta com o
// progresso da noite (shrink vai de 1.0 ate' 1 - SPRINT_GAP_SHRINK).
void scheduleNextSprint() {
    float shrink = 1.0f - SPRINT_GAP_SHRINK * nightProgress();
    g_nextSprintIn = randRange(SPRINT_GAP_MIN, SPRINT_GAP_MAX) * shrink;
}

// Comeca uma noite nova do zero (estado PLAYING). E' chamada pelo menu
// ("Jogar"/"Reiniciar") e quando o jogador aperta R depois de perder/ganhar.
// Restaura TUDO (bateria, porta, monstro, cronometro).
void resetGame() {
    g_state             = STATE_PLAYING;
    g_stateTimer        = 0.0f;
    g_nightTime         = 0.0f;

    g_power             = POWER_MAX;
    g_flashlightOn      = false;
    g_monitorOn         = false;
    g_beamHit           = false;
    g_danger            = 0.0f;
    g_lastHour          = 0;
    g_monsterMoved      = false;
    g_doorMoving        = false;
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
// 4. CAMERA, LANTERNA E POSICAO DO MONSTRO
// ===============================================================

// [Requisito E] Direcao "para frente" da camera a partir de yaw/pitch
// (controlados pelo mouse). Sao coordenadas esfericas convertidas em
// vetor: yaw=0/pitch=0 aponta para -Z (a abertura da sala / corredor).
// dyaw/dpitch: deslocamento extra em graus (usado so' pelo balanco visual
// da camera; a mira da lanterna usa os valores sem balanco).
Vector3 computeForward(float dyaw = 0.0f, float dpitch = 0.0f) {
    float yawRad   = (g_yawDeg   + dyaw)   * DEG2RAD;
    float pitchRad = (g_pitchDeg + dpitch) * DEG2RAD;
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
    // O monstro e' alto (~3 m) e no susto ele tomba o tronco pra frente
    // (ver enemy.cpp), entao os pes dele param a 2 m da camera: assim a
    // cabeca fica a ~0.8 m dos olhos, de frente pra tela.
    Vector3 lungeTarget(g_eye.x, doorwayPos.y, g_eye.z - 2.0f);
    return doorwayPos + (lungeTarget - doorwayPos) * g_jumpscareProgress;
}

// --- Lanterna com bateria baixa --------------------------------
// lowPower(): 0 = bateria normal (>= 25%), 1 = vazia. Tudo abaixo e'
// funcao disso, entao a lanterna piora de forma gradual.
float lowPower() { return clamp01(1.0f - g_power / (POWER_MAX * FLASH_LOW_FRAC)); }

// Abertura e alcance EFETIVOS do feixe: encolhem com a bateria baixa. A
// mira (flashlightHits), a luz (spot) e o desenho do feixe usam estes valores.
float beamHalfAngle() { return lerpf(BEAM_HALF_ANGLE_DEG, BEAM_HALF_ANGLE_DEG * 0.6f, lowPower()); }
float beamRange()     { return lerpf(BEAM_RANGE, BEAM_RANGE * 0.5f, lowPower()); }

// g_flashLevel: intensidade da lanterna agora. Depende so' do relogio e da
// bateria (sem aleatoriedade guardada), entao display e logica concordam.
//  - escurece ate' 55% conforme a bateria cai;
//  - "falhas": a 24 vezes por segundo sorteia (hash) se a luz cai pra 15%;
//    a chance sobe com o quadrado de lowPower();
//  - abaixo de ~50% de lowPower aparecem APAGOES de ate' meio segundo.
void updateFlashLevel() {
    float low = lowPower();
    float level = 1.0f - 0.45f * low;
    if (low > 0.0f) {
        int tick = (int)(g_uiTime * 24.0f);
        float chance = 0.04f + 0.55f * low * low;
        if (hash01(tick, 77) < chance) level *= 0.15f;
        if (low > 0.5f && fmodf(g_uiTime, 2.1f) < (low - 0.5f)) level = 0.0f;
    }
    g_flashLevel = level;
}

// A MIRA DA LANTERNA. O feixe e' um cone com vertice no olho do
// jogador, eixo na direcao "dir" (vetor unitario) e meia abertura
// BEAM_HALF_ANGLE_DEG. Um alvo (centro + raio) esta' iluminado se o
// angulo entre "dir" e a direcao ate' o centro dele for menor que a
// meia abertura MAIS o angulo que o proprio raio ocupa a essa distancia
// (um alvo grande conta mesmo que so' a ponta dele entre no cone).
// Funcao generica: serve pro monstro de hoje e pros de amanha.
//
// Matematica: cos(angulo) = (v . dir) / |v|, onde v = alvo - olho.
bool flashlightHits(const Vector3& eye, const Vector3& dir,
                    const Vector3& target, float radius) {
    Vector3 v = target - eye;
    float dist = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (dist > beamRange()) return false;
    if (dist < 0.001f) return true;
    float cosA  = clampf((v.x * dir.x + v.y * dir.y + v.z * dir.z) / dist, -1.0f, 1.0f);
    float angle = acosf(cosA) / DEG2RAD;                       // angulo ate' o centro do alvo
    float slack = asinf(clampf(radius / dist, 0.0f, 1.0f)) / DEG2RAD; // angulo do raio do alvo
    return angle <= beamHalfAngle() + slack;
}

// Configura a luz da lanterna (GL_LIGHT0) por inteiro: um SPOT, ou seja, um
// cone de luz saindo do olho do jogador pra frente, em vez de uma luz que
// brilha igual pra todos os lados.
//  - Posicao e direcao sao dadas com a matriz IDENTIDADE: assim ficam em
//    coordenadas do OLHO (posicao (0,0,0), direcao (0,0,-1)), grudadas na
//    camera (inclusive no balanco e no roll), sem depender do que o
//    lighting.cpp fez.
//  - Cor e intensidade: FLASH_* x nivel da lanterna (cai com a bateria
//    baixa), puxada pro amarelo/laranja conforme a bateria acaba. Nao le
//    nada da luz anterior: valores lidos com a lanterna apagada seriam
//    pretos e a lanterna nao iluminaria nada.
//  - GL_SPOT_EXPONENT concentra a intensidade no centro do cone.
// Desligada: so' apaga a luz.
void setupFlashlight() {
    if (!g_flashlightOn) { glDisable(GL_LIGHT0); return; }
    glEnable(GL_LIGHT0);

    glPushMatrix();
    glLoadIdentity();
    const GLfloat pos[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    const GLfloat dir[3] = { 0.0f, 0.0f, -1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, dir);
    glPopMatrix();

    glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, beamHalfAngle() + 3.0f);
    glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, 12.0f);
    glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION,  1.0f);
    glLightf(GL_LIGHT0, GL_LINEAR_ATTENUATION,    0.03f);
    glLightf(GL_LIGHT0, GL_QUADRATIC_ATTENUATION, 0.004f);

    float low = lowPower();
    float k = FLASH_INTENSITY * g_flashLevel;
    const GLfloat amb[4]  = { 0.0f, 0.0f, 0.0f, 1.0f };
    const GLfloat diff[4] = { FLASH_R * k,
                              FLASH_G * k * (1.0f - 0.20f * low),
                              FLASH_B * k * (1.0f - 0.45f * low), 1.0f };
    const GLfloat spec[4] = { 0.8f * diff[0], 0.8f * diff[1], 0.8f * diff[2], 1.0f };
    glLightfv(GL_LIGHT0, GL_AMBIENT,  amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  diff);
    glLightfv(GL_LIGHT0, GL_SPECULAR, spec);
}

// Desenha o FEIXE VISIVEL da lanterna: luz volumetrica saindo da "mao"
// do jogador, com poeira flutuando dentro. Tecnicas:
//  - FATIAS: em vez de uma casca de cone (que teria uma borda dura), o
//    feixe e' uma pilha de 64 discos perpendiculares ao eixo, cada um com
//    um gradiente radial (opaco no centro, transparente na borda). A
//    soma deles da' um brilho suave, mais forte no eixo, que some nas
//    bordas e com a distancia (o alpha de cada fatia cai com a distancia).
//  - Transparencia ADITIVA (GL_SRC_ALPHA, GL_ONE): a cor do feixe e'
//    SOMADA ao que ja' esta' na tela, entao ele so' clareia, como luz.
//  - Escrita de profundidade desligada: o feixe nao esconde nada, mas
//    paredes e o monstro que estao na frente dele ainda o escondem (o
//    teste de profundidade continua ligado).
// O desenho acontece num referencial girado pra que -Z local aponte pra
// onde a camera olha (Ry(-yaw) * Rx(pitch)).
void drawFlashlightBeam(const Vector3& eye, const Vector3& dir, bool hit) {
    float yaw   = atan2f(dir.x, -dir.z) / DEG2RAD;
    float pitch = asinf(clampf(dir.y, -1.0f, 1.0f)) / DEG2RAD;

    // intensidade: pisca quando a bateria esta' acabando; mais forte
    // quando acerta o monstro
    // Com bateria baixa o feixe acompanha a lanterna: some e volta
    // (g_flashLevel), fica amarelado, mais curto e mais estreito.
    float low = lowPower();
    float k = (hit ? 1.6f : 1.0f) * g_flashLevel;
    // cor: branco quente; branco-azulado quando acerta o monstro;
    // amarelo-alaranjado com bateria baixa
    float cr = hit ? 0.85f : 1.00f;
    float cg = (hit ? 0.92f : 0.95f) - 0.20f * low;
    float cb = (hit ? 1.00f : 0.80f) - 0.40f * low;

    const float L = 16.0f * (beamRange() / BEAM_RANGE);       // comprimento do feixe desenhado
    const float R = L * tanf(beamHalfAngle() * DEG2RAD);      // raio do feixe na ponta
    const float ox = 0.13f, oy = -0.10f, oz = -0.15f;         // onde a "mao" segura (local)

    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                 GL_CURRENT_BIT | GL_POINT_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);        // o feixe tem o proprio fade; a nevoa so' o apagaria
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    glPushMatrix();
        glTranslatef(eye.x, eye.y, eye.z);
        glRotatef(-yaw,  0.0f, 1.0f, 0.0f);
        glRotatef(pitch, 1.0f, 0.0f, 0.0f);

        const int SLICES = 64, SEG = 24;
        for (int s = 0; s < SLICES; ++s) {
            // fatias mais densas perto da mao (expoente 1.5): e' ali que o
            // cone e' estreito e precisa de mais luz pra aparecer
            float u  = powf((float)(s + 1) / (float)SLICES, 1.5f); // 0 = mao, 1 = ponta do feixe
            // o eixo vai da mao ate' a linha de visao (0,0,-L), como uma
            // lanterna segurada apontando pra frente
            float cx = ox * (1.0f - u), cy = oy * (1.0f - u);
            float cz = oz + (-L - oz) * u;
            float rr = R * u;                                    // raio da fatia (cone)
            // opacidade no centro: cai com a distancia, mas com reforco
            // perto da mao (a luz e' mais concentrada la')
            float a  = 0.024f * k * powf(1.0f - u, 1.2f) / (0.35f + 2.0f * u);

            glBegin(GL_TRIANGLE_FAN);                            // miolo do disco
                glColor4f(cr, cg, cb, a);
                glVertex3f(cx, cy, cz);
                glColor4f(cr, cg, cb, a * 0.45f);
                for (int i = 0; i <= SEG; ++i) {
                    float t = 6.2831853f * (float)i / (float)SEG;
                    glVertex3f(cx + 0.5f * rr * cosf(t), cy + 0.5f * rr * sinf(t), cz);
                }
            glEnd();
            glBegin(GL_QUAD_STRIP);                              // borda que some
                for (int i = 0; i <= SEG; ++i) {
                    float t = 6.2831853f * (float)i / (float)SEG;
                    glColor4f(cr, cg, cb, a * 0.45f);
                    glVertex3f(cx + 0.5f * rr * cosf(t), cy + 0.5f * rr * sinf(t), cz);
                    glColor4f(cr, cg, cb, 0.0f);
                    glVertex3f(cx + rr * cosf(t), cy + rr * sinf(t), cz);
                }
            glEnd();
        }

        // poeira: pontos espalhados dentro do cone, flutuando devagar.
        // A posicao de cada um vem de hash01 (estavel) + um deslocamento
        // que depende do tempo.
        glEnable(GL_POINT_SMOOTH);
        glPointSize(2.0f);
        glBegin(GL_POINTS);
            for (int i = 0; i < 70; ++i) {
                float u   = 0.04f + 0.96f * hash01(i, 1);                // posicao ao longo do feixe
                float rad = sqrtf(hash01(i, 2)) * R * u * 0.85f;          // raio dentro do cone
                rad *= 0.75f + 0.25f * sinf(g_uiTime * 0.7f + (float)i);
                float ang = 6.2831853f * hash01(i, 3) + g_uiTime * 0.12f * (float)((i % 3) - 1);
                float cx = ox * (1.0f - u), cy = oy * (1.0f - u);
                float cz = oz + (-L - oz) * u;
                float a  = (0.25f + 0.55f * hash01(i, 4)) * (1.0f - 0.6f * u) * k;
                glColor4f(1.0f, 1.0f, 0.9f, a);
                glVertex3f(cx + rad * cosf(ang), cy + rad * sinf(ang) + 0.04f * sinf(g_uiTime * 0.5f + (float)i), cz);
            }
        glEnd();
    glPopMatrix();
    glPopAttrib();
}

// ===============================================================
// 5. CAPTURA DE TELA (PNG) E MODO FOTO
//
// Escrevemos o PNG "na mao", sem biblioteca. Um arquivo PNG e':
//   assinatura de 8 bytes + chunks (IHDR cabecalho, IDAT dados, IEND fim).
// Cada chunk = tamanho + tipo + dados + CRC32. Os pixels vao dentro de
// um fluxo "zlib" (formato deflate). Pra o arquivo nao ficar enorme:
//   1. filtro "Sub": cada byte vira a diferenca pro pixel da esquerda
//      (cenas escuras e suaves viram quase so' zeros);
//   2. compressao LZ77: sequencias repetidas viram "volte N bytes e copie
//      M" (achadas com uma tabela hash de 3 bytes);
//   3. codigos de Huffman FIXOS do padrao deflate pra gravar isso em bits.
// ===============================================================

unsigned int g_crcTable[256];
bool         g_crcReady = false;

void initCrcTable() {
    for (unsigned int n = 0; n < 256; ++n) {
        unsigned int c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crcTable[n] = c;
    }
    g_crcReady = true;
}

unsigned int crcUpdate(unsigned int crc, const unsigned char* d, size_t n) {
    for (size_t i = 0; i < n; ++i) crc = g_crcTable[(crc ^ d[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

void putU32(std::vector<unsigned char>& v, unsigned int x) {
    v.push_back((unsigned char)((x >> 24) & 0xFFu));
    v.push_back((unsigned char)((x >> 16) & 0xFFu));
    v.push_back((unsigned char)((x >> 8) & 0xFFu));
    v.push_back((unsigned char)(x & 0xFFu));
}

void writeChunk(FILE* f, const char* type, const std::vector<unsigned char>& data) {
    std::vector<unsigned char> head;
    putU32(head, (unsigned int)data.size());
    fwrite(&head[0], 1, 4, f);
    fwrite(type, 1, 4, f);
    if (!data.empty()) fwrite(&data[0], 1, data.size(), f);
    unsigned int crc = crcUpdate(0xFFFFFFFFu, (const unsigned char*)type, 4);
    if (!data.empty()) crc = crcUpdate(crc, &data[0], data.size());
    crc ^= 0xFFFFFFFFu;
    std::vector<unsigned char> tail;
    putU32(tail, crc);
    fwrite(&tail[0], 1, 4, f);
}

// Escritor de bits: o deflate grava os campos "LSB primeiro" (bit menos
// significativo antes), mas os codigos de Huffman "MSB primeiro".
struct BitWriter {
    std::vector<unsigned char>* out;
    unsigned int acc;
    int nbits;
    explicit BitWriter(std::vector<unsigned char>* o) : out(o), acc(0), nbits(0) {}
    void put(unsigned int value, int count) {            // campo comum (LSB primeiro)
        acc |= value << nbits;
        nbits += count;
        while (nbits >= 8) { out->push_back((unsigned char)(acc & 0xFFu)); acc >>= 8; nbits -= 8; }
    }
    void putCode(unsigned int code, int len) {           // codigo de Huffman (MSB primeiro)
        unsigned int r = 0;
        for (int i = 0; i < len; ++i) r = (r << 1) | ((code >> i) & 1u);
        put(r, len);
    }
    void flush() { if (nbits > 0) { out->push_back((unsigned char)(acc & 0xFFu)); acc = 0; nbits = 0; } }
};

// Tabelas do deflate: comprimentos 3..258 e distancias 1..32768 sao
// divididos em faixas; cada faixa tem um simbolo e alguns bits "extras".
const int LEN_BASE[29]  = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
const int LEN_EXTRA[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
const int DIST_BASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
const int DIST_EXTRA[30]= { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

// Simbolo do alfabeto literal/comprimento no codigo de Huffman FIXO:
//   0-143 -> 8 bits, 144-255 -> 9 bits, 256-279 -> 7 bits, 280-287 -> 8 bits
void putFixedSymbol(BitWriter& bw, int sym) {
    if (sym < 144)      bw.putCode(0x30u + (unsigned int)sym, 8);
    else if (sym < 256) bw.putCode(0x190u + (unsigned int)(sym - 144), 9);
    else if (sym < 280) bw.putCode((unsigned int)(sym - 256), 7);
    else                bw.putCode(0xC0u + (unsigned int)(sym - 280), 8);
}

void putMatch(BitWriter& bw, int len, int dist) {
    int li = 28;
    while (LEN_BASE[li] > len) --li;
    putFixedSymbol(bw, 257 + li);
    if (LEN_EXTRA[li]) bw.put((unsigned int)(len - LEN_BASE[li]), LEN_EXTRA[li]);
    int di = 29;
    while (DIST_BASE[di] > dist) --di;
    bw.putCode((unsigned int)di, 5);                      // distancias: codigo fixo de 5 bits
    if (DIST_EXTRA[di]) bw.put((unsigned int)(dist - DIST_BASE[di]), DIST_EXTRA[di]);
}

// Comprime "d" (n bytes) num unico bloco deflate de Huffman fixo.
void deflateFixed(const std::vector<unsigned char>& d, std::vector<unsigned char>& out) {
    BitWriter bw(&out);
    bw.put(1, 1);   // BFINAL = 1 (ultimo bloco)
    bw.put(1, 2);   // BTYPE  = 01 (Huffman fixo)

    const size_t n = d.size();
    const int HASH_SIZE = 1 << 15;
    std::vector<int> head(HASH_SIZE, -1), prev(n, -1);

    size_t i = 0;
    while (i < n) {
        int bestLen = 0, bestDist = 0;
        if (i + 2 < n) {
            unsigned int h = (((unsigned int)d[i] << 10) ^ ((unsigned int)d[i + 1] << 5) ^ d[i + 2]) & (HASH_SIZE - 1);
            int cand = head[h];
            int depth = 0;
            size_t maxLen = n - i; if (maxLen > 258) maxLen = 258;
            while (cand >= 0 && (int)i - cand <= 32768 && depth++ < 24) {
                size_t l = 0;
                while (l < maxLen && d[cand + l] == d[i + l]) ++l;
                if ((int)l > bestLen) {
                    bestLen = (int)l; bestDist = (int)i - cand;
                    if (l == maxLen) break;
                }
                cand = prev[cand];
            }
        }
        size_t step = 1;
        if (bestLen >= 3) { putMatch(bw, bestLen, bestDist); step = (size_t)bestLen; }
        else              { putFixedSymbol(bw, d[i]); }
        for (size_t k = 0; k < step; ++k) {                 // registra as posicoes cobertas na tabela hash
            size_t pos = i + k;
            if (pos + 2 < n) {
                unsigned int h = (((unsigned int)d[pos] << 10) ^ ((unsigned int)d[pos + 1] << 5) ^ d[pos + 2]) & (HASH_SIZE - 1);
                prev[pos] = head[h];
                head[h] = (int)pos;
            }
        }
        i += step;
    }
    putFixedSymbol(bw, 256);   // fim de bloco
    bw.flush();
}

// Grava um PNG RGB 8 bits. "rgb" vem do glReadPixels (linhas de BAIXO
// pra CIMA), entao invertemos a ordem das linhas ao montar o arquivo.
bool savePng(const char* path, int w, int h, const unsigned char* rgb) {
    if (!g_crcReady) initCrcTable();

    // dados filtrados: cada linha comeca com o byte de filtro 1 ("Sub"),
    // e cada byte vira (byte - byte do pixel da esquerda)
    const size_t stride = (size_t)w * 3;
    std::vector<unsigned char> raw;
    raw.reserve((size_t)h * (1 + stride));
    for (int y = h - 1; y >= 0; --y) {
        const unsigned char* row = rgb + (size_t)y * stride;
        raw.push_back(1);
        for (size_t i = 0; i < stride; ++i) {
            unsigned char left = (i >= 3) ? row[i - 3] : 0;
            raw.push_back((unsigned char)(row[i] - left));
        }
    }

    // fluxo zlib = cabecalho (0x78 0x01) + deflate + checksum Adler-32 dos dados crus
    std::vector<unsigned char> z;
    z.push_back(0x78); z.push_back(0x01);
    deflateFixed(raw, z);
    unsigned int a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); ++i) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    putU32(z, (b << 16) | a);

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    std::vector<unsigned char> ihdr;
    putU32(ihdr, (unsigned int)w);
    putU32(ihdr, (unsigned int)h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    writeChunk(f, "IHDR", ihdr);
    writeChunk(f, "IDAT", z);
    writeChunk(f, "IEND", std::vector<unsigned char>());
    fclose(f);
    return true;
}

// Le o quadro que acabou de ser desenhado (buffer de tras, antes do
// glutSwapBuffers) e salva em PNG.
bool captureFrame(const char* path) {
    std::vector<unsigned char> pix((size_t)g_windowW * g_windowH * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, g_windowW, g_windowH, GL_RGB, GL_UNSIGNED_BYTE, &pix[0]);
    return savePng(path, g_windowW, g_windowH, &pix[0]);
}

// --- Modo foto -------------------------------------------------
// "./frederico --fotos" monta cada cena de uma lista (applyShot), espera
// alguns quadros estabilizarem, salva o PNG em docs/imagens/ e passa pra
// proxima. Cada foto usa um "relogio" fixo (g_photoTime) pra que as
// lampadas e o ventilador saiam sempre na mesma pose, e uma luz ambiente
// extra opcional pra enxergar o cenario (no jogo normal e' quase breu).
const int NUM_SHOTS = 14;
const char* SHOT_NAMES[NUM_SHOTS] = {
    "01_pov_jogador", "02_corredor", "03_corredor_piscando", "04_monstro_corpo",
    "05_monstro_rosto", "06_sala_geral", "07_mesa_monitor", "08_porta",
    "09_susto", "10_game_over", "11_menu", "12_vitoria", "13_lanterna_fraca", "14_perigo"
};

void setCam(const Vector3& e, const Vector3& c) {
    g_camOverride = true;
    g_ovEye = e;
    g_ovCenter = c;
}

void applyShot(int i) {
    resetGame();
    g_camOverride = false;
    g_photoAmbient = -1.0f;
    g_photoMouth = -1.0f;
    g_photoHud = false;
    g_photoTime = 2.0f;
    g_danger = 0.0f;
    g_heartPhase = 0.0f;
    g_breathPhase = 3.2f;
    g_walkTime = 1.1f;
    g_yawDeg = 0.0f;
    g_pitchDeg = 0.0f;

    BezierPath path = Enemy::getPath();
    const float F    = Scene::ROOM_FRONT_Z;
    const float HW   = Scene::ROOM_HALF_WIDTH;
    const float zMid = (Scene::ROOM_FRONT_Z + Scene::ROOM_BACK_Z) * 0.5f;
    const float deskZ = g_eye.z - 1.2f;

    switch (i) {
    case 0: { // visao do jogador, com HUD, lanterna apontada pro monstro
        g_flashlightOn = true; g_monitorOn = true; g_power = 78.0f; g_nightTime = 95.0f;
        g_monsterT = 0.55f; g_photoHud = true; g_danger = 0.2f;
        break; }
    case 1: { // corredor visto da porta, lanterna ligada
        setCam(Vector3(0.0f, 1.5f, F + 0.3f), Vector3(0.0f, 1.4f, Scene::CORRIDOR_FAR_Z));
        g_flashlightOn = true; g_monsterT = 0.45f; g_photoAmbient = 0.12f;
        break; }
    case 2: { // corredor no instante em que a lampada pisca forte
        setCam(Vector3(0.0f, 1.5f, F + 0.3f), Vector3(0.0f, 1.4f, Scene::CORRIDOR_FAR_Z));
        g_photoTime = 5.65f; g_monsterT = 0.62f; g_photoAmbient = 0.14f;
        break; }
    case 3: { // monstro de corpo inteiro
        Vector3 mp = evaluateBezier(path, 0.8f);
        setCam(Vector3(mp.x * 0.5f, 1.7f, F + 0.8f), Vector3(mp.x, 1.5f, mp.z));
        g_flashlightOn = true; g_monsterT = 0.8f; g_photoAmbient = 0.22f;
        break; }
    case 4: { // rosto do monstro, boca aberta
        Vector3 mp = evaluateBezier(path, 0.97f);
        setCam(Vector3(mp.x, 2.25f, mp.z + 1.9f), Vector3(mp.x, 2.3f, mp.z + 0.82f));
        g_flashlightOn = true; g_monsterT = 0.97f; g_photoMouth = 0.55f;
        g_photoAmbient = 0.25f; g_walkTime = 1.3f;
        break; }
    case 5: { // panorama da sala
        setCam(Vector3(HW - 0.5f, 2.1f, Scene::ROOM_BACK_Z - 0.4f), Vector3(-0.5f, 0.9f, zMid - 0.5f));
        g_monitorOn = true; g_monsterT = 0.8f; g_photoAmbient = 0.35f;
        break; }
    case 6: { // mesa com o monitor ligado
        setCam(Vector3(0.15f, 1.45f, deskZ + 1.0f), Vector3(-0.6f, 0.95f, deskZ));
        g_monitorOn = true; g_monsterT = 0.8f; g_photoAmbient = 0.12f;
        break; }
    case 7: { // porta de aco fechada
        g_doorClosing = true; g_doorOffsetY = Scene::DOORWAY_HEIGHT;
        setCam(Vector3(0.5f, 1.4f, F + 1.5f), Vector3(0.0f, 1.1f, F));
        g_flashlightOn = true; g_photoAmbient = 0.25f;
        break; }
    case 8: { // o susto
        g_state = STATE_JUMPSCARE; g_jumpscareProgress = 0.92f;
        g_flashlightOn = true; g_monsterT = 1.0f; g_danger = 1.0f;
        break; }
    case 9: { // game over
        g_state = STATE_GAME_OVER; g_jumpscareProgress = 1.0f; g_stateTimer = 3.2f;
        g_nightTime = 187.0f; g_photoHud = true;
        break; }
    case 10: { // menu inicial
        g_state = STATE_MENU; g_stateTimer = 1.0f; g_photoHud = true;
        break; }
    case 11: { // vitoria
        g_state = STATE_WON; g_stateTimer = 3.0f; g_nightTime = NIGHT_DURATION; g_photoHud = true;
        break; }
    case 12: { // lanterna com bateria quase no fim: fraca, amarelada e estreita
        g_flashlightOn = true; g_power = 6.0f; g_nightTime = 240.0f;
        g_monsterT = 0.45f; g_photoHud = true; g_danger = 0.3f;
        break; }
    default: { // perigo alto: monstro perto, vinheta vermelha, nevoa mais densa
        g_flashlightOn = true; g_power = 55.0f; g_nightTime = 200.0f;
        g_monsterT = 0.88f; g_photoHud = true; g_danger = 0.9f; g_heartPhase = 1.5707963f;
        break; }
    }
}

void startPhotoSession(bool exitAtEnd) {
    MAKE_DIR("docs");
    MAKE_DIR("docs/imagens");
    g_photoActive = true;
    g_photoExitAtEnd = exitAtEnd;
    g_photoIndex = 0;
    g_photoFrames = 0;
    applyShot(0);
}

// Chamada no fim do display(), depois de desenhar e antes do swap.
void photoAfterRender() {
    if (++g_photoFrames < 4) return;           // deixa a cena estabilizar
    char path[128];
    std::snprintf(path, sizeof(path), "docs/imagens/%s.png", SHOT_NAMES[g_photoIndex]);
    if (captureFrame(path)) std::printf("foto salva: %s\n", path);
    else                    std::printf("ERRO ao salvar: %s\n", path);

    g_photoFrames = 0;
    if (++g_photoIndex >= NUM_SHOTS) {
        g_photoActive = false;
        g_camOverride = false;
        g_photoAmbient = -1.0f;
        g_photoMouth = -1.0f;
        if (g_photoExitAtEnd) exit(0);
        resetGame();
        g_state = STATE_MENU;
        g_stateTimer = 0.0f;
    } else {
        applyShot(g_photoIndex);
    }
}

// ===============================================================
// 6. HUD 2D
//
// O HUD (barra de energia, cronometro, menus, telas de fim) e' desenhado
// DEPOIS da cena 3D, por cima. Como o OpenGL classico so' sabe desenhar
// com uma projecao por vez, usamos a tecnica padrao:
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

// Titulo "defeituoso": o efeito de tubo de TV com problema, usado no menu
// e no game over. Feito com numeros aleatorios: desloca o texto alguns
// pixels a cada frame; de vez em quando (a cada 1.7 s) faz uma "rajada"
// de tremor forte com um "fantasma" branco; e as vezes apaga o brilho
// por um instante. "tt" e' o tempo que alimenta a pulsacao.
void drawGlitchText(const char* text, float cx, float cy, float h, float tt,
                    float r, float g, float b) {
    bool  burst = fmodf(tt, 1.7f) < 0.12f;       // 0.12 s de rajada a cada 1.7 s
    float shake = burst ? 10.0f : 1.5f;          // amplitude do tremor em pixels
    float ox = randRange(-shake, shake);
    float oy = randRange(-shake, shake);
    float alpha = 0.8f + 0.2f * sinf(tt * 25.0f);
    if (rand() % 14 == 0) alpha *= 0.15f;        // ~1 frame em 14 quase apaga

    glColor4f(r * 0.65f, g * 0.65f, b * 0.65f, 0.30f * alpha);   // brilho (linha grossa translucida)
    drawStrokeCentered(GLUT_STROKE_ROMAN, text, cx + ox, cy + oy, h, 9.0f);
    glColor4f(r, g, b, alpha);                                   // o titulo em si
    drawStrokeCentered(GLUT_STROKE_ROMAN, text, cx + ox, cy + oy, h, 4.0f);
    if (burst) {                                                 // "fantasma" branco deslocado
        glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
        drawStrokeCentered(GLUT_STROKE_ROMAN, text, cx - ox, cy + oy * 3.0f, h, 2.0f);
    }
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
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);

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

// Legenda das teclas no rodape, no centro. Cada item acende (dourado)
// quando o recurso esta' ligado e fica cinza quando desligado.
void drawControlHints() {
    struct Hint { const char* text; bool on; };
    const Hint hints[4] = {
        { "F LANTERNA", g_flashlightOn },
        { "D PORTA",    g_doorClosing },
        { "C MONITOR",  g_monitorOn },
        { "P PAUSA",    false }
    };
    void* font = GLUT_BITMAP_HELVETICA_12;
    const float GAP = 26.0f;
    float total = GAP * 3.0f;
    for (int i = 0; i < 4; ++i) total += (float)glutBitmapLength(font, (const unsigned char*)hints[i].text);
    float x = g_windowW * 0.5f - total * 0.5f;
    for (int i = 0; i < 4; ++i) {
        if (hints[i].on) glColor4f(1.0f, 0.85f, 0.4f, 1.0f);
        else             glColor4f(0.55f, 0.55f, 0.55f, 0.9f);
        drawBitmapText(font, x, 16.0f, hints[i].text);
        x += (float)glutBitmapLength(font, (const unsigned char*)hints[i].text) + GAP;
    }
}

// Painel de depuracao (F3): mostra os numeros internos do jogo. Ajuda a
// balancear (ver a velocidade do monstro, o sprint) e a conferir a mira.
void drawDebugInfo() {
    char line[160];
    std::snprintf(line, sizeof(line),
                  "t=%.3f  mira=%s  sprint=%s  humor=%.2fx  noite=%.0fs  energia=%.0f%%  yaw=%.0f pitch=%.0f",
                  g_monsterT, g_beamHit ? "SIM" : "nao", g_sprinting ? "SIM" : "nao",
                  currentSpeedMultiplier(), g_nightTime, g_power, g_yawDeg, g_pitchDeg);
    glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
    fillRect(8.0f, g_windowH - 30.0f, 8.0f + 760.0f, g_windowH - 8.0f);
    glColor4f(0.5f, 1.0f, 0.6f, 1.0f);
    drawBitmapText(GLUT_BITMAP_HELVETICA_12, 14.0f, g_windowH - 24.0f, line);
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

// --- Menus (inicial e de pausa) --------------------------------
// Os dois usam a mesma tela: no MENU as opcoes sao Jogar/Sair; na
// PAUSA sao Continuar/Reiniciar/Sair. As posicoes dos itens saem de
// menuItemY(), usada tanto pra DESENHAR quanto pra detectar o mouse
// em cima (menuItemAt), entao os dois sempre concordam.
int menuCount() { return g_state == STATE_MENU ? 2 : 3; }

const char* menuLabel(int i) {
    if (g_state == STATE_MENU) return i == 0 ? "JOGAR" : "SAIR";
    return i == 0 ? "CONTINUAR" : (i == 1 ? "REINICIAR" : "SAIR");
}

float menuItemY(int i) { return g_windowH * 0.50f - 62.0f * (float)i; } // y do centro (de baixo pra cima)

// Qual item esta' sob o mouse (x,y em pixels da janela, y de CIMA pra baixo)? -1 = nenhum.
int menuItemAt(int mx, int my) {
    float gy = (float)g_windowH - (float)my;   // converte pro y de baixo pra cima
    for (int i = 0; i < menuCount(); ++i) {
        if (fabsf((float)mx - g_windowW * 0.5f) < 190.0f && fabsf(gy - menuItemY(i)) < 26.0f) return i;
    }
    return -1;
}

void activateMenu(int i) {
    audioEvent(EVT_MENU_SELECT);
    if (g_state == STATE_MENU) {
        if (i == 0) { resetGame(); g_yawDeg = 0.0f; g_pitchDeg = 0.0f; }
        else exit(0);
    } else { // PAUSED
        if (i == 0)      g_state = STATE_PLAYING;
        else if (i == 1) resetGame();
        else             exit(0);
    }
}

void drawMenuScreen(bool title) {
    const float t = g_stateTimer;
    const float W = (float)g_windowW, H = (float)g_windowH;
    float fade = clamp01(t / 0.4f);

    glColor4f(0.0f, 0.0f, 0.0f, (title ? 0.55f : 0.70f) * fade);
    fillRect(0.0f, 0.0f, W, H);
    const float vc[4] = { 0.1f, 0.0f, 0.0f, 0.0f };
    const float ve[4] = { 0.1f, 0.0f, 0.0f, 0.80f * fade };
    drawRadial(vc, ve);

    if (title) {
        const char* name = "1 NOITE NO FREDERICO";
        float h = fitStrokeHeight(GLUT_STROKE_ROMAN, name, H * 0.11f, W * 0.88f);
        drawGlitchText(name, W * 0.5f, H * 0.76f, h, t, 0.90f, 0.08f, 0.06f);
        glColor4f(0.75f, 0.72f, 0.72f, fade);
        drawBitmapCentered(GLUT_BITMAP_HELVETICA_18, W * 0.5f, H * 0.66f, "Sobreviva ate as 6 da manha.");
    } else {
        const char* name = "PAUSADO";
        float h = fitStrokeHeight(GLUT_STROKE_ROMAN, name, H * 0.11f, W * 0.6f);
        glColor4f(0.8f, 0.8f, 0.8f, 0.25f * fade);
        drawStrokeCentered(GLUT_STROKE_ROMAN, name, W * 0.5f, H * 0.76f, h, 9.0f);
        glColor4f(0.92f, 0.92f, 0.92f, fade);
        drawStrokeCentered(GLUT_STROKE_ROMAN, name, W * 0.5f, H * 0.76f, h, 3.5f);
    }

    // itens: o selecionado ganha caixa vermelha e setas "> <"
    for (int i = 0; i < menuCount(); ++i) {
        float y = menuItemY(i);
        bool sel = (i == g_menuSel);
        char label[48];
        if (sel) {
            glColor4f(0.45f, 0.02f, 0.02f, 0.35f * fade);
            fillRect(W * 0.5f - 190.0f, y - 26.0f, W * 0.5f + 190.0f, y + 26.0f);
            glLineWidth(1.5f);
            glColor4f(0.9f, 0.15f, 0.1f, 0.8f * fade);
            outlineRect(W * 0.5f - 190.0f, y - 26.0f, W * 0.5f + 190.0f, y + 26.0f);
            std::snprintf(label, sizeof(label), "> %s <", menuLabel(i));
            glColor4f(1.0f, 0.9f, 0.85f, fade);
        } else {
            std::snprintf(label, sizeof(label), "%s", menuLabel(i));
            glColor4f(0.70f, 0.70f, 0.70f, 0.55f * fade);
        }
        drawStrokeCentered(GLUT_STROKE_ROMAN, label, W * 0.5f, y, 30.0f, sel ? 2.4f : 1.6f);
    }

    glColor4f(0.65f, 0.65f, 0.65f, fade);
    drawBitmapCentered(GLUT_BITMAP_HELVETICA_12, W * 0.5f, H * 0.14f,
                       "Mouse: olhar     F: lanterna (mire no monstro)     D: porta     C: monitor     P: pausa");
    glColor4f(0.5f, 0.5f, 0.5f, fade);
    drawBitmapCentered(GLUT_BITMAP_HELVETICA_12, W * 0.5f, H * 0.10f,
                       "Setas ou W/S: escolher     Enter ou clique: confirmar     Esc: voltar");
}

// Tela de game over. Tudo e' funcao do tempo "t" desde que o estado
// comecou (g_stateTimer), o que da' uma pequena sequencia animada:
//   0.00 a 0.25 s : flash vermelho que some
//   0.00 a 0.70 s : a tela escurece (fade) e a vinheta vermelha aparece
//   a partir de 0.5 s : o titulo aparece tremendo e piscando
//   a partir de 1.2 s : subtitulo e tempo sobrevivido surgem (fade-in)
//   a partir de GAMEOVER_LOCK : aparece o "R - tentar de novo" piscando
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
        const char* title = "GAME OVER";
        float titleH = fitStrokeHeight(GLUT_STROKE_ROMAN, title, H * 0.20f, W * 0.85f);
        drawGlitchText(title, W * 0.5f, H * 0.58f, titleH, t - 0.5f, 0.95f, 0.05f, 0.05f);

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

// --- Atmosfera ---------------------------------------------------
// Efeitos de tela cheia que reagem ao perigo (g_danger). Sao desenhados
// em 2D por cima da cena, antes do HUD (pra nao escurecer os textos).

// VINHETA: escurece as bordas e os cantos da tela, como a visao de
// tunel do medo. E' uma faixa elipsoidal entre dois aneis: o anel de
// dentro (45% do raio) e' transparente e o de fora e' escuro; o OpenGL
// interpola o alpha entre eles. "red" tinge as bordas de vermelho.
void drawVignette(float alpha, float red) {
    const int SEG = 48;
    float cx = g_windowW * 0.5f, cy = g_windowH * 0.5f;
    float rx = g_windowW * 0.78f, ry = g_windowH * 0.78f; // maior que a tela: cantos ficam bem escuros
    const float inner = 0.45f;
    glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= SEG; ++i) {
            float a = 6.2831853f * (float)i / (float)SEG;
            float c = cosf(a), sn = sinf(a);
            glColor4f(red, 0.0f, 0.0f, 0.0f);
            glVertex2f(cx + c * rx * inner, cy + sn * ry * inner);
            glColor4f(red, 0.0f, 0.0f, alpha);
            glVertex2f(cx + c * rx, cy + sn * ry);
        }
    glEnd();
}

// GRANULADO DE FILME: um quadrado de ruido (textura 256x256 de pontos
// aleatorios) cobrindo a tela, SOMADO a' imagem (blending aditivo) com
// pouca forca. A cada frame o deslocamento da textura e' sorteado, entao
// o ruido "ferve" como numa filmagem velha.
void drawGrain(float alpha) {
    const float W = (float)g_windowW, H = (float)g_windowH;
    const float texel = 2.0f;                          // cada ponto de ruido tem 2x2 pixels
    float ox = randRange(0.0f, 1.0f), oy = randRange(0.0f, 1.0f);
    float sx = W / (256.0f * texel), sy = H / (256.0f * texel);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_grainTex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);                 // soma, sem escurecer a cena
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
        glTexCoord2f(ox,      oy);      glVertex2f(0.0f, 0.0f);
        glTexCoord2f(ox + sx, oy);      glVertex2f(W,    0.0f);
        glTexCoord2f(ox + sx, oy + sy); glVertex2f(W,    H);
        glTexCoord2f(ox,      oy + sy); glVertex2f(0.0f, H);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// Junta os efeitos. O perigo escurece mais as bordas e as tinge de
// vermelho, e a vinheta PULSA como um batimento cardiaco ("lub-dub": duas
// batidas por ciclo) cada vez mais rapido. No modo foto o granulado fica
// desligado (ruido aleatorio nao comprime e deixaria os PNGs enormes).
void drawAtmosphere() {
    hudBegin();
    float d = g_danger;
    float ph = g_heartPhase;                          // fase do batimento (rad), integrada no timer
    float beat = powf(fmaxf(0.0f, sinf(ph)), 8.0f)
               + 0.6f * powf(fmaxf(0.0f, sinf(ph - 0.9f)), 8.0f);
    float edge = (VIGNETTE_BASE + 0.45f * d) * (1.0f + 0.35f * d * beat);
    drawVignette(clamp01(edge), 0.12f * d);
    if (!g_photoActive) drawGrain(GRAIN_BASE + 0.09f * d);
    hudEnd();
}

// Decide o que o HUD mostra conforme o estado do jogo.
void drawHud() {
    hudBegin();
    switch (g_state) {
        case STATE_MENU:
            drawMenuScreen(true);
            break;
        case STATE_PAUSED:
            drawPowerBar();
            drawNightClock();
            drawMenuScreen(false);
            break;
        case STATE_PLAYING:
            drawPowerBar();
            drawNightClock();
            drawControlHints();
            if (g_debug) drawDebugInfo();
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

// --- Balanco da camera -------------------------------------------
// Pequenos deslocamentos e giros da camera, so' visuais, somados de senos
// (periodicos e baratos, sem sorteio: o resultado so' depende do tempo):
//  - RESPIRACAO: sobe/desce ~1 cm e inclina de leve; fica mais rapida e
//    mais ampla conforme o perigo cresce;
//  - TREMOR DE MAO: oscilacao rapida e minuscula que cresce com o
//    quadrado do perigo (o jogador "treme de medo");
//  - SUSTO: tremor forte (posicao, giro e inclinacao lateral = "roll")
//    que cresce durante o salto e some em ~1 s na tela de game over.
struct CamSway { float dx, dy, yaw, pitch, roll; }; // metros e graus

CamSway computeSway() {
    CamSway s = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    const float t = g_uiTime, d = g_danger;
    float amp  = 1.0f + 1.5f * d;
    s.dy    = 0.010f * amp * sinf(g_breathPhase);          // fase integrada no timer
    s.pitch = 0.35f  * amp * sinf(g_breathPhase + 0.6f);
    s.yaw   = 0.25f  * amp * sinf(t * 0.9f + 1.3f);
    s.roll  = 0.30f  * amp * sinf(t * 0.7f);

    float tr = 0.002f + 0.010f * d * d;
    s.dx    += tr * (sinf(t * 17.0f) + 0.6f * sinf(t * 29.0f + 1.0f));
    s.dy    += tr * (sinf(t * 23.0f + 2.0f) + 0.6f * sinf(t * 37.0f));
    s.yaw   += 20.0f * tr * sinf(t * 19.0f + 0.5f);
    s.pitch += 20.0f * tr * sinf(t * 31.0f);

    float shake = 0.0f;
    if (g_state == STATE_JUMPSCARE)      shake = g_jumpscareProgress * g_jumpscareProgress;
    else if (g_state == STATE_GAME_OVER) shake = expf(-4.0f * g_stateTimer);
    if (shake > 0.0f) {
        s.dx    += 0.05f * shake * (sinf(t * 53.0f) + 0.7f * sinf(t * 71.0f + 1.0f));
        s.dy    += 0.05f * shake * (sinf(t * 59.0f + 2.0f) + 0.7f * sinf(t * 83.0f));
        s.roll  += 2.5f  * shake * sinf(t * 61.0f);
        s.pitch += 1.5f  * shake * sinf(t * 47.0f + 1.0f);
        s.yaw   += 1.5f  * shake * sinf(t * 43.0f + 2.0f);
    }
    return s;
}

// ===============================================================
// 7. DISPLAY
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
    // No modo foto a camera pode ser posicionada livremente.
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    Vector3 eye = g_eye;
    Vector3 center;
    CamSway sw = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };   // camera livre (fotos) nao balanca
    if (g_camOverride) {
        eye = g_ovEye;
        center = g_ovCenter;
    } else {
        sw = computeSway();
        eye = Vector3(g_eye.x + sw.dx, g_eye.y + sw.dy, g_eye.z);
        Vector3 forward = computeForward(sw.yaw, sw.pitch);
        center = Vector3(eye.x + forward.x, eye.y + forward.y, eye.z + forward.z);
    }

    // No susto, o olhar do jogador e' "puxado" pra cara do monstro (como
    // se ele nao conseguisse desviar os olhos): o alvo da camera passa
    // gradualmente (lerp) do ponto onde ele olhava pra altura da cabeca do
    // monstro. Sem isso, com a camera baixa (sentado), a cabeca ficaria
    // fora da tela.
    bool lunging = (g_state == STATE_JUMPSCARE || g_state == STATE_GAME_OVER);
    Vector3 monsterPos = currentMonsterPosition();
    if (lunging) {
        // Onde a cabeca esta' de verdade: o tronco tomba pra frente em torno
        // do quadril (inclinacao = 10 + 45*progresso graus, igual ao enemy.cpp),
        // entao a cabeca (1.44 m acima do quadril, que fica a 1.1 m do chao)
        // sobe menos e avanca em direcao a camera.
        float lean = (10.0f + 45.0f * clamp01(g_jumpscareProgress)) * DEG2RAD;
        Vector3 face(monsterPos.x, 1.1f + 1.44f * cosf(lean), monsterPos.z + 1.44f * sinf(lean));
        float k = clamp01(g_jumpscareProgress * 2.5f);
        center = center + (face - center) * k;
    }

    // O vetor "pra cima" inclinado pelo roll faz a imagem girar de leve.
    float rollRad = sw.roll * DEG2RAD;
    gluLookAt(eye.x, eye.y, eye.z,
              center.x, center.y, center.z,
              sinf(rollRad), cosf(rollRad), 0.0);

    // A nevoa fecha um pouco quando o perigo sobe (ate' +50% de densidade).
    glFogf(GL_FOG_DENSITY, FOG_DENSITY * (1.0f + 0.5f * g_danger));

    // Direcao (unitaria) pra onde a camera/lanterna apontam
    Vector3 dir = center - eye;
    float dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dl > 0.0001f) dir = dir * (1.0f / dl);

    // [Requisito D/F] A lanterna e' atualizada logo apos a camera (ver
    // lighting.cpp): a posicao de uma luz e' transformada pela matriz
    // ATUAL, entao precisa ser definida depois do gluLookAt pra "grudar"
    // na camera. Em seguida a transformamos num cone (spot) apontado pra dir.
    updateFlashlight(g_flashlightOn);
    updateFlashLevel();            // lanterna fraca com bateria baixa
    setupFlashlight();             // o main.cpp define a luz da lanterna por inteiro

    // Modo foto: luz ambiente extra pra enxergar o cenario
    GLfloat prevAmbient[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    bool ambientChanged = (g_photoActive && g_photoAmbient >= 0.0f);
    if (ambientChanged) {
        glGetFloatv(GL_LIGHT_MODEL_AMBIENT, prevAmbient);
        const GLfloat a = g_photoAmbient;
        const GLfloat amb[4] = { a, a, a, 1.0f };
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    }

    // Moveis da sala e do corredor. Vem ANTES das paredes porque tambem
    // configuram as luzes extras (monitor e lampadas do corredor), que
    // precisam estar ativas quando as paredes forem desenhadas. g_uiTime
    // anima o ventilador e a tela; o monitor mostra a posicao do monstro.
    drawRoomProps(g_uiTime, g_monitorOn, monsterPos);

    drawSecurityRoom();
    drawCorridor();
    drawDoor(g_doorOffsetY);

    // O monstro: posicao vem da curva de Bezier (ou do salto do susto),
    // g_walkTime anima as pernas/bracos e mouthOpen abre a boca no susto.
    // Quando o feixe da lanterna o acerta, ele "treme" (recuo).
    Vector3 drawPos = monsterPos;
    if (g_beamHit && g_state == STATE_PLAYING) {
        drawPos.x += randRange(-0.03f, 0.03f);
        drawPos.z += randRange(-0.03f, 0.03f);
    }
    float monsterYaw = 180.0f; // o monstro sempre encara quem esta' olhando pra ele
    float mouthOpen  = lunging ? g_jumpscareProgress : 0.05f;
    if (g_photoActive && g_photoMouth >= 0.0f) mouthOpen = g_photoMouth;
    Enemy::draw(drawPos, monsterYaw, g_walkTime, mouthOpen);

    // Interruptor da porta (LED: verde = aberta, vermelho = fechada).
    // Vem depois das paredes porque o brilho do LED usa transparencia.
    drawDoorSwitch(g_doorClosing, g_power > 0.0f);

    // Manchas de sangue (decalques transparentes): depois de piso e paredes.
    drawDecals();

    // Feixe visivel da lanterna: por ultimo na cena 3D (e' translucido).
    if (g_flashlightOn && (g_state == STATE_PLAYING || g_state == STATE_PAUSED || g_photoActive)) {
        drawFlashlightBeam(eye, dir, g_beamHit);
    }

    if (ambientChanged) glLightModelfv(GL_LIGHT_MODEL_AMBIENT, prevAmbient);

    // Vinheta e granulado: durante o jogo e no susto (menus e telas de fim tem o proprio overlay)
    if (g_state == STATE_PLAYING || g_state == STATE_JUMPSCARE) drawAtmosphere();

    if (!g_photoActive || g_photoHud) drawHud();

    // Captura de tela: F12 (manual) ou o modo foto (automatico)
    if (g_screenshotRequested) {
        g_screenshotRequested = false;
        MAKE_DIR("capturas");
        char path[64];
        std::snprintf(path, sizeof(path), "capturas/captura_%03d.png", ++g_screenshotCount);
        if (captureFrame(path)) std::printf("captura salva: %s\n", path);
    }
    if (g_photoActive) photoAfterRender();

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
// 8. ENTRADA (TECLADO, SETAS E MOUSE)
// ===============================================================

void pauseGame() {
    g_state = STATE_PAUSED;
    g_stateTimer = 0.0f;
    g_menuSel = 0;
    audioEvent(EVT_MENU_MOVE);
}

// Muda o item selecionado do menu (com tick sonoro so' se mudou de fato).
void menuSelect(int i) {
    if (i != g_menuSel) {
        g_menuSel = i;
        audioEvent(EVT_MENU_MOVE);
    }
}

// [Requisito D] Teclado. Depende do estado atual do jogo:
//   MENU / PAUSED : W/S escolhem, Enter/Espaco confirmam, Esc volta
//   PLAYING       : F lanterna, D porta, C monitor, P ou Esc pausam
//   GAME_OVER/WON : R recomeca, Esc sai
void keyboard(unsigned char key, int, int) {
    if (g_photoActive) return;

    if (g_state == STATE_MENU || g_state == STATE_PAUSED) {
        int n = menuCount();
        if (key == 27) {                                   // ESC
            if (g_state == STATE_PAUSED) g_state = STATE_PLAYING; else exit(0);
        } else if (key == 'p' || key == 'P') {
            if (g_state == STATE_PAUSED) g_state = STATE_PLAYING;
        } else if (key == 'w' || key == 'W') {
            menuSelect((g_menuSel + n - 1) % n);
        } else if (key == 's' || key == 'S') {
            menuSelect((g_menuSel + 1) % n);
        } else if (key == 13 || key == ' ') {              // Enter / Espaco
            activateMenu(g_menuSel);
        }
        return;
    }

    // Nas telas de fim, so' o R (ou Enter) importa, e so' depois do
    // tempo de "trava", pra ninguem pular a tela sem querer.
    if (g_state == STATE_GAME_OVER || g_state == STATE_WON) {
        if (key == 27) exit(0);
        float lock = (g_state == STATE_GAME_OVER) ? GAMEOVER_LOCK : WIN_LOCK;
        if ((key == 'r' || key == 'R' || key == 13) && g_stateTimer >= lock) {
            resetGame();
        }
        return;
    }
    if (g_state != STATE_PLAYING) return; // durante o jumpscare nao tem o que fazer

    switch (key) {
        case 27:  // ESC
        case 'p':
        case 'P':
            pauseGame();
            break;
        case 'f':
        case 'F':
            // So' liga se ainda tiver energia; desligar sempre pode.
            if (g_flashlightOn)      { g_flashlightOn = false; audioEvent(EVT_FLASH_OFF); }
            else if (g_power > 0.0f) { g_flashlightOn = true;  audioEvent(EVT_FLASH_ON); }
            break;
        case 'd':
        case 'D':
            if (g_doorClosing)       g_doorClosing = false;
            else if (g_power > 0.0f) g_doorClosing = true;
            break;
        case 'c':
        case 'C':
            if (g_monitorOn)         { g_monitorOn = false; audioEvent(EVT_MONITOR_OFF); }
            else if (g_power > 0.0f) { g_monitorOn = true;  audioEvent(EVT_MONITOR_ON); }
            break;
        default:
            break;
    }
}

// Teclas especiais: setas navegam no menu; F12 tira uma captura de tela;
// F3 liga/desliga o painel de depuracao.
void special(int key, int, int) {
    if (key == GLUT_KEY_F12) {
        g_screenshotRequested = true;
        return;
    }
    if (key == GLUT_KEY_F3) {
        g_debug = !g_debug;
        return;
    }
    if (g_photoActive) return;
    if (g_state == STATE_MENU || g_state == STATE_PAUSED) {
        int n = menuCount();
        if (key == GLUT_KEY_UP)        menuSelect((g_menuSel + n - 1) % n);
        else if (key == GLUT_KEY_DOWN) menuSelect((g_menuSel + 1) % n);
    }
}

// Clique do mouse: so' interessa nos menus (botao esquerdo em cima de um item).
void mouse(int button, int st, int x, int y) {
    if (g_photoActive) return;
    if (button != GLUT_LEFT_BUTTON || st != GLUT_DOWN) return;
    if (g_state == STATE_MENU || g_state == STATE_PAUSED) {
        int i = menuItemAt(x, y);
        if (i >= 0) { g_menuSel = i; activateMenu(i); }
    }
}

// [Requisito D] Mouse: olhar ao redor. Usa a tecnica classica de FPS:
// o cursor e' sempre recentralizado (glutWarpPointer) e o quanto ele
// se afastou do centro (dx, dy) vira rotacao de camera (yaw/pitch).
// Os limites (clamp) simulam o limite de movimento do pescoco humano.
// Nos menus o cursor fica livre e o mouse so' seleciona itens.
void passiveMotion(int x, int y) {
    // Quando NOS movemos o cursor de volta ao centro, o GLUT gera um
    // evento de movimento falso. A flag serve pra ignora-lo.
    if (g_warping) {
        g_warping = false;
        return;
    }
    if (g_photoActive) return;

    if (g_state == STATE_MENU || g_state == STATE_PAUSED) {
        int i = menuItemAt(x, y);
        if (i >= 0) menuSelect(i);
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

// --- Ponte jogo -> audio -----------------------------------------
// true se a fase "atravessou" um pico de seno (pi/2 + 2*pi*k) entre o
// frame anterior (prev) e este (cur). "offset" desloca o pico (0 = 1a
// batida do coracao, 0.9 = 2a). E' o mesmo seno da vinheta, entao o som
// bate junto com o pulso visual.
bool crossedPeak(float prev, float cur, float offset) {
    const float PEAK = 1.5707963f, TAU = 6.2831853f;
    int a = (int)floorf((prev - offset - PEAK) / TAU);
    int b = (int)floorf((cur  - offset - PEAK) / TAU);
    return b > a;
}

// Descreve ao audio o que esta' acontecendo. Chamada todo frame.
void updateAudio() {
    AudioParams a;
    bool menuLike = (g_state == STATE_MENU || g_state == STATE_PAUSED);
    bool playing  = (g_state == STATE_PLAYING);
    a.master = menuLike ? 0.35f : (g_state == STATE_GAME_OVER ? 0.55f : 1.0f);
    a.danger = g_danger;

    // Posicao do monstro em relacao ao jogador: distancia e direcao. O pan
    // usa o angulo do monstro MENOS pra onde o jogador olha: virar a cabeca
    // move o som no estereo.
    Vector3 mp = currentMonsterPosition();
    float dx = mp.x - g_eye.x, dz = mp.z - g_eye.z, dy = MONSTER_CENTER_Y - g_eye.y;
    a.monsterDist = sqrtf(dx * dx + dz * dz + dy * dy);
    float ang = atan2f(dx, -dz);                     // 0 = direto a frente; positivo = direita
    a.monsterPan = sinf(ang - g_yawDeg * DEG2RAD);

    a.stepHz       = (playing && g_monsterMoved) ? (g_sprinting ? 2.25f : 1.02f) : 0.0f;
    a.doorMoving   = (playing && g_doorMoving) ? 1.0f : 0.0f;
    a.flashLevel   = (playing && g_flashlightOn) ? g_flashLevel : 0.0f;
    a.beamHit      = (playing && g_beamHit) ? 1.0f : 0.0f;
    a.monitorOn    = (playing && g_monitorOn) ? 1.0f : 0.0f;
    float f = clamp01((mp.z - Scene::CORRIDOR_FAR_Z) / (Scene::ROOM_FRONT_Z - Scene::CORRIDOR_FAR_Z));
    a.monitorClose = clamp01((f - 0.6f) * 2.5f);
    a.powerDead    = (playing && g_power <= 0.0f) ? 1.0f : 0.0f;
    audioSetParams(a);
}

// ===============================================================
// 9. TIMER (LOGICA DO JOGO)
// [Requisito C] Roda a cada ~16 ms (glutTimerFunc), independente de
// input ou de desenho. Faz TODA a atualizacao do jogo, na ordem:
//   1. calcula dt (tempo real desde o frame anterior)
//   2. mostra/esconde o cursor conforme o estado
//   3. anima a porta
//   4. conforme o estado:
//      PLAYING   -> cronometro, bateria, sprint, mira, monstro, checa fim
//      JUMPSCARE -> anima o salto do monstro
//      MENU / PAUSED / GAME_OVER / WON -> so' avanca o relogio da tela
//   5. pede redesenho e se reagenda
// ===============================================================
void timerFunc(int) {
    // --- 1. delta time ---
    int now = glutGet(GLUT_ELAPSED_TIME);   // milissegundos desde o inicio do programa
    float dt = (now - g_lastTimeMs) / 1000.0f;
    g_lastTimeMs = now;
    if (dt > 0.1f) dt = 0.1f; // evita saltos grandes (ex: janela minimizada/arrastada)
    if (dt < 0.0f) dt = 0.0f;

    // Modo foto: o "relogio" fica fixo e nada da logica roda
    if (g_photoActive) {
        g_uiTime = g_photoTime;
        glutPostRedisplay();
        glutTimerFunc(16, timerFunc, 0);
        return;
    }

    g_uiTime += dt;

    // --- 2. Cursor: aparece so' nos menus; ao voltar pro jogo some e
    // recentraliza (senao a camera daria um "pulo") ---
    bool wantCursor = (g_state == STATE_MENU || g_state == STATE_PAUSED);
    if (wantCursor != g_cursorShown) {
        g_cursorShown = wantCursor;
        glutSetCursor(wantCursor ? GLUT_CURSOR_LEFT_ARROW : GLUT_CURSOR_NONE);
        if (!wantCursor) {
            g_warping = true;
            glutWarpPointer(g_windowW / 2, g_windowH / 2);
        }
    }

    // --- 3. Porta: anima suavemente ate' a posicao alvo ---
    // Em vez de teleportar, move DOOR_SPEED * dt por frame na direcao do
    // alvo, sem passar dele (fminf/fmaxf).
    float doorTarget = g_doorClosing ? Scene::DOORWAY_HEIGHT : 0.0f;
    bool doorWasMoving = g_doorMoving;
    if (g_state != STATE_PAUSED) {
        if (g_doorOffsetY < doorTarget)
            g_doorOffsetY = fminf(g_doorOffsetY + DOOR_SPEED * dt, doorTarget);
        else if (g_doorOffsetY > doorTarget)
            g_doorOffsetY = fmaxf(g_doorOffsetY - DOOR_SPEED * dt, doorTarget);
    }
    // Som da porta: rele ao comecar a mexer; pancada forte ao fechar por
    // completo, baque leve ao terminar de abrir.
    g_doorMoving = (g_state != STATE_PAUSED) && fabsf(g_doorOffsetY - doorTarget) > 0.001f;
    if (g_state == STATE_PLAYING) {
        if (!doorWasMoving && g_doorMoving) audioEvent(EVT_RELAY);
        if (doorWasMoving && !g_doorMoving)
            audioEvent(g_doorOffsetY > 0.5f * Scene::DOORWAY_HEIGHT ? EVT_DOOR_SLAM : EVT_DOOR_CLUNK);
    }
    // A porta so' "veda" quando chegou ao chao (o 0.01 e' folga numerica).
    bool doorSealed = g_doorOffsetY >= (Scene::DOORWAY_HEIGHT - 0.01f);

    if (g_state != STATE_PLAYING) g_monsterMoved = false;

    // --- 4. Logica por estado ---
    if (g_state == STATE_PLAYING) {
        g_nightTime += dt;
        g_walkTime  += dt * (g_sprinting ? SPRINT_ANIM_BOOST : 1.0f);

        // sino distante a cada "hora" da noite (a noite tem 6)
        int hour = (int)(g_nightTime / (NIGHT_DURATION / 6.0f));
        if (hour > g_lastHour) { g_lastHour = hour; if (hour < 6) audioEvent(EVT_CHIME); }

        // Bateria: lanterna, porta fechada e monitor consomem energia. Ao
        // zerar, tudo para de funcionar e nao volta mais.
        float drain = 0.0f;
        if (g_flashlightOn) drain += POWER_DRAIN_LIGHT;
        if (g_doorClosing)  drain += POWER_DRAIN_DOOR;
        if (g_monitorOn)    drain += POWER_DRAIN_MONITOR;
        float powerBefore = g_power;
        g_power -= drain * dt;
        if (g_power <= 0.0f) {
            if (powerBefore > 0.0f) audioEvent(EVT_POWER_OUT);
            g_power        = 0.0f;
            g_flashlightOn = false;
            g_monitorOn    = false;
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
                g_sprintTimeLeft = randRange(SPRINT_DURATION_MIN, SPRINT_DURATION_MAX);
            }
        }

        // MIRA: a lanterna so' afasta o monstro se o feixe estiver
        // apontado pra ele. Usamos a mesma direcao da camera que o
        // display usa pro cone de luz.
        Vector3 mp = currentMonsterPosition();
        Vector3 monsterCenter(mp.x, mp.y + MONSTER_CENTER_Y, mp.z);
        updateFlashLevel();
        g_beamHit = g_flashlightOn && g_flashLevel > 0.2f &&   // lanterna falhando nao conta
                    flashlightHits(g_eye, computeForward(), monsterCenter, MONSTER_HIT_RADIUS);

        // Movimento do monstro. "rate" e' a variacao do parametro t da
        // curva por segundo: positivo = se aproxima, negativo = recua.
        //   porta vedada            -> parado (contido do lado de fora)
        //   lanterna acesa E mirada -> recua (RETREAT_RATE)
        //   lanterna apagada ou fora de mira -> avanca (caminhada normal)
        //   sprint                  -> avanca rapido; o feixe so' desconta
        //                              RETREAT_RATE dessa velocidade
        float rate;
        if (doorSealed) {
            rate = 0.0f;
        } else {
            float advance = g_sprinting ? SPRINT_RATE
                                        : ADVANCE_RATE_BASE * currentSpeedMultiplier();
            float retreat = RETREAT_RATE * g_flashLevel;   // luz fraca afasta menos
            if (g_beamHit) rate = g_sprinting ? (advance - retreat) : -retreat;
            else           rate = advance;
        }
        float prevMonsterT = g_monsterT;
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
        g_monsterMoved = fabsf(g_monsterT - prevMonsterT) > 0.00001f;

        // Condicoes de fim. Perder tem prioridade sobre ganhar: se o
        // monstro chegou no mesmo frame em que amanheceu, voce perde.
        if (g_monsterT >= 1.0f && !doorSealed) {
            g_state             = STATE_JUMPSCARE;
            g_jumpscareProgress = 0.0f;
            g_beamHit           = false;
            audioEvent(EVT_JUMPSCARE);
        } else if (g_nightTime >= NIGHT_DURATION) {
            g_nightTime  = NIGHT_DURATION;
            g_state      = STATE_WON;
            g_stateTimer = 0.0f;
            audioEvent(EVT_WIN);
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
        g_stateTimer += dt; // MENU / PAUSED / GAME_OVER / WON: so' avanca o relogio da animacao da tela
    }

    // --- PERIGO: alvo calculado do estado do jogo; g_danger acompanha o alvo
    // com suavizacao (sobe rapido, desce devagar) pra os efeitos nao "pularem".
    float dangerTarget = 0.0f;
    if (g_state == STATE_PLAYING) {
        dangerTarget = smooth01(0.30f, 0.95f, g_monsterT)    // monstro se aproximando
                     + (g_sprinting ? 0.30f : 0.0f)          // sprint
                     + 0.20f * lowPower();                   // bateria no fim
    } else if (g_state == STATE_JUMPSCARE || g_state == STATE_GAME_OVER) {
        dangerTarget = 1.0f;
    } else if (g_state == STATE_MENU || g_state == STATE_PAUSED) {
        dangerTarget = 0.1f;
    }
    dangerTarget = clamp01(dangerTarget);
    g_danger += (dangerTarget - g_danger) * clamp01(dt * (dangerTarget > g_danger ? 4.0f : 1.2f));

    // --- FASES do batimento e da respiracao (integradas dt a dt) e batidas
    // do coracao: cada vez que a fase passa por um pico, toca uma batida.
    float prevHeart = g_heartPhase;
    g_heartPhase  += dt * (4.5f + 5.0f * g_danger);
    g_breathPhase += dt * 1.6f * (1.0f + 1.2f * g_danger);
    if (g_state == STATE_PLAYING || g_state == STATE_JUMPSCARE) {
        if (crossedPeak(prevHeart, g_heartPhase, 0.0f)) audioEvent(EVT_HEART_LUB);
        if (crossedPeak(prevHeart, g_heartPhase, 0.9f)) audioEvent(EVT_HEART_DUB);
    }
    updateAudio();

    // --- 5. Redesenha e se reagenda (16 ms ~ 60 quadros por segundo) ---
    glutPostRedisplay();
    glutTimerFunc(16, timerFunc, 0);
}

// Textura de ruido do granulado: 256x256 pontos cinza aleatorios. O gray
// e' elevado a 4 (r*r*r*r) pra a maioria ficar escura e so' alguns pontos
// brilharem: o granulado clareia pouco a cena em media. Filtro NEAREST
// mantem cada ponto nitido.
void initGrainTexture() {
    const int G = 256;
    std::vector<unsigned char> px((size_t)G * G * 4);
    for (size_t i = 0; i < (size_t)G * G; ++i) {
        float r = (float)rand() / (float)RAND_MAX;
        unsigned char v = (unsigned char)(r * r * r * r * 0.9f * 255.0f);
        px[i * 4] = v; px[i * 4 + 1] = v; px[i * 4 + 2] = v; px[i * 4 + 3] = 255;
    }
    glGenTextures(1, &g_grainTex);
    glBindTexture(GL_TEXTURE_2D, g_grainTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, G, G, 0, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void initGL() {
    glEnable(GL_DEPTH_TEST); // z-buffer: objetos mais proximos escondem os mais distantes
    initLighting();
    initTextures();          // gera as texturas procedurais (precisa do contexto OpenGL)
    initGrainTexture();      // ruido do granulado de filme

    // NEVOA: o OpenGL mistura a cor de cada pixel com a cor da nevoa, em
    // quantidade crescente com a distancia ate' a camera. GL_EXP2 = queda
    // exponencial ao quadrado (suave de perto, fecha rapido de longe). A
    // cor e' um cinza-azulado escuro, so' um pouco mais claro que o fundo:
    // o fim do corredor "some" numa bruma em vez de num corte seco.
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_EXP2);
    glFogf(GL_FOG_DENSITY, FOG_DENSITY);
    const GLfloat fogColor[4] = { 0.03f, 0.035f, 0.04f, 1.0f };
    glFogfv(GL_FOG_COLOR, fogColor);
    glHint(GL_FOG_HINT, GL_NICEST);
}

} // namespace

// ===============================================================
// 10. MAIN
// Cria a janela, registra os callbacks e entrega o controle ao GLUT.
// Argumento opcional: --fotos gera as imagens em docs/imagens/ e sai.
// ===============================================================
int main(int argc, char** argv) {
    bool photoMode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--fotos") == 0) photoMode = true;
    }
    if (photoMode) { g_windowW = 1280; g_windowH = 720; } // 16:9 pras imagens do README

    glutInit(&argc, argv);
    // DOUBLE = dois buffers (desenha num, mostra o outro: sem flicker);
    // RGB = cores; DEPTH = z-buffer pra esconder o que esta' atras.
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(g_windowW, g_windowH);
    glutCreateWindow("1 Noite no Frederico");

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    glutMouseFunc(mouse);
    glutPassiveMotionFunc(passiveMotion);
    glutIgnoreKeyRepeat(1);              // segurar a tecla nao repete o evento
    glutSetCursor(GLUT_CURSOR_NONE);     // o timer mostra o cursor so' nos menus

    initGL();

    std::srand((unsigned int)std::time(0)); // semente aleatoria: cada partida e' diferente
    resetGame();
    g_state = STATE_MENU;                // o jogo abre no menu inicial
    g_stateTimer = 0.0f;

    if (!photoMode) {
        // Audio: se nao houver dispositivo, o jogo segue mudo.
        if (audioInit()) { std::atexit(audioShutdown); std::printf("Audio: ligado.\n"); }
        else             { std::printf("Audio: sem dispositivo (o jogo roda mudo).\n"); }
    }

    g_lastTimeMs = glutGet(GLUT_ELAPSED_TIME);
    glutTimerFunc(16, timerFunc, 0);     // dispara o primeiro tick do laco do jogo

    if (photoMode) {
        std::printf("Modo foto: gerando imagens em docs/imagens/ ...\n");
        startPhotoSession(true);
    } else {
        std::printf("=== 1 Noite no Frederico ===\n");
        std::printf("Sobreviva ate' as 6 da manha (5 minutos).\n\n");
        std::printf("Controles:\n");
        std::printf("  Mouse - olhar ao redor (limitado, como um pescoco humano)\n");
        std::printf("  F     - lanterna (so' afasta o monstro se o feixe apontar pra ele)\n");
        std::printf("  D     - fechar/abrir a porta (gasta energia bem mais rapido)\n");
        std::printf("  C     - ligar/desligar o monitor (mostra onde ele esta')\n");
        std::printf("  P/Esc - pausar\n");
        std::printf("  F12   - captura de tela (pasta capturas/)   F3 - painel de debug\n");
        std::printf("A bateria e' compartilhada. Quando acaba, tudo para de vez.\n");
        std::printf("Fique de olho: de vez em quando ele dispara.\n");
    }

    glutMainLoop(); // entrega o controle ao GLUT; nunca retorna
    return 0;
}