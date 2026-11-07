#include<string.h>
#include"router.h"
#include"config.h"
#include"static.h"

void route(int client, Request *req){
	for (const cs_route *r = cs_config.routes; r->method != NULL; r++){
		if (strcmp(req->method, r->method) == 0 && strcmp(req->path, r->path) == 0){
			r->handler(client, req);
			return;
		}
	}

	// aucune route : on cherche un fichier statique
	if (strcmp(req->method, "GET") == 0){
		if (static_serve(client, req)){return;}

		if (cs_config.not_found != NULL){
			cs_config.not_found(client, req);
		}
		else {
			send_error(client, 404);
		}
		return;
	}

	send_error(client, 405);
}
