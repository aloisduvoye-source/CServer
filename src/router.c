#include<stdio.h>
#include<string.h>
#include<time.h>
#include"router.h"
#include"static.h"
#include"util.h"
#include"db.h"
#include"guestbook.h"

typedef void (*Handler)(int client, Request *req);

typedef struct {
	const char *method;
	const char *path;
	Handler     handler;
} Route;

// route d'exemple : renvoie l'heure du serveur
static void api_heure(int client, Request *req){
	(void)req;
	time_t now = time(NULL);
	char body[32];
	size_t len = strftime(body, sizeof(body), "%H:%M:%S\n", localtime(&now));
	send_response(client, 200, "text/plain", body, len);
}

// exemple : GET  /api/bonjour?nom=Jean+Dupont
//           POST /api/bonjour  avec le corps "nom=Jean+Dupont" (formulaire)
//        -> "Bonjour Jean Dupont"
static void api_bonjour(int client, Request *req){
	// un formulaire POST a exactement le même format que la query
	const char *params = strcmp(req->method, "POST") == 0 ? req->body : req->query;

	char nom[128];
	if (query_get(params, "nom", nom, sizeof(nom)) < 0 || nom[0] == '\0'){
		strcpy(nom, "inconnu");
	}

	char body[256];
	int len = snprintf(body, sizeof(body), "Bonjour %s\n", nom);
	send_response(client, 200, "text/plain; charset=utf-8", body, len);
}

// exemple HTML : /bonjour?nom=<b>x</b> affiche "<b>x</b>" comme du texte
static void page_bonjour(int client, Request *req){
	char nom[128];
	if (query_get(req->query, "nom", nom, sizeof(nom)) < 0 || nom[0] == '\0'){
		strcpy(nom, "inconnu");
	}

	// TOUJOURS échapper ce qui vient de l'extérieur avant de le mettre dans du HTML
	char nom_html[HTML_ESCAPE_MAX(sizeof(nom))];
	html_escape(nom, nom_html, sizeof(nom_html));   // ne peut pas échouer : buffer assez grand

	char body[1024];
	int len = snprintf(body, sizeof(body),
		"<!doctype html>\n"
		"<meta charset=\"utf-8\">\n"
		"<h1>Bonjour %s</h1>\n",
		nom_html
	);
	send_response(client, 200, "text/html; charset=utf-8", body, len);
}

// exemple SQLite : compteur partagé par tous les processus enfants
// (une variable globale ne marcherait pas : chaque enfant a sa propre copie)
static void api_visites(int client, Request *req){
	(void)req;
	sqlite3 *db = db_open();
	long n = (db != NULL) ? db_counter_incr(db, "visites") : -1;
	sqlite3_close(db);

	if (n < 0){
		send_error(client, 500);
		return;
	}

	char body[32];
	int len = snprintf(body, sizeof(body), "%ld\n", n);
	send_response(client, 200, "text/plain", body, len);
}

// ajouter une page = ajouter une ligne ici
static Route routes[] = {
	{ "GET",  "/api/heure",   api_heure },
	{ "GET",  "/api/visites", api_visites },
	{ "GET",  "/api/bonjour", api_bonjour },
	{ "POST", "/api/bonjour", api_bonjour },
	{ "GET",  "/bonjour",     page_bonjour },
	{ "GET",  "/guestbook",    guestbook_list },
	{ "POST", "/guestbook",    guestbook_post },
	{ "GET",  "/api/messages", api_messages_list },
	{ "POST", "/api/messages", api_messages_post },
};

void route(int client, Request *req){
	size_t count = sizeof(routes) / sizeof(routes[0]);
	for (size_t i = 0; i < count; i++){
		if (strcmp(req->method, routes[i].method) == 0 && strcmp(req->path, routes[i].path) == 0){
			routes[i].handler(client, req);
			return;
		}
	}

	// aucune route : on cherche un fichier dans www/
	if (strcmp(req->method, "GET") == 0){
		static_serve(client, req);
		return;
	}

	send_error(client, 405);
}
