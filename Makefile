CC ?= cc
MINGW_CC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -O2 -g
WARN = -Wall -Wextra
PIPEWIRE_CFLAGS := $(shell pkg-config --cflags libpipewire-0.3)
PIPEWIRE_LIBS := $(shell pkg-config --libs libpipewire-0.3)

.PHONY: all clean test
all: bin/speakup-cerence bin/speakup-cerence-voice-manager bin/wine-bridge-speakup-cerence.exe

bin/speakup-cerence: src/speakup-cerence.c | bin
	$(CC) $(CFLAGS) $(WARN) $(PIPEWIRE_CFLAGS) -o $@ $< $(PIPEWIRE_LIBS) -lpthread

bin/speakup-cerence-voice-manager: src/speakup-cerence-voice-manager.py | bin
	cp $< $@
	chmod 755 $@

bin/wine-bridge-speakup-cerence.exe: src/wine-bridge-speakup-cerence.c src/cerence-server.c | bin
	$(MINGW_CC) $(CFLAGS) $(WARN) -municode -o $@ src/wine-bridge-speakup-cerence.c -lshell32

bin:
	mkdir -p $@

clean:
	rm -f bin/speakup-cerence bin/speakup-cerence-voice-manager bin/wine-bridge-speakup-cerence.exe

test: all
	./tests/cli.sh
