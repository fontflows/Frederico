# 1 Noite com o Frederico

Você é o vigia noturno. Sua sala é pequena, mal iluminada, e tem só uma
abertura: um corredor comprido que se perde na escuridão. Você não pode
sair dali — só girar a cabeça, acender a lanterna e, se as coisas
apertarem, fechar a porta.

No fundo do corredor, alguma coisa se move. Ela não anda em linha reta:
avança serpenteando pelas sombras, um pouco pra esquerda, um pouco pra
direita, cada vez mais perto. Enquanto ela estiver no escuro, ela se
aproxima. Se a lanterna a pegar, ela recua. Só que lanterna e porta
dividem a mesma bateria — e ela não recarrega. Cada uso conta, e cada
vez que você repele o bicho e ele volta, ele vem um pouco mais rápido
(e um pouco imprevisível) que da vez anterior. E se ele chegar até a
porta sem nunca ter sido visto... bom, você vai saber na hora.

É uma cena 3D interativa em C++ e OpenGL. O monstro é montado à mão, osso por osso, com
esferas e cubos do GLUT; o jeito como ele anda vem de senos e cossenos
mexendo nas juntas em tempo real; e o caminho dele pelo corredor é
literalmente uma curva de Bézier calculada a cada frame.

## Como rodar

**Linux (Ubuntu/Debian):**
```
sudo apt-get install freeglut3-dev
make
./quarto_do_vigia
```

**macOS:** o GLUT já vem no sistema, é só `make && ./quarto_do_vigia` —
o Makefile detecta o macOS sozinho e ajusta o link.

**Windows (MSYS2/MinGW):**
```
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-freeglut
mingw32-make
quarto_do_vigia.exe
```

## Controles

| O quê                        | Como             |
|-------------------------------|------------------|
| Olhar ao redor                | Mexer o mouse    |
| Ligar/desligar a lanterna      | `F`              |
| Fechar/abrir a porta de emergência | `D`         |
| Sair                           | `ESC`            |

Dica: a porta contém o monstro por completo, mas gasta bateria bem mais
rápido que a lanterna — é o botão de pânico, não uma solução pra deixar
fechada o tempo todo. A lanterna sozinha já seguindo o corredor costuma
bastar; guarde a porta pra quando ele estiver perto demais.

## Por dentro do código

```
src/
  bezier.h / bezier.cpp        — a curva de Bézier cúbica que guia o monstro
  lighting.h / lighting.cpp    — a luz ambiente (quase breu total) e a lanterna (luz pontual que segue a câmera)
  scene_builder.cpp            — a sala, o corredor e a porta, desenhados à mão com GL_QUADS
  enemy.h / enemy.cpp          — o monstro: corpo hierárquico + caminhada animada
  main.cpp                     — câmera, mouse, teclado e o laço do jogo (glutTimerFunc)
Makefile
```

O código tem comentários marcando onde cada exigência do enunciado (
modelagem com primitivas, hierarquia com push/pop matrix, animação por
tempo, mouse/teclado, câmera/perspectiva, iluminação e a curva de
Bézier) foi implementada — útil na hora da correção, se for o caso.