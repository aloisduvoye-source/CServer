#include<string.h>
#include"router.h"
#include"static.h"

void route(int client, Request *req){
	for (size_t i = 0; i < routes_count; i++){
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
