#ifndef CSERVER_H
#define CSERVER_H

// le seul en-tête qu'un site a besoin d'inclure
#include"http.h"
#include"buffer.h"
#include"util.h"
#include"db.h"
#include"auth.h"
#include"page.h"

typedef void (*cs_handler)(int client, Request *req);

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
