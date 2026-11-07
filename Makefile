CC     = gcc
CFLAGS = -Wall -Wextra -g -Icserver/src
LDLIBS = -lsqlite3 -lcrypt

# le framework
CSERVER_SRC = $(wildcard cserver/src/*.c)
CSERVER_HDR = $(wildcard cserver/src/*.h)

# le site (un dossier par site dans examples/)
APP     = examples/guestbook
APP_SRC = $(wildcard $(APP)/*.c)
APP_HDR = $(wildcard $(APP)/*.h)

$(APP)/server: $(CSERVER_SRC) $(CSERVER_HDR) $(APP_SRC) $(APP_HDR)
	$(CC) $(CFLAGS) $(CSERVER_SRC) $(APP_SRC) -o $@ $(LDLIBS)

# le serveur cherche www/ et data/ dans le dossier courant : on le lance depuis le site
run: $(APP)/server
	cd $(APP) && ./server

test: $(APP)/server
	$(APP)/test.sh

clean:
	rm -f $(APP)/server

.PHONY: run test clean
