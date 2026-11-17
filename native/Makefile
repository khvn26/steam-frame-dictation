ROOT := $(abspath ..)
WHISPER_DIR := $(ROOT)/lib/whisper
INCLUDE_DIR := $(ROOT)/include
PREFIX ?= $(ROOT)
GIT_REVISION := $(shell git -C $(ROOT) describe --always --dirty --broken 2>/dev/null || echo unknown)
CFLAGS := -O3 -flto -DNDEBUG -Wall -Wextra -I$(INCLUDE_DIR) -DGIT_REVISION=\"$(GIT_REVISION)\"
LDFLAGS := -L$(WHISPER_DIR) -Wl,-rpath,'$$ORIGIN/../lib/whisper'
LDLIBS := -lwhisper -lggml -lggml-base -lggml-cpu -lm -lpthread

SOURCES := main.c help_zh_tw.c util.c queues.c feedback.c output_uinput.c audio_pw.c audio_policy.c transcriber_whisper.c session.c
OBJECTS := $(SOURCES:.c=.o)
TARGET := steam-frame-dictation
X11_COPY := sfd-x11-copy

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.c app.h
	$(CC) $(CFLAGS) -c $< -o $@

# Optional clipboard helper for --paste-command; needs libX11 headers.
$(X11_COPY): x11_copy.c
	$(CC) -O2 -Wall -Wextra $< -o $@ -lX11

install: $(TARGET)
	install -Dm755 $(TARGET) $(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET) $(X11_COPY) $(OBJECTS)
