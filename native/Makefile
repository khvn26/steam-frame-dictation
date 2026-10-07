WHISPER_DIR := /home/steamos/voice-dictation/lib/whisper
INCLUDE_DIR := /home/steamos/voice-dictation/include
GIT_REVISION := $(shell git -C .. describe --always --dirty --broken 2>/dev/null || echo unknown)
CFLAGS := -O3 -flto -DNDEBUG -Wall -Wextra -I$(INCLUDE_DIR) -DGIT_REVISION=\"$(GIT_REVISION)\"
LDFLAGS := -L$(WHISPER_DIR) -Wl,-rpath,$(WHISPER_DIR)
LDLIBS := -lwhisper -lggml -lggml-base -lggml-cpu -lm -lpthread

SOURCES := main.c util.c queues.c feedback.c output_uinput.c audio_pw.c audio_policy.c transcriber_whisper.c session.c
OBJECTS := $(SOURCES:.c=.o)
TARGET := steam-frame-dictation

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) $(LDLIBS)

%.o: %.c app.h
	$(CC) $(CFLAGS) -c $< -o $@

install: $(TARGET)
	install -Dm755 $(TARGET) /home/steamos/voice-dictation/bin/$(TARGET)

clean:
	rm -f $(TARGET) $(OBJECTS)
