# 1 Noite no Frederico

Jogo de terror em primeira pessoa feito em C++ com OpenGL e GLUT.

Você é o vigia noturno de um prédio velho, sentado numa saleta com uma abertura para o corredor, uma lanterna, um monitor e uma porta de emergência. Seu trabalho é simples: chegar às 6 da manhã. O problema é o Frederico, que passa a noite inteira vindo pelo corredor na sua direção.

![Visão do jogador](docs/imagens/01_pov_jogador.png)

## Como funciona

No escuro, o Frederico se aproxima. A lanterna só afasta ele se o **feixe estiver apontado para ele**: ligar a lanterna e olhar para o outro lado não adianta nada, e ainda gasta bateria. Mire, segure, e ele recua.

Ele também não é previsível. De tempos em tempos ele arranca numa corrida, e quanto mais a noite avança, mais rápido e mais frequente isso fica. Se ficar esperando ele chegar perto para reagir, provavelmente vai ser tarde demais.

A lanterna, a porta e o monitor dividem a mesma bateria, e ela não recarrega:

- **Lanterna:** gasta pouco, mas exige mira.
- **Porta:** segura o Frederico do lado de fora, mas gasta energia muito mais rápido. É o botão de pânico, não uma solução.
- **Monitor:** mostra um mapa do corredor com um ponto vermelho onde o Frederico está, e as lâmpadas piscando de verdade. Quanto mais perto ele chega, mais chiado na tela. Gasta energia enquanto estiver ligado.

A noite dura 5 minutos reais. O relógio no canto superior direito mostra quanto falta para amanhecer, e a barra no canto inferior esquerdo mostra a bateria. Do lado da porta tem um interruptor com um LED: verde quando ela está aberta, vermelho quando está fechada e apagado quando a energia acaba.

![Frederico no corredor](docs/imagens/04_monstro_corpo.png)

## Controles

| Tecla | O que faz |
|---|---|
| Mouse | Olhar ao redor (e mirar a lanterna) |
| `F` | Liga e desliga a lanterna |
| `D` | Fecha e abre a porta |
| `C` | Liga e desliga o monitor |
| `P` ou `Esc` | Pausa |
| `R` | Recomeça, nas telas de game over e de vitória |
| `F12` | Tira uma captura de tela (pasta `capturas/`) |
| `F3` | Painel de depuração (posição do monstro, mira, sprint) |

Nos menus: setas ou `W`/`S` para escolher, `Enter` ou clique para confirmar.

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

## Capturas de tela

![Rosto do Frederico](docs/imagens/05_monstro_rosto.png)

![Sala de segurança](docs/imagens/06_sala_geral.png)

![Corredor](docs/imagens/02_corredor.png)

![Porta de aço](docs/imagens/08_porta.png)

As imagens acima são geradas pelo próprio jogo. Para refazê-las (por exemplo, depois de mudar o cenário), rode na raiz do projeto:

```
./frederico --fotos
```

Ele monta 12 cenas (visão do jogador, corredor, monstro de corpo inteiro e de perto, sala, mesa, porta, susto, game over, menu e vitória), salva os PNGs em `docs/imagens/` e fecha sozinho.

## Arquivos

```
src/
  main.cpp           câmera, lanterna, mouse, teclado, menus, HUD, capturas e o laço do jogo
  scene_builder.*    sala, corredor, porta, interruptor, móveis e luzes do cenário
  enemy.*            o Frederico: modelo, animação e trajetória
  bezier.*           curva de Bézier que guia o monstro pelo corredor
  lighting.*         iluminação ambiente e a lanterna
Makefile
```

Tudo é desenhado com primitivas do GLUT (cubos, esferas, cones, quads), sem modelos ou texturas externas. Até o gravador de PNG das capturas é escrito à mão, sem bibliotecas.