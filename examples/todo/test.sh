#!/bin/sh
# Tests du site todo (port 8082 par défaut, à côté du livre d'or sur 8081).

cd "$(dirname "$0")" || exit 1
. ../../cserver/tests/lib.sh
cs_start 8082

ALICE="$DBDIR/alice.cookies"
BOB="$DBDIR/bob.cookies"

# la migration du site
grep -q "^migration appliquée : site/001_todos$" "$DBDIR/server.log" \
	&& pass "migration site/001_todos appliquée" || fail "migration site/001_todos absente"

# accueil
check 200 GET /
check_contains "<title>todo</title>"             /
check_contains '<a href="/signup">Sign up</a>'   /
check_contains 'href="/style.css"'               /

# tout le reste est réservé aux personnes connectées
check 303 GET  /todos
check 303 POST /todos --data-binary "title=x"
check 303 POST /todos/1/done
check 303 POST /todos/1/delete
check_contains "Location: /login" /todos -D -

# comptes : pages du framework, dans le layout du site
check_contains "<title>log in · todo</title>"   /login
check_absent   '<a href="/todos">my todos</a>'  /login                # pas connecté : pas de lien
check 303 POST /signup --data-binary "name=alice&password=motdepasse1" -c "$ALICE"
check 303 POST /signup --data-binary "name=bob&password=motdepasse2"   -c "$BOB"
check_contains "Location: /todos"   /login --data-binary "name=alice&password=motdepasse1" -D -   # after_login
check_contains "Set-Cookie: todo_session=" /login --data-binary "name=alice&password=motdepasse1" -D -
check_contains '<a href="/todos">See my todos' / -b "$ALICE"

# ajouter
check_contains "Nothing to do"  /todos -b "$ALICE"
check 303 POST /todos --data-binary "title=acheter+du+pain"                -b "$ALICE"
check 303 POST /todos --data-binary "title=%3Cb%3Eimportant%3C%2Fb%3E"    -b "$ALICE"
check 303 POST /todos --data-binary "title=secret+de+bob"                  -b "$BOB"
check_contains "acheter du pain"                 /todos -b "$ALICE"
check_contains "&lt;b&gt;important&lt;/b&gt;"    /todos -b "$ALICE"
check_contains "2 left"                          /todos -b "$ALICE"

# chacun ne voit que ses tâches
check_absent   "secret de bob"    /todos -b "$ALICE"
check_absent   "acheter du pain"  /todos -b "$BOB"
check_contains "1 left"           /todos -b "$BOB"

# tâches refusées
LONG=$(head -c 801 /dev/zero | tr '\0' a)
check 400 POST /todos --data-binary "title="       -b "$ALICE"
check 400 POST /todos --data-binary "title=+++"    -b "$ALICE"
check 400 POST /todos --data-binary "title=%FF"    -b "$ALICE"
check 400 POST /todos --data-binary "title=$LONG"  -b "$ALICE"

# l'id d'une tâche, lu dans la page : la ligne <form ... /todos/ID/done> précède le titre
id_of() { # id_of <titre> <cookies>
	curl -s -b "$2" "$BASE/todos" | grep -B1 "$1" | grep -o '/todos/[0-9]*/done' | grep -o '[0-9]*'
}
PAIN=$(id_of "acheter du pain" "$ALICE")
SECRET=$(id_of "secret de bob" "$BOB")
[ -n "$PAIN" ] && [ -n "$SECRET" ] && pass "ids trouvés : $PAIN, $SECRET" || fail "ids introuvables"

# cocher, décocher (route à paramètre /todos/:id/done)
check 303 POST "/todos/$PAIN/done" -b "$ALICE"
check_contains '<li class="done">' /todos -b "$ALICE"
check_contains "1 left"            /todos -b "$ALICE"
check 303 POST "/todos/$PAIN/done" -b "$ALICE"
check_contains "2 left"            /todos -b "$ALICE"

# les tâches de quelqu'un d'autre : 404, et rien ne change
check 404 POST "/todos/$SECRET/done"   -b "$ALICE"
check 404 POST "/todos/$SECRET/delete" -b "$ALICE"
check_contains "secret de bob" /todos -b "$BOB"
check_contains "1 left"        /todos -b "$BOB"

# paramètres invalides
check 404 POST /todos/abc/done    -b "$ALICE"
check 404 POST /todos/0/done      -b "$ALICE"
check 404 POST /todos/-1/done     -b "$ALICE"
check 404 POST /todos/99999/done  -b "$ALICE"
check 405 POST /todos/1/autre     -b "$ALICE"
check 404 GET  "/todos/$PAIN/done" -b "$ALICE"

# supprimer
check 303 POST "/todos/$PAIN/delete" -b "$ALICE"
check_absent   "acheter du pain" /todos -b "$ALICE"
check_contains "1 left"          /todos -b "$ALICE"
check 404 POST "/todos/$PAIN/delete" -b "$ALICE"                 # déjà supprimée

cs_finish
