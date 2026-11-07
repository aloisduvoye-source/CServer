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
check 200 GET  /signup
check 200 GET  /login
check_contains "<title>log in · test</title>" /login           # login.html du framework
check 303 POST /signup --data-binary "name=jean&password=motdepasse1" -c "$JEAN"
check 409 POST /signup --data-binary "name=Jean&password=autrechose1"        # pris (casse ignorée)
check 400 POST /signup --data-binary "name=ab&password=motdepasse1"          # trop court
check 400 POST /signup --data-binary "name=jean+d&password=motdepasse1"      # espace interdit
check 400 POST /signup --data-binary "name=%3Cb%3E&password=motdepasse1"     # < > interdits
check 400 POST /signup --data-binary "name=zoe&password=court"               # mot de passe trop court
check_contains 'value="Jean"' /signup --data-binary "name=Jean&password=autrechose1"   # nom réaffiché
check 303 POST /signup --data-binary "name=zoe&password=motdepasse2" -c "$ZOE"
check_contains "Set-Cookie: test_session="  /signup --data-binary "name=max&password=motdepasse3" -D -
check_contains "HttpOnly; SameSite=Lax"     /signup --data-binary "name=lea&password=motdepasse4" -D -
check_contains "Location: /prive"           /signup --data-binary "name=tom&password=motdepasse5" -D -   # after_login

# --------------------------------------------------------------- comptes : session
check_contains "bonjour jean" /prive -b "$JEAN"
check_contains "bonjour zoe"  /prive -b "$ZOE"
check 303 GET  /prive                                          # anonyme
check_contains "Location: /login" /prive -D -
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
check 303 POST /login --data-binary "name=jean&password=motdepasse1" -c "$JEAN"
check_contains "bonjour jean" /prive -b "$JEAN"
check 303 POST /login --data-binary "name=JEAN&password=motdepasse1"          # casse ignorée

# connexion par API : écrite par le site de test avec les briques publiques
API="$DBDIR/api.cookies"
check_contains '{"user":"jean"}'  /api/login --data-binary "name=jean&password=motdepasse1" -c "$API"
check_contains "bonjour jean"     /prive -b "$API"
check_contains '{"ok":true}'      /api/logout --data-binary "" -b "$API" -c "$API"
check 303 GET /prive -b "$API"

# 5 échecs par IP en 15 minutes, puis blocage
check 401 POST /login --data-binary "name=jean&password=mauvais"                                   # 1
check_contains "invalid name or password" /login --data-binary "name=inconnu&password=motdepasse1" # 2
check_contains "invalid name or password" /login --data-binary "name=jean&password=mauvais"        # 3
check 401 POST /login --data-binary "name=jean&password=mauvais"                                   # 4
check 401 POST /login --data-binary "name=jean&password=mauvais"                                   # 5
check 429 POST /login --data-binary "name=jean&password=motdepasse1"       # bloqué, même avec le bon
check_contains '{"error":"too many attempts"}' /api/login --data-binary "name=jean&password=motdepasse1"

# briques : en-têtes et mots de passe
check_contains "X-Test: oui" /entete -D -
check_absent   "X-Injecte"   /entete -D -
grep -q "cs_header : en-tête refusé" "$DBDIR/server.log" && pass "injection d'en-tête refusée" || fail "injection d'en-tête acceptée"
check_contains "hash ok" "/hash?p=secret"

# --------------------------------------------------------------- redémarrage
restart_server "$DBDIR/server2.log"
n=$(grep -c "^migration appliquée" "$DBDIR/server2.log")
[ "$n" -eq 0 ] && pass "redémarrage : aucune migration rejouée" || fail "redémarrage : $n migration(s) rejouée(s)"
check_contains "bonjour jean" /prive -b "$JEAN"                 # la session a survécu
[ "$(curl -s "$BASE/notes")" = "20" ] && pass "redémarrage : données conservées" || fail "redémarrage : données perdues"

cs_finish
