VOSK_DIR := /home/steamos/voice-dictation/.venv/lib/python3.12/site-packages/vosk
GIT_REVISION := $(shell git -C .. describe --always --dirty --broken 2>/dev/null || echo unknown)
CFLAGS := -O3 -flto -DNDEBUG -Wall -Wextra -DGIT_REVISION=\"$(GIT_REVISION)\"
LDFLAGS := -L$(VOSK_DIR) -Wl,-rpath,$(VOSK_DIR)
LDLIBS := -lvosk -lm -lpthread

SOURCES := main.c util.c queues.c feedback.c output_uinput.c audio_pw.c audio_policy.c transcriber_vosk.c session.c
OBJECTS := $(SOURCES:.c=.o)

all: frame-dictate-vosk-native

frame-dictate-vosk-native: $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.c app.h
	$(CC) $(CFLAGS) -c $< -o $@

install: frame-dictate-vosk-native
	install -Dm755 frame-dictate-vosk-native /home/steamos/voice-dictation/bin/frame-dictate-vosk-native

clean:
	rm -f frame-dictate-vosk-native $(OBJECTS)
