CC     = gcc
CFLAGS = -Wall -Wextra -g
LDLIBS = -lsqlite3 -lcrypt
SRC    = $(wildcard src/*.c)

server: $(SRC) $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(SRC) -o server $(LDLIBS)

clean:
	rm -f server

.PHONY: clean
