#ifndef HTTP_H
#define HTTP_H

#include<stddef.h>

typedef struct {
	char   method[8];
	char   path[256];   // sans la query, déjà décodé (%20 -> ' ')
	char   query[512];  // ce qui suit le "?", BRUT : lire avec query_get()
	char  *body;        // corps (POST), terminé par '\0', jamais NULL si le parse a réussi
	size_t body_len;
} Request;

// lit et analyse la requête (en-têtes + corps)
// renvoie 0 si OK, -1 si rien reçu, sinon le code HTTP d'erreur à renvoyer (400, 413...)
int  http_parse(int client, Request *req);
void http_free(Request *req);

int  write_all(int fd, const char *buf, size_t len);
void send_header(int client, int status, const char *content_type, size_t len);
void send_response(int client, int status, const char *content_type, const char *body, size_t len);
void send_error(int client, int status);
void send_redirect(int client, const char *location);   // 303 See Other
void send_404(int client);

#endif
