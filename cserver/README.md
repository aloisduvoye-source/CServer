# cserver

Mini framework web en C : un serveur HTTP, des routes, des modèles HTML, une base SQLite avec migrations, et des comptes utilisateurs. Tout est écrit à la main ; les seules bibliothèques sont SQLite et libcrypt (pour les mots de passe).

Un site n'inclut qu'un seul fichier, [`include/cserver.h`](include/cserver.h), où chaque fonction est documentée. Ce guide en donne la vue d'ensemble.

- [Démarrage rapide](#démarrage-rapide)
- [Créer son projet](#créer-son-projet)
- [Organisation d'un site](#organisation-dun-site)
- [Routes](#routes)
- [Handlers : lire la requête, répondre](#handlers--lire-la-requête-répondre)
- [Modèles HTML](#modèles-html)
- [Base de données](#base-de-données)
- [Comptes utilisateurs](#comptes-utilisateurs)
- [Configuration](#configuration)
- [Compiler et tester](#compiler-et-tester)
- [Limites connues](#limites-connues)

## Démarrage rapide

Un site est un dossier qui contient `cserver/` et un Makefile d'une ligne (voir [Créer son projet](#créer-son-projet)). Le plus petit tient en un fichier :

```c
// main.c
#include"cserver.h"

static void hello(cs_ctx *c){
	char name[64];
	if (cs_query(c, "name", name, sizeof(name)) < 0){
		cs_error(c, 400);
		return;
	}
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "hello");
	cs_set(&v, "name", name);
	cs_page(c, 200, "hello.html", &v);
}

static const cs_route routes[] = {
	CS_GET("/hello", hello),
	CS_END
};

int main(void){
	cs_app app = { .name = "hello", .routes = routes };
	return cs_run(&app);
}
```

```html
<!-- templates/hello.html -->
<h1>Hello {{name}}!</h1>
```

```make
# Makefile
include cserver/cserver.mk
```

```sh
make run
```

http://localhost:8080/hello?name=Zoé affiche « Hello Zoé! » dans le layout par défaut du framework. `{{name}}` est échappé : `?name=<script>` s'affiche comme du texte.

## Créer son projet

**Le plus simple : cloner le dépôt**, qui sert de modèle, puis créer le projet à sa racine :

```sh
git clone https://github.com/aloisduvoye-source/CServer.git mon-site && cd mon-site
make new
make run                            # puis ouvrir http://localhost:8080
```

`make run DIR=examples/todo` lance un des exemples. Voir le [README du dépôt](../README.md) pour garder les mises à jour du framework tout en ayant son propre dépôt git.

Le générateur ([`tools/new.sh`](tools/new.sh)) crée un projet qui compile et passe ses tests tout de suite : `main.c` (une page, un handler, les comptes prêts à être décommentés), `templates/` (dont un `layout.html` à personnaliser), `www/style.css`, `migrations/`, `test.sh`, et s'ils n'existent pas déjà `Makefile`, `.gitignore` et `README.md`. Il n'écrase jamais rien : si un de ces fichiers existe, rien n'est créé.

- Lancé dans un dossier qui contient déjà ce cserver (le dépôt cloné, un sous-module), il l'**utilise sur place**.
- Lancé vers un autre dossier (`make new ../autre-site`), il **copie** le framework dans `autre-site/cserver/`.

Avec un sous-module :

  ```sh
  mkdir mon-site && cd mon-site && git init
  git submodule add https://github.com/aloisduvoye-source/CServer.git cserver-src
  sh cserver-src/cserver/tools/new.sh .
  ```

**À la main**, un projet vit dans son propre dépôt, avec `cserver` dedans. Deux façons de l'y mettre :

**Avec un sous-module git** (recommandé : chaque projet fixe la version qu'il utilise, et la met à jour quand il veut) :

```sh
mkdir mon-site && cd mon-site && git init
git submodule add https://github.com/aloisduvoye-source/CServer.git cserver-src
echo 'include cserver-src/cserver/cserver.mk' > Makefile
```

Pour récupérer une nouvelle version : `git -C cserver-src pull`, puis commiter le sous-module.

**En copiant le dossier `cserver/`** du dépôt dans le projet (le plus simple, mais les mises à jour se font à la main) :

```sh
mkdir mon-site && cd mon-site
cp -r ../chemin/vers/le/depot/cserver cserver
echo 'include cserver/cserver.mk' > Makefile
```

Dans les deux cas, le Makefile du projet tient en cette seule ligne, et `make`, `make run`, `make test` et `make clean` fonctionnent. Tout ce que `make` produit (`server`, `build/`) est créé dans le projet : le dossier de cserver n'est jamais modifié. À mettre dans le `.gitignore` du projet :

```
server
build/
data/
```

Les sites de [`examples/`](../examples/) sont des projets comme les autres : le Makefile du dépôt choisit seulement lequel compiler (`make run DIR=examples/todo`, `.` par défaut) avant d'inclure `cserver.mk`.

## Organisation d'un site

```
mon-site/
  Makefile            include cserver/cserver.mk
  cserver/            le framework (copie ou sous-module)
  main.c, *.c, *.h    le code (compilé avec la bibliothèque)
  src/                la suite du code, rangée comme on veut : src/pages/admin.c...
  templates/          modèles HTML, relus à chaque requête (sous-dossiers possibles)
  www/                fichiers publics, servis tels quels (CSS, JS, images)
  migrations/         001_xxx.sql, 002_yyy.sql... : les tables du site
  data/site.db        la base, créée automatiquement
  test.sh             ses tests
```

**Rien n'est obligatoire** : sans `www/`, aucun fichier statique ; sans `templates/`, les modèles par défaut du framework ; sans base ni comptes, pas de `data/`. Les noms des dossiers se changent dans la [configuration](#configuration).

`www/` est public : tout ce qu'il contient se télécharge tel quel. Les modèles et la base sont donc à côté, jamais dedans.

**Avec beaucoup de fichiers**, on les range dans des sous-dossiers :

| Fichiers | Sous-dossiers |
|---|---|
| `.c` et `.h` | dans `src/`, à n'importe quelle profondeur. Les en-têtes s'incluent par leur chemin depuis `src/` : `#include "pages/admin.h"` pour `src/pages/admin.h`. Deux fichiers peuvent avoir le même nom dans deux dossiers |
| `templates/` | oui : `cs_page(c, 200, "admin/users.html", &v)` charge `templates/admin/users.html` |
| `www/` | oui : `www/css/style.css` est servi à `/css/style.css` |
| `migrations/` | non, volontairement : l'ordre alphabétique des noms donne l'ordre d'application, il doit rester sans ambiguïté |

## Routes

```c
static const cs_route routes[] = {
	CS_PAGE("/",                "index.html", NULL),     // un modèle, sans code C
	CS_PAGE("/about",           "about.html", "about"),  // avec un titre
	CS_GET ("/todos",           todos_list),
	CS_POST("/todos",           todos_create),
	CS_POST("/todos/:id/done",  todos_toggle),           // :id capture un segment
	CS_AUTH_ROUTES,                                      // /signup, /login, /logout
	CS_END
};
```

- Les routes sont essayées dans l'ordre ; la première qui correspond (méthode et chemin) gagne.
- `:nom` capture un segment non vide : `/todos/:id/done` accepte `/todos/42/done`, pas `/todos//done` ni `/todos/42/done/x`.
- Sans route : un `GET` cherche un fichier dans `www/`, sinon la page 404 ; les autres méthodes reçoivent une 405.

## Handlers : lire la requête, répondre

Un handler reçoit le contexte de la requête, `cs_ctx *c`, et doit envoyer **exactement une** réponse. S'il n'en envoie aucune, le framework envoie une 500 ; une deuxième est ignorée (les deux cas s'affichent dans le terminal).

**Lire** — toutes ces fonctions décodent les `%XX` et renvoient `-1` si la valeur est absente ou trop longue pour le buffer (qui n'est alors pas une chaîne valide) :

| Fonction | Lit |
|---|---|
| `cs_query(c, "q", buf, size)` | `?q=...` dans l'URL |
| `cs_form(c, "name", buf, size)` | un champ d'un formulaire POST (classique ou `multipart/form-data`) |
| `cs_param(c, "id", buf, size)` | un segment `:id` de la route |
| `cs_param_id(c, "id")` | le même, comme entier > 0 (sinon `-1`) |
| `cs_cookie(c, "x", buf, size)` | un cookie |
| `c->req.method`, `c->req.path`, `c->req.ip` | directement |

**Répondre :**

| Fonction | Envoie |
|---|---|
| `cs_page(c, 200, "x.html", &v)` | un modèle dans le layout |
| `cs_html(c, 200, &buf)` | du HTML construit à la main (`cs_buf`) |
| `cs_json(c, 200, &buf)` | du JSON construit à la main |
| `cs_json_error(c, 400, "message")` | `{"error":"message"}` |
| `cs_send(c, 200, "text/plain", body, len)` | n'importe quoi |
| `cs_redirect(c, "/todos")` | une redirection 303 |
| `cs_error(c, 404)` | une page d'erreur minimale |

**Fichiers envoyés** — un formulaire avec `<input type="file">` doit être en `enctype="multipart/form-data"`. `cs_form` y lit les champs texte, dont le jeton CSRF, et `cs_form_file` le fichier :

```c
cs_file f;
if (cs_form_file(c, "avatar", &f) < 0){ ... }        // aucun fichier choisi
if (f.len > 256 * 1024){ ... }                         // sa taille : au site de la fixer (au plus 1 Mo, la limite d'une requête)
const char *type = cs_image_type(f.data, f.len);       // "image/png"... ou NULL
```

Tout y est choisi par le visiteur : `f.filename` peut valoir `../../etc/passwd` (ne jamais s'en servir comme chemin), et `f.type` peut mentir. `cs_image_type` reconnaît PNG, JPEG, GIF et WebP à leurs premiers octets, et refuse le reste, SVG compris (il peut contenir du JavaScript). Chaque réponse porte `X-Content-Type-Options: nosniff` : le navigateur s'en tient au type annoncé, et un faux PNG rempli de HTML n'est jamais affiché comme une page. Exemple complet : [`examples/profils`](../examples/profils/), où les avatars sont stockés dans la base.

`cs_header(c, "Cache-Control: no-store")` ajoute un en-tête à la réponse qui va partir (refusé s'il contient un retour à la ligne).

Pour construire un texte de taille inconnue, `cs_buf` s'agrandit tout seul ; `cs_buf_append_html` et `cs_buf_append_json` échappent au passage, selon le format de sortie.

## Modèles HTML

| Syntaxe | Effet |
|---|---|
| `{{nom}}` | la valeur, **échappée** (`<` devient `&lt;`) : à utiliser par défaut |
| `{{{nom}}}` | la valeur telle quelle : seulement pour du HTML déjà prêt |
| `{{#nom}}…{{/nom}}` | affiché si `nom` n'est pas vide |
| `{{^nom}}…{{/nom}}` | affiché si `nom` est vide ou absent |
| `{{! commentaire }}` | rien |

C'est un sous-ensemble de [Mustache](https://mustache.github.io/). Une balise de section ou de commentaire seule sur sa ligne ne laisse pas de ligne vide.

**Variables** : celles du handler (`cs_set`, `cs_set_html` pour du HTML déjà prêt, `cs_set_long`), plus celles toujours disponibles : `site_name`, `user` (nom de la personne connectée, ou vide), `accounts`, `login_path`, `signup_path`.

**Le layout** : `cs_page` rend le modèle, puis le place dans `layout.html` (variable `content`), avec `title` et `script` si le handler les a données. Le framework fournit `layout.html`, `login.html` et `signup.html` ; un fichier du même nom dans le `templates/` du site les remplace.

**Les boucles** se font en C : rendre un petit modèle par élément avec `cs_render`, puis passer le tout à la page.

```c
cs_buf items;
cs_buf_init(&items);
for (...){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", titre);
	cs_render(c, &items, "item.html", &v);
	cs_vars_free(&v);
}
cs_vars page;
cs_vars_init(&page);
cs_set_html(&page, "items", items.data);   // déjà du HTML : rendu par item.html
cs_buf_free(&items);
cs_page(c, 200, "list.html", &page);
```

Une erreur de modèle (fichier absent, section jamais fermée) donne une 500 et un message précis dans le terminal.

## Base de données

Un site a une base SQLite dès qu'il renseigne `db_path`, `migrations` ou `accounts`. Sinon, `cs_db()` renvoie `NULL`.

```c
sqlite3 *db = cs_db(c);          // ouverte au premier appel, fermée après la réponse
sqlite3_stmt *stmt = cs_db_prepare(db, "INSERT INTO todos (user_id, title) VALUES (?, ?);");
sqlite3_bind_int64(stmt, 1, user->id);
sqlite3_bind_text(stmt, 2, title, -1, SQLITE_STATIC);
if (cs_db_run(db, stmt) < 0){    // exécute et libère
	cs_error(c, 500);
	return;
}
```

Toujours des `?` et `sqlite3_bind_*`, jamais de `snprintf` dans une requête : c'est ce qui rend l'injection SQL impossible.

**Migrations** : un fichier par étape dans `migrations/` (`001_todos.sql`, `002_xxx.sql`...). Le Makefile les intègre au binaire. Au démarrage, le framework applique dans l'ordre celles qui ne l'ont jamais été, chacune dans une transaction (tout ou rien), et retient leur nom dans `cs_migrations`.

- Une migration déjà appliquée ne se modifie **jamais** : pour changer le schéma, on en ajoute une.
- Les tables du framework (`cs_users`, `cs_sessions`, `cs_login_failures`) existent avant celles du site : une table du site peut faire `REFERENCES cs_users(id)`.
- Si une migration échoue, rien n'est modifié et le serveur refuse de démarrer.

## Comptes utilisateurs

Activés avec `.accounts = 1`. Trois niveaux, du plus simple au plus libre :

**1. Prêts à l'emploi** : `CS_AUTH_ROUTES` dans les routes, et c'est tout. Inscription, connexion et déconnexion, avec les modèles `signup.html` et `login.html` (remplaçables).

**2. Les briques**, pour écrire ses propres pages (connexion par API, inscription sur invitation...) sans réécrire les parties délicates. Les pages du niveau 1 sont écrites uniquement avec elles :

| Fonction | Rôle |
|---|---|
| `cs_user_create(c, name, password)` | crée un compte ; l'id, `0` si le nom est pris |
| `cs_user_login(c, name, password, &user)` | vérifie ; `CS_LOGIN_OK`, `_WRONG`, `_BLOCKED` (trop d'essais) ou `_ERROR` |
| `cs_session_start(c, user_id)` | ajoute le cookie de session à la prochaine réponse |
| `cs_session_end(c)` | supprime la session et efface le cookie |
| `cs_password_hash` / `cs_password_verify` | yescrypt, comparaison en temps constant |

```c
// une connexion par API, qui répond en JSON
static void api_login(cs_ctx *c){
	char name[64], password[129];
	cs_user user;
	int r = cs_user_login(c, cs_form(c, "name", name, sizeof(name)) == 0 ? name : NULL,
	                         cs_form(c, "password", password, sizeof(password)) == 0 ? password : NULL, &user);
	if (r == CS_LOGIN_OK && cs_session_start(c, user.id) == 0){
		cs_buf b;
		cs_buf_init(&b);
		cs_buf_append(&b, "{\"user\":");
		cs_buf_append_json(&b, user.name);
		cs_buf_append(&b, "}");
		cs_json(c, 200, &b);
	}
	else {
		cs_json_error(c, r == CS_LOGIN_BLOCKED ? 429 : 401, "login failed");
	}
}
```

**3. Rien** : sans `.accounts`, ni tables ni cookie ; `cs_current_user` renvoie toujours `NULL`.

Dans un handler, `cs_current_user(c)` donne la personne connectée (ou `NULL`), et `cs_require_user(c)` redirige en plus vers la page de connexion :

```c
const cs_user *user = cs_require_user(c);
if (user == NULL){return;}        // la redirection est déjà partie
```

Ce qui est garanti, quel que soit le niveau :

- mots de passe hachés avec yescrypt et un sel aléatoire, jamais stockés en clair ;
- même message et même durée que le nom ou le mot de passe soit faux : on ne peut pas deviner quels comptes existent ;
- après `login_attempts` échecs depuis une IP, refus sans vérifier ;
- cookie `HttpOnly` (illisible par le JavaScript) et `SameSite=Lax` (pas envoyé par les formulaires d'autres sites) ;
- **jeton CSRF** : tout `POST` qui porte un cookie de session doit renvoyer le jeton du navigateur, sinon le framework répond 403 avant d'appeler le handler. Dans un modèle, chaque formulaire `POST` contient `<input type="hidden" name="csrf" value="{{csrf}}">` ; en JavaScript, on l'envoie dans l'en-tête `X-CSRF-Token`. Un autre site peut faire envoyer un formulaire avec tes cookies, pas lire ce jeton ;
- au plus `signup_limit` inscriptions par IP, et `cs_rate_limit` pour limiter n'importe quelle action (un message, une tâche...) ;
- jeton de session de 256 bits tiré par `getrandom()`, supprimé côté serveur à la déconnexion.

## Configuration

Tout ce qui est propre à un site est dans `cs_app`. Un champ laissé à 0 ou `NULL` prend sa valeur par défaut : `cs_app app = { 0 };` est un site valide.

| Champ | Défaut | Rôle |
|---|---|---|
| `name` | `"cserver"` | nom du site (titre des pages, `site_name`) |
| `port` | `8080` | port d'écoute |
| `host` | toutes | adresse d'écoute : `"127.0.0.1"` pour n'être joignable que depuis la machine (derrière un proxy) |
| `trusted_proxy` | aucun | adresses du proxy, ex : `"127.0.0.1"` ou `"10.0.0.0/8, 127.0.0.1"` (voir [Mise en ligne](#mise-en-ligne)) |
| `user` | aucun | lancé en root (pour le port 80...) : l'utilisateur sans droits à prendre une fois le port ouvert |
| `routes` | aucune | table terminée par `CS_END` |
| `not_found` | page minimale | handler de la page 404 |
| `max_connections` | `64` | connexions traitées en même temps ; au-delà, les suivantes attendent une place |
| `read_timeout` | `10` | secondes pour recevoir **toute** la requête, et pour chaque écriture vers le client |
| `request_timeout` | `30` | secondes au total, handler compris ; au-delà, le processus est arrêté |
| `keepalive_timeout` | `5` | secondes d'attente de la requête suivante sur une connexion gardée ouverte |
| `static_dir` | `"www"` | fichiers publics |
| `templates_dir` | `"templates"` | modèles HTML |
| `db_path` | `"data/site.db"` | base SQLite (si le site en a une) |
| `migrations` | aucune | `site_migrations`, généré depuis `migrations/*.sql` |
| `accounts` | `0` | `1` pour activer les comptes |
| `cookie_name` | `"cs_session"` | à changer : sur localhost, tous les projets partagent les cookies |
| `after_login` | `"/"` | page après connexion, inscription et déconnexion |
| `username_min`, `username_max` | `3`, `20` | longueur d'un nom (au plus 31) |
| `password_min` | `8` | longueur minimale d'un mot de passe |
| `login_attempts`, `login_window` | `5`, `900` | échecs autorisés par IP, sur cette durée (secondes) |
| `signup_limit`, `signup_window` | `5`, `3600` | inscriptions par IP, sur cette durée (secondes) |
| `login_path`, `signup_path` | `"/login"`, `"/signup"` | si on les change, déclarer les routes de comptes à la main |

Des variables d'environnement l'emportent sur la configuration : `CSERVER_PORT`, `CSERVER_HOST`, `CSERVER_DB`, `CSERVER_USER` et `CSERVER_PROXY`. Les tests s'en servent pour tourner sur un autre port, avec une base jetable.

Une configuration incohérente est refusée au démarrage, avec un message : `CS_AUTH_ROUTES` sans `.accounts = 1`, des routes de comptes qui ne correspondent pas à `login_path`, des longueurs impossibles.

## Compiler et tester

Dans le dossier du projet :

| Commande | Effet |
|---|---|
| `make` | compile la bibliothèque et le site |
| `make run` | le lance (depuis son dossier, pour trouver `www/` et `data/`) |
| `make test` | lance `test.sh` |
| `make clean` | supprime tout ce qui est généré (`server`, `build/`) |

Chaque `.c` du site est compilé à part (dans `build/obj/`), et le compilateur retient quels `.h` il inclut : après une modification, seuls les fichiers concernés sont recompilés.

Le framework est compilé en `libcserver.a` (dans `build/cserver/`), avec ses migrations et ses modèles par défaut intégrés. Seuls les noms `cs_*` en sortent : un site peut avoir ses propres fonctions `route()` ou `db_open()` sans conflit.

`cserver.mk` accepte quelques variables, à définir avant l'`include` : `SITE_DIR` (le dossier du site, si le Makefile est ailleurs), `CS_BUILD` (où compiler la bibliothèque), `CFLAGS`.

**Tests d'un site** : un `test.sh` qui inclut [`tests/lib.sh`](tests/lib.sh) :

```sh
#!/bin/sh
cd "$(dirname "$0")" || exit 1
. cserver/tests/lib.sh                   # le chemin vers lib.sh
cs_start 8085 .                          # compile (Makefile dans "."), lance sur ce
                                         # port, avec une base jetable

check 200 GET "/hello?name=Zo%C3%A9"
check 400 GET /hello                                        # sans name
check_contains "Hello Zoé!"           "/hello?name=Zo%C3%A9"
check_contains "&lt;script&gt;"       "/hello?name=%3Cscript%3E"
check_absent   "<script>"             "/hello?name=%3Cscript%3E"

cs_finish
```

Le framework a ses propres tests ([`tests/test.sh`](tests/test.sh), et [`tests/minimal/`](tests/minimal/) pour un site sans base ni comptes). Dans le dépôt, `make test-all` les lance, puis ceux de chaque exemple.

**Fuzzing** : `make fuzz` (dans le dépôt, avec clang) envoie des millions d'entrées aléatoires ou abîmées aux parties qui lisent des données venues de l'extérieur, compilées avec AddressSanitizer pour repérer le moindre accès mémoire invalide :

| Cible | Teste |
|---|---|
| `fuzz_http` | l'analyse des requêtes (plusieurs à la suite, corps en morceaux), puis la lecture de la query, du formulaire et des cookies |
| `fuzz_text` | `cs_url_decode`, `cs_params_get`, les échappements HTML et JSON, l'UTF-8 |
| `fuzz_template` | le moteur de modèles, avec des modèles mal formés |

Chaque cible tourne `FUZZ_TIME` secondes (60 par défaut : `make fuzz FUZZ_TIME=600` pour une longue session). Un plantage arrête tout et laisse l'entrée qui le provoque dans `tests/fuzz/build/crash-*` ; une fois corrigé, on la copie dans [`tests/fuzz/seeds/`](tests/fuzz/seeds/), où elle sera rejouée à chaque session.

## Mise en ligne

**Jamais en root.** Une faille dans le serveur donnerait alors tous les droits sur la machine. Deux façons de l'éviter :

- **Docker** (le plus simple) : le [`Dockerfile`](../Dockerfile) du dépôt compile un site et le lance avec un utilisateur `cserver` qui ne peut écrire que dans `data/`.

  ```sh
  docker build -t mon-site .                                    # le projet à la racine
  docker build -t guestbook --build-arg DIR=examples/guestbook .   # ou un exemple
  docker run -d -p 8080:8080 -v mon-site-data:/site/data --restart unless-stopped mon-site
  ```

  La base est dans le volume `mon-site-data` : elle survit à une nouvelle image. `docker stop` arrête le serveur tout de suite.

- **Sans Docker** : lancer `./server` avec un utilisateur créé pour lui (`sudo useradd --system cserver`). S'il faut le lancer en root, par exemple pour ouvrir le port 80, `.user = "cserver"` (ou `CSERVER_USER=cserver`) lui fait prendre cet utilisateur juste après l'ouverture du port, avant d'ouvrir la base. Sans cela, il démarre quand même mais affiche un avertissement.

**HTTPS, avec un proxy.** cserver ne parle que HTTP. Un proxy placé devant reçoit les visiteurs en HTTPS et lui transmet leurs requêtes. Le dépôt en contient un prêt à l'emploi, avec [Caddy](https://caddyserver.com) qui obtient et renouvelle les certificats tout seul :

```sh
DOMAIN=mon-site.fr docker compose up -d --build                          # le projet
DOMAIN=mon-site.fr DIR=examples/guestbook docker compose up -d --build   # un exemple
```

Le domaine doit pointer vers la machine, avec les ports 80 et 443 ouverts. Sans `DOMAIN`, le site est sur `https://localhost` avec un certificat local, pour essayer. Les réglages sont dans [`compose.yaml`](../compose.yaml) et [`Caddyfile`](../Caddyfile).

Derrière un proxy, toutes les connexions viennent de lui : sans réglage, les limites par adresse (connexions, inscriptions, `cs_rate_limit`) compteraient tous les visiteurs ensemble. Avec **`trusted_proxy`** (ou `CSERVER_PROXY`), le serveur croit ce que le proxy lui dit :

- `X-Forwarded-For` : l'adresse du visiteur, dans `c->req.ip` (les journaux et les limites l'utilisent) ;
- `X-Forwarded-Proto: https` : `c->req.https` vaut 1, et les cookies prennent l'option `Secure` (jamais envoyés en clair).

Ces en-têtes ne sont crus que si la connexion vient d'une adresse de `trusted_proxy` : venant de n'importe qui d'autre, ils sont ignorés, sinon chacun pourrait changer d'adresse à volonté. Le `compose.yaml` règle tout cela.

Sans Docker, avec Caddy installé sur la machine : `.host = "127.0.0.1"` (le site n'est joignable que par le proxy), `.trusted_proxy = "127.0.0.1"`, et dans le `Caddyfile` : `mon-site.fr { reverse_proxy localhost:8080 }`.

## Limites connues

- **Pas de HTTPS intégré** : il passe par un proxy (voir [Mise en ligne](#mise-en-ligne)). IPv4 seulement pour l'écoute et `trusted_proxy`.
- **Un processus par requête** (`fork`) : simple et robuste (un plantage ne touche qu'une requête), mais pas fait pour des milliers de visiteurs simultanés. Leur nombre est plafonné par `max_connections`, et chaque requête est bornée dans le temps (`read_timeout`, `request_timeout`) : un client qui envoie sa requête goutte à goutte, ou un handler qui ne finit pas, ne bloquent pas le serveur.
- **SQLite uniquement**, une base par site.
- **HTTP/1.1, sans HTTP/2** : une connexion sert plusieurs requêtes à la suite (100 au plus), les requêtes envoyées d'un coup sont toutes lues, les corps en morceaux (`chunked`) sont acceptés, `HEAD` et `Expect: 100-continue` sont gérés. Une connexion gardée ouverte occupe un processus jusqu'à `keepalive_timeout` : c'est ce que plafonne `max_connections`.
