#!/bin/sh
# Compile, lance un serveur de test, vérifie ses réponses, puis l'arrête.
# Port 8081 par défaut : le serveur de dev peut continuer de tourner sur 8080.

cd "$(dirname "$0")" || exit 1   # le serveur cherche www/ dans le dossier courant
PORT=${PORT:-8081}
BASE="localhost:$PORT"

make -s -C ../.. || exit 1

# base jetable : les tests ne touchent pas à data/site.db
DBDIR=$(mktemp -d)
CSERVER_DB="$DBDIR/test.db" CSERVER_PORT=$PORT ./server > /dev/null &
PID=$!
TMP=$(mktemp)
trap 'kill $PID 2>/dev/null; rm -rf "$TMP" "$DBDIR"' EXIT

for i in $(seq 50); do
	curl -s -o /dev/null "$BASE/" && break
	sleep 0.1
done
if ! kill -0 $PID 2>/dev/null; then
	echo "le serveur n'a pas démarré (port $PORT déjà pris ?)"
	exit 1
fi

FAIL=0

# méthode affichée : POST si les options curl contiennent des données
method_of() {
	for a in "$@"; do [ "$a" = "--data-binary" ] && { echo POST; return; }; done
	echo GET
}

check() { # check <code attendu> <méthode> <chemin> [options curl...]
	code=$1; method=$2; path=$3; shift 3
	got=$(curl -s --path-as-is -o /dev/null -w '%{http_code}' -X "$method" "$@" "$BASE$path")
	if [ "$got" = "$code" ]; then
		echo "ok     $method $path -> $got"
	else
		echo "ECHEC  $method $path $* -> $got (attendu $code)"
		FAIL=1
	fi
}

# printf et pas echo : le echo de sh interpréterait les \n et \\ du texte
check_contains() { # check_contains <texte attendu> <chemin> [options curl...]
	text=$1; path=$2; shift 2
	method=$(method_of "$@")
	if curl -s --path-as-is "$@" "$BASE$path" | grep -qF -- "$text"; then
		printf 'ok     %s %s contient %s\n' "$method" "$path" "$text"
	else
		printf 'ECHEC  %s %s ne contient pas %s\n' "$method" "$path" "$text"
		FAIL=1
	fi
}

check_absent() { # check_absent <texte interdit> <chemin> [options curl...]
	text=$1; path=$2; shift 2
	method=$(method_of "$@")
	if curl -s --path-as-is "$@" "$BASE$path" | grep -qF -- "$text"; then
		printf 'ECHEC  %s %s contient %s\n' "$method" "$path" "$text"
		FAIL=1
	else
		printf 'ok     %s %s ne contient pas %s\n' "$method" "$path" "$text"
	fi
}

check_type() { # check_type <content-type attendu> <chemin>
	got=$(curl -s -o /dev/null -w '%{content_type}' "$BASE$2")
	if [ "$got" = "$1" ]; then
		echo "ok     GET $2 -> $got"
	else
		echo "ECHEC  GET $2 -> $got (attendu $1)"
		FAIL=1
	fi
}

# fichiers statiques
check 200 GET  /
check 200 GET  /about.html
check 200 GET  /image.webp
check 200 GET  "/index.html?v=2"
check 200 GET  /index%2Ehtml
check 404 GET  /nope.html
check_contains "404 dumbass" /nope.html                            # la page 404 du site
check 405 POST /index.html
check 200 GET  /style.css
check 200 GET  /guestbook.js
check_type "text/css"        /style.css
check_type "text/javascript" /guestbook.js
check_contains 'href="/style.css"'   /about.html
check_contains 'src="/guestbook.js"' /guestbook

# sécurité : test.sh existe juste au-dessus de www/, il ne doit pas être servi
check 404 GET  /../test.sh
check 404 GET  /%2e%2e/test.sh
check 400 GET  /%ZZ
check 400 GET  /a%00b

# requêtes invalides (refusées avant même d'arriver à une route)
check 400 POST /api/messages -H "Content-Length: abc"
check 400 POST /api/messages -H "Content-Length: -5"
head -c 2000000 /dev/zero > "$TMP"
check 413 POST /api/messages --data-binary @"$TMP"                # > 1 Mo

# comptes : inscription (curl garde les cookies dans un fichier, comme un navigateur)
JEAN="$DBDIR/jean.cookies"
ZOE="$DBDIR/zoe.cookies"
check 200 GET  /signup
check 200 GET  /login
check 303 POST /signup --data-binary "name=jean&password=motdepasse1" -c "$JEAN"
check 409 POST /signup --data-binary "name=Jean&password=autrechose1"        # pris (casse ignorée)
check 400 POST /signup --data-binary "name=ab&password=motdepasse1"          # trop court
check 400 POST /signup --data-binary "name=jean+d&password=motdepasse1"      # espace interdit
check 400 POST /signup --data-binary "name=%3Cb%3E&password=motdepasse1"     # < > interdits
check 400 POST /signup --data-binary "name=zoe&password=court"               # mot de passe trop court
check_contains 'value="Jean"' /signup --data-binary "name=Jean&password=autrechose1"   # nom réaffiché
check 303 POST /signup --data-binary "name=zoe&password=motdepasse2" -c "$ZOE"
check_contains "HttpOnly; SameSite=Lax" /signup --data-binary "name=max&password=motdepasse3" -D -

# comptes : la session est reconnue
check_contains '<span class="user">jean'  /guestbook -b "$JEAN"
check_contains '<span class="user">zoe'   /guestbook -b "$ZOE"
check_contains '<a href="/login">log in</a> or' /guestbook             # visiteur anonyme
check_absent   'id="guestbook-form"'      /guestbook
check_contains 'id="guestbook-form"'      /guestbook -b "$JEAN"
TOKEN=$(awk '$6 == "cserver_session" { print $7 }' "$JEAN")
check_contains '<span class="user">jean'  /guestbook -H "Cookie: autre=1; cserver_session=$TOKEN; x=y"
check_contains '<span class="user"><a href="/login">' /guestbook -H "Cookie: cserver_session=0000000000000000000000000000000000000000000000000000000000000000"

# livre d'or : poster (connecté seulement)
check 303 POST /guestbook --data-binary "message=premier+message" -b "$JEAN"
check 303 POST /guestbook --data-binary "message=ligne+1%0D%0Aligne+2" -b "$ZOE"
check 303 POST /guestbook --data-binary "message=anonyme"                    # -> /login
check_contains "<strong>jean</strong>"  /guestbook
check_contains "<strong>zoe</strong>"   /guestbook
check_contains "premier message"        /guestbook
check_absent   "anonyme"                /guestbook
check_absent   "No messages yet"        /guestbook

# livre d'or : échappement HTML
check 303 POST /guestbook --data-binary "message=%3Cscript%3Ealert(1)%3C%2Fscript%3E" -b "$JEAN"
check 303 POST /guestbook --data-binary "message=Tom+%26+%22Jerry%22+l%27ami" -b "$ZOE"
check_contains "&lt;script&gt;alert(1)&lt;/script&gt;"     /guestbook
check_contains "Tom &amp; &quot;Jerry&quot; l&#39;ami"     /guestbook
check_absent   "<script>"                                  /guestbook

# livre d'or : le plus récent en premier
first=$(curl -s "$BASE/guestbook" | grep -o 'premier message\|alert(1)' | head -1)
if [ "$first" = "alert(1)" ]; then
	echo "ok     GET /guestbook : le plus récent en premier"
else
	echo "ECHEC  GET /guestbook : ordre ($first en premier)"
	FAIL=1
fi

# livre d'or : messages refusés
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
check_contains '"message":"Zoé & 1+1=2"'       /api/messages --data-binary 'message=Zo%C3%A9+%26+1%2B1%3D2' -b "$ZOE"
BIG=$(head -c 100000 /dev/zero | tr '\0' a)
check_contains '"message":"gros corps"'        /api/messages --data-binary "x=$BIG&message=gros+corps" -b "$ZOE"   # 100 Ko : plusieurs read()
check 400 POST /api/messages --data-binary "message=" -b "$ZOE"
check_contains '{"error":"message is required' /api/messages --data-binary "message=" -b "$ZOE"
check 400 POST /api/messages --data-binary "message=%FF" -b "$ZOE"

# SQLite : 20 messages en parallèle par 20 processus, aucun ne doit se perdre
pids=""
for i in $(seq 20); do
	curl -s -o /dev/null --data-binary "message=parallele+$i" -b "$ZOE" "$BASE/api/messages" &
	pids="$pids $!"
done
wait $pids
n=$(curl -s "$BASE/api/messages" | grep -o '"message":"parallele [0-9]*"' | sort -u | wc -l)
if [ "$n" -eq 20 ]; then
	echo "ok     POST /api/messages x20 en parallèle -> 20 enregistrés"
else
	echo "ECHEC  POST /api/messages x20 en parallèle -> $n enregistrés"
	FAIL=1
fi

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

# déconnexion : le cookie est effacé ET la session supprimée côté serveur
cp "$JEAN" "$DBDIR/jean.old"
check 303 POST /logout -b "$JEAN" -c "$JEAN"
check_absent '<span class="user">jean' /guestbook -b "$JEAN"
check_absent '<span class="user">jean' /guestbook -b "$DBDIR/jean.old"       # ancien cookie copié

# connexion
check 303 POST /login --data-binary "name=jean&password=motdepasse1" -c "$JEAN"
check_contains '<span class="user">jean' /guestbook -b "$JEAN"
check 303 POST /login --data-binary "name=JEAN&password=motdepasse1"          # casse ignorée

# connexion : 5 échecs par IP en 15 minutes, puis blocage
check 401 POST /login --data-binary "name=jean&password=mauvais"                                   # 1
check_contains "invalid name or password" /login --data-binary "name=inconnu&password=motdepasse1" # 2
check_contains "invalid name or password" /login --data-binary "name=jean&password=mauvais"        # 3
check 401 POST /login --data-binary "name=jean&password=mauvais"                                   # 4
check 401 POST /login --data-binary "name=jean&password=mauvais"                                   # 5
check 429 POST /login --data-binary "name=jean&password=motdepasse1"       # bloqué, même avec le bon

exit $FAIL
