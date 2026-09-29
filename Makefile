CXXFLAGS := -std=c++11 -Wall -O2
SRC_DIR  := src

SRCS := $(SRC_DIR)/main.cpp \
        $(SRC_DIR)/bezier.cpp \
        $(SRC_DIR)/lighting.cpp \
        $(SRC_DIR)/scene_builder.cpp \
        $(SRC_DIR)/enemy.cpp

OBJS := $(SRCS:.cpp=.o)

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
    LIBS := -lfreeglut -lopengl32 -lglu32
    BIN  := quarto_do_vigia.exe
    RM   := del /Q
    FIXPATH = $(subst /,\,$1)
else
    CXX := g++
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        LIBS := -framework GLUT -framework OpenGL
    else
        LIBS := -lglut -lGLU -lGL
    endif
    BIN := quarto_do_vigia
    RM  := rm -f
    FIXPATH = $1
endif

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) $(call FIXPATH, $(OBJS) $(BIN))

.PHONY: all clean