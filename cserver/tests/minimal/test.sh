#!/bin/sh
# Tests du noyau seul : un site sans base de données ni comptes.

cd "$(dirname "$0")" || exit 1
. ../lib.sh
cs_start 8084 ../../..

check 200 GET /
check_contains "user=[] accounts=[] db=[non]" /
check_contains "<title>minimal · cserver</title>" /           # nom par défaut
check_absent   'href="/login"'                    /           # le layout n'affiche pas de comptes
check 404 GET  /nope
check_contains "bonjour depuis src/texte/format a+b" /      # le code de src/, sur plusieurs niveaux
for o in main.o src/pages/hello.o src/texte/format/greet.o src/a/util.o src/b/util.o; do
	[ -f "build/obj/$o" ] && pass "compilé séparément : $o" || fail "pas d'objet build/obj/$o"
done
[ ! -e "$DBDIR/test.db" ] && [ ! -e data ] && pass "aucune base créée" || fail "une base a été créée"
grep -q "^migration" "$DBDIR/server.log" && fail "des migrations ont tourné" || pass "aucune migration"

cs_finish
