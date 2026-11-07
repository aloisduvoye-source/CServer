#ifndef INTERNAL_H
#define INTERNAL_H

// en-tête interne au framework : les sites n'y ont pas accès (il n'est pas
// dans cserver/include/)

#include"cserver.h"

// configuration du site, complétée avec les valeurs par défaut par cs_run()
// (variable globale : chaque processus enfant en reçoit une copie au fork)
extern cs_app cs_config;

/* ---------------------------------------------------------------- http.c */

// lit et analyse la requête (en-têtes + corps)
// renvoie 0 si OK, -1 si rien reçu, sinon le code HTTP d'erreur à renvoyer (400, 413...)
int  http_parse(int client, cs_request *req);
void http_free(cs_request *req);

int  http_cookie_get(const cs_request *req, const char *name, char *out, size_t out_size);

int  http_write_all(int fd, const char *buf, size_t len);
void http_send_header(int client, int status, const char *content_type, size_t len);
void http_send(int client, int status, const char *content_type, const char *body, size_t len);
void http_send_error(int client, int status);
// 303 ; extra_headers : lignes en plus ("Set-Cookie: ...\r\n") ou NULL
void http_redirect(int client, const char *location, const char *extra_headers);

/* ---------------------------------------------------------------- ctx.c */

void ctx_redirect_with(cs_ctx *c, const char *location, const char *extra_headers);
void ctx_finish(cs_ctx *c);   // après le handler : 500 s'il n'a rien envoyé, ferme la base

/* ---------------------------------------------------------------- router.c, static.c */

void route(cs_ctx *c);         // route du site, sinon fichier statique, sinon 404
int  static_serve(cs_ctx *c);  // 1 si le fichier a été envoyé, 0 s'il n'existe pas

/* ---------------------------------------------------------------- db.c */

int      db_init(void);        // une fois au démarrage, avant les fork : tables + migrations
sqlite3 *db_open(void);

// généré par le Makefile depuis cserver/migrations/*.sql
extern const cs_migration framework_migrations[];

/* ---------------------------------------------------------------- template.c, buffer.c */

typedef struct {
	const char *name;   // ex : "layout.html"
	const char *text;
} embedded_file;

// modèles par défaut, générés par le Makefile depuis cserver/templates/*.html
extern const embedded_file framework_templates[];

void buf_append_n(cs_buf *b, const char *s, size_t n);   // n octets de s, tels quels

// comptes : 0 si le nom est pris, l'id sinon, -1 si erreur
long db_user_create(sqlite3 *db, const char *name, const char *password_hash);
int  db_user_find(sqlite3 *db, const char *name, cs_user *user, char *hash, size_t hash_size);

int  db_session_create(sqlite3 *db, const char *token, long user_id, long expires_at);
int  db_session_user(sqlite3 *db, const char *token, long now, cs_user *user);
void db_session_delete(sqlite3 *db, const char *token);

int  db_login_failures(sqlite3 *db, const char *ip, long since);
void db_login_failure_add(sqlite3 *db, const char *ip, long now, long forget_before);

/* ---------------------------------------------------------------- auth.c */

int auth_lookup_user(cs_ctx *c, cs_user *user);   // 1 si le cookie correspond à une session

#endif
