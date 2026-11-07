#include<string.h>
#include"internal.h"

void route(cs_ctx *c){
	for (const cs_route *r = cs_config.routes; r->method != NULL; r++){
		if (strcmp(c->req.method, r->method) == 0 && strcmp(c->req.path, r->path) == 0){
			r->handler(c);
			return;
		}
	}

	// aucune route : on cherche un fichier statique
	if (strcmp(c->req.method, "GET") == 0){
		if (static_serve(c)){
			c->responded = 1;
			return;
		}

		if (cs_config.not_found != NULL){
			cs_config.not_found(c);
		}
		else {
			cs_error(c, 404);
		}
		return;
	}

	cs_error(c, 405);
}
