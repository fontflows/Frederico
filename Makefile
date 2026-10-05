CXXFLAGS := -std=c++11 -Wall -O2 -MMD -MP
SRC_DIR  := src

SRCS := $(SRC_DIR)/main.cpp \
        $(SRC_DIR)/bezier.cpp \
        $(SRC_DIR)/lighting.cpp \
        $(SRC_DIR)/scene_builder.cpp \
        $(SRC_DIR)/enemy.cpp \
        $(SRC_DIR)/textures.cpp \
        $(SRC_DIR)/audio.cpp

OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)

# Deteccao de plataforma: Windows_NT e' definida pelo proprio Windows
# (funciona tanto no cmd/PowerShell com mingw32-make quanto no MSYS2/Git Bash),
# entao nao depende de 'uname' existir.
ifeq ($(OS),Windows_NT)
    # Caminho FIXO pro g++ do ambiente MINGW64 do MSYS2. Isso evita depender
    # de qual "g++" o PATH acha primeiro -- em maquinas com mais de um
    # ambiente do MSYS2 instalado (ex: ucrt64 e mingw64 ao mesmo tempo), o
    # freeglut so' foi instalado num deles, entao precisamos garantir que
    # SEMPRE compilamos e linkamos com o mesmo g++ que tem o freeglut do lado.
    CXX  := C:/msys64/mingw64/bin/g++.exe
    # Audio no Windows: o miniaudio carrega o WASAPI do proprio sistema em
    # tempo de execucao, entao NAO precisa de nenhuma biblioteca extra aqui.
    LIBS := -lfreeglut -lopengl32 -lglu32
    BIN  := frederico.exe
    RM   := del /Q
    FIXPATH = $(subst /,\,$1)
else
    CXX := g++
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        # macOS: o audio usa os frameworks CoreAudio do sistema
        LIBS := -framework GLUT -framework OpenGL \
                -framework CoreFoundation -framework CoreAudio -framework AudioToolbox
    else
        # Linux: o miniaudio abre o ALSA/PulseAudio com dlopen (-ldl) numa thread (-lpthread)
        LIBS := -lglut -lGLU -lGL -lm -ldl -lpthread
    endif
    BIN := frederico
    RM  := rm -f
    FIXPATH = $1
endif

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# O audio.cpp inclui o miniaudio.h (uma biblioteca enorme, que gera muitos
# avisos que nao sao do nosso codigo). Esta regra, so' pra esse arquivo,
# silencia os avisos (-w). Por ser maior, ele demora uns 15-20 s na 1a vez.
$(SRC_DIR)/audio.o: $(SRC_DIR)/audio.cpp
	$(CXX) $(CXXFLAGS) -w -c $< -o $@

clean:
	$(RM) $(call FIXPATH, $(OBJS) $(DEPS) $(BIN))

# Dependencias de header (geradas pelo -MMD): se voce mudar um .h, os .cpp que
# o usam sao recompilados sozinhos. Sem isso, trocar uma assinatura no
# scene_builder.h deixaria objetos antigos e daria erro de link estranho.
-include $(DEPS)

.PHONY: all clean