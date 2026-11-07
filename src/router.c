#include<stdio.h>
#include<string.h>
#include<time.h>
#include"router.h"
#include"static.h"
#include"util.h"

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

// exemple : /api/bonjour?nom=Jean+Dupont -> "Bonjour Jean Dupont"
static void api_bonjour(int client, Request *req){
	char nom[128];
	if (query_get(req->query, "nom", nom, sizeof(nom)) < 0 || nom[0] == '\0'){
		strcpy(nom, "inconnu");
	}

	char body[256];
	int len = snprintf(body, sizeof(body), "Bonjour %s\n", nom);
	send_response(client, 200, "text/plain; charset=utf-8", body, len);
}

// ajouter une page = ajouter une ligne ici
static Route routes[] = {
	{ "GET", "/api/heure",   api_heure },
	{ "GET", "/api/bonjour", api_bonjour },
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

	const char *body = "<h1>405 Method Not Allowed</h1>\n";
	send_response(client, 405, "text/html", body, strlen(body));
}
