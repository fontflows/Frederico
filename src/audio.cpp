#include "audio.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

// miniaudio: so' faz a SAIDA (abrir o alto-falante e chamar nossa funcao
// de preencher amostras). Compilando com -DNO_AUDIO ele nem e' incluido.
#ifndef NO_AUDIO
    #define MINIAUDIO_IMPLEMENTATION
    #include "miniaudio.h"
#endif

// ============================================================
// COMO FUNCIONA
//
// O dispositivo pede blocos de amostras (~10 ms) numa THREAD DE AUDIO
// propria, chamando audioRender(). Ali dentro, amostra por amostra:
//
//  1. CAMADAS CONTINUAS: sons que existem enquanto uma condicao vale,
//     com volume suavizado (pra nao dar "estalo" ao ligar/desligar):
//       ronco grave + vento, zumbido das lampadas, chiado e bipe do
//       monitor, motor da porta, rosnado do monstro sob a lanterna,
//       passos do monstro (disparados num ritmo).
//  2. VOZES (one-shots): sons curtos disparados por eventos (clique,
//       pancada da porta, sino, batimento, susto...). Cada vox tem seu
//       tempo "t" desde o disparo e calcula a forma de onda direto da
//       formula (ex.: pancada = seno grave que cai de frequencia * um
//       envelope que decai exponencialmente).
//  3. ESPACO: os sons do monstro sao posicionados no estereo (pan) e a
//       distancia reduz o volume e abafa os agudos. Um reverb simples
//       (4 filtros "comb") da' a sensacao de corredor grande.
//  4. SAIDA: soma tudo, aplica o volume geral e um limitador suave (tanh)
//       que impede de estourar.
//
// O jogo e a thread de audio compartilham so' variaveis simples (floats,
// uma fila de eventos). Sem trava: no pior caso uma leitura pega um valor
// do quadro anterior, o que nao faz diferenca pro ouvido.
// ============================================================

namespace {

const float SR      = 44100.0f;
const float TWO_PI  = 6.28318531f;

// ---------------------------------------------------------------
// Estado compartilhado com o jogo
// ---------------------------------------------------------------
AudioParams g_params = { 1.0f, 0.0f, 20.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
float g_lampA = 0.3f, g_lampB = 0.3f;

// Fila de eventos (produtor = jogo, consumidor = thread de audio).
const int QSIZE = 64;
volatile int g_qHead = 0, g_qTail = 0;
volatile int g_queue[QSIZE];

// ---------------------------------------------------------------
// Pecas basicas de sintese
// ---------------------------------------------------------------
unsigned int g_rng = 2463534242u;

// Ruido branco em [-1,1] (gerador xorshift: barato e sem biblioteca).
float noise() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return (float)(g_rng & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

float fract(float x) { return x - floorf(x); }
float saw(float cycles) { return 2.0f * fract(cycles) - 1.0f; }       // onda dente-de-serra
float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

// Filtro passa-baixa de 1 polo: deixa passar os graves e abafa os agudos.
// "a" vem de lpCoef(frequencia de corte): perto de 0 = muito abafado.
struct OnePole {
    float y;
    OnePole() : y(0.0f) {}
    float run(float x, float a) { y += a * (x - y); return y; }
};
float lpCoef(float fc) { return 1.0f - expf(-TWO_PI * fc / SR); }

// ---------------------------------------------------------------
// VOZES: sons curtos
// ---------------------------------------------------------------
enum VoiceType {
    V_CLICK_ON, V_CLICK_OFF, V_RELAY, V_ZAP, V_SLAM, V_CLUNK, V_BEEP_UP, V_BEEP_DOWN,
    V_JUMPSCARE, V_POWEROUT, V_CHIME, V_WINBELL, V_LUB, V_DUB, V_MENU_MOVE, V_MENU_SEL, V_STEP
};

struct Voice {
    bool    active;
    int     type;
    float   t;       // segundos desde o disparo (negativo = ainda esperando o atraso)
    float   gain;
    float   pan;     // -1..1
    float   wet;     // quanto do som vai pro reverb
    float   aux;     // parametro extra (frequencia do sino, brilho do passo...)
    float   ph[5];   // fases de osciladores
    OnePole lp;
    Voice() : active(false), type(0), t(0.0f), gain(0.0f), pan(0.0f), wet(0.0f), aux(0.0f) {
        for (int i = 0; i < 5; ++i) ph[i] = 0.0f;
    }
};
const int MAXV = 32;
Voice g_voices[MAXV];

// Dispara uma voz. Sem vaga, "rouba" a mais antiga.
void spawn(int type, float gain, float pan, float wet, float delay, float aux) {
    int idx = -1, oldest = 0;
    float oldestT = -1.0f;
    for (int i = 0; i < MAXV; ++i) {
        if (!g_voices[i].active) { idx = i; break; }
        if (g_voices[i].t > oldestT) { oldestT = g_voices[i].t; oldest = i; }
    }
    if (idx < 0) idx = oldest;
    Voice& v = g_voices[idx];
    v = Voice();
    v.active = true; v.type = type; v.t = -delay;
    v.gain = gain; v.pan = pan; v.wet = wet; v.aux = aux;
}

// Calcula UMA amostra (mono, seca) da voz. Cada caso e' a "receita" do som.
// Padrao de receita: oscilador * envelope. O envelope exp(-t*k) faz o som
// morrer rapido (k grande) ou devagar (k pequeno).
float voiceStep(Voice& v) {
    const float dt = 1.0f / SR;
    if (v.t < 0.0f) { v.t += dt; return 0.0f; }
    const float t = v.t;
    float s = 0.0f;

    switch (v.type) {
    case V_CLICK_ON:
    case V_CLICK_OFF: {                       // clique de interruptor: estalo de ruido + tom curtinho
        float f = (v.type == V_CLICK_ON) ? 2600.0f : 1700.0f;
        s = noise() * 0.55f * expf(-t * 160.0f) + sinf(TWO_PI * f * t) * 0.45f * expf(-t * 110.0f);
        if (t > 0.08f) v.active = false;
        break; }
    case V_RELAY: {                           // rele: estalo seco
        s = noise() * 0.6f * expf(-t * 200.0f) + sinf(TWO_PI * 760.0f * t) * 0.4f * expf(-t * 70.0f);
        if (t > 0.10f) v.active = false;
        break; }
    case V_ZAP: {                             // faisca/estalo eletrico
        s = v.lp.run(noise(), 0.6f) * expf(-t * 120.0f) * 0.9f;
        if (t > 0.06f) v.active = false;
        break; }
    case V_SLAM: {                            // porta de aco batendo
        float f = 35.0f + 60.0f * expf(-t * 9.0f);          // seno grave que CAI de frequencia = pancada
        v.ph[0] += TWO_PI * f * dt;
        float thud = sinf(v.ph[0]) * expf(-t * 5.5f);
        float ring = (sinf(TWO_PI * 213.0f * t) * 0.5f + sinf(TWO_PI * 347.0f * t) * 0.4f +
                      sinf(TWO_PI * 589.0f * t) * 0.3f + sinf(TWO_PI * 812.0f * t) * 0.2f)
                     * expf(-t * 5.0f) * 0.20f;              // parciais "inarmonicas" = metal
        float hit  = v.lp.run(noise(), 0.3f) * expf(-t * 28.0f) * 0.9f;
        s = thud * 0.9f + ring + hit;
        if (t > 2.2f) v.active = false;
        break; }
    case V_CLUNK: {                           // baque leve do fim de curso
        float f = 55.0f + 40.0f * expf(-t * 20.0f);
        v.ph[0] += TWO_PI * f * dt;
        float thud = sinf(v.ph[0]) * expf(-t * 14.0f) * 0.6f;
        float ring = (sinf(TWO_PI * 420.0f * t) + sinf(TWO_PI * 690.0f * t) * 0.7f) * expf(-t * 14.0f) * 0.05f;
        float hit  = v.lp.run(noise(), 0.3f) * expf(-t * 40.0f) * 0.5f;
        s = thud + ring + hit;
        if (t > 0.5f) v.active = false;
        break; }
    case V_BEEP_UP:
    case V_BEEP_DOWN: {                       // dois bipes (sobe ou desce)
        bool second = (t >= 0.08f);
        float seg = second ? t - 0.08f : t;
        bool up = (v.type == V_BEEP_UP);
        float f = (second == up) ? 1320.0f : 880.0f;
        float env = (1.0f - seg / 0.08f); if (env < 0.0f) env = 0.0f;
        float att = seg * 600.0f; if (att > 1.0f) att = 1.0f;
        s = sinf(TWO_PI * f * t) * 0.35f * env * att;
        if (t > 0.16f) v.active = false;
        break; }
    case V_JUMPSCARE: {                       // o susto: pancada grave + grito distorcido + rugido
        float f = 28.0f + 70.0f * expf(-t * 3.5f);
        v.ph[0] += TWO_PI * f * dt;
        float boom = sinf(v.ph[0]) * expf(-t * 2.2f);
        float rise = t / 0.5f; if (rise > 1.0f) rise = 1.0f;
        rise = rise * rise * (3.0f - 2.0f * rise);                        // curva suave 0..1
        float base = 520.0f + 900.0f * rise + 40.0f * sinf(TWO_PI * 9.0f * t); // altura sobe + vibrato
        float sc = 0.0f;
        for (int k = 1; k <= 4; ++k) {                                    // 4 serras levemente desafinadas
            float det = 1.0f + ((float)k - 2.5f) * 0.014f;
            v.ph[k] += base * det * dt;
            sc += saw(v.ph[k]);
        }
        sc = tanhf(3.5f * sc * 0.25f);                                    // distorcao: "grito" saturado
        float env = t * 80.0f; if (env > 1.0f) env = 1.0f;
        if (t > 1.1f) env *= expf(-(t - 1.1f) * 5.0f);
        float roar = v.lp.run(noise(), 0.35f) * (0.6f + 0.4f * sinf(TWO_PI * 22.0f * t)) * env;
        s = boom * 0.7f + sc * env * 0.5f + roar * 0.55f;
        if (t > 2.2f) v.active = false;
        break; }
    case V_POWEROUT: {                        // "desligando": varredura grave descendo + estalo
        float f = 26.0f + 200.0f * expf(-t * 1.6f);
        v.ph[0] += TWO_PI * f * dt;
        s = (sinf(v.ph[0]) + 0.5f * sinf(2.0f * v.ph[0])) * expf(-t * 1.1f) * 0.45f
            + noise() * expf(-t * 180.0f) * 0.6f;
        if (t > 2.5f) v.active = false;
        break; }
    case V_CHIME:
    case V_WINBELL: {                         // sino: varias parciais inarmonicas decaindo
        static const float R[5] = { 1.0f, 2.0f, 2.756f, 5.404f, 8.933f };   // razoes tipicas de sino
        static const float A[5] = { 1.0f, 0.55f, 0.45f, 0.28f, 0.12f };
        static const float D[5] = { 0.9f, 1.3f, 1.8f, 2.6f, 3.5f };
        float decayScale = (v.type == V_CHIME) ? 1.0f : 0.9f;
        for (int k = 0; k < 5; ++k)
            s += A[k] * sinf(TWO_PI * v.aux * R[k] * t) * expf(-t * D[k] * decayScale);
        s *= 0.35f * (1.0f - expf(-t * 700.0f));                          // ataque rapido da batida
        if (v.type == V_CHIME) s = v.lp.run(s, 0.18f);                     // abafado = distante
        if (t > 5.0f) v.active = false;
        break; }
    case V_LUB:
    case V_DUB: {                             // batida de coracao: baque grave curtinho
        bool lub = (v.type == V_LUB);
        float f = lub ? 40.0f + 38.0f * expf(-t * 26.0f) : 48.0f + 42.0f * expf(-t * 30.0f);
        v.ph[0] += TWO_PI * f * dt;
        float env = expf(-t * (lub ? 15.0f : 20.0f)) * (lub ? 1.0f : 0.75f);
        s = sinf(v.ph[0]) * env * 0.95f + v.lp.run(noise(), 0.15f) * expf(-t * 60.0f) * 0.12f;
        if (t > 0.3f) v.active = false;
        break; }
    case V_MENU_MOVE: {
        s = sinf(TWO_PI * 1250.0f * t) * expf(-t * 95.0f) * 0.3f;
        if (t > 0.06f) v.active = false;
        break; }
    case V_MENU_SEL: {
        bool second = (t >= 0.07f);
        float seg = second ? t - 0.07f : t;
        s = sinf(TWO_PI * (second ? 1050.0f : 700.0f) * t) * expf(-seg * 40.0f) * 0.3f;
        if (t > 0.2f) v.active = false;
        break; }
    case V_STEP: {                            // passo pesado de animatronico: baque + clank metalico
        float f = 38.0f + 34.0f * expf(-t * 38.0f);
        v.ph[0] += TWO_PI * f * dt;
        float thud  = sinf(v.ph[0]) * expf(-t * 13.0f);
        float clank = (sinf(TWO_PI * 530.0f * t) * 0.5f + sinf(TWO_PI * 791.0f * t) * 0.35f +
                       sinf(TWO_PI * 1234.0f * t) * 0.2f) * expf(-t * 22.0f) * 0.22f * v.aux; // aux = brilho (some com a distancia)
        float tap   = v.lp.run(noise(), 0.2f) * expf(-t * 70.0f) * 0.5f;
        s = thud + clank + tap;
        if (t > 0.6f) v.active = false;
        break; }
    default:
        v.active = false;
        break;
    }

    v.t += dt;
    return s * v.gain;
}

// ---------------------------------------------------------------
// Eventos -> vozes
// ---------------------------------------------------------------
void handleEvent(int e) {
    const AudioParams& p = g_params;
    float heart = clamp01((p.danger - 0.2f) / 0.8f);        // batimento so' aparece com perigo
    switch (e) {
    case EVT_FLASH_ON:     spawn(V_CLICK_ON,  0.55f, 0.0f, 0.05f, 0.0f, 0.0f); break;
    case EVT_FLASH_OFF:    spawn(V_CLICK_OFF, 0.45f, 0.0f, 0.05f, 0.0f, 0.0f); break;
    case EVT_RELAY:        spawn(V_RELAY,     0.50f, 0.0f, 0.10f, 0.0f, 0.0f); break;
    case EVT_DOOR_SLAM:    spawn(V_SLAM,      0.95f, 0.0f, 0.45f, 0.0f, 0.0f); break;
    case EVT_DOOR_CLUNK:   spawn(V_CLUNK,     0.60f, 0.0f, 0.25f, 0.0f, 0.0f); break;
    case EVT_MONITOR_ON:   spawn(V_BEEP_UP,   0.50f, 0.0f, 0.05f, 0.0f, 0.0f); break;
    case EVT_MONITOR_OFF:  spawn(V_BEEP_DOWN, 0.50f, 0.0f, 0.05f, 0.0f, 0.0f); break;
    case EVT_JUMPSCARE:    spawn(V_JUMPSCARE, 0.95f, 0.0f, 0.35f, 0.0f, 0.0f); break;
    case EVT_POWER_OUT:    spawn(V_POWEROUT,  0.80f, 0.0f, 0.30f, 0.0f, 0.0f); break;
    case EVT_CHIME:        spawn(V_CHIME,     0.60f, 0.0f, 0.45f, 0.0f, 330.0f); break;
    case EVT_WIN:
        for (int i = 0; i < 6; ++i)                          // 6 badaladas, 1.1 s entre elas
            spawn(V_WINBELL, 0.55f, 0.0f, 0.40f, 0.2f + 1.1f * (float)i, 440.0f);
        break;
    case EVT_HEART_LUB:    if (heart > 0.0f) spawn(V_LUB, 0.95f * heart, 0.0f, 0.10f, 0.0f, 0.0f); break;
    case EVT_HEART_DUB:    if (heart > 0.0f) spawn(V_DUB, 0.95f * heart, 0.0f, 0.10f, 0.0f, 0.0f); break;
    case EVT_MENU_MOVE:    spawn(V_MENU_MOVE, 0.50f, 0.0f, 0.0f, 0.0f, 0.0f); break;
    case EVT_MENU_SELECT:  spawn(V_MENU_SEL,  0.55f, 0.0f, 0.0f, 0.0f, 0.0f); break;
    default: break;
    }
}

// ---------------------------------------------------------------
// Estado das camadas continuas (vive entre chamadas de audioRender)
// ---------------------------------------------------------------
struct Mixer {
    double time;
    // volumes suavizados
    float sMaster, sDanger, sMonitor, sClose, sMotor, sGrowl, sFlash, sLamp, sDead;
    // fases dos osciladores (em ciclos)
    float phHum1, phHum2, phHum3, phTens1, phTens2, phSub, phBuzz, phMotor, phGrowl, phGrowlLfo, phFlash;
    // filtros
    OnePole wind, buzzLp, motorLp, growlLp, growlNoise, hissLp;
    // bipe do monitor
    float beepClock, beepEnv, beepT;
    // passos
    float stepClock;
    // para detectar saltos bruscos (faisca)
    float lastLamp, lastFlash;
    // reverb: 4 filtros comb
    float comb[4][4096];
    int   combPos[4];
    float combLp[4];
    Mixer() {
        time = 0.0;
        sMaster = 1.0f; sDanger = sMonitor = sClose = sMotor = sGrowl = sFlash = sLamp = sDead = 0.0f;
        phHum1 = phHum2 = phHum3 = phTens1 = phTens2 = phSub = phBuzz = phMotor = phGrowl = phGrowlLfo = phFlash = 0.0f;
        beepClock = 0.0f; beepEnv = 0.0f; beepT = 0.0f; stepClock = 0.0f;
        lastLamp = lastFlash = 0.0f;
        for (int c = 0; c < 4; ++c) {
            combPos[c] = 0; combLp[c] = 0.0f;
            for (int i = 0; i < 4096; ++i) comb[c][i] = 0.0f;
        }
    }
};
Mixer g_mx;
const int COMB_LEN[4] = { 2411, 2689, 2953, 3251 };   // tamanhos "primos" evitam ressonancia metalica

} // namespace

// ============================================================
// audioRender: o coracao do audio
// ============================================================
void audioRender(float* out, int frames) {
    // 1. eventos pendentes viram vozes
    while (g_qTail != g_qHead) {
        int e = g_queue[g_qTail];
        g_qTail = (g_qTail + 1) & (QSIZE - 1);
        handleEvent(e);
    }

    const AudioParams p = g_params;           // copia: valores consistentes durante o bloco
    Mixer& m = g_mx;

    // 2. faiscas: se a lampada ou a lanterna mudam de repente, estala
    float lampMix = 0.5f * (g_lampA + g_lampB);
    if (fabsf(lampMix - m.lastLamp) > 0.25f)    spawn(V_ZAP, 0.35f, 0.0f, 0.2f, 0.0f, 0.0f);
    if (fabsf(p.flashLevel - m.lastFlash) > 0.4f) spawn(V_ZAP, 0.30f, 0.0f, 0.1f, 0.0f, 0.0f);
    m.lastLamp = lampMix;
    m.lastFlash = p.flashLevel;

    // coeficientes de suavizacao (constantes de tempo) e de filtros
    const float kFast = 1.0f - expf(-1.0f / (0.05f * SR));
    const float kMid  = 1.0f - expf(-1.0f / (0.15f * SR));
    const float kSlow = 1.0f - expf(-1.0f / (0.60f * SR));
    const float aWind = lpCoef(300.0f), aBuzz = lpCoef(1200.0f), aMotor = lpCoef(700.0f);
    const float aGrowl = lpCoef(500.0f), aGrowlN = lpCoef(300.0f), aHiss = lpCoef(2500.0f);
    const float panAng = (p.monsterPan + 1.0f) * 0.7853982f;            // pan igual-potencia: cos/sin
    const float panL = cosf(panAng), panR = sinf(panAng);
    const float distGain = 1.0f / (1.0f + 0.12f * p.monsterDist);        // rosnado: cai com a distancia
    const float stepVol  = 0.95f / (1.0f + 0.30f * p.monsterDist);       // passos: caem mais rapido
    const float stepBright = expf(-p.monsterDist * 0.09f);               // agudos somem de longe
    const float stepRate = p.stepHz / SR;
    const float beepRate = (0.7f + 5.5f * p.monitorClose) / SR;          // bipe acelera quando ele chega perto

    for (int n = 0; n < frames; ++n) {
        m.time += 1.0 / SR;
        const float tt = (float)m.time;

        // suaviza os parametros
        m.sMaster  += (p.master - m.sMaster) * kMid;
        m.sDanger  += (p.danger - m.sDanger) * kSlow;
        m.sMonitor += (p.monitorOn - m.sMonitor) * kFast;
        m.sClose   += (p.monitorClose - m.sClose) * kMid;
        m.sMotor   += (p.doorMoving - m.sMotor) * kFast;
        m.sGrowl   += (p.beamHit - m.sGrowl) * ((p.beamHit > m.sGrowl) ? kFast : kMid);
        m.sFlash   += (p.flashLevel - m.sFlash) * kFast;
        m.sLamp    += (lampMix - m.sLamp) * kFast;
        m.sDead    += (p.powerDead - m.sDead) * kSlow;

        float mono = 0.0f;       // camadas centrais
        float sideL = 0.0f, sideR = 0.0f;   // camadas posicionadas (monstro)
        float bus = 0.0f;        // o que vai pro reverb

        // --- RONCO GRAVE + VENTO: o "som do lugar". Tres senos graves
        // (48 Hz e harmonicos desafinados) com leve vai-e-vem de volume.
        m.phHum1 += 48.0f / SR; m.phHum2 += 96.3f / SR; m.phHum3 += 143.0f / SR;
        float hum = 0.5f * sinf(TWO_PI * m.phHum1) + 0.3f * sinf(TWO_PI * m.phHum2) + 0.12f * sinf(TWO_PI * m.phHum3);
        hum *= 0.85f + 0.15f * sinf(TWO_PI * 0.11f * tt);
        mono += hum * (0.05f + 0.03f * m.sDanger);
        float wind = m.wind.run(noise(), aWind) * (0.6f + 0.4f * sinf(TWO_PI * 0.13f * tt + 1.0f));
        mono += wind * 0.28f;

        // --- TENSAO: dois tons quase iguais (220 e 233 Hz) "batem" um no outro
        // (intervalo dissonante). So' aparece com perigo, e cresce com o quadrado.
        m.phTens1 += 220.0f / SR; m.phTens2 += 233.1f / SR;
        mono += 0.5f * (sinf(TWO_PI * m.phTens1) + sinf(TWO_PI * m.phTens2)) * 0.05f * m.sDanger * m.sDanger;

        // --- SEM ENERGIA: subgrave pesado
        m.phSub += 31.0f / SR;
        mono += sinf(TWO_PI * m.phSub) * 0.08f * m.sDead;

        // --- ZUMBIDO DAS LAMPADAS: onda dente-de-serra a 120 Hz (rede de 60 Hz),
        // filtrada. O volume segue o brilho REAL das lampadas: some no apagao,
        // dispara quando piscam forte.
        m.phBuzz += 120.0f / SR;
        float buzz = m.buzzLp.run(0.5f * saw(m.phBuzz) + 0.2f * sinf(TWO_PI * 2.0f * m.phBuzz), aBuzz);
        mono += buzz * 0.07f * sqrtf(clamp01(m.sLamp));

        // --- ZUMBIDO DA LANTERNA: tom fino, so' enquanto funciona
        m.phFlash += 190.0f / SR;
        mono += sinf(TWO_PI * m.phFlash) * 0.006f * m.sFlash;

        // --- MONITOR: chiado (ruido de agudos, mais forte quando ele chega perto)
        // e bipe de proximidade cuja frequencia de repeticao sobe com o perigo.
        float hiss = noise() - m.hissLp.run(noise(), aHiss);
        mono += hiss * (0.010f + 0.050f * m.sClose) * m.sMonitor;
        if (p.monitorOn > 0.5f) {
            m.beepClock += beepRate;
            if (m.beepClock >= 1.0f) { m.beepClock -= 1.0f; m.beepEnv = 1.0f; m.beepT = 0.0f; }
        }
        if (m.beepEnv > 0.001f) {
            m.beepT += 1.0f / SR;
            mono += sinf(TWO_PI * 1500.0f * m.beepT) * m.beepEnv * 0.05f * m.sMonitor;
            m.beepEnv *= 0.99924f;               // decai em ~30 ms
        }

        // --- MOTOR DA PORTA: dente-de-serra grave filtrada, com variacao lenta de altura
        m.phMotor += (85.0f + 10.0f * sinf(TWO_PI * 3.0f * tt)) / SR;
        mono += m.motorLp.run(saw(m.phMotor), aMotor) * 0.12f * m.sMotor;

        // --- ROSNADO DO MONSTRO sob a luz: grave com tremolo de 14 Hz + ruido
        // grave. Posicionado como o monstro. E' o "retorno" sonoro da mira.
        m.phGrowl += (70.0f + 6.0f * sinf(TWO_PI * 7.0f * tt)) / SR;
        m.phGrowlLfo += 14.0f / SR;
        float growl = m.growlLp.run(saw(m.phGrowl), aGrowl) * (0.6f + 0.4f * sinf(TWO_PI * m.phGrowlLfo))
                    + m.growlNoise.run(noise(), aGrowlN) * 0.6f;
        float g = growl * 0.22f * m.sGrowl * distGain;
        sideL += g * panL; sideR += g * panR;
        bus += g * 0.4f;

        // --- PASSOS: um relogio dispara um passo a cada ciclo. Volume cai com
        // a distancia; no sprint (ritmo > 1.5/s) ficam mais fortes.
        if (stepRate > 0.0f) {
            m.stepClock += stepRate;
            if (m.stepClock >= 1.0f) {
                m.stepClock -= 1.0f;
                float boost = (p.stepHz > 1.5f) ? 1.25f : 1.0f;
                spawn(V_STEP, stepVol * boost, p.monsterPan, 0.45f, 0.0f, stepBright);
            }
        }

        // --- VOZES: soma todas, cada uma com seu pan e sua parte pro reverb
        float vL = 0.0f, vR = 0.0f;
        for (int i = 0; i < MAXV; ++i) {
            Voice& v = g_voices[i];
            if (!v.active) continue;
            float s = voiceStep(v);
            float pa = (v.pan + 1.0f) * 0.7853982f;
            vL += s * cosf(pa);
            vR += s * sinf(pa);
            bus += s * v.wet;
        }

        // --- REVERB: 4 filtros comb em paralelo (eco que se realimenta, com
        // as altas frequencias morrendo a cada volta). 2 vao pra cada lado.
        float w[4];
        float in = bus * 0.35f;
        for (int c = 0; c < 4; ++c) {
            float y = m.comb[c][m.combPos[c]];
            m.combLp[c] += (y - m.combLp[c]) * 0.35f;
            m.comb[c][m.combPos[c]] = in + m.combLp[c] * 0.80f;
            if (++m.combPos[c] >= COMB_LEN[c]) m.combPos[c] = 0;
            w[c] = y;
        }
        float wetL = (w[0] + w[2]) * 0.5f, wetR = (w[1] + w[3]) * 0.5f;

        // --- SAIDA: soma, volume geral e limitador suave (tanh nunca passa de +-1)
        float L = (mono + sideL + vL + wetL) * m.sMaster;
        float R = (mono + sideR + vR + wetR) * m.sMaster;
        out[2 * n]     = tanhf(L);
        out[2 * n + 1] = tanhf(R);
    }
}

void audioSetParams(const AudioParams& p) { g_params = p; }
void audioSetLamps(float a, float b)      { g_lampA = a; g_lampB = b; }

void audioEvent(AudioEvent e) {
    int h = g_qHead;
    int next = (h + 1) & (QSIZE - 1);
    if (next == g_qTail) return;              // fila cheia: descarta
    g_queue[h] = (int)e;
    g_qHead = next;
}

// ============================================================
// SAIDA PELO MINIAUDIO
// ============================================================
#ifndef NO_AUDIO

namespace {
ma_device g_device;
bool g_deviceOk = false;

// Chamada pela thread de audio do miniaudio sempre que o alto-falante
// precisa de mais amostras.
void dataCallback(ma_device*, void* out, const void*, ma_uint32 frames) {
    audioRender((float*)out, (int)frames);
}
} // namespace

bool audioInit() {
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;     // amostras float
    cfg.playback.channels = 2;                 // estereo
    cfg.sampleRate        = 44100;             // o miniaudio converte se o aparelho usar outra taxa
    cfg.dataCallback      = dataCallback;
    if (ma_device_init(NULL, &cfg, &g_device) != MA_SUCCESS) return false;
    if (ma_device_start(&g_device) != MA_SUCCESS) { ma_device_uninit(&g_device); return false; }
    g_deviceOk = true;
    return true;
}

void audioShutdown() {
    if (g_deviceOk) { ma_device_uninit(&g_device); g_deviceOk = false; }
}

#else   // -DNO_AUDIO: jogo mudo

bool audioInit()     { return false; }
void audioShutdown() {}

#endif