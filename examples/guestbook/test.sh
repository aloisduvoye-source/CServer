#!/bin/sh
# Tests du livre d'or (port 8081 par défaut). Ce que le framework garantit
# (HTTP, sécurité, comptes, modèles...) est testé par cserver/tests/test.sh.

cd "$(dirname "$0")" || exit 1
. ../../cserver/tests/lib.sh
cs_start 8081

# migrations : celles du framework, puis celles du site, dans l'ordre
expected="cserver/001_comptes cserver/002_prefixe_cs cserver/003_limites site/001_messages site/002_supprimer_counters"
got=$(sed -n 's/^migration appliquée : //p' "$DBDIR/server.log" | tr '\n' ' ' | sed 's/ $//')
[ "$got" = "$expected" ] && pass "migrations appliquées : $got" || fail "migrations : '$got' (attendu '$expected')"

# pages : chacune passe par le layout du site
check 200 GET  /
check 200 GET  /about
check_contains "<title>cServer</title>"             /               # CS_PAGE sans titre
check_contains "<title>about · cServer</title>"     /about
check_contains "<title>guestbook · cServer</title>" /guestbook
check_contains "<title>log in · cServer</title>"    /login
for p in / /about /guestbook /login /signup /nope; do              # la navigation partout
	check_contains '<a href="/about">about</a> <a href="/guestbook">guestbook</a>' "$p"
done
check_contains "<h1>Hello World</h1>" /
check 404 GET  /about.html                                         # plus un fichier statique
check 404 GET  /nope.html
check_contains "404 dumbass" /nope.html                            # la page 404 du site

# fichiers statiques du site
check 200 GET  /image.webp
check_type "text/css"        /style.css
check_type "text/javascript" /guestbook.js
check_contains 'href="/style.css"'   /about
check_contains 'src="/guestbook.js"' /guestbook
check 404 GET  /../test.sh                                         # juste au-dessus de www/

# deux comptes (curl garde les cookies dans un fichier, comme un navigateur)
JEAN="$DBDIR/jean.cookies"
ZOE="$DBDIR/zoe.cookies"
check 303 POST /signup --data-binary "name=jean&password=motdepasse1" -c "$JEAN"
check 303 POST /signup --data-binary "name=zoe&password=motdepasse2"  -c "$ZOE"
check_contains "Location: /guestbook" /login --data-binary "name=jean&password=motdepasse1" -D -   # after_login
check_contains '<span class="user">jean' /guestbook -b "$JEAN"

# le formulaire : seulement pour les personnes connectées
check_contains '<a href="/login">log in</a> or' /guestbook
check_absent   'id="guestbook-form"'            /guestbook
check_contains 'id="guestbook-form"'            /guestbook -b "$JEAN"
check_contains "No messages yet"                /guestbook

# poster
check 303 POST /guestbook --data-binary "message=premier+message" -b "$JEAN"
check 303 POST /guestbook --data-binary "message=ligne+1%0D%0Aligne+2" -b "$ZOE"
check 303 POST /guestbook --data-binary "message=anonyme"                    # -> /login
check_contains "<strong>jean</strong>"  /guestbook
check_contains "<strong>zoe</strong>"   /guestbook
check_contains "premier message"        /guestbook
check_absent   "anonyme"                /guestbook
check_absent   "No messages yet"        /guestbook

# échappement HTML des messages
check 303 POST /guestbook --data-binary "message=%3Cscript%3Ealert(1)%3C%2Fscript%3E" -b "$JEAN"
check 303 POST /guestbook --data-binary "message=Tom+%26+%22Jerry%22+l%27ami" -b "$ZOE"
check_contains "&lt;script&gt;alert(1)&lt;/script&gt;"     /guestbook
check_contains "Tom &amp; &quot;Jerry&quot; l&#39;ami"     /guestbook
check_absent   "<script>"                                  /guestbook

# le plus récent en premier
first=$(curl -s "$BASE/guestbook" | grep -o 'premier message\|alert(1)' | head -1)
[ "$first" = "alert(1)" ] && pass "le plus récent en premier" || fail "ordre ($first en premier)"

# messages refusés
LONG=$(head -c 4001 /dev/zero | tr '\0' a)
check 400 POST /guestbook --data-binary "message="      -b "$JEAN"
check 400 POST /guestbook --data-binary "message=+++"   -b "$JEAN"
check 400 POST /guestbook --data-binary "message=%FF"   -b "$JEAN"
check 400 POST /guestbook --data-binary "message=%ZZ"   -b "$JEAN"
check 400 POST /guestbook --data-binary "message=$LONG" -b "$JEAN"

# API JSON : lecture
check_type "application/json" /api/messages
check_contains '"name":"zoe","message":"ligne 1\r\nligne 2"'   /api/messages
check_contains '"message":"<script>alert(1)</script>"'         /api/messages   # JSON : pas d'échappement HTML
check_contains '"mine":true'  /api/messages -b "$JEAN"
check_absent   '"mine":true'  /api/messages                                    # anonyme : rien n'est à lui

# API JSON : écriture
check 401 POST /api/messages --data-binary "message=sans+session"
check_contains '{"error":"login required"}' /api/messages --data-binary "message=x"
check 201 POST /api/messages --data-binary "message=via+fetch" -b "$ZOE"
check_contains '"name":"zoe","message":"via fetch"' /api/messages
check_contains '"message":"Tom \"T\" \\ x"'   /api/messages --data-binary 'message=Tom+%22T%22+%5C+x' -b "$ZOE"
check 400 POST /api/messages --data-binary "message=" -b "$ZOE"
check_contains '{"error":"message is required' /api/messages --data-binary "message=" -b "$ZOE"
check 400 POST /api/messages --data-binary "message=%FF" -b "$ZOE"

# suppression : seulement ses propres messages
ID=$(curl -s "$BASE/api/messages" | sed -n 's/.*"id":\([0-9]*\),"name":"jean","message":"premier message".*/\1/p')
check_contains '>delete</button>' /guestbook -b "$JEAN"
check_absent   '>delete</button>' /guestbook
check 403 POST /guestbook/delete --data-binary "id=$ID" -b "$ZOE"            # pas à elle
check 303 POST /guestbook/delete --data-binary "id=$ID"                      # anonyme -> /login
check_contains "premier message" /guestbook
check 303 POST /guestbook/delete --data-binary "id=$ID" -b "$JEAN"
check_absent   "premier message" /guestbook
check 403 POST /guestbook/delete --data-binary "id=$ID"  -b "$JEAN"          # déjà supprimé
check 403 POST /guestbook/delete --data-binary "id=abc"  -b "$JEAN"

# redémarrage : les messages sont toujours là
restart_server "$DBDIR/server2.log"
check_contains "<strong>zoe</strong>"     /guestbook
check_contains '<span class="user">jean' /guestbook -b "$JEAN"

cs_finish
