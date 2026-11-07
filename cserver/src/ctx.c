#include<stdio.h>
#include<string.h>
#include"internal.h"

/* ---------------------------------------------------------------- lecture */

int cs_query(cs_ctx *c, const char *key, char *out, size_t size){
	return cs_params_get(c->req.query, key, out, size);
}

int cs_form(cs_ctx *c, const char *key, char *out, size_t size){
	// un formulaire POST a le même format que la query : "a=1&b=2"
	return cs_params_get(c->req.body, key, out, size);
}

int cs_cookie(cs_ctx *c, const char *name, char *out, size_t size){
	return http_cookie_get(&c->req, name, out, size);
}

int cs_param(cs_ctx *c, const char *name, char *out, size_t size){
	for (int i = 0; i < c->params_count; i++){
		if (strcmp(c->params[i].name, name) == 0){
			if (strlen(c->params[i].value) >= size){return -1;}
			strcpy(out, c->params[i].value);
			return 0;
		}
	}
	return -1;
}

long cs_param_id(cs_ctx *c, const char *name){
	char text[32];
	if (cs_param(c, name, text, sizeof(text)) < 0){return -1;}

	size_t n = strlen(text);
	if (n == 0 || n > 18){return -1;}   // 18 chiffres : pas de dépassement d'un long
	long id = 0;
	for (const char *s = text; *s != '\0'; s++){
		if (*s < '0' || *s > '9'){return -1;}
		id = id * 10 + (*s - '0');
	}
	return id > 0 ? id : -1;
}

/* ---------------------------------------------------------------- réponses */

// toutes les réponses passent par ici : on retient qu'une réponse est partie
static int begin_response(cs_ctx *c){
	if (c->responded){
		fprintf(stderr, "%s %s : deuxième réponse ignorée\n", c->req.method, c->req.path);
		return 0;
	}
	c->responded = 1;
	return 1;
}

void cs_send(cs_ctx *c, int status, const char *content_type, const char *body, size_t len){
	if (begin_response(c)){
		if (c->head){   // HEAD : les mêmes en-têtes que GET, sans le corps
			http_send_header(c->client, status, content_type, len, c->headers);
		}
		else {
			http_send(c->client, status, content_type, body, len, c->headers);
		}
	}
}

void cs_error(cs_ctx *c, int status){
	if (begin_response(c)){
		http_send_error(c->client, status, c->headers, !c->head);
	}
}

void cs_redirect(cs_ctx *c, const char *location){
	if (begin_response(c)){
		http_redirect(c->client, location, c->headers);
	}
}

void cs_header(cs_ctx *c, const char *line){
	// un retour à la ligne permettrait d'injecter d'autres en-têtes (ou un corps)
	if (strpbrk(line, "\r\n") != NULL || strchr(line, ':') == NULL){
		fprintf(stderr, "cs_header : en-tête refusé (pas de \\r ni de \\n, et un ':' obligatoire)\n");
		return;
	}
	size_t used = strlen(c->headers);
	size_t n = strlen(line);
	if (used + n + 3 > sizeof(c->headers)){
		fprintf(stderr, "cs_header : trop d'en-têtes, \"%s\" ignoré\n", line);
		return;
	}
	memcpy(c->headers + used, line, n);
	memcpy(c->headers + used + n, "\r\n", 3);
}

static void send_buf(cs_ctx *c, int status, const char *content_type, cs_buf *b){
	if (b->error){
		cs_error(c, 500);
	}
	else {
		cs_send(c, status, content_type, b->data, b->len);
	}
	cs_buf_free(b);
}

void cs_html(cs_ctx *c, int status, cs_buf *page){
	send_buf(c, status, "text/html; charset=utf-8", page);
}

void cs_json(cs_ctx *c, int status, cs_buf *json){
	send_buf(c, status, "application/json", json);
}

void cs_json_error(cs_ctx *c, int status, const char *message){
	cs_buf json;
	cs_buf_init(&json);
	cs_buf_append(&json, "{\"error\":");
	cs_buf_append_json(&json, message);
	cs_buf_append(&json, "}\n");
	cs_json(c, status, &json);
}

/* ---------------------------------------------------------------- base et compte */

sqlite3 *cs_db(cs_ctx *c){
	if (cs_config.db_path == NULL){
		fprintf(stderr, "cs_db : ce site n'a pas de base (ni db_path, ni migrations, ni accounts)\n");
		return NULL;
	}
	if (c->db == NULL){
		c->db = db_open();
	}
	return c->db;
}

const cs_user *cs_current_user(cs_ctx *c){
	if (!cs_config.accounts){return NULL;}

	// cherché une seule fois par requête, même si plusieurs fonctions le demandent
	if (c->user_state == 0){
		c->user_state = auth_lookup_user(c, &c->user) ? 1 : -1;
	}
	return c->user_state == 1 ? &c->user : NULL;
}

const cs_user *cs_require_user(cs_ctx *c){
	const cs_user *user = cs_current_user(c);
	if (user == NULL){
		cs_redirect(c, cs_config.login_path);
	}
	return user;
}

/* ---------------------------------------------------------------- fin de requête */

void ctx_finish(cs_ctx *c){
	if (!c->responded){
		fprintf(stderr, "%s %s : le handler n'a envoyé aucune réponse\n", c->req.method, c->req.path);
		http_send_error(c->client, 500, "Connection: close\r\n", !c->head);
	}
	if (c->db != NULL){
		sqlite3_close(c->db);
		c->db = NULL;
	}
	http_free(&c->req);
	template_cache_clear();
}
