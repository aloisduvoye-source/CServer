# Fonctions communes aux tests des sites. Un test.sh de site commence par :
#
#   cd "$(dirname "$0")" || exit 1
#   . ../../cserver/tests/lib.sh
#   cs_start 8082        # compile le site, le lance sur ce port, avec une base jetable
#   ...des check...
#   cs_finish
#
# Le port peut être changé : PORT=9000 ./test.sh

# compile le site du dossier courant et lance un serveur de test
cs_start() {
	PORT=${PORT:-$1}
	BASE="localhost:$PORT"
	make -s -C ../.. SITE="$(basename "$PWD")" || exit 1

	DBDIR=$(mktemp -d)
	TMP=$(mktemp)
	trap 'kill $PID 2>/dev/null; rm -rf "$TMP" "$DBDIR"' EXIT
	FAIL=0
	start_server "$DBDIR/server.log"
}

start_server() { # start_server <fichier de log>
	CSERVER_DB="$DBDIR/test.db" CSERVER_PORT=$PORT ./server > "$1" 2>&1 &
	PID=$!
	for i in $(seq 50); do
		curl -s -o /dev/null "$BASE/" && break
		sleep 0.1
	done
	if ! kill -0 $PID 2>/dev/null; then
		echo "le serveur n'a pas démarré (port $PORT déjà pris ?)"
		exit 1
	fi
}

# arrête le serveur et le relance sur la même base
restart_server() { # restart_server <fichier de log>
	kill $PID
	wait $PID 2>/dev/null
	start_server "$1"
}

cs_finish() {
	if [ $FAIL -eq 0 ]; then echo "== tout est ok"; else echo "== des tests ont échoué"; fi
	exit $FAIL
}

# pour les vérifications écrites à la main
pass() { printf 'ok     %s\n' "$*"; }
fail() { printf 'ECHEC  %s\n' "$*"; FAIL=1; }

# méthode affichée : POST si les options curl contiennent des données
method_of() {
	for a in "$@"; do [ "$a" = "--data-binary" ] && { echo POST; return; }; done
	echo GET
}

check() { # check <code attendu> <méthode> <chemin> [options curl...]
	code=$1; method=$2; path=$3; shift 3
	got=$(curl -s --path-as-is -o /dev/null -w '%{http_code}' -X "$method" "$@" "$BASE$path")
	if [ "$got" = "$code" ]; then
		pass "$method $path -> $got"
	else
		fail "$method $path $* -> $got (attendu $code)"
	fi
}

# printf et pas echo : le echo de sh interpréterait les \n et \\ du texte
check_contains() { # check_contains <texte attendu> <chemin> [options curl...]
	text=$1; path=$2; shift 2
	method=$(method_of "$@")
	if curl -s --path-as-is "$@" "$BASE$path" | grep -qF -- "$text"; then
		pass "$method $path contient $text"
	else
		fail "$method $path ne contient pas $text"
	fi
}

check_absent() { # check_absent <texte interdit> <chemin> [options curl...]
	text=$1; path=$2; shift 2
	method=$(method_of "$@")
	if curl -s --path-as-is "$@" "$BASE$path" | grep -qF -- "$text"; then
		fail "$method $path contient $text"
	else
		pass "$method $path ne contient pas $text"
	fi
}

check_type() { # check_type <content-type attendu> <chemin>
	got=$(curl -s -o /dev/null -w '%{content_type}' "$BASE$2")
	if [ "$got" = "$1" ]; then
		pass "GET $2 -> $got"
	else
		fail "GET $2 -> $got (attendu $1)"
	fi
}
