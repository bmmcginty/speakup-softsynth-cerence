CC ?= cc
MINGW_CC ?= x86_64-w64-mingw32-gcc
CFLAGS ?= -O2 -g
WARN = -Wall -Wextra
PIPEWIRE_CFLAGS := $(shell pkg-config --cflags libpipewire-0.3)
PIPEWIRE_LIBS := $(shell pkg-config --libs libpipewire-0.3)

.PHONY: all clean test test-installed-voices
all: bin/speakup-cerence bin/speakup-cerence-voice-manager bin/wine-bridge-speakup-cerence.exe

bin/speakup-cerence: src/speakup-cerence.c src/voice-list.c src/voice-list.h src/speakup-scale.c src/speakup-scale.h src/voice-settings.c src/voice-settings.h src/voice-store.c src/voice-store.h | bin
	$(CC) $(CFLAGS) $(WARN) $(PIPEWIRE_CFLAGS) -Isrc -o $@ src/speakup-cerence.c src/voice-list.c src/speakup-scale.c src/voice-settings.c src/voice-store.c $(PIPEWIRE_LIBS) -lpthread

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
	$(CC) $(CFLAGS) $(WARN) -Isrc -o /tmp/speakup-cerence-voice-list-test tests/voice-list.c src/voice-list.c
	/tmp/speakup-cerence-voice-list-test
	rm -f /tmp/speakup-cerence-voice-list-test
	$(CC) $(CFLAGS) $(WARN) -Isrc -o /tmp/speakup-cerence-scale-test tests/speakup-scale.c src/speakup-scale.c
	/tmp/speakup-cerence-scale-test
	rm -f /tmp/speakup-cerence-scale-test
	$(CC) $(CFLAGS) $(WARN) -Isrc -o /tmp/speakup-cerence-voice-settings-test tests/voice-settings.c src/voice-settings.c
	/tmp/speakup-cerence-voice-settings-test
	rm -f /tmp/speakup-cerence-voice-settings-test
	$(CC) $(CFLAGS) $(WARN) -Isrc -o /tmp/speakup-cerence-voice-store-test tests/voice-store.c src/voice-store.c
	/tmp/speakup-cerence-voice-store-test
	rm -f /tmp/speakup-cerence-voice-store-test
	./tests/cli.sh

test-installed-voices: all
	./tests/installed-voices.sh
