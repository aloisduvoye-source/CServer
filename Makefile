CC     = gcc
CFLAGS = -Wall -Wextra -g
SRC    = $(wildcard src/*.c)

server: $(SRC) $(wildcard src/*.h)
	$(CC) $(CFLAGS) $(SRC) -o server

clean:
	rm -f server

.PHONY: clean
