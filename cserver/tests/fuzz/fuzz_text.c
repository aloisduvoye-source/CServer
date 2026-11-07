// Fuzzing des fonctions de texte : décodage %XX, paramètres "a=1&b=2", échappement
// HTML et JSON, UTF-8. Les données reçues servent à la fois de texte et de réglages
// (taille des buffers), pour tester les cas limites de chaque fonction.
#include<stdint.h>
#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include"internal.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size){
	if (size < 2){return 0;}

	// le premier octet choisit la taille du buffer de sortie (0 à 255)
	size_t out_size = data[0];
	const char *text = (const char *)data + 1;
	size_t len = size - 1;

	// une copie terminée par '\0', comme le reste du serveur en manipule
	char *s = malloc(len + 1);
	if (s == NULL){return 0;}
	memcpy(s, text, len);
	s[len] = '\0';

	// cs_url_decode : ne doit jamais écrire au-delà de out_size
	// exactement out_size octets : AddressSanitizer verra tout dépassement. Pour 0,
	// NULL : ASan ne signale pas une écriture dans un malloc(0) (il réserve un octet)
	char *out = out_size > 0 ? malloc(out_size) : NULL;
	int n = cs_url_decode(s, strlen(s), out, out_size, data[0] & 1);
	if (n >= 0 && ((size_t)n >= out_size || strlen(out) != (size_t)n)){abort();}

	// cs_params_get, avec une clé tirée du texte lui-même
	char key[8];
	snprintf(key, sizeof(key), "%.7s", s);
	cs_params_get(s, key, out, out_size);
	cs_params_get(s, "a", out, out_size);

	// cs_html_escape : avec CS_HTML_ESCAPE_MAX, elle ne doit jamais échouer
	size_t big = CS_HTML_ESCAPE_MAX(strlen(s));
	char *escaped = malloc(big);
	if (escaped != NULL){
		if (cs_html_escape(s, escaped, big) < 0){abort();}
		if (strchr(escaped, '<') != NULL || strchr(escaped, '"') != NULL){abort();}
		cs_html_escape(s, escaped, out_size);   // trop petit : -1, sans déborder
		free(escaped);
	}

	// cs_buf et ses échappements
	cs_buf b;
	cs_buf_init(&b);
	cs_buf_append_html(&b, s);
	cs_buf_append_json(&b, s);
	cs_buf_append_long(&b, (long)len);
	if (!b.error && strlen(b.data) != b.len){abort();}
	cs_buf_free(&b);

	cs_utf8_valid(s);

	free(out);
	free(s);
	return 0;
}
