CC = cc
CFLAGS = -Wall -O2 -DTROFFROOT=\"/root/troff/home\"
LDFLAGS =

all: xroff
%.o: %.c xroff.h
	$(CC) -c $(CFLAGS) $<
xroff: xroff.o dev.o font.o in.o cp.o tr.o ren.o out.o reg.o sbuf.o adj.o eval.o line.o
	$(CC) -o $@ $^ $(LDFLAGS)
clean:
	rm -f *.o xroff
