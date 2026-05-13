CC		?= gcc
CFLAGS		?= -Wall
LDFLAGS		?=
PKG_CONFIG	?= pkg-config

# Debian/Ubuntu-style -l*: works when Allegro headers are on the default include path.
DEFAULT_ALLEG_LIBS := -lallegro -lallegro_main -lallegro_primitives \
	-lallegro_image -lallegro_font -lallegro_audio -lallegro_acodec -lm

# Homebrew/macOS CI: Allegro headers live outside the default path; pkg-config exposes them.
# Build with: make USE_ALLEGRO_PKG_CONFIG=1
ALLEG_PC_MODULES ?= allegro-5 allegro_main-5 allegro_primitives-5 allegro_image-5 \
	allegro_font-5 allegro_audio-5 allegro_acodec-5

ifeq ($(USE_ALLEGRO_PKG_CONFIG),1)
CFLAGS	+= $(shell $(PKG_CONFIG) --cflags $(ALLEG_PC_MODULES))
LDLIBS	:= $(shell $(PKG_CONFIG) --libs $(ALLEG_PC_MODULES)) -lm
else
LDLIBS	:= $(DEFAULT_ALLEG_LIBS)
endif

.PHONY: all clean

all: icytower

icytower: icytower.o gfx.o sfx.o menu.o options.o characters.o floor_types.o fullscreen.o game.o physics.o highscores.o third_party/sonic/sonic.o

icytower.o: icytower.c icytower.h gfx.h sfx.h menu.h options.h characters.h floor_types.h game.h highscores.h
gfx.o: gfx.c gfx.h
sfx.o: sfx.c sfx.h options.h third_party/sonic/sonic.h
menu.o: menu.c menu.h icytower.h gfx.h sfx.h options.h characters.h floor_types.h fullscreen.h highscores.h
options.o: options.c options.h characters.h floor_types.h
characters.o: characters.c characters.h gfx.h sfx.h
floor_types.o: floor_types.c floor_types.h gfx.h
fullscreen.o: fullscreen.c fullscreen.h
game.o: game.c game.h icytower.h gfx.h sfx.h options.h characters.h floor_types.h physics.h highscores.h
physics.o: physics.c physics.h
highscores.o: highscores.c highscores.h options.h

clean:
	rm -f icytower *.o third_party/sonic/*.o
