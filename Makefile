CC = cc
CFLAGS = -Wall -O2 -DTROFFROOT=\"/root/troff/home\"
LDFLAGS =

all: roff
%.o: %.c roff.h
	$(CC) -c $(CFLAGS) $<
roff: roff.o dev.o font.o in.o cp.o tr.o ren.o out.o reg.o sbuf.o adj.o eval.o draw.o wb.o hyph.o map.o clr.o char.o
	$(CC) -o $@ $^ $(LDFLAGS)
clean:
	rm -f *.o roff
