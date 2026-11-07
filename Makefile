CC     = gcc
CFLAGS = -Wall -Wextra -g -Icserver/include
LDLIBS = -lsqlite3 -lcrypt
EMBED  = cserver/tools/embed.sh

# le framework : une bibliothèque statique qui ne connaît aucun site
LIB     = cserver/libcserver.a
LIB_SRC = $(wildcard cserver/src/*.c)
LIB_SQL = $(sort $(wildcard cserver/migrations/*.sql))
LIB_TPL = $(sort $(wildcard cserver/templates/*.html))
LIB_GEN = cserver/build/migrations.o cserver/build/templates.o
LIB_OBJ = $(LIB_SRC:cserver/src/%.c=cserver/build/%.o) $(LIB_GEN)
LIB_HDR = $(wildcard cserver/include/*.h cserver/src/*.h)

# le site : un dossier par site dans examples/ ; on choisit avec SITE=...
#   make SITE=todo run
SITE   ?= guestbook
SITES   = $(notdir $(wildcard examples/*))
APP    ?= examples/$(SITE)
APP_SRC = $(wildcard $(APP)/*.c)
APP_SQL = $(sort $(wildcard $(APP)/migrations/*.sql))
APP_HDR = $(wildcard $(APP)/*.h)

$(APP)/server: $(APP_SRC) $(APP_HDR) $(APP)/build/migrations.c $(LIB)
	$(CC) $(CFLAGS) $(APP_SRC) $(APP)/build/migrations.c $(LIB) -o $@ $(LDLIBS)

# intégrés au binaire : chaque fichier devient une chaîne C (embed.sh)
#  - les migrations (.sql), du site et du framework
#  - les modèles par défaut du framework (.html) ; ceux du site sont lus au
#    démarrage de chaque requête, pour pouvoir les modifier sans recompiler
$(APP)/build/migrations.c: $(APP_SQL) $(EMBED)
	mkdir -p $(APP)/build
	sh $(EMBED) cserver.h cs_migration site_migrations .sql $(APP_SQL) > $@

cserver/build/migrations.c: $(LIB_SQL) $(EMBED) | cserver/build
	sh $(EMBED) cserver.h cs_migration framework_migrations .sql $(LIB_SQL) > $@

cserver/build/templates.c: $(LIB_TPL) $(EMBED) | cserver/build
	sh $(EMBED) internal.h embedded_file framework_templates "" $(LIB_TPL) > $@

# fichiers générés : compilés avec l'en-tête interne du framework
cserver/build/%.o: cserver/build/%.c $(LIB_HDR)
	$(CC) $(CFLAGS) -Icserver/src -c $< -o $@

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

# tous les sites d'un coup
all:
	@for s in $(SITES); do $(MAKE) -s SITE=$$s || exit 1; done

# les tests du framework, sur son site de test (cserver/tests/site/)
test-cserver:
	cserver/tests/test.sh
	cserver/tests/minimal/test.sh

test-all:
	@echo "== cserver"; cserver/tests/test.sh || exit 1
	@echo "== minimal"; cserver/tests/minimal/test.sh || exit 1
	@for s in $(SITES); do echo "== $$s"; $(MAKE) -s SITE=$$s test || exit 1; done

clean:
	rm -rf cserver/build $(LIB) $(SITES:%=examples/%/server) $(SITES:%=examples/%/build)
	rm -rf cserver/tests/site/server cserver/tests/site/build
	rm -rf cserver/tests/minimal/server cserver/tests/minimal/build

# si une commande échoue, on ne garde pas un fichier généré à moitié
.DELETE_ON_ERROR:

.PHONY: run test all test-cserver test-all clean
