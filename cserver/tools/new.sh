#!/bin/sh
# Crée un projet cserver minimal, prêt à compiler :
#
#   cserver/tools/new.sh <dossier>
#
# - Si ce cserver/ se trouve déjà dans <dossier> (le dépôt cloné, un sous-module
#   git...), le projet l'utilise sur place.
# - Sinon, le dossier cserver/ est copié dans le projet.
#
# Rien n'est jamais écrasé : si un fichier du projet (main.c, templates/...) existe
# déjà, rien n'est créé ; Makefile, README.md et .gitignore déjà présents sont gardés.

set -e

if [ $# -ne 1 ]; then
	echo "usage : $0 <dossier du projet>" >&2
	exit 1
fi

CSERVER=$(cd "$(dirname "$0")/.." && pwd -P)    # le dossier cserver/ de ce script
mkdir -p "$1"
PROJECT=$(cd "$1" && pwd -P)
NAME=$(basename "$PROJECT")

# le framework : sur place s'il est dans le projet, sinon copié dans cserver/
case "$CSERVER/" in
	"$PROJECT"/*) CS_REL=${CSERVER#"$PROJECT"/}; COPY=0 ;;
	*)            CS_REL=cserver;               COPY=1 ;;
esac

# les fichiers du projet : s'il y en a déjà un, on ne touche à rien
FILES="main.c src/pages/hello.c src/pages/hello.h test.sh templates/layout.html templates/index.html templates/hello.html www/style.css migrations/001_init.sql"
[ $COPY -eq 1 ] && FILES="$FILES cserver"
for f in $FILES; do
	if [ -e "$PROJECT/$f" ]; then
		echo "$PROJECT/$f existe déjà : rien n'a été créé (new.sh n'écrase jamais rien)" >&2
		exit 1
	fi
done

# écrit l'entrée standard dans $1, seulement si le fichier n'existe pas encore
create_if_absent() {
	if [ -e "$PROJECT/$1" ]; then
		echo "gardé : $1 (déjà présent)"
		cat > /dev/null
	else
		cat > "$PROJECT/$1"
	fi
}

case $COPY in
	0)
		echo "cserver utilisé sur place : $CS_REL/" ;;
	*)
		mkdir -p "$PROJECT/cserver/tests"
		cp -R "$CSERVER/include" "$CSERVER/src" "$CSERVER/migrations" "$CSERVER/templates" \
		      "$CSERVER/tools" "$CSERVER/cserver.mk" "$CSERVER/README.md" "$PROJECT/cserver/"
		cp "$CSERVER/tests/lib.sh" "$PROJECT/cserver/tests/"
		echo "cserver copié dans $PROJECT/cserver/" ;;
esac

# un nom de cookie propre au projet : seulement des lettres, chiffres et _
COOKIE=$(printf '%s' "$NAME" | tr -c 'A-Za-z0-9_' '_')_session

mkdir -p "$PROJECT/src/pages" "$PROJECT/templates" "$PROJECT/www" "$PROJECT/migrations"

create_if_absent Makefile <<EOF
include $CS_REL/cserver.mk
EOF

# le code des pages est rangé dans src/ (à n'importe quelle profondeur) ; un
# en-tête s'inclut par son chemin depuis src/ : #include"pages/hello.h"
cat > "$PROJECT/src/pages/hello.h" <<'EOF'
#ifndef PAGES_HELLO_H
#define PAGES_HELLO_H

#include"cserver.h"

void hello(cs_ctx *c);   // GET /hello?name=...

#endif
EOF

cat > "$PROJECT/src/pages/hello.c" <<'EOF'
// un exemple de handler, rangé dans src/pages/
#include<string.h>
#include"pages/hello.h"

void hello(cs_ctx *c){
	char name[64];
	if (cs_query(c, "name", name, sizeof(name)) < 0){
		strcpy(name, "world");
	}

	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "hello");
	cs_set(&v, "name", name);          // échappé à l'affichage
	cs_page(c, 200, "hello.html", &v);
}
EOF

cat > "$PROJECT/main.c" <<EOF
#include"cserver.h"
#include"pages/hello.h"            // src/pages/hello.h

// généré par le Makefile depuis migrations/*.sql
extern const cs_migration site_migrations[];

static const cs_route routes[] = {
	CS_PAGE("/", "index.html", NULL),  // un modèle, sans code C
	CS_GET ("/hello", hello),

	// CS_AUTH_ROUTES,                 // inscription / connexion (avec .accounts = 1)
	CS_END
};

int main(void){
	cs_app app = {
		.name        = "$NAME",
		.routes      = routes,
		.migrations  = site_migrations,  // les tables de migrations/*.sql
		.cookie_name = "$COOKIE",
		// .accounts    = 1,              // comptes utilisateurs
		// .after_login = "/",
	};
	return cs_run(&app);
}
EOF

cat > "$PROJECT/templates/layout.html" <<'EOF'
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>{{#title}}{{title}} · {{/title}}{{site_name}}</title>
    <link rel="stylesheet" href="/style.css">
{{#script}}    <script src="{{script}}" defer></script>
{{/script}}</head>
<body>
    <nav><a href="/">{{site_name}}</a> <a href="/hello">hello</a>{{#accounts}}
        <span class="user">{{#user}}{{user}} <form class="inline" method="post" action="/logout"><input type="hidden" name="csrf" value="{{csrf}}"><button class="link">log out</button></form>{{/user}}{{^user}}<a href="{{login_path}}">log in</a> <a href="{{signup_path}}">sign up</a>{{/user}}</span>{{/accounts}}</nav>
    <main>
{{{content}}}
    </main>
</body>
</html>
EOF

cat > "$PROJECT/templates/index.html" <<EOF
    <h1>$NAME</h1>
    <p>Ça marche ! Cette page est templates/index.html.</p>
    <p>Essaie <a href="/hello?name=Zoé">/hello?name=Zoé</a>.</p>
EOF

cat > "$PROJECT/templates/hello.html" <<'EOF'
    <h1>Hello {{name}}!</h1>
EOF

cat > "$PROJECT/www/style.css" <<'EOF'
body { font-family: system-ui, sans-serif; max-width: 40rem; margin: 3rem auto; padding: 0 1rem; }
nav { display: flex; flex-wrap: wrap; gap: 1rem; align-items: baseline; margin-bottom: 2rem; }
nav .user { margin-left: auto; display: flex; gap: 1rem; }
form.inline { display: inline; }
button.link { background: none; border: none; padding: 0; color: inherit; text-decoration: underline; cursor: pointer; }
.error { color: #c00; }
EOF

cat > "$PROJECT/migrations/001_init.sql" <<'EOF'
-- les tables du site : un fichier par étape (001_xxx.sql, 002_yyy.sql...).
-- Une migration déjà appliquée ne se modifie jamais : on en ajoute une nouvelle.
-- Exemple :
--   CREATE TABLE notes (
--       id    INTEGER PRIMARY KEY,
--       texte TEXT NOT NULL
--   );
EOF

cat > "$PROJECT/test.sh" <<EOF
#!/bin/sh
cd "\$(dirname "\$0")" || exit 1
. $CS_REL/tests/lib.sh
cs_start 8090 .                     # compile, lance sur ce port avec une base jetable

check 200 GET /
check_contains "<title>$NAME</title>" /
check_contains "Hello world!"  /hello
check_contains "Hello Zoé!"    "/hello?name=Zo%C3%A9"
check_absent   "<script>"      "/hello?name=%3Cscript%3E"
check 200 GET /style.css
check 404 GET /nope

cs_finish
EOF
chmod +x "$PROJECT/test.sh"

create_if_absent .gitignore <<'EOF'
server
build/
data/
EOF

create_if_absent README.md <<EOF
# $NAME

Site écrit avec [cserver]($CS_REL/README.md).

    make run     # puis ouvrir http://localhost:8080
    make test
EOF

echo "projet créé : $PROJECT"
if [ "$PROJECT" = "$(pwd -P)" ]; then
	echo "  make run      # puis ouvrir http://localhost:8080"
else
	echo "  cd $1 && make run      # puis ouvrir http://localhost:8080"
fi
