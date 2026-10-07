VOSK_DIR := /home/steamos/voice-dictation/.venv/lib/python3.12/site-packages/vosk
GIT_REVISION := $(shell git -C .. describe --always --dirty --broken 2>/dev/null || echo unknown)
CFLAGS := -O3 -flto -DNDEBUG -Wall -Wextra -DGIT_REVISION=\"$(GIT_REVISION)\"
LDFLAGS := -L$(VOSK_DIR) -Wl,-rpath,$(VOSK_DIR)
LDLIBS := -lvosk -lm -lpthread

all: frame-dictate-vosk-native

frame-dictate-vosk-native: frame-dictate-vosk.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) $(LDLIBS)

install: frame-dictate-vosk-native
	install -Dm755 frame-dictate-vosk-native /home/steamos/voice-dictation/bin/frame-dictate-vosk-native

clean:
	rm -f frame-dictate-vosk-native
