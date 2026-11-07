# Les exemples du dépôt. Chacun est un site ordinaire : ce Makefile choisit lequel
# avec SITE=..., puis utilise les mêmes règles qu'un projet indépendant
# (cserver/cserver.mk).
#
#   make SITE=todo run
#   make test-all

SITE     ?= guestbook
SITE_DIR ?= examples/$(SITE)
# une seule bibliothèque pour tous les exemples (attention : en Make, un commentaire
# en fin de ligne garderait les espaces qui le précèdent dans la valeur)
CS_BUILD  = cserver/build
SITES     = $(notdir $(wildcard examples/*))

include cserver/cserver.mk

# tous les sites d'un coup
all:
	@for s in $(SITES); do $(MAKE) -s SITE=$$s || exit 1; done

# les tests du framework, sur ses sites de test (cserver/tests/)
test-cserver:
	cserver/tests/test.sh
	cserver/tests/minimal/test.sh

test-all:
	@echo "== cserver"; cserver/tests/test.sh || exit 1
	@echo "== minimal"; cserver/tests/minimal/test.sh || exit 1
	@for s in $(SITES); do echo "== $$s"; $(MAKE) -s SITE=$$s test || exit 1; done

# en plus du clean de cserver.mk : tous les exemples et les sites de test
clean::
	rm -rf $(SITES:%=examples/%/server) $(SITES:%=examples/%/build)
	rm -rf cserver/tests/site/server cserver/tests/site/build
	rm -rf cserver/tests/minimal/server cserver/tests/minimal/build
	rm -f cserver/libcserver.a

.PHONY: all test-cserver test-all
