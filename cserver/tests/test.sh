#!/bin/sh
# Tests du framework, sur un site de test minimal (cserver/tests/site/) qui utilise
# les valeurs par défaut : HTTP, sécurité, routes, modèles, JSON, base, comptes.

cd "$(dirname "$0")/site" || exit 1
. ../lib.sh
cs_start 8083 ../../..

# --------------------------------------------------------------- migrations
expected="cserver/001_comptes cserver/002_prefixe_cs site/001_notes"
got=$(sed -n 's/^migration appliquée : //p' "$DBDIR/server.log" | tr '\n' ' ' | sed 's/ $//')
[ "$got" = "$expected" ] && pass "migrations dans l'ordre : $got" || fail "migrations : '$got' (attendu '$expected')"

# --------------------------------------------------------------- fichiers statiques
check 200 GET  /hello.txt
check 200 GET  "/hello.txt?v=2"                 # la query ne fait pas partie du chemin
check 200 GET  /hello%2Etxt                     # le chemin est décodé
check_type "text/plain" /hello.txt
check_type "text/css"   /style.css
check 404 GET  /nope.txt
check_contains "<h1>404 Not Found</h1>" /nope.txt   # page 404 par défaut
check 405 POST /hello.txt

# --------------------------------------------------------------- sécurité
check 404 GET  /../secret.txt                   # secret.txt est juste au-dessus de www/
check 404 GET  /%2e%2e/secret.txt
check 400 GET  /%ZZ
check 400 GET  /a%00b

# --------------------------------------------------------------- requêtes invalides
check 400 POST /echo -H "Content-Length: abc"
check 400 POST /echo -H "Content-Length: -5"
head -c 2000000 /dev/zero > "$TMP"
check 413 POST /echo --data-binary @"$TMP"       # corps > 1 Mo
check 431 GET  /echo -H "X-Gros: $(head -c 9000 /dev/zero | tr '\0' a)"   # en-têtes > 8 Ko

# --------------------------------------------------------------- query et formulaires
check_contains "Zoé & 1+1=2" "/echo?q=Zo%C3%A9+%26+1%2B1%3D2"
check_contains "Tom&Jerry"   "/echo?x=1&q=Tom%26Jerry"
check_contains "(absent)"    "/echo?q=%ZZ"
check_contains "(absent)"    "/echo"
check_contains "Zoé & 1+1=2" /echo --data-binary "q=Zo%C3%A9+%26+1%2B1%3D2"
BIG=$(head -c 100000 /dev/zero | tr '\0' a)
check_contains "gros corps"  /echo --data-binary "x=$BIG&q=gros+corps"      # 100 Ko : plusieurs read()

# --------------------------------------------------------------- routes à paramètre
check_contains "a=42 b=zé id=42" "/p/42/x/z%C3%A9"
check_contains "a=abc b=1 id=-1" "/p/abc/x/1"
check_contains "id=-1"           "/p/0/x/1"
check 404 GET "/p//x/1"
check 404 GET "/p/1/x/"
check 404 GET "/p/1/x/2/3"

# --------------------------------------------------------------- handlers mal écrits
check 500 GET /oublie                          # aucune réponse : le framework envoie une 500
check_contains "un"   /deux                     # deux réponses : seule la première part
check_absent   "deux" /deux
grep -q "/oublie : le handler n'a envoyé aucune réponse" "$DBDIR/server.log" \
	&& pass "oubli signalé dans le terminal" || fail "oubli non signalé"

# --------------------------------------------------------------- modèles
check_contains "<title>accueil · test</title>" /                # CS_PAGE + layout du framework
check_contains '<a href="/">test</a>'          /                # site_name dans la navigation
check_contains '<p id="echappe">&lt;b&gt;gras&lt;/b&gt; &amp; &quot;guillemets&quot;</p>' /vars
check_contains '<p id="brut"><i>déjà du HTML</i></p>'  /vars
check_contains '<p id="html"><i>déjà du HTML</i></p>'  /vars    # cs_set_html : brut même entre {{ }}
check_contains '<p id="nombre">42</p>'                 /vars
check_contains '<p id="absente">[]</p>'                /vars
check_contains '<p id="sections">[plein][][vide][]</p>' /vars
check_contains '<p id="site">test</p>'                 /vars
check_contains '<p id="user">[]</p>'                   /vars
check_absent   "commentaire"                           /vars
page=$(curl -s "$BASE/vars" | tr '\n' '|')
case $page in
	*'[]</p>|<p id="ligne-seule">'*'</p>|<p id="fin">'*) pass "balises seules sur leur ligne : pas de ligne vide" ;;
	*) fail "lignes vides autour des balises de section" ;;
esac
check_contains "<h1>modèle en sous-dossier</h1>" /sous         # templates/sous/dossier/page.html
check_contains "color: navy" /css/theme/bleu.css               # www/css/theme/bleu.css
check 500 GET /casse                           # section jamais fermée
check 500 GET /absent                          # modèle introuvable
grep -q "modèle casse.html : {{#jamais}} sans {{/jamais}}" "$DBDIR/server.log" \
	&& pass "erreur de modèle signalée dans le terminal" || fail "erreur de modèle non signalée"

# --------------------------------------------------------------- JSON
check_type "application/json" "/json?q=x"
check_contains '{"q":"a\"b\\c\nd"}' "/json?q=a%22b%5Cc%0Ad"
check 400 GET  /json
check_contains '{"error":"q manquant"}' /json

# --------------------------------------------------------------- base de données
# 20 écritures en parallèle par 20 processus : aucune ne doit se perdre
pids=""
for i in $(seq 20); do
	curl -s -o /dev/null --data-binary "texte=note+$i" "$BASE/notes" &
	pids="$pids $!"
done
wait $pids
[ "$(curl -s "$BASE/notes")" = "20" ] && pass "20 écritures parallèles enregistrées" || fail "écritures parallèles perdues"

# --------------------------------------------------------------- comptes : inscription
JEAN="$DBDIR/jean.cookies"
ZOE="$DBDIR/zoe.cookies"
check 200 GET  /inscription
check 200 GET  /connexion
check_contains "<title>log in · test</title>" /connexion           # login.html du framework
check 303 POST /inscription --data-binary "name=jean&password=motdepasse1" -c "$JEAN"
check 409 POST /inscription --data-binary "name=Jean&password=autrechose1"        # pris (casse ignorée)
check 400 POST /inscription --data-binary "name=ab&password=motdepasse1"          # trop court
check 400 POST /inscription --data-binary "name=jean+d&password=motdepasse1"      # espace interdit
check 400 POST /inscription --data-binary "name=%3Cb%3E&password=motdepasse1"     # < > interdits
check 400 POST /inscription --data-binary "name=zoe&password=court"               # mot de passe trop court
check 400 POST /inscription --data-binary "name=zoe&password=neufchars"           # 9 : password_min = 10
check 400 POST /inscription --data-binary "name=nomtroplong13&password=motdepasse1"   # 13 : username_max = 12
check_contains "password: 10 to 128 characters" /inscription --data-binary "name=zoe&password=court"
check_contains "name: 3 to 12 letters"          /inscription --data-binary "name=ab&password=motdepasse1"
check_contains 'minlength="10"'                 /inscription            # la règle dans le formulaire
check_contains 'action="/connexion"'            /connexion              # login_path dans login.html
check_contains 'value="Jean"' /inscription --data-binary "name=Jean&password=autrechose1"   # nom réaffiché
check 303 POST /inscription --data-binary "name=zoe&password=motdepasse2" -c "$ZOE"
check_contains "Set-Cookie: test_session="  /inscription --data-binary "name=max&password=motdepasse3" -D -
check_contains "HttpOnly; SameSite=Lax"     /inscription --data-binary "name=lea&password=motdepasse4" -D -
check_contains "Location: /prive"           /inscription --data-binary "name=tom&password=motdepasse5" -D -   # after_login

# --------------------------------------------------------------- comptes : session
check_contains "bonjour jean" /prive -b "$JEAN"
check_contains "bonjour zoe"  /prive -b "$ZOE"
check 303 GET  /prive                                          # anonyme
check_contains "Location: /connexion" /prive -D -
check_contains '<p id="user">[jean]</p>'  /vars -b "$JEAN"     # variable user des modèles
check_contains '<span class="user">jean'  /vars -b "$JEAN"     # layout du framework
TOKEN=$(awk '$6 == "test_session" { print $7 }' "$JEAN")
check_contains "bonjour jean" /prive -H "Cookie: autre=1; test_session=$TOKEN; x=y"
check 303 GET /prive -H "Cookie: test_session=0000000000000000000000000000000000000000000000000000000000000000"

# --------------------------------------------------------------- comptes : déconnexion
# le cookie est effacé ET la session supprimée côté serveur
cp "$JEAN" "$DBDIR/jean.old"
check 303 POST /logout -b "$JEAN" -c "$JEAN"
check 303 GET  /prive  -b "$JEAN"
check 303 GET  /prive  -b "$DBDIR/jean.old"                     # ancien cookie copié

# --------------------------------------------------------------- comptes : connexion
check 303 POST /connexion --data-binary "name=jean&password=motdepasse1" -c "$JEAN"
check_contains "bonjour jean" /prive -b "$JEAN"
check 303 POST /connexion --data-binary "name=JEAN&password=motdepasse1"          # casse ignorée

# connexion par API : écrite par le site de test avec les briques publiques
API="$DBDIR/api.cookies"
check_contains '{"user":"jean"}'  /api/login --data-binary "name=jean&password=motdepasse1" -c "$API"
check_contains "bonjour jean"     /prive -b "$API"
check_contains '{"ok":true}'      /api/logout --data-binary "" -b "$API" -c "$API"
check 303 GET /prive -b "$API"

# login_attempts = 3 échecs par IP, puis blocage
check 401 POST /connexion --data-binary "name=jean&password=mauvais"                                   # 1
check_contains "invalid name or password" /connexion --data-binary "name=inconnu&password=motdepasse1" # 2
check_contains "invalid name or password" /connexion --data-binary "name=jean&password=mauvais"        # 3
check 429 POST /connexion --data-binary "name=jean&password=motdepasse1"       # bloqué, même avec le bon
check_contains "try again in 10 minutes" /connexion --data-binary "name=jean&password=motdepasse1"    # login_window
check_contains '{"error":"too many attempts"}' /api/login --data-binary "name=jean&password=motdepasse1"

# briques : en-têtes et mots de passe
check_contains "X-Test: oui" /entete -D -
check_absent   "X-Injecte"   /entete -D -
grep -q "cs_header : en-tête refusé" "$DBDIR/server.log" && pass "injection d'en-tête refusée" || fail "injection d'en-tête acceptée"
check_contains "hash ok" "/hash?p=secret"

# --------------------------------------------------------------- HTTP/1.1

# une requête brute, octet par octet (ce que curl ne sait pas envoyer) ; elle doit
# finir par fermer la connexion (Connection: close, HTTP/1.0, ou une erreur)
command -v nc > /dev/null || fail "nc est nécessaire pour les tests HTTP/1.1 bruts"
raw() { printf "$1" | timeout 5 nc localhost "$PORT"; }
raw_has() { # raw_has <requête> <texte attendu> <description>
	if raw "$1" | grep -qF -- "$2"; then pass "$3"; else fail "$3 (attendu : $2)"; fi
}
raw_count() { # raw_count <requête> <texte> <nombre attendu> <description>
	n=$(raw "$1" | grep -cF -- "$2")
	[ "$n" -eq "$3" ] && pass "$4" || fail "$4 ($n au lieu de $3)"
}

# connexion réutilisée : curl envoie les deux requêtes sur la même connexion
n=$(curl -s -o /dev/null -o /dev/null -w '%{num_connects} ' "$BASE/hello.txt" "$BASE/style.css")
[ "$n" = "1 0 " ] && pass "keep-alive : 2 requêtes, 1 seule connexion" || fail "keep-alive : connexions ouvertes '$n' (attendu '1 0 ')"
check_contains "Connection: keep-alive" /hello.txt -D -

# deux requêtes envoyées d'un coup : la seconde n'est pas perdue
raw_count 'GET /hello.txt HTTP/1.1\r\nHost: x\r\n\r\nGET /echo?q=deuxieme HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' \
	"HTTP/1.1 200" 2 "requêtes enchaînées : 2 réponses"
raw_has 'GET /hello.txt HTTP/1.1\r\nHost: x\r\n\r\nGET /echo?q=deuxieme HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' \
	"deuxieme" "requêtes enchaînées : la seconde est bien lue"

# en-têtes et versions
raw_has 'GET /hello.txt HTTP/1.1\r\n\r\n'              "HTTP/1.1 400"   "HTTP/1.1 sans Host : 400"
raw_has 'GET /hello.txt HTTP/1.0\r\n\r\n'              "Connection: close" "HTTP/1.0 : connexion fermée après la réponse"
raw_has 'GET /hello.txt HTTP/2.0\r\nHost: x\r\n\r\n'   "HTTP/1.1 505"   "version inconnue : 505"
raw_has 'GET /hello.txt\r\n\r\n'                       "HTTP/1.1 400"   "sans version : 400"

# HEAD : les en-têtes de GET, sans le corps
check_contains "Content-Length: 8" /hello.txt -I
raw_has    'HEAD /hello.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' "HTTP/1.1 200" "HEAD sur un fichier : 200"
raw_count  'HEAD /hello.txt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' "bonjour" 0 "HEAD sur un fichier : pas de corps"
raw_count  'HEAD /echo?q=xyz HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n' "xyz" 0 "HEAD sur une route : pas de corps"

# corps envoyé en morceaux (Transfer-Encoding: chunked)
check_contains "en morceaux" /echo -H "Transfer-Encoding: chunked" --data-binary "q=en+morceaux"
raw_has 'POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n2\r\nq=\r\n4;ext=1\r\nabcd\r\n0\r\nX-Fin: 1\r\n\r\n' \
	"abcd" "morceaux avec extension et en-tête final"
raw_has 'POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n' "HTTP/1.1 400" "taille de morceau invalide : 400"
raw_has 'POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFF\r\n' "HTTP/1.1 413" "morceau énorme : 413"

# Content-Length ET chunked : refusé (request smuggling)
raw_has 'POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nq=a\r\n0\r\n\r\n' \
	"HTTP/1.1 400" "Content-Length et chunked ensemble : 400"
raw_has 'POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: gzip\r\n\r\n' "HTTP/1.1 501" "Transfer-Encoding inconnu : 501"

# Expect: 100-continue : le serveur donne son accord tout de suite (curl attendait 1 s)
set -- $(curl -s -o /dev/null -w '%{http_code} %{time_total}' -H "Expect: 100-continue" --data-binary "q=attendu" "$BASE/echo")
[ "$1" = "200" ] && awk -v t="$2" 'BEGIN { exit !(t < 0.8) }' && pass "Expect: 100-continue en $2 s" || fail "Expect: 100-continue : $1 en $2 s"
check_contains "attendu" /echo -H "Expect: 100-continue" --data-binary "q=attendu"

# 405 : l'en-tête Allow dit quelles méthodes sont acceptées
check_contains "Allow: GET, HEAD, POST" /echo      -X DELETE -D -
check_contains "Allow: GET, HEAD"       /hello.txt -X DELETE -D -

# --------------------------------------------------------------- limites et délais
# (le site de test : read_timeout = 2 s, request_timeout = 3 s, max_connections = 4)

# code et durée d'une requête : "200 0.012"
timed() { curl -s -o /dev/null -w '%{http_code} %{time_total}' "$@"; }
between() { awk -v t="$1" -v a="$2" -v b="$3" 'BEGIN { exit !(t >= a && t <= b) }'; }

# un corps envoyé goutte à goutte (20 octets/s pour 200 octets : 10 s) : chaque read()
# reçoit quelque chose, mais la requête entière doit arriver en 2 s -> 408
head -c 200 /dev/zero | tr '\0' a > "$TMP"
set -- $(timed --limit-rate 20 --data-binary @"$TMP" "$BASE/echo")
[ "$1" = "408" ] && between "$2" 1.5 4 && pass "envoi goutte à goutte coupé : $1 en $2 s" || fail "envoi goutte à goutte : $1 en $2 s (attendu 408 vers 2 s)"

# un handler trop long : arrêté au bout de request_timeout, le reste continue
set -- $(timed "$BASE/lent")
[ "$1" = "000" ] && between "$2" 2.5 5 && pass "handler trop long arrêté en $2 s" || fail "handler trop long : $1 en $2 s (attendu coupé vers 3 s)"
grep -q "requête trop longue" "$DBDIR/server.log" && pass "arrêt signalé dans le terminal" || fail "arrêt non signalé"
check 200 GET /hello.txt

# 4 connexions occupées : la 5e attend qu'une place se libère (vers 3 s), sans être refusée
pids=""
for i in 1 2 3 4; do
	curl -s -o /dev/null "$BASE/lent" &
	pids="$pids $!"
done
sleep 0.5
set -- $(timed "$BASE/hello.txt")
[ "$1" = "200" ] && between "$2" 2 5 && pass "5e connexion servie après une attente de $2 s" || fail "5e connexion : $1 en $2 s (attendu 200 après ~2,5 s)"
wait $pids
grep -q "limite de 4 connexions atteinte" "$DBDIR/server.log" && pass "limite signalée dans le terminal" || fail "limite non signalée"
set -- $(timed "$BASE/hello.txt")
[ "$1" = "200" ] && between "$2" 0 1 && pass "ensuite, plus d'attente ($2 s)" || fail "après : $1 en $2 s"

# les enfants terminés sont bien récupérés : aucun zombie
sleep 0.5
z=$(ps -o stat= --ppid $PID | grep -c '^Z')
[ "$z" -eq 0 ] && pass "aucun processus zombie" || fail "$z processus zombie(s)"

# --------------------------------------------------------------- redémarrage
restart_server "$DBDIR/server2.log"
n=$(grep -c "^migration appliquée" "$DBDIR/server2.log")
[ "$n" -eq 0 ] && pass "redémarrage : aucune migration rejouée" || fail "redémarrage : $n migration(s) rejouée(s)"
check_contains "bonjour jean" /prive -b "$JEAN"                 # la session a survécu
[ "$(curl -s "$BASE/notes")" = "20" ] && pass "redémarrage : données conservées" || fail "redémarrage : données perdues"

cs_finish
