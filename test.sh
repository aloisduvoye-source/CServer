#!/bin/sh
# Compile, lance le serveur, vérifie quelques réponses, puis l'arrête.

make -s || exit 1

# base jetable : les tests ne touchent pas à data/site.db
DBDIR=$(mktemp -d)
CSERVER_DB="$DBDIR/test.db" ./server > /dev/null &
PID=$!
TMP=$(mktemp)
trap 'kill $PID 2>/dev/null; rm -rf "$TMP" "$DBDIR"' EXIT

for i in $(seq 50); do
	curl -s -o /dev/null localhost:8080/ && break
	sleep 0.1
done
if ! kill -0 $PID 2>/dev/null; then
	echo "le serveur n'a pas démarré (port 8080 déjà pris ?)"
	exit 1
fi

FAIL=0
check() { # check <code attendu> <méthode> <chemin> [options curl...]
	code=$1; method=$2; path=$3; shift 3
	got=$(curl -s --path-as-is -o /dev/null -w '%{http_code}' -X "$method" "$@" "localhost:8080$path")
	if [ "$got" = "$code" ]; then
		echo "ok     $method $path $* -> $got"
	else
		echo "ECHEC  $method $path $* -> $got (attendu $code)"
		FAIL=1
	fi
}

check_contains() { # check_contains <texte attendu> <chemin> [options curl...]
	text=$1; path=$2; shift 2
	method=GET; [ $# -gt 0 ] && method=POST
	# printf et pas echo : le echo de sh interpréterait les \n et \\ du texte
	if curl -s --path-as-is "$@" "localhost:8080$path" | grep -qF -- "$text"; then
		printf 'ok     %s %s contient %s\n' "$method" "$path" "$text"
	else
		printf 'ECHEC  %s %s ne contient pas %s\n' "$method" "$path" "$text"
		FAIL=1
	fi
}

check_type() { # check_type <content-type attendu> <chemin>
	got=$(curl -s -o /dev/null -w '%{content_type}' "localhost:8080$2")
	if [ "$got" = "$1" ]; then
		echo "ok     GET $2 -> $got"
	else
		echo "ECHEC  GET $2 -> $got (attendu $1)"
		FAIL=1
	fi
}

check_absent() { # check_absent <texte interdit> <chemin>
	if curl -s --path-as-is "localhost:8080$2" | grep -qF -- "$1"; then
		echo "ECHEC  GET $2 contient $1"
		FAIL=1
	else
		echo "ok     GET $2 ne contient pas $1"
	fi
}

check_post() { # check_post <corps attendu> <chemin> <données>
	got=$(curl -s --data-binary "$3" "localhost:8080$2")
	if [ "$got" = "$1" ]; then
		echo "ok     POST $2 -> $got"
	else
		echo "ECHEC  POST $2 -> $got (attendu $1)"
		FAIL=1
	fi
}

check_body() { # check_body <corps attendu> <chemin>
	got=$(curl -s --path-as-is "localhost:8080$2")
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
check 405 POST /index.html

# sécurité
check 404 GET  /../main.c
check 404 GET  /%2e%2e/main.c
check 400 GET  /%ZZ
check 400 GET  /a%00b

# routes
check 200 GET  /api/heure
check_body "Bonjour inconnu"     "/api/bonjour"
check_body "Bonjour Jean Dupont" "/api/bonjour?nom=Jean+Dupont"
check_body "Bonjour Zoé"         "/api/bonjour?nom=Zo%C3%A9"
check_body "Bonjour Tom&Jerry"   "/api/bonjour?x=1&nom=Tom%26Jerry"
check_body "Bonjour 1+1=2"       "/api/bonjour?nom=1%2B1%3D2"
check_body "Bonjour inconnu"     "/api/bonjour?nom=%ZZ"

# POST
BIG=$(head -c 100000 /dev/zero | tr '\0' a)
check_post "Bonjour Jean Dupont" /api/bonjour "nom=Jean+Dupont"
check_post "Bonjour Zoé"         /api/bonjour "x=1&nom=Zo%C3%A9"
check_post "Bonjour Jean"        /api/bonjour "x=$BIG&nom=Jean"   # 100 Ko : plusieurs read()
check_post "Bonjour inconnu"     /api/bonjour ""
check 400 POST /api/bonjour -H "Content-Length: abc"
check 400 POST /api/bonjour -H "Content-Length: -5"
head -c 2000000 /dev/zero > "$TMP"
check 413 POST /api/bonjour --data-binary @"$TMP"                 # > 1 Mo

# échappement HTML
check_contains "<h1>Bonjour inconnu</h1>" "/bonjour"
check_contains "<h1>Bonjour Zoé</h1>"     "/bonjour?nom=Zo%C3%A9"
check_contains "<h1>Bonjour &lt;script&gt;alert(1)&lt;/script&gt;</h1>" \
               "/bonjour?nom=%3Cscript%3Ealert(1)%3C%2Fscript%3E"
check_contains "<h1>Bonjour Tom &amp; &quot;Jerry&quot; l&#39;ami</h1>" \
               "/bonjour?nom=Tom+%26+%22Jerry%22+l%27ami"

# SQLite : 20 écritures en parallèle par 20 processus, aucune ne doit se perdre
check_body "1" /api/visites
pids=""
for i in $(seq 20); do
	curl -s -o /dev/null localhost:8080/api/visites &
	pids="$pids $!"
done
wait $pids
check_body "22" /api/visites

# livre d'or
check_contains "No messages yet" /guestbook
check 303 POST /guestbook --data-binary "name=Jean&message=premier+message"
check 303 POST /guestbook --data-binary "name=Zo%C3%A9&message=ligne+1%0D%0Aligne+2"
check_contains "<strong>Jean</strong>" /guestbook
check_contains "premier message"       /guestbook
check_contains "<strong>Zoé</strong>"  /guestbook
check_absent   "No messages yet"       /guestbook

# livre d'or : XSS
check 303 POST /guestbook --data-binary "name=%3Cscript%3Ealert(1)%3C%2Fscript%3E&message=hack"
check_contains "&lt;script&gt;alert(1)&lt;/script&gt;" /guestbook
check_absent   "<script>"                              /guestbook

# livre d'or : le plus récent en premier
first=$(curl -s localhost:8080/guestbook | grep -o 'premier message\|hack' | head -1)
if [ "$first" = "hack" ]; then
	echo "ok     GET /guestbook : le plus récent en premier"
else
	echo "ECHEC  GET /guestbook : ordre ($first en premier)"
	FAIL=1
fi

# livre d'or : formulaires refusés
LONG=$(head -c 201 /dev/zero | tr '\0' a)
check 400 POST /guestbook --data-binary "name=Jean&message="
check 400 POST /guestbook --data-binary "name=+++&message=salut"
check 400 POST /guestbook --data-binary "message=salut"
check 400 POST /guestbook --data-binary "name=$LONG&message=salut"
check 400 POST /guestbook --data-binary "name=%FF&message=utf8"

# frontend
check 200 GET /style.css
check 200 GET /guestbook.js
check_type "text/css"        /style.css
check_type "text/javascript" /guestbook.js
check_contains 'href="/style.css"'   /about.html
check_contains 'src="/guestbook.js"' /guestbook

# API JSON : lecture (messages postés plus haut)
check_type "application/json" /api/messages
check_contains '"name":"Jean","message":"premier message"' /api/messages
check_contains '"message":"ligne 1\r\nligne 2"'            /api/messages
check_contains '"name":"<script>alert(1)</script>"'       /api/messages   # JSON : pas d'échappement HTML

# API JSON : écriture
check 201 POST /api/messages --data-binary "name=Api&message=via+fetch"
check_contains '"name":"Api","message":"via fetch"' /api/messages
check_contains '"name":"Tom \"T\" \\ x"' /api/messages --data-binary 'name=Tom+%22T%22+%5C+x&message=guillemets'
check 400 POST /api/messages --data-binary "name=&message=vide"
check_contains '{"error":"name is required' /api/messages --data-binary "name=&message=vide"
check 400 POST /api/messages --data-binary "name=%FF&message=utf8"

exit $FAIL
