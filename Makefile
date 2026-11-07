CC     = gcc
CFLAGS = -Wall -Wextra -g -Icserver/include
LDLIBS = -lsqlite3 -lcrypt
SQL2C  = cserver/tools/sql2c.sh

# le framework : une bibliothèque statique qui ne connaît aucun site
LIB     = cserver/libcserver.a
LIB_SRC = $(wildcard cserver/src/*.c)
LIB_SQL = $(sort $(wildcard cserver/migrations/*.sql))
LIB_OBJ = $(LIB_SRC:cserver/src/%.c=cserver/build/%.o) cserver/build/migrations.o
LIB_HDR = $(wildcard cserver/include/*.h cserver/src/*.h)

# le site (un dossier par site dans examples/)
APP     = examples/guestbook
APP_SRC = $(wildcard $(APP)/*.c)
APP_SQL = $(sort $(wildcard $(APP)/migrations/*.sql))
APP_HDR = $(wildcard $(APP)/*.h)

$(APP)/server: $(APP_SRC) $(APP_HDR) $(APP)/build/migrations.c $(LIB)
	$(CC) $(CFLAGS) $(APP_SRC) $(APP)/build/migrations.c $(LIB) -o $@ $(LDLIBS)

# les migrations sont intégrées au binaire : chaque .sql devient une chaîne C
$(APP)/build/migrations.c: $(APP_SQL) $(SQL2C)
	mkdir -p $(APP)/build
	sh $(SQL2C) site_migrations $(APP_SQL) > $@

cserver/build/migrations.c: $(LIB_SQL) $(SQL2C) | cserver/build
	sh $(SQL2C) framework_migrations $(LIB_SQL) > $@

cserver/build/migrations.o: cserver/build/migrations.c $(LIB_HDR)
	$(CC) $(CFLAGS) -c $< -o $@

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
	rm -rf cserver/build $(LIB) $(APP)/server $(APP)/build

# si une commande échoue, on ne garde pas un fichier généré à moitié
.DELETE_ON_ERROR:

.PHONY: run test clean
