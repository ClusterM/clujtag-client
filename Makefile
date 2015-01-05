CC=gcc
CFLAGS=-c -Wall -O2
LDFLAGS=
SOURCES=clujtag.c xsvf.c svf.c play.c memname.c scan.c statename.c tap.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=clujtag.exe

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf *.o $(EXECUTABLE)
