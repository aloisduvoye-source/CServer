#ifndef BUFFER_H
#define BUFFER_H

#include<stddef.h>

// chaîne qui s'agrandit toute seule (realloc), pour construire une page
// dont on ne connaît pas la taille à l'avance
typedef struct {
	char  *data;   // toujours terminée par '\0'
	size_t len;
	size_t cap;
	int    error;  // passe à 1 si une allocation échoue ; les ajouts suivants sont ignorés
} Buffer;

void buf_init(Buffer *b);
void buf_free(Buffer *b);
void buf_append(Buffer *b, const char *s);       // ajoute s tel quel
void buf_append_html(Buffer *b, const char *s);  // ajoute s échappé (< -> &lt; ...)

#endif
