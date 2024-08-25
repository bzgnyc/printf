#!/usr/bin/make -f
#


PROG=printf
LDLIBS=-liconv
#CFLAGS=-O3
#CFLAGS=-g -fsanitize-cfi-cross-dso -fstack-protector-all -Wall
#LDFLAGS=-O3
#LDFLAGS=-fsanitize-address-poison-custom-array-cookie -fsanitize-address-use-after-scope -fsanitize-address-use-odr-indicator -fsanitize-cfi-canonical-jump-tables -fsanitize-cfi-cross-dso -fsanitize-memory-track-origins -fsanitize-memory-use-after-dtor -fstack-protector-all -Wall


$(PROG):

printf.o: cstandards.h

clean:
	rm -f printf printf.o
