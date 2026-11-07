# cserver.mk : les règles pour compiler un site avec cserver.
#
# Le Makefile d'un site tient en une ligne :
#
#     include cserver/cserver.mk          (le chemin vers ce fichier)
#
# Cibles : make (compile), make run, make test, make clean
#
# Variables facultatives, à définir avant l'include :
#     SITE_DIR   dossier du site                        défaut : .
#     CS_BUILD   où compiler la bibliothèque            défaut : $(SITE_DIR)/build/cserver
#     CFLAGS     options de compilation                 défaut : -Wall -Wextra -g
#     CS_HINT    message si le dossier n'a encore aucun fichier .c

# le dossier de cserver : celui de ce fichier, où qu'il soit dans le projet
CSERVER_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

SITE_DIR ?= .
# préfixe des chemins du site : "" pour ".", sinon "dossier/"
CS_P      = $(patsubst ./%,%,$(SITE_DIR)/)

CFLAGS   ?= -Wall -Wextra -g
CS_FLAGS  = $(CFLAGS) -I$(CSERVER_DIR)/include
LDLIBS   += -lsqlite3 -lcrypt
CS_EMBED  = $(CSERVER_DIR)/tools/embed.sh

# --- la bibliothèque, compilée dans le projet : le dossier de cserver reste intact
#     (utile si c'est un sous-module git)
CS_BUILD ?= $(CS_P)build/cserver
CS_LIB    = $(CS_BUILD)/libcserver.a
CS_SRC    = $(wildcard $(CSERVER_DIR)/src/*.c)
CS_SQL    = $(sort $(wildcard $(CSERVER_DIR)/migrations/*.sql))
CS_TPL    = $(sort $(wildcard $(CSERVER_DIR)/templates/*.html))
CS_HDR    = $(wildcard $(CSERVER_DIR)/include/*.h $(CSERVER_DIR)/src/*.h)
CS_OBJ    = $(CS_SRC:$(CSERVER_DIR)/src/%.c=$(CS_BUILD)/%.o) $(CS_BUILD)/migrations.o $(CS_BUILD)/templates.o

# --- le site : les .c à sa racine, et tous ceux de src/, à n'importe quelle profondeur
#     (src/pages/admin.c...). Les en-têtes s'incluent par leur chemin depuis src/ :
#     #include "pages/admin.h"
SITE_SRC  = $(wildcard $(CS_P)*.c) $(sort $(shell find $(CS_P)src -name '*.c' 2>/dev/null))
SITE_SQL  = $(sort $(wildcard $(CS_P)migrations/*.sql))
SITE_GEN  = $(CS_P)build/migrations.c
SITE_BIN  = $(CS_P)server

# un .o par .c, dans build/obj/ en reproduisant l'arborescence : src/a/util.c et
# src/b/util.c ne se gênent pas. Seuls les .c modifiés sont recompilés.
SITE_OBJ  = $(patsubst $(CS_P)%.c,$(CS_P)build/obj/%.o,$(SITE_SRC) $(SITE_GEN))
SITE_INC  = -I$(CS_P). -I$(CS_P)src

# pas encore de fichier .c : un message clair plutôt qu'une erreur de compilation
CS_HINT ?= ajouter un main.c
ifeq ($(strip $(SITE_SRC)),)
$(SITE_BIN):
	@echo "aucun fichier .c dans « $(SITE_DIR) » : $(CS_HINT)"; exit 1
else
$(SITE_BIN): $(SITE_OBJ) $(CS_LIB)
	$(CC) $(SITE_OBJ) $(CS_LIB) -o $@ $(LDLIBS)
endif

# -MMD -MP : le compilateur écrit, à côté de chaque .o, la liste des .h qu'il inclut
# (fichier .d) ; modifier un .h ne recompile que les .c qui l'utilisent
$(CS_P)build/obj/%.o: $(CS_P)%.c
	@mkdir -p $(dir $@)
	$(CC) $(CS_FLAGS) $(SITE_INC) -MMD -MP -c $< -o $@

-include $(SITE_OBJ:.o=.d)

# intégrés au binaire : chaque fichier devient une chaîne C (embed.sh)
#  - les migrations (.sql), du site et du framework
#  - les modèles par défaut du framework (.html) ; ceux du site sont lus à
#    chaque requête, pour pouvoir les modifier sans recompiler
$(SITE_GEN): $(SITE_SQL) $(CS_EMBED)
	@mkdir -p $(dir $@)
	sh $(CS_EMBED) cserver.h cs_migration site_migrations .sql $(SITE_SQL) > $@

$(CS_BUILD)/migrations.c: $(CS_SQL) $(CS_EMBED) | $(CS_BUILD)
	sh $(CS_EMBED) cserver.h cs_migration framework_migrations .sql $(CS_SQL) > $@

$(CS_BUILD)/templates.c: $(CS_TPL) $(CS_EMBED) | $(CS_BUILD)
	sh $(CS_EMBED) internal.h embedded_file framework_templates "" $(CS_TPL) > $@

# fichiers générés : compilés avec l'en-tête interne du framework
$(CS_BUILD)/%.o: $(CS_BUILD)/%.c $(CS_HDR)
	$(CC) $(CS_FLAGS) -I$(CSERVER_DIR)/src -c $< -o $@

$(CS_BUILD)/%.o: $(CSERVER_DIR)/src/%.c $(CS_HDR) | $(CS_BUILD)
	$(CC) $(CS_FLAGS) -c $< -o $@

# les objets du framework sont réunis en un seul (ld -r), puis objcopy rend locaux
# tous les symboles sauf cs_* : un site peut avoir sa propre fonction route() ou
# db_open() sans conflit avec l'intérieur du framework
$(CS_BUILD)/cserver.o: $(CS_OBJ)
	ld -r -o $@ $^
	objcopy -w --keep-global-symbol='cs_*' $@

$(CS_LIB): $(CS_BUILD)/cserver.o
	ar rcs $@ $<

$(CS_BUILD):
	mkdir -p $@

# le serveur cherche www/, templates/ et data/ dans le dossier courant
run: $(SITE_BIN)
	cd $(SITE_DIR) && ./server

test: $(SITE_BIN)
	$(SITE_DIR)/test.sh

# double deux-points : le Makefile qui inclut ce fichier peut ajouter son propre clean::
clean::
	rm -rf $(SITE_BIN) $(CS_P)build $(CS_BUILD)

# si une commande échoue, on ne garde pas un fichier généré à moitié
.DELETE_ON_ERROR:

.PHONY: run test clean
