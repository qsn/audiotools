LDFLAGS=`pkg-config --cflags --libs jack` -lpthread -lm

EXECUTABLES=recjack
HEADERS=recjack.h wave.h metronome.h
SOURCES=recjack.c wave.c metronome.c

recjack_OBJ=$(SOURCES:.c=.o)

.PHONY: all clean

all: recjack

recjack: $(recjack_OBJ)

clean:
	rm -rf *.o *\~ $(EXECUTABLES)
