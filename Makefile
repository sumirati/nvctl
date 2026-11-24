CC?=		cc
CFLAGS?=	-O2 -Wall -Wextra -Werror -std=c11

PROG=		nvctl
SRCS=		src/main.c src/rm_ioctl.c src/rm_thermal.c src/rm_pmgr.c

all: $(PROG)

$(PROG): $(SRCS) src/nvctl.h
	$(CC) $(CFLAGS) -o $(PROG) $(SRCS)

clean:
	rm -f $(PROG)

.PHONY: all clean
