#ifndef HTTP_H
#define HTTP_H

#include<stddef.h>

typedef struct {
	char method[8];
	char path[256];   // sans la query, déjà décodé (%20 -> ' ')
	char query[512];  // ce qui suit le "?", BRUT : lire avec query_get()
} Request;

// lit et analyse la requête
// renvoie 0 si OK, -1 si rien reçu, -2 si requête invalide (-> 400)
int  http_parse(int client, Request *req);

int  write_all(int fd, const char *buf, size_t len);
void send_header(int client, int status, const char *content_type, size_t len);
void send_response(int client, int status, const char *content_type, const char *body, size_t len);
void send_404(int client);

#endif
