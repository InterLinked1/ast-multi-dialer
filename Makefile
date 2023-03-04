#
# AstMultiDialer: CLI dialer for development
#
# Copyright (C) 2023, Naveen Albert
#
# Naveen Albert <asterisk@phreaknet.org>
#

CC		= gcc
CFLAGS = -Wall -Werror -Wno-unused-parameter -Wextra -Wstrict-prototypes -Wmissing-prototypes -Wdeclaration-after-statement -Wmissing-declarations -Wmissing-format-attribute -Wformat=2 -Wshadow -std=gnu99 -pthread -O0 -g -Wstack-protector -fno-omit-frame-pointer -D_FORTIFY_SOURCE=2
EXE		= astmultidialer
LIBS	= -lm
RM		= rm -f

MAIN_OBJ := astmultidialer.o

all : main

%.o: %.c
	$(CC) $(CFLAGS) -c $^

cami.o: cami.c
	$(CC) $(CFLAGS) -c cami.c

cami :
	@if [ ! -f cami.o ]; then\
		git clone https://github.com/InterLinked1/cami.git cami;\
		cp -r cami/include .;\
		cp cami/cami.c .;\
		rm -rf cami;\
	fi

main : cami $(MAIN_OBJ) cami.o
	$(CC) $(CFLAGS) -o $(EXE) $(LIBS) *.o -ldl

clean :
	$(RM) *.i *.o

.PHONY: cami
.PHONY: all
.PHONY: main
.PHONY: clean
