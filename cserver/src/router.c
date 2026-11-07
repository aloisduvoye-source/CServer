#include<stdio.h>
#include<string.h>
#include"internal.h"

void route(cs_ctx *c){
	for (const cs_route *r = cs_config.routes; r->method != NULL; r++){
		if (strcmp(c->req.method, r->method) == 0 && strcmp(c->req.path, r->path) == 0){
			if (r->handler != NULL){
				r->handler(c);
			}
			else if (r->page != NULL){
				// route sans handler : on affiche simplement le modèle
				cs_vars v;
				cs_vars_init(&v);
				if (r->title != NULL){cs_set(&v, "title", r->title);}
				cs_page(c, 200, r->page, &v);
			}
			else {
				fprintf(stderr, "route %s %s : ni handler, ni page\n", r->method, r->path);
				cs_error(c, 500);
			}
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
