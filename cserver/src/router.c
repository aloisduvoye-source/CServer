#include<stdio.h>
#include<string.h>
#include"internal.h"

// compare le chemin au motif d'une route ; un segment ":nom" du motif accepte
// n'importe quel segment non vide du chemin, et le retient dans c->params.
//   motif "/todos/:id/done", chemin "/todos/42/done" -> oui, id = "42"
static int match(const char *pattern, const char *path, cs_ctx *c){
	c->params_count = 0;
	const char *p = pattern;
	const char *s = path;

	while (*p != '\0' && *s != '\0'){
		if (*p != ':'){
			if (*p != *s){return 0;}
			p++;
			s++;
			continue;
		}

		const char *name = ++p;
		while (*p != '\0' && *p != '/'){p++;}
		const char *value = s;
		while (*s != '\0' && *s != '/'){s++;}

		size_t name_len = p - name;
		size_t value_len = s - value;
		size_t max = sizeof(c->params) / sizeof(c->params[0]);
		if (value_len == 0 || c->params_count == (int)max
		 || name_len >= sizeof(c->params[0].name) || value_len >= sizeof(c->params[0].value)){
			return 0;
		}
		memcpy(c->params[c->params_count].name, name, name_len);
		c->params[c->params_count].name[name_len] = '\0';
		memcpy(c->params[c->params_count].value, value, value_len);
		c->params[c->params_count].value[value_len] = '\0';
		c->params_count++;
	}

	return *p == '\0' && *s == '\0';
}

void route(cs_ctx *c){
	for (const cs_route *r = cs_config.routes; r->method != NULL; r++){
		if (strcmp(c->req.method, r->method) != 0 || !match(r->path, c->req.path, c)){
			continue;
		}

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
	c->params_count = 0;

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
