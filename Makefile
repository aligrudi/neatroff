CC = cc
CFLAGS = -Wall -O2
LDFLAGS =

all: xroff
%.o: %.c xroff.h
	$(CC) -c $(CFLAGS) $<
xroff: xroff.o dev.o font.o in.o cp.o tr.o ren.o out.o reg.o
	$(CC) -o $@ $^ $(LDFLAGS)
clean:
	rm -f *.o xroff
