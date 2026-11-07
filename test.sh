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

check_contains() { # check_contains <texte attendu> <chemin>
	if curl -s --path-as-is "localhost:8080$2" | grep -qF -- "$1"; then
		echo "ok     GET $2 contient $1"
	else
		echo "ECHEC  GET $2 ne contient pas $1"
		FAIL=1
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

exit $FAIL
