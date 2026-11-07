#include<string.h>
#include<time.h>
#include"router.h"
#include"static.h"

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

// ajouter une page = ajouter une ligne ici
static Route routes[] = {
	{ "GET", "/api/heure", api_heure },
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
