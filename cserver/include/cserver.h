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

// début et fin communs des pages HTML : <head>, style, navigation, utilisateur connecté
// script : chemin d'un .js à charger, ou NULL
void cs_page_begin(cs_ctx *c, cs_buf *b, const char *title, const char *script);
void cs_page_end(cs_buf *b);

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
// au binaire avec cserver/tools/sql2c.sh, dans un tableau site_migrations.
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

// pages et actions prêtes à l'emploi, à déclarer dans les routes du site :
// GET /signup, POST /signup, GET /login, POST /login, POST /logout
void cs_signup_page(cs_ctx *c);
void cs_signup(cs_ctx *c);
void cs_login_page(cs_ctx *c);
void cs_login(cs_ctx *c);
void cs_logout(cs_ctx *c);

/* ================================================================ site */

typedef struct {
	const char *method;    // "GET", "POST"...
	const char *path;      // chemin exact, ex : "/guestbook"
	cs_handler  handler;
} cs_route;

// tout ce qui est propre à un site ; un champ laissé à 0 ou NULL prend la valeur par défaut
typedef struct {
	const char     *name;          // nom du site, affiché dans le <title>      défaut : "cserver"
	int             port;          // port d'écoute ; CSERVER_PORT l'emporte    défaut : 8080
	const char     *static_dir;    // dossier des fichiers statiques            défaut : "www"
	const char     *db_path;       // base SQLite ; CSERVER_DB l'emporte        défaut : "data/site.db"
	const cs_migration *migrations;// tables du site, liste terminée par { NULL }
	const cs_route *routes;        // table terminée par { NULL }
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
