#ifndef TEXTURES_H
#define TEXTURES_H

// ============================================================
// Texturas PROCEDURAIS: nenhuma imagem em arquivo. Cada textura e'
// calculada pixel a pixel no inicio do programa, a partir de funcoes
// de ruido e de padroes geometricos (ladrilhos, rejunte, rachaduras,
// ferrugem, veios de madeira, sangue...). Todas sao "ladrilhaveis":
// as bordas se encaixam, entao podem se repetir pela parede sem
// emendas visiveis.
// ============================================================

enum TextureId {
    TEX_NONE = -1,     // sem textura (cor lisa)
    TEX_FLOOR_TILE,    // piso de ladrilhos xadrez, sujo e rachado (2x2 ladrilhos)
    TEX_WALL_TILE,     // azulejo de parede com rejunte, manchas e ferrugem
    TEX_PLASTER,       // reboco / tinta velha com manchas de umidade
    TEX_CONCRETE,      // concreto do corredor: poros, rachaduras e oleo
    TEX_CEILING,       // forro de placas com manchas de goteira
    TEX_METAL,         // aco escovado com ferrugem e arranhoes
    TEX_WOOD,          // tabuas de madeira com veios
    TEX_BLOOD,         // mancha de sangue com transparencia (RGBA), pra "decalques"
    TEX_COUNT
};

// Gera todas as texturas e as envia pra GPU. Precisa de um contexto
// OpenGL ativo: chamar depois de glutCreateWindow.
void initTextures();

// Liga a textura (glEnable + glBind + modo MODULATE: a textura
// MULTIPLICA a cor iluminada). TEX_NONE desliga o uso de texturas.
void useTexture(TextureId id);

// Desliga o uso de texturas (volta pra cor lisa).
void noTexture();

#endif