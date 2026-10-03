#include "textures.h"

#ifdef __APPLE__
    #include <GLUT/glut.h>
#else
    #include <GL/glut.h>
#endif

#include <cmath>
#include <vector>

// ============================================================
// COMO FUNCIONA
//
// 1. RUIDO DE VALOR (vnoise): uma grade de numeros aleatorios fixos
//    (hash), interpolados suavemente entre si. Da' manchas "organicas".
//    A grade e' PERIODICA (volta ao inicio depois de px/py celulas),
//    e por isso a textura final e' ladrilhavel.
// 2. FBM (fractal): soma varias camadas de ruido, cada uma com o dobro
//    da frequencia e metade da forca. Camadas grandes dao as manchas
//    largas (umidade, sujeira); as pequenas dao o granulado.
// 3. Cada gerador combina isso com padroes (grade de ladrilhos,
//    rejunte, "ridged noise" pras rachaduras, riscos pros arranhoes) e
//    escolhe a cor de cada pixel.
// 4. O resultado vira textura OpenGL com mipmaps (versoes reduzidas,
//    que evitam o "brilho" quando a superficie esta' longe).
// ============================================================

namespace {

const int N = 256;                    // lado de cada textura em pixels
GLuint g_tex[TEX_COUNT];

float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Degrau suave: 0 antes de a, 1 depois de b, curva em S no meio.
// (Funciona tambem com a > b: inverte o sentido.)
float smoothstep(float a, float b, float x) {
    float t = clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

// Numero pseudo-aleatorio fixo em [0,1] pra (x, y, seed).
float hashf(int x, int y, int seed) {
    unsigned int n = (unsigned int)x * 73856093u ^ (unsigned int)y * 19349663u ^ (unsigned int)seed * 83492791u;
    n = (n << 13) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return (float)(n & 0x7fffffffu) / 2147483647.0f;
}

// Ruido de valor ladrilhavel, periodo px (em x) por py (em y).
float vnoise(float x, float y, int px, int py, int seed) {
    float fx0 = floorf(x), fy0 = floorf(y);
    int ix = (int)fx0, iy = (int)fy0;
    float fx = x - fx0, fy = y - fy0;
    float sx = fx * fx * (3.0f - 2.0f * fx);               // suaviza a interpolacao
    float sy = fy * fy * (3.0f - 2.0f * fy);
    int x0 = ((ix % px) + px) % px, x1 = (x0 + 1) % px;    // "% px": a grade da' a volta
    int y0 = ((iy % py) + py) % py, y1 = (y0 + 1) % py;
    float a = hashf(x0, y0, seed), b = hashf(x1, y0, seed);
    float c = hashf(x0, y1, seed), d = hashf(x1, y1, seed);
    return lerpf(lerpf(a, b, sx), lerpf(c, d, sx), sy);
}

// Soma de oitavas de ruido, com u,v em [0,1).
float fbm(float u, float v, int px, int py, int octaves, int seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        sum  += amp * vnoise(u * px, v * py, px, py, seed + o * 17);
        norm += amp;
        amp *= 0.5f; px *= 2; py *= 2;
    }
    return sum / norm;
}

typedef std::vector<unsigned char> Image;   // N*N pixels RGBA

void setPx(Image& img, int x, int y, float r, float g, float b, float a) {
    unsigned char* p = &img[(size_t)(y * N + x) * 4];
    p[0] = (unsigned char)(clamp01(r) * 255.0f + 0.5f);
    p[1] = (unsigned char)(clamp01(g) * 255.0f + 0.5f);
    p[2] = (unsigned char)(clamp01(b) * 255.0f + 0.5f);
    p[3] = (unsigned char)(clamp01(a) * 255.0f + 0.5f);
}

// Distancia do ponto (fu,fv) em [0,1] ate' a borda mais proxima do ladrilho.
float edgeDist(float fu, float fv) {
    float a = fu < 1.0f - fu ? fu : 1.0f - fu;
    float b = fv < 1.0f - fv ? fv : 1.0f - fv;
    return a < b ? a : b;
}

// ---------------------------------------------------------------
// PISO DE LADRILHOS: 2x2 ladrilhos em xadrez (claro/escuro), cada
// um com tom proprio, sujeira em manchas, granulado, rejunte escuro
// e algumas rachaduras. 1 repeticao = 1 m (ladrilhos de 50 cm).
// ---------------------------------------------------------------
void genFloorTile(Image& img) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float tu = u * 2.0f, tv = v * 2.0f;
            int ci = (int)tu, cj = (int)tv;
            float fu = tu - ci, fv = tv - cj;
            bool light = ((ci + cj) & 1) == 0;

            float dirt  = fbm(u, v, 4, 4, 4, 11);                  // sujeira em manchas
            float grain = vnoise(u * 64.0f, v * 64.0f, 64, 64, 5); // granulado fino
            float base  = light ? 0.52f : 0.10f;
            float c = base * (0.85f + 0.3f * hashf(ci, cj, 3))     // cada ladrilho um tom
                           * (0.55f + 0.75f * dirt)
                           * (0.90f + 0.20f * grain);
            float r = light ? c : c * 0.92f;
            float g = light ? c * 0.97f : c * 0.97f;
            float b = light ? c * 0.88f : c * 1.05f;                // claro amarelado, escuro azulado

            float e = edgeDist(fu, fv);
            if (e < 0.022f) {                                       // rejunte sujo
                float gr = 0.07f * (0.6f + 0.8f * dirt);
                r = gr; g = gr; b = gr * 0.9f;
            } else if (e < 0.04f) {                                 // chanfro do ladrilho
                r *= 0.78f; g *= 0.78f; b *= 0.78f;
            }

            // rachadura: linha onde um ruido "cruza" 0.5 (iso-contorno),
            // so' em alguns ladrilhos
            float ridge = fabsf(vnoise(u * 6.0f, v * 6.0f, 6, 6, 31) - 0.5f);
            if (ridge < 0.014f && hashf(ci, cj, 9) < 0.5f) { r *= 0.25f; g *= 0.25f; b *= 0.25f; }

            setPx(img, x, y, r, g, b, 1.0f);
        }
    }
}

// ---------------------------------------------------------------
// AZULEJO DE PAREDE: 4x4 azulejos verde-azulados com brilho de
// esmalte, rejunte, escorridos de umidade e ferrugem. 1 repeticao = 60 cm.
// ---------------------------------------------------------------
void genWallTile(Image& img) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float tu = u * 4.0f, tv = v * 4.0f;
            int ci = (int)tu, cj = (int)tv;
            float fu = tu - ci, fv = tv - cj;

            float dirt = fbm(u, v, 4, 4, 4, 21);
            float var  = 0.80f + 0.35f * hashf(ci, cj, 4);
            float gloss = 1.0f + 0.35f * (1.0f - fu) * (1.0f - fv) - 0.2f * fu * fv; // brilho de esmalte
            float r = 0.14f * var * gloss, g = 0.27f * var * gloss, b = 0.25f * var * gloss;

            float k = 0.65f + 0.6f * dirt;
            r *= k; g *= k; b *= k;

            float streak = vnoise(u * 10.0f, v * 2.0f, 10, 2, 22);   // escorridos verticais
            float stain  = smoothstep(0.60f, 0.80f, streak);
            r *= 1.0f - 0.55f * stain; g *= 1.0f - 0.5f * stain; b *= 1.0f - 0.5f * stain;

            float rust = smoothstep(0.72f, 0.88f, vnoise(u * 5.0f, v * 2.0f, 5, 2, 23)); // ferrugem
            r = lerpf(r, 0.30f, 0.6f * rust); g = lerpf(g, 0.13f, 0.6f * rust); b = lerpf(b, 0.05f, 0.6f * rust);

            float e = edgeDist(fu, fv);
            if (e < 0.04f) {                                           // rejunte
                float gr = 0.10f * (0.6f + 0.8f * dirt);
                r = gr; g = gr; b = gr * 0.9f;
            }
            float ridge = fabsf(vnoise(u * 5.0f, v * 5.0f, 5, 5, 33) - 0.5f);
            if (ridge < 0.012f && hashf(ci, cj, 8) < 0.4f) { r *= 0.3f; g *= 0.3f; b *= 0.3f; }

            setPx(img, x, y, r, g, b, 1.0f);
        }
    }
}

// ---------------------------------------------------------------
// REBOCO / TINTA VELHA: creme com manchas grandes de umidade marrom,
// pontinhos de sujeira e placas onde a tinta descascou. 1 rep. = 2 m.
// ---------------------------------------------------------------
void genPlaster(Image& img) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float cloud = fbm(u, v, 2, 2, 5, 41);
            float r = 0.38f, g = 0.36f, b = 0.30f;
            float shade = 0.75f + 0.5f * fbm(u, v, 8, 8, 4, 42);
            r *= shade; g *= shade; b *= shade;

            float damp = smoothstep(0.52f, 0.72f, cloud);             // umidade
            r = lerpf(r, 0.22f, 0.7f * damp); g = lerpf(g, 0.16f, 0.7f * damp); b = lerpf(b, 0.10f, 0.7f * damp);

            float peel = smoothstep(0.78f, 0.86f, vnoise(u * 12.0f, v * 12.0f, 12, 12, 43)); // tinta soltando
            r = lerpf(r, 0.50f, 0.6f * peel); g = lerpf(g, 0.47f, 0.6f * peel); b = lerpf(b, 0.40f, 0.6f * peel);

            float speck = 0.93f + 0.14f * hashf(x, y, 44);            // granulado por pixel
            setPx(img, x, y, r * speck, g * speck, b * speck, 1.0f);
        }
    }
}

// ---------------------------------------------------------------
// CONCRETO: cinza com poros, rachaduras, manchas de oleo e uma junta
// de dilatacao na borda (uma laje por repeticao).
// ---------------------------------------------------------------
void genConcrete(Image& img) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float c = 0.27f * (0.55f + 0.9f * fbm(u, v, 4, 4, 5, 51));
            c *= 0.92f + 0.16f * hashf(x, y, 52);
            float r = c, g = c, b = c * 1.02f;

            if (vnoise(u * 48.0f, v * 48.0f, 48, 48, 53) > 0.84f) { r *= 0.5f; g *= 0.5f; b *= 0.5f; } // poros

            float oil = smoothstep(0.60f, 0.72f, fbm(u, v, 2, 2, 3, 54));                               // oleo
            r *= 1.0f - 0.55f * oil; g *= 1.0f - 0.55f * oil; b *= 1.0f - 0.45f * oil;

            float ridge = fabsf(vnoise(u * 5.0f, v * 5.0f, 5, 5, 55) - 0.5f);
            if (ridge < 0.013f) { r *= 0.2f; g *= 0.2f; b *= 0.2f; }                                     // rachaduras

            float e = edgeDist(u, v);
            if (e < 0.012f) { r *= 0.35f; g *= 0.35f; b *= 0.35f; }                                      // junta da laje

            setPx(img, x, y, r, g, b, 1.0f);
        }
    }
}

// ---------------------------------------------------------------
// FORRO: 2x2 placas com poros, juntas escuras e manchas marrons de
// goteira. 1 repeticao = 1.2 m (placas de 60 cm).
// ---------------------------------------------------------------
void genCeiling(Image& img) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float tu = u * 2.0f, tv = v * 2.0f;
            float fu = tu - (int)tu, fv = tv - (int)tv;
            float c = 0.32f * (0.8f + 0.4f * fbm(u, v, 4, 4, 3, 61));
            float r = c, g = c, b = c * 0.92f;
            if (vnoise(u * 96.0f, v * 96.0f, 96, 96, 62) > 0.70f) { r *= 0.7f; g *= 0.7f; b *= 0.7f; } // poros
            float leak = smoothstep(0.55f, 0.68f, fbm(u, v, 2, 2, 4, 63));                              // goteira
            r = lerpf(r, 0.22f, 0.7f * leak); g = lerpf(g, 0.15f, 0.7f * leak); b = lerpf(b, 0.08f, 0.7f * leak);
            if (edgeDist(fu, fv) < 0.03f) { r = 0.08f; g = 0.08f; b = 0.075f; }                         // juntas
            setPx(img, x, y, r, g, b, 1.0f);
        }
    }
}

// ---------------------------------------------------------------
// ACO ESCOVADO: riscos horizontais (ruido esticado: muito fino em v,
// largo em u), manchas de ferrugem e arranhoes claros.
// ---------------------------------------------------------------
void genMetal(Image& img) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float brush = vnoise(u * 2.0f, v * 128.0f, 2, 128, 71);
            float c = 0.30f * (0.78f + 0.44f * brush) * (0.85f + 0.3f * fbm(u, v, 3, 3, 3, 72));
            float r = c * 0.92f, g = c * 0.97f, b = c * 1.05f;

            float rust = smoothstep(0.55f, 0.70f, fbm(u, v, 4, 4, 5, 73));
            float rs   = 0.6f + 0.8f * vnoise(u * 64.0f, v * 64.0f, 64, 64, 74);
            r = lerpf(r, 0.40f * rs, 0.75f * rust);
            g = lerpf(g, 0.16f * rs, 0.75f * rust);
            b = lerpf(b, 0.05f * rs, 0.75f * rust);

            for (int k = 0; k < 10; ++k) {                         // arranhoes: segmentos de reta
                float ax = hashf(k, 1, 75), ay = hashf(k, 2, 75);
                float ang = (hashf(k, 3, 75) - 0.5f) * 1.2f + 0.3f;
                float len = 0.10f + 0.25f * hashf(k, 4, 75);
                float dx = cosf(ang), dy = sinf(ang);
                float px = u - ax, py = v - ay;
                float t = px * dx + py * dy;
                if (t < 0.0f || t > len) continue;
                if (fabsf(px * dy - py * dx) < 0.0025f) { r = 0.62f; g = 0.63f; b = 0.66f; }
            }
            setPx(img, x, y, r, g, b, 1.0f);
        }
    }
}

// ---------------------------------------------------------------
// MADEIRA: 4 tabuas, cada uma com os veios deslocados. Os veios vem
// de ruido esticado ao longo de u (lento em u, rapido em v) somado a
// um seno (os "aneis").
// ---------------------------------------------------------------
void genWood(Image& img) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float pv = v * 4.0f;
            int j = (int)pv;
            float fv = pv - j;
            float uu = u + hashf(j, 5, 81);                        // cada tabua com o veio deslocado
            uu -= floorf(uu);
            float g = vnoise(uu * 3.0f, v * 40.0f, 3, 40, 82);
            float band = 0.5f + 0.5f * sinf(g * 18.0f + v * 60.0f);
            float c = 0.8f * (0.7f + 0.4f * g) * (0.85f + 0.15f * band);
            float r = 0.38f * c, gg = 0.24f * c, b = 0.13f * c;
            if (fv < 0.03f || fv > 0.985f) { r = 0.05f; gg = 0.035f; b = 0.02f; }   // fresta entre tabuas
            setPx(img, x, y, r, gg, b, 1.0f);
        }
    }
}

// ---------------------------------------------------------------
// SANGUE (RGBA): uma poca de borda irregular no centro + gotas
// espalhadas em volta. O canal alfa e' o formato da mancha; o resto
// da textura fica transparente. Desenhada por cima de pisos e paredes
// como um "decalque".
// ---------------------------------------------------------------
void genBlood(Image& img) {
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
            float dx = u - 0.5f, dy = v - 0.5f;
            float d = sqrtf(dx * dx + dy * dy);

            float n  = fbm(u, v, 5, 5, 3, 91);                     // deixa a borda irregular
            float r0 = 0.26f + 0.18f * (n - 0.5f) * 2.0f;
            float a  = smoothstep(r0 + 0.03f, r0 - 0.05f, d);

            for (int k = 0; k < 16; ++k) {                         // gotas em volta
                float ang = hashf(k, 1, 92) * 6.2831853f;
                float rad = 0.28f + 0.16f * hashf(k, 2, 92);
                float cx = 0.5f + rad * cosf(ang), cy = 0.5f + rad * sinf(ang);
                float rr = 0.012f + 0.035f * hashf(k, 3, 92);
                float ddx = u - cx, ddy = v - cy;
                float dd = sqrtf(ddx * ddx + ddy * ddy);
                float ga = smoothstep(rr * 1.2f, rr * 0.6f, dd);
                if (ga > a) a = ga;
            }

            float shade = 0.55f + 0.9f * fbm(u, v, 8, 8, 3, 93);
            float core  = 1.0f - 0.4f * smoothstep(0.2f, 0.0f, d); // centro mais escuro (coagulado)
            float r = 0.36f * shade * core, g = 0.015f * shade, b = 0.015f * shade;
            setPx(img, x, y, r, g, b, a * 0.92f);
        }
    }
}

// Envia a imagem pra GPU com mipmaps e repeticao.
void upload(TextureId id, const Image& img) {
    glBindTexture(GL_TEXTURE_2D, g_tex[id]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, N, N, GL_RGBA, GL_UNSIGNED_BYTE, &img[0]);
}

} // namespace

void initTextures() {
    glGenTextures(TEX_COUNT, g_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    Image img((size_t)N * N * 4);
    genFloorTile(img); upload(TEX_FLOOR_TILE, img);
    genWallTile(img);  upload(TEX_WALL_TILE, img);
    genPlaster(img);   upload(TEX_PLASTER, img);
    genConcrete(img);  upload(TEX_CONCRETE, img);
    genCeiling(img);   upload(TEX_CEILING, img);
    genMetal(img);     upload(TEX_METAL, img);
    genWood(img);      upload(TEX_WOOD, img);
    genBlood(img);     upload(TEX_BLOOD, img);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void useTexture(TextureId id) {
    if (id == TEX_NONE) { glDisable(GL_TEXTURE_2D); return; }
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_tex[id]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void noTexture() {
    glDisable(GL_TEXTURE_2D);
}