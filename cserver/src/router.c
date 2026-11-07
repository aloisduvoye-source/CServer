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

// ajoute `method` à la liste "GET, HEAD, POST" si elle n'y est pas déjà
static void allow_add(char *allow, size_t size, const char *method){
	const char *p = allow;
	size_t n = strlen(method);
	while ((p = strstr(p, method)) != NULL){
		if ((p == allow || p[-1] == ' ') && (p[n] == ',' || p[n] == '\0')){return;}
		p += n;
	}
	size_t used = strlen(allow);
	snprintf(allow + used, size - used, "%s%s", used > 0 ? ", " : "", method);
}

// 405 : la méthode n'est pas acceptée ici ; l'en-tête Allow dit lesquelles le sont
static void method_not_allowed(cs_ctx *c){
	char allow[128] = "";
	for (const cs_route *r = cs_config.routes; r->method != NULL; r++){
		if (match(r->path, c->req.path, c)){
			allow_add(allow, sizeof(allow), r->method);
			if (strcmp(r->method, "GET") == 0){allow_add(allow, sizeof(allow), "HEAD");}
		}
	}
	c->params_count = 0;
	if (allow[0] == '\0'){
		snprintf(allow, sizeof(allow), "GET, HEAD");   // aucune route : un fichier statique
	}

	char header[160];
	snprintf(header, sizeof(header), "Allow: %s", allow);
	cs_header(c, header);
	cs_error(c, 405);
}

void route(cs_ctx *c){
	// HEAD se comporte comme GET : les réponses n'enverront que leurs en-têtes
	const char *method = c->head ? "GET" : c->req.method;

	for (const cs_route *r = cs_config.routes; r->method != NULL; r++){
		if (strcmp(method, r->method) != 0 || !match(r->path, c->req.path, c)){
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
	if (strcmp(method, "GET") == 0){
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

	method_not_allowed(c);
}
