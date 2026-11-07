#!/bin/sh
# Compile, lance le serveur, vérifie quelques réponses, puis l'arrête.

make -s || exit 1
./server > /dev/null &
PID=$!
trap 'kill $PID 2>/dev/null' EXIT

for i in $(seq 50); do
	curl -s -o /dev/null localhost:8080/ && break
	sleep 0.1
done
if ! kill -0 $PID 2>/dev/null; then
	echo "le serveur n'a pas démarré (port 8080 déjà pris ?)"
	exit 1
fi

FAIL=0
check() { # check <code attendu> <méthode> <chemin>
	got=$(curl -s --path-as-is -o /dev/null -w '%{http_code}' -X "$2" "localhost:8080$3")
	if [ "$got" = "$1" ]; then
		echo "ok     $2 $3 -> $got"
	else
		echo "ECHEC  $2 $3 -> $got (attendu $1)"
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

exit $FAIL
