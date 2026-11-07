#!/bin/sh
# Tests de l'exemple profils (port 8086 par défaut). L'envoi de fichiers lui-même
# (multipart, cs_image_type) est testé par cserver/tests/test.sh.

cd "$(dirname "$0")" || exit 1
. ../../cserver/tests/lib.sh
cs_start 8086

expected="cserver/001_comptes cserver/002_prefixe_cs cserver/003_limites site/001_profils"
got=$(sed -n 's/^migration appliquée : //p' "$DBDIR/server.log" | tr '\n' ' ' | sed 's/ $//')
[ "$got" = "$expected" ] && pass "migrations appliquées : $got" || fail "migrations : '$got' (attendu '$expected')"

# des images de test : un vrai PNG 1x1, et des fichiers qui n'en sont pas
F="$DBDIR/files"; mkdir -p "$F"
printf '\211\120\116\107\015\012\032\012\000\000\000\015\111\110\104\122\000\000\000\001\000\000\000\001\010\002\000\000\000\220\167\123\336\000\000\000\014\111\104\101\124\170\234\143\370\337\300\000\000\004\001\001\200\305\052\030\135\000\000\000\000\111\105\116\104\256\102\140\202' > "$F/moi.png"
printf '\377\330\377\340\000\020JFIF\000' > "$F/moi.jpg"
printf '<svg xmlns="http://www.w3.org/2000/svg"><script>alert(1)</script></svg>' > "$F/x.svg"
printf '<html><script>alert(1)</script></html>' > "$F/faux.png"
{ cat "$F/moi.png"; head -c 300000 /dev/zero; } > "$F/gros.png"
: > "$F/vide"

# pages publiques
check 200 GET /
check_contains "No members yet" /
check_contains "<title>sign up · members</title>" /signup
check_contains 'placeholder="name or email"' /login
check 404 GET /u/personne
check 303 GET /profile                                                   # -> /login

# inscription : nom, e-mail, mot de passe
JEAN="$DBDIR/jean.cookies"
ZOE="$DBDIR/zoe.cookies"
check 400 POST /signup --data-binary "name=j&email=jean@exemple.fr&password=motdepasse1"
for bad in "jean" "jean@" "@exemple.fr" "jean@exemple" "jean@exemple." "a@b@c.fr" "jean%20x@exemple.fr" "jean@exe%0Ample.fr"; do
	check 400 POST /signup --data-binary "name=jean&email=$bad&password=motdepasse1"
done
check_contains "invalid email address" /signup --data-binary "name=jean&email=jean&password=motdepasse1"
check_contains 'value="jean"' /signup --data-binary "name=jean&email=jean&password=motdepasse1"   # réaffiché
check 400 POST /signup --data-binary "name=jean&email=jean@exemple.fr&password=court"
check_contains "Location: /profile" /signup --data-binary "name=jean&email=Jean@Exemple.fr&password=motdepasse1" -c "$JEAN" -D -
check 303 POST /signup --data-binary "name=zoe&email=zoe@exemple.fr&password=motdepasse2" -c "$ZOE"
check 409 POST /signup --data-binary "name=JEAN&email=autre@exemple.fr&password=motdepasse1"
check_contains "this email is already used" /signup --data-binary "name=paul&email=jean@EXEMPLE.fr&password=motdepasse1"
# l'e-mail refusé a tout annulé : pas de compte "paul" sans profil
check 404 GET /u/paul
check_contains "2 member(s)" /

# connexion par nom ou par e-mail
L="$DBDIR/login.cookies"
check 303 POST /login --data-binary "login=jean&password=motdepasse1" -c "$L"
check 303 POST /login --data-binary "login=JEAN@exemple.fr&password=motdepasse1" -c "$L"
check_contains '<span class="user">jean' /profile -b "$L"
check 401 POST /login --data-binary "login=jean@exemple.fr&password=mauvais"
check_contains "invalid name, email or password" /login --data-binary "login=inconnu@exemple.fr&password=motdepasse1"

# l'e-mail : sur son profil seulement
check_contains 'value="Jean@Exemple.fr"' /profile -b "$JEAN"
check_absent   "exemple.fr" /u/jean
check_absent   "exemple.fr" /u/jean -b "$ZOE"
check_absent   "exemple.fr" /
check_contains "edit my profile" /u/jean -b "$JEAN"
check_absent   "edit my profile" /u/jean -b "$ZOE"
check 200 GET  /u/JEAN                                                   # nom sans casse

# changer d'e-mail
check 400 POST /profile/email --data-binary "email=pas-un-email" -b "$JEAN"
check 409 POST /profile/email --data-binary "email=ZOE@exemple.fr" -b "$JEAN"
check_contains "Location: /profile?done=email" /profile/email --data-binary "email=jean@ailleurs.fr" -b "$JEAN" -D -
check_contains "email saved" "/profile?done=email" -b "$JEAN"
check_absent   "&lt;b&gt;" "/profile?done=%3Cb%3E" -b "$JEAN"            # le message n'est pas recopié de l'URL
check_contains 'value="jean@ailleurs.fr"' /profile -b "$JEAN"
check 303 POST /profile/email --data-binary "email=%3Cb%3Ex%3C%2Fb%3E@ailleurs.fr" -b "$JEAN"
check_contains 'value="&lt;b&gt;x&lt;/b&gt;@ailleurs.fr"' /profile -b "$JEAN"   # échappé
check 303 POST /profile/email --data-binary "email=jean@ailleurs.fr" -b "$JEAN"
check 303 POST /profile/email --data-binary "email=x@ailleurs.fr"         # pas connecté : -> /login

# avatar : rien par défaut, l'initiale à la place
check_contains '<span class="avatar small">J</span>' /
check 404 GET /avatars/1
check_absent "remove my avatar" /profile -b "$JEAN"

# avatar : les envois refusés
check 400 POST /profile/avatar -F "avatar=@$F/vide;filename=" -b "$JEAN"             # aucun fichier choisi
check 400 POST /profile/avatar --data-binary "avatar=x" -b "$JEAN"                   # pas multipart
check 415 POST /profile/avatar -F "avatar=@$F/x.svg" -b "$JEAN"
check 415 POST /profile/avatar -F "avatar=@$F/faux.png;type=image/png" -b "$JEAN"   # nom et type mentent
check_contains "only PNG, JPEG, GIF or WebP" /profile/avatar -F "avatar=@$F/x.svg" -b "$JEAN"
check 413 POST /profile/avatar -F "avatar=@$F/gros.png" -b "$JEAN"
check_contains "image too large" /profile/avatar -F "avatar=@$F/gros.png" -b "$JEAN"
check 303 POST /profile/avatar -F "avatar=@$F/moi.png"                               # pas connecté
got=$(curl -s -o /dev/null -w '%{http_code}' -b "$JEAN" -F "avatar=@$F/moi.png" "$BASE/profile/avatar")
[ "$got" = 403 ] && pass "avatar sans jeton CSRF : 403" || fail "avatar sans jeton CSRF -> $got"

# avatar : un envoi réussi, jeton dans le formulaire comme le ferait le navigateur
got=$(curl -s -o /dev/null -w '%{http_code} %{redirect_url}' -b "$JEAN" \
	-F "csrf=$(csrf_of "$JEAN")" -F "avatar=@$F/moi.png" "$BASE/profile/avatar")
[ "$got" = "303 http://$BASE/profile?done=avatar" ] && pass "avatar envoyé" || fail "avatar -> $got"
check_contains '<img class="avatar small" src="/avatars/1?v=1"' /
check_contains '<img class="avatar large" src="/avatars/1?v=1"' /u/jean
check_contains "remove my avatar" /profile -b "$JEAN"
check_type "image/png" /avatars/1
curl -s "$BASE/avatars/1" -o "$F/recu"
cmp -s "$F/moi.png" "$F/recu" && pass "image rendue à l'identique" || fail "image différente"
check_contains "X-Content-Type-Options: nosniff" /avatars/1 -D -
check_contains "Content-Security-Policy: default-src 'none'" /avatars/1 -D -
check_contains "Cache-Control: public" /avatars/1 -D -
check 404 GET /avatars/2                                                 # zoé n'en a pas
check 404 GET /avatars/abc
check 404 GET /avatars/999

# une nouvelle image : nouvelle version, nouvelle URL ; le type suit le contenu
check 303 POST /profile/avatar -F "avatar=@$F/moi.jpg;filename=photo.png" -b "$JEAN"
check_contains 'src="/avatars/1?v=2"' /
check_type "image/jpeg" /avatars/1

# supprimer
check_contains "Location: /profile?done=removed" /profile/avatar/delete --data-binary "" -b "$JEAN" -D -
check 404 GET /avatars/1
check_contains '<span class="avatar large">J</span>' /u/jean
check_contains '<span class="avatar small">Z</span>' /

# déconnexion
check 303 POST /logout --data-binary "" -b "$JEAN" -c "$JEAN"
check 303 GET /profile -b "$JEAN"

cs_finish
