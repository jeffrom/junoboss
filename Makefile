CC=clang
FLAGS=-Wall -Wextra -ggdb
FEXTRA=-Wl,-dead_strip -framework CoreMIDI -framework CoreFoundation
CFLAGS=-c -std=c90 -ansi
PROGRAM=junoboss
OBJECTS=main.o midi_io.o bithex.o file_io.o settings.o conv_faders.o conv_buttons.o errcheck.o

junoboss: $(OBJECTS)
	$(CC) $(FLAGS) $(FEXTRA) $(OBJECTS) -o $(PROGRAM)

%.o : %.c
	$(CC) $(FLAGS) $(CFLAGS) $<

clean:
	rm *.o
