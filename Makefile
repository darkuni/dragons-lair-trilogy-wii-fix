# dltfix - build with gcc (Linux) or MinGW-w64 (Windows / cross)
CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-misleading-indentation -ffp-contract=off -fno-fast-math
LDFLAGS += -lpthread -lm
SRC = src/main.c src/crypto.c src/wiidisc.c src/moc5.c src/mobidec.c src/mobienc.c src/color.c src/model.c src/segments.c
dltfix: $(SRC) src/*.h src/refhashes.inc
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
dltfix.exe: $(SRC) src/*.h src/refhashes.inc
	x86_64-w64-mingw32-gcc $(CFLAGS) -static -o $@ $(SRC) -lpthread
clean:
	rm -f dltfix dltfix.exe
.PHONY: clean
