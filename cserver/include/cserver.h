#ifndef CSERVER_H
#define CSERVER_H

// cserver : mini framework web en C. C'est le seul en-tête qu'un site inclut.
// Tout ce qui est public commence par cs_ : le C n'a pas d'espaces de noms.

#include<stddef.h>
#include<sqlite3.h>

/* ================================================================ requête */

typedef struct {
	char   method[8];
	char   path[256];     // sans la query, déjà décodé (%20 -> ' ')
	char   query[512];    // ce qui suit le "?", brut : lire avec cs_query()
	char   cookie[4096];  // en-tête Cookie brut : lire avec cs_cookie()
	char   ip[46];        // adresse du client
	char  *body;          // corps (POST), terminé par '\0' : lire avec cs_form()
	size_t body_len;
} cs_request;

typedef struct {
	long id;
	char name[32];
} cs_user;

// contexte d'une requête, passé à chaque handler
typedef struct {
	int        client;      // socket du client
	cs_request req;

	// interne : passer par cs_db() et cs_current_user()
	sqlite3   *db;          // ouverte au premier besoin, fermée après la réponse
	int        user_state;  // 0 : pas encore cherché, 1 : connecté, -1 : anonyme
	cs_user    user;
	int        responded;   // une réponse est déjà partie
} cs_ctx;

typedef void (*cs_handler)(cs_ctx *c);

// valeur décodée du paramètre `key` ; 0 si trouvé, -1 si absent ou trop long pour out
// (si -1, `out` n'est pas une chaîne valide)
int cs_query(cs_ctx *c, const char *key, char *out, size_t size);    // dans l'URL : ?key=...
int cs_form(cs_ctx *c, const char *key, char *out, size_t size);     // dans le corps d'un POST
int cs_cookie(cs_ctx *c, const char *name, char *out, size_t size);

/* ================================================================ texte */

// chaîne qui s'agrandit toute seule (realloc), pour construire une page ou du JSON
typedef struct {
	char  *data;   // toujours terminée par '\0'
	size_t len;
	size_t cap;
	int    error;  // 1 si une allocation a échoué ; les ajouts suivants sont ignorés
} cs_buf;

void cs_buf_init(cs_buf *b);
void cs_buf_free(cs_buf *b);
void cs_buf_append(cs_buf *b, const char *s);       // tel quel
void cs_buf_append_html(cs_buf *b, const char *s);  // échappé pour du HTML (< -> &lt; ...)
void cs_buf_append_json(cs_buf *b, const char *s);  // "entre guillemets", échappé pour du JSON
void cs_buf_append_long(cs_buf *b, long n);

// remplace < > & " ' par leurs entités ; longueur écrite, ou -1 si dst est trop petit
int cs_html_escape(const char *src, char *dst, size_t dst_size);
#define CS_HTML_ESCAPE_MAX(n) ((n) * 6 + 1)   // taille de dst suffisante pour n octets

// décode les %XX (et '+' -> ' ' si plus_as_space) ; refuse %00 ; longueur ou -1
int cs_url_decode(const char *src, size_t src_len, char *dst, size_t dst_size, int plus_as_space);

// valeur décodée de `key` dans "a=1&b=2" ; 0 si trouvée, -1 sinon
int cs_params_get(const char *params, const char *key, char *out, size_t size);

int cs_utf8_valid(const char *s);   // 1 si UTF-8 bien formé

/* ================================================================ réponse */
// un handler envoie exactement une réponse ; s'il l'oublie, le framework envoie une 500

void cs_send(cs_ctx *c, int status, const char *content_type, const char *body, size_t len);
void cs_html(cs_ctx *c, int status, cs_buf *page);   // envoie puis libère (500 si b->error)
void cs_json(cs_ctx *c, int status, cs_buf *json);   // envoie puis libère (500 si b->error)
void cs_json_error(cs_ctx *c, int status, const char *message);   // {"error":"..."}
void cs_error(cs_ctx *c, int status);                // page d'erreur minimale
void cs_redirect(cs_ctx *c, const char *location);   // 303 See Other

/* ================================================================ modèles HTML */
// Les modèles sont des fichiers HTML dans templates/ (cs_app.templates_dir), relus à
// chaque requête : on peut les modifier sans recompiler.
//
//   {{nom}}               valeur échappée (< devient &lt;...) : à utiliser par défaut
//   {{{nom}}}             valeur insérée telle quelle : seulement pour du HTML déjà prêt
//   {{#nom}}...{{/nom}}   affiché si nom n'est pas vide
//   {{^nom}}...{{/nom}}   affiché si nom est vide ou absent
//   {{! commentaire }}    rien
//
// Une variable absente vaut "". Toujours disponibles : site_name, et user (le nom de
// l'utilisateur connecté, ou vide). Les boucles se font en C : rendre un petit modèle
// par élément avec cs_render(), puis passer le tout au modèle de la page avec cs_set_html().
//
// Le framework fournit layout.html, login.html et signup.html ; un fichier du même nom
// dans le templates/ du site les remplace.

#define CS_VARS_MAX 32

typedef struct {
	int count;
	int error;   // 1 si une allocation a échoué, ou s'il y a trop de variables
	struct {
		const char *name;   // non copié : une chaîne écrite dans le code, ex : "title"
		char       *value;  // copiée
		int         raw;    // 1 : HTML à insérer tel quel
	} items[CS_VARS_MAX];
} cs_vars;

void cs_vars_init(cs_vars *v);
void cs_vars_free(cs_vars *v);
void cs_set(cs_vars *v, const char *name, const char *text);       // échappée à l'affichage
void cs_set_html(cs_vars *v, const char *name, const char *html);  // insérée telle quelle
void cs_set_long(cs_vars *v, const char *name, long n);

// ajoute le modèle `tpl` (ex : "message.html") rendu avec v (ou NULL) à la fin de out
// renvoie 0, ou -1 si le modèle est introuvable ou mal formé (message dans le terminal)
int cs_render(cs_ctx *c, cs_buf *out, const char *tpl, const cs_vars *v);

// rend `tpl`, le place dans layout.html, envoie la page, puis libère v.
// Variables utilisées par le layout : title, script (chemin d'un .js) ; content est
// rempli avec le résultat de `tpl`. En cas d'erreur de modèle : 500.
void cs_page(cs_ctx *c, int status, const char *tpl, cs_vars *v);

/* ================================================================ base de données */

// connexion SQLite de la requête : ouverte au premier appel, fermée par le framework
// après la réponse ; NULL si la base est inaccessible
sqlite3 *cs_db(cs_ctx *c);

// prépare une requête (NULL et message d'erreur si elle est invalide)
sqlite3_stmt *cs_db_prepare(sqlite3 *db, const char *sql);
// exécute une requête qui ne renvoie aucune ligne, puis la libère ; 0 si OK
int cs_db_run(sqlite3 *db, sqlite3_stmt *stmt);

// une étape de l'évolution du schéma. Un site les écrit dans migrations/*.sql
// (un fichier par étape : 001_xxx.sql, 002_yyy.sql...) et le Makefile les intègre
// au binaire avec cserver/tools/embed.sh, dans un tableau site_migrations.
// Au démarrage, le framework applique dans l'ordre celles qui ne l'ont pas encore
// été (chacune dans une transaction) et retient leur nom dans la table cs_migrations.
// Une migration déjà appliquée ne se modifie JAMAIS : on en ajoute une nouvelle.
// Les tables du framework (cs_users, cs_sessions...) existent déjà quand celles
// du site sont créées : une table du site peut faire REFERENCES cs_users(id).
typedef struct {
	const char *name;   // unique et définitif, ex : "001_messages"
	const char *sql;    // une ou plusieurs requêtes
} cs_migration;

/* ================================================================ comptes */

// utilisateur connecté, ou NULL
const cs_user *cs_current_user(cs_ctx *c);
// pareil, mais si personne n'est connecté : redirige vers /login et renvoie NULL
const cs_user *cs_require_user(cs_ctx *c);

// pages et actions prêtes à l'emploi (modèles login.html et signup.html) ;
// à déclarer dans les routes du site avec CS_AUTH_ROUTES
void cs_signup_page(cs_ctx *c);
void cs_signup(cs_ctx *c);
void cs_login_page(cs_ctx *c);
void cs_login(cs_ctx *c);
void cs_logout(cs_ctx *c);

/* ================================================================ site */

typedef struct {
	const char *method;    // "GET", "POST"...
	const char *path;      // chemin exact, ex : "/guestbook"
	cs_handler  handler;   // ou NULL : la route affiche simplement le modèle `page`
	const char *page;      // sans handler : modèle à afficher, ex : "about.html"
	const char *title;     // sans handler : titre de la page (NULL : le nom du site seul)
} cs_route;

// pour écrire la table des routes :
//   static const cs_route routes[] = {
//       CS_PAGE("/about", "about.html", "about"),   // un modèle, sans code C
//       CS_GET ("/guestbook", guestbook_list),
//       CS_POST("/guestbook", guestbook_post),
//       CS_AUTH_ROUTES,
//       CS_END
//   };
#define CS_GET(p, h)        { .method = "GET",  .path = (p), .handler = (h) }
#define CS_POST(p, h)       { .method = "POST", .path = (p), .handler = (h) }
#define CS_PAGE(p, tpl, t)  { .method = "GET",  .path = (p), .page = (tpl), .title = (t) }
#define CS_END              { .method = NULL }

// inscription, connexion, déconnexion, aux chemins qu'utilisent les modèles du framework
#define CS_AUTH_ROUTES \
	CS_GET ("/signup", cs_signup_page), \
	CS_POST("/signup", cs_signup),      \
	CS_GET ("/login",  cs_login_page),  \
	CS_POST("/login",  cs_login),       \
	CS_POST("/logout", cs_logout)

// tout ce qui est propre à un site ; un champ laissé à 0 ou NULL prend la valeur par défaut
typedef struct {
	const char     *name;          // nom du site, affiché dans le <title>      défaut : "cserver"
	int             port;          // port d'écoute ; CSERVER_PORT l'emporte    défaut : 8080
	const char     *static_dir;    // dossier des fichiers statiques            défaut : "www"
	const char     *templates_dir; // dossier des modèles HTML                  défaut : "templates"
	const char     *db_path;       // base SQLite ; CSERVER_DB l'emporte        défaut : "data/site.db"
	const cs_migration *migrations;// tables du site, liste terminée par { NULL }
	const cs_route *routes;        // table terminée par CS_END
	cs_handler      not_found;     // page 404                                  défaut : page minimale
	const char     *cookie_name;   // cookie de session : un nom propre au site, car sur localhost
	                               // le navigateur envoie les cookies de tous les projets
	                               //                                           défaut : "cs_session"
	const char     *after_login;   // où aller après connexion, inscription
	                               // et déconnexion                            défaut : "/"
} cs_app;

// lance le serveur ; ne revient qu'en cas d'erreur (et renvoie alors 1)
int cs_run(const cs_app *app);

#endif
