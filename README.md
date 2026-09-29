# 1 Noite no Frederico

Jogo de terror em primeira pessoa feito em C++ com OpenGL e GLUT.

Você é o vigia noturno de um prédio velho, preso numa saleta com uma abertura para o corredor, uma lanterna e uma porta de emergência. Seu trabalho é simples: chegar às 6 da manhã. O problema é o Frederico, que passa a noite inteira vindo pelo corredor na sua direção.

## Como funciona

No escuro, o Frederico se aproxima. Com a lanterna acesa, ele recua. Só que a lanterna e a porta dividem a mesma bateria, e ela não recarrega. A porta segura ele do lado de fora, mas gasta energia muito mais rápido, então ela é o botão de pânico, não uma solução.

Ele também não é previsível. De tempos em tempos ele arranca numa corrida, e quanto mais a noite avança, mais rápido e mais frequente isso fica. Se ficar esperando ele chegar perto para reagir, provavelmente vai ser tarde demais.

A noite dura 5 minutos reais. O relógio no canto superior direito mostra quanto falta para amanhecer, e a barra no canto inferior esquerdo mostra a bateria. Do lado da porta tem um interruptor com um LED: verde quando ela está aberta, vermelho quando está fechada e apagado quando a energia acaba.

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

O GLUT já vem no sistema. O Makefile detecta o macOS e ajusta o link sozinho.

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

## Arquivos

```
src/
  main.cpp           câmera, mouse, teclado, HUD e o laço do jogo
  scene_builder.*    sala, corredor, porta, interruptor e os móveis
  enemy.*            o Frederico: modelo, animação e trajetória
  bezier.*           curva de Bézier que guia o monstro pelo corredor
  lighting.*         iluminação ambiente e a lanterna
Makefile
```

Tudo é desenhado com primitivas do GLUT (cubos, esferas, quads), sem modelos ou texturas externas.