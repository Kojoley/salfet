# use pkg-config for getting CFLAGS abd LDFLAGS
FFMPEG_LIBS=libavformat libavfilter libavcodec libswscale libavutil
CFLAGS+=$(shell pkg-config  --cflags $(FFMPEG_LIBS))
LDFLAGS+=$(shell pkg-config --libs $(FFMPEG_LIBS))

PROG=salfet
SOURCES=$(PROG)

CFLAGS+=-std=c99 -Wall

OBJS=$(addsuffix .o,$(SOURCES))

release: CFLAGS+=-O2
release: $(PROG)

debug: CFLAGS+=-O0 -g -ggdb -DDEBUG
debug: $(PROG)

$(PROG): $(OBJS)
	$(CC) $< $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $< $(CFLAGS) -c -o $@

.PHONY: all clean

all: $(PROG)

clean:
	rm -rf $(PROG) $(OBJS)
