# Ce dépôt est un modèle : on le clone, et le clone devient son projet.
#
#   make new                          crée le projet à la racine (main.c, templates/...)
#   make run                          le lance        (make test : ses tests)
#   make run DIR=examples/guestbook   lance un exemple
#   make new DIR=../mon-site          crée un projet à part, avec sa copie de cserver
#   make test-all                     les tests du framework, des exemples et du projet
#
# Les règles de compilation sont dans cserver/cserver.mk, les mêmes que pour un
# projet indépendant.

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
