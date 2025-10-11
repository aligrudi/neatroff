# neatroff's default font and macro directories
FDIR ?= /neatroff/font
MDIR ?= /neatroff/tmac

CC ?= cc
CFLAGS ?= -Wall -O2
LDFLAGS ?=
DIRFLAGS = "-DTROFFFDIR=\"$(FDIR)\"" "-DTROFFMDIR=\"$(MDIR)\""
OBJS = roff.o dev.o font.o in.o cp.o tr.o ren.o out.o reg.o sbuf.o fmt.o \
	eval.o draw.o wb.o hyph.o map.o clr.o char.o dict.o iset.o dir.o

all: roff
%.o: %.c roff.h
	$(CC) -c $(CFLAGS) $(DIRFLAGS) $<
roff: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)
clean:
	rm -f *.o roff
