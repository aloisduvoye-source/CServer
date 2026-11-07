CC     = gcc
CFLAGS = -Wall -Wextra -g -Icserver/include
LDLIBS = -lsqlite3 -lcrypt

# le framework : une bibliothèque statique qui ne connaît aucun site
LIB     = cserver/libcserver.a
LIB_SRC = $(wildcard cserver/src/*.c)
LIB_OBJ = $(LIB_SRC:cserver/src/%.c=cserver/build/%.o)
LIB_HDR = $(wildcard cserver/include/*.h cserver/src/*.h)

# le site (un dossier par site dans examples/)
APP     = examples/guestbook
APP_SRC = $(wildcard $(APP)/*.c)
APP_HDR = $(wildcard $(APP)/*.h)

$(APP)/server: $(APP_SRC) $(APP_HDR) $(LIB)
	$(CC) $(CFLAGS) $(APP_SRC) $(LIB) -o $@ $(LDLIBS)

# les objets du framework sont réunis en un seul (ld -r), puis objcopy rend locaux
# tous les symboles sauf cs_* : un site peut avoir sa propre fonction route() ou
# db_open() sans conflit avec l'intérieur du framework
cserver/build/cserver.o: $(LIB_OBJ)
	ld -r -o $@ $^
	objcopy -w --keep-global-symbol='cs_*' $@

$(LIB): cserver/build/cserver.o
	ar rcs $@ $<

cserver/build/%.o: cserver/src/%.c $(LIB_HDR) | cserver/build
	$(CC) $(CFLAGS) -c $< -o $@

cserver/build:
	mkdir -p $@

# le serveur cherche www/ et data/ dans le dossier courant : on le lance depuis le site
run: $(APP)/server
	cd $(APP) && ./server

test: $(APP)/server
	$(APP)/test.sh

clean:
	rm -rf cserver/build $(LIB) $(APP)/server

.PHONY: run test clean
