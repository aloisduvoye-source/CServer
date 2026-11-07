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
[ ! -e "$DBDIR/test.db" ] && [ ! -e data ] && pass "aucune base créée" || fail "une base a été créée"
grep -q "^migration" "$DBDIR/server.log" && fail "des migrations ont tourné" || pass "aucune migration"

cs_finish
