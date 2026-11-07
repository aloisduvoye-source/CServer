// Fuzzing de l'analyse des requêtes HTTP : le premier code qui lit ce qu'envoie un
// inconnu. libFuzzer appelle cette fonction des millions de fois avec des données
// différentes ; AddressSanitizer arrête tout au moindre accès mémoire invalide.
#include<stdint.h>
#include<string.h>
#include<stdlib.h>
#include"internal.h"

// si l'analyse réussit, ce qu'elle a rempli doit être cohérent
static void check(const cs_request *req){
	if (strlen(req->method) >= sizeof(req->method)){abort();}
	if (strlen(req->path) >= sizeof(req->path)){abort();}
	if (strlen(req->query) >= sizeof(req->query)){abort();}
	if (strlen(req->cookie) >= sizeof(req->cookie)){abort();}
	if (req->body == NULL || req->body[req->body_len] != '\0'){abort();}
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size){
	cs_request req;
	if (http_parse_buffer((const char *)data, size, &req) == 0){
		check(&req);

		// et ce que les handlers en font ensuite : query, formulaire, cookie
		char out[64];
		cs_params_get(req.query, "a", out, sizeof(out));
		cs_params_get(req.body, "name", out, sizeof(out));
		http_cookie_get(&req, "cs_session", out, sizeof(out));
	}
	http_free(&req);
	return 0;
}
