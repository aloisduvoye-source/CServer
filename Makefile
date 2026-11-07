# Ce dépôt est un modèle : on le clone, et le clone devient son projet.
#
#   make new                          crée le projet à la racine (main.c, templates/...)
#   make run                          le lance        (make test : ses tests)
#   make run DIR=examples/guestbook   lance un exemple
#   make new ../mon-site              crée un projet à part, avec sa copie de cserver
#                                     (ou make new DIR=../mon-site)
#   make test-all                     les tests du framework, des exemples et du projet
#
# Les règles de compilation sont dans cserver/cserver.mk, les mêmes que pour un
# projet indépendant.

# "make new ../mon-site" : pour make, ../mon-site serait une deuxième cible à
# construire. On la prend comme dossier du projet, et on en fait une cible vide.
ifeq ($(firstword $(MAKECMDGOALS)),new)
NEW_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS))
ifneq ($(words $(NEW_ARGS)),0)
ifneq ($(words $(NEW_ARGS)),1)
$(error make new : un seul dossier à la fois (reçu : $(NEW_ARGS)))
endif
ifneq ($(origin DIR),undefined)
$(error make new : donner le dossier soit après new, soit avec DIR=, pas les deux)
endif
DIR := $(NEW_ARGS)
$(eval .PHONY: $(NEW_ARGS))
$(eval $(NEW_ARGS):;@:)
endif
endif

DIR      ?= .
SITE_DIR  = $(DIR)
EXAMPLES  = $(wildcard examples/*)
# une seule bibliothèque pour tout le dépôt (attention : en Make, un commentaire en
# fin de ligne garderait les espaces qui le précèdent dans la valeur)
CS_BUILD  = cserver/build
CS_HINT   = pas encore de projet ici : make new, ou make run DIR=examples/guestbook

include cserver/cserver.mk

new:
	@sh cserver/tools/new.sh "$(DIR)"

# tous les sites d'un coup : les exemples, et le projet s'il existe
all:
	@for d in $(EXAMPLES); do $(MAKE) -s DIR=$$d || exit 1; done
	@if [ -n "$(wildcard *.c)" ]; then $(MAKE) -s DIR=.; fi

# les tests du framework, sur ses sites de test (cserver/tests/)
test-cserver:
	cserver/tests/test.sh
	cserver/tests/minimal/test.sh

test-all:
	@echo "== cserver"; cserver/tests/test.sh || exit 1
	@echo "== minimal"; cserver/tests/minimal/test.sh || exit 1
	@for d in $(EXAMPLES); do echo "== $$d"; $(MAKE) -s DIR=$$d test || exit 1; done
	@if [ -n "$(wildcard *.c)" ]; then echo "== projet"; $(MAKE) -s DIR=. test || exit 1; fi

# en plus du clean de cserver.mk : tous les exemples et les sites de test
clean::
	rm -rf $(EXAMPLES:%=%/server) $(EXAMPLES:%=%/build)
	rm -rf cserver/tests/site/server cserver/tests/site/build
	rm -rf cserver/tests/minimal/server cserver/tests/minimal/build
	rm -f cserver/libcserver.a

.PHONY: new all test-cserver test-all

# --- fuzzing (clang et libFuzzer) : make fuzz, ou make fuzz FUZZ_TIME=300
#     Chaque cible tourne FUZZ_TIME secondes ; un plantage s'arrête avec l'entrée
#     qui le provoque, dans cserver/tests/fuzz/build/crash-*
CLANG     ?= $(shell command -v clang || command -v clang-18)
FUZZ_TIME ?= 60
FUZZ      = cserver/tests/fuzz
FUZZERS   = http text template
FUZZ_FLAGS = -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined

fuzz: $(FUZZERS:%=$(FUZZ)/build/fuzz_%)
	@for f in $(FUZZERS); do \
		echo "== fuzz_$$f ($(FUZZ_TIME) s)"; \
		mkdir -p $(FUZZ)/build/corpus/$$f; \
		$(FUZZ)/build/fuzz_$$f -max_total_time=$(FUZZ_TIME) -print_final_stats=1 \
			-artifact_prefix=$(FUZZ)/build/ $(FUZZ)/build/corpus/$$f $(FUZZ)/seeds/$$f 2>&1 \
			| grep -E 'ERROR|SUMMARY|Done|stat::number_of_executed_units|stat::peak|cov:' | tail -4 || exit 1; \
	done

# le framework est recompilé avec les sanitizers, directement depuis ses sources
$(FUZZ)/build/fuzz_%: $(FUZZ)/fuzz_%.c $(CS_SRC) $(CS_HDR) $(CS_BUILD)/migrations.c $(CS_BUILD)/templates.c
	@test -n "$(CLANG)" || { echo "le fuzzing demande clang (sudo apt install clang)"; exit 1; }
	@mkdir -p $(dir $@)
	$(CLANG) $(FUZZ_FLAGS) -Icserver/include -Icserver/src $< $(CS_SRC) \
		$(CS_BUILD)/migrations.c $(CS_BUILD)/templates.c -o $@ $(LDLIBS)

.PHONY: fuzz
