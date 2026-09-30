# 1 Noite no Frederico

Jogo de terror em primeira pessoa feito em C++ com OpenGL e GLUT, para o Trabalho 1 de Computação Gráfica.

Você é o vigia noturno de uma saleta com uma abertura para o corredor, uma lanterna e uma porta de emergência. Seu trabalho é chegar às 6 da manhã. O problema é o Frederico, que vem pelo corredor na sua direção.

## Como funciona

- No escuro, o Frederico avança. Com a lanterna acesa, ele recua.
- A lanterna e a porta dividem a mesma bateria, que não recarrega. A porta segura o monstro do lado de fora, mas gasta energia 5x mais rápido que a lanterna: é o botão de pânico.
- A noite dura 5 minutos reais (relógio no canto superior direito).
- **A dificuldade cresce com o tempo:** até o amanhecer o monstro fica até 4x mais rápido e a lanterna perde até 40% da força de empurrá-lo. No começo dá tempo de sobra; no fim você precisa reagir rápido e economizar bateria.

## Controles

| Tecla | O que faz |
|---|---|
| Mouse | Olhar ao redor |
| `F` | Liga e desliga a lanterna |
| `D` | Fecha e abre a porta |
| `R` | Recomeça, nas telas de game over e de vitória |
| `ESC` | Sai |

## Como rodar

**Linux (Ubuntu/Debian)**

```
sudo apt-get install build-essential freeglut3-dev
make
./frederico
```

**macOS**

```
make
./frederico
```

**Windows (MSYS2/MinGW)**

```
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-freeglut
mingw32-make
frederico.exe
```

## Requisitos do trabalho: onde cada um está

| # | Requisito | Onde |
|---|---|---|
| 1 | Modelagem de objetos 3D com primitivas | `scene_builder.cpp` (quads com normais e cubos) e `enemy.cpp` (cubos e esfera) |
| 2 | Transformações geométricas | `enemy.cpp` (`drawLimb`: hierarquia coxa→canela, braço→antebraço), `scene_builder.cpp` (`drawDesk` e `drawDoor`) |
| 3 | Animações (controle do tempo) | `main.cpp`, função `update()`: usa `dt` para porta, monstro, bateria e cronômetro |
| 4 | Controle de mouse e teclado | `main.cpp`: `onMouseMove()` e `onKey()` |
| 5 | Posicionamento da câmera e perspectiva | `main.cpp`, função `display()`: `gluPerspective` e `gluLookAt` |
| 6 | Iluminação | `lighting.cpp`: luz ambiente e lanterna (`GL_LIGHT0`) |
| 7 | Curvas paramétricas | `bezier.cpp` (Bézier cúbica), usada em `monsterPosition()` para o trajeto do monstro |

## Arquivos

```
src/
  main.cpp           câmera, mouse, teclado, HUD e o laço do jogo
  scene_builder.*    sala, corredor, porta e mesa
  enemy.*            o Frederico: modelo hierárquico, caminhada e trajetória
  bezier.*           curva de Bézier que guia o monstro pelo corredor
  lighting.*         luz ambiente e lanterna
Makefile
```

Tudo é desenhado com primitivas do GLUT (cubos, esferas, quads), sem modelos ou texturas externas.

## Sugestão de roteiro para a apresentação

1. **`bezier.cpp`** (9 linhas): a fórmula da curva, sozinha. É o requisito 7.
2. **`scene_builder.cpp`**: quads com normais (`wallX/Y/Z`), cubos esticados (`box`) e o `drawDesk` como exemplo de transformação em grupo. Requisitos 1 e 2.
3. **`enemy.cpp`**: `drawLimb` mostra a hierarquia de transformações; `draw` anima com senos do tempo. Requisitos 1, 2 e 3.
4. **`lighting.cpp`**: luz ambiente, atenuação, e por que a lanterna é atualizada logo depois do `gluLookAt`. Requisito 6.
5. **`main.cpp`**: primeiro o comentário do topo (callbacks do GLUT), depois `display()` (requisito 5), `onKey`/`onMouseMove` (requisito 4) e `update()` (requisito 3, com a dificuldade progressiva).
