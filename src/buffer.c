#include<stdlib.h>
#include<string.h>
#include"buffer.h"
#include"util.h"

// s'assure qu'il reste au moins `extra` octets libres (+ la place du '\0')
static int buf_reserve(Buffer *b, size_t extra){
	if (b->error){return -1;}

	size_t need = b->len + extra + 1;
	if (need <= b->cap){return 0;}

	// on double la taille : peu de realloc, même pour une grosse page
	size_t cap = b->cap ? b->cap : 256;
	while (cap < need){cap *= 2;}

	char *data = realloc(b->data, cap);
	if (data == NULL){
		b->error = 1;
		return -1;
	}
	b->data = data;
	b->cap = cap;
	return 0;
}

void buf_init(Buffer *b){
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
	b->error = 0;
	if (buf_reserve(b, 0) == 0){
		b->data[0] = '\0';
	}
}

void buf_free(Buffer *b){
	free(b->data);
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

void buf_append(Buffer *b, const char *s){
	size_t n = strlen(s);
	if (buf_reserve(b, n) < 0){return;}
	memcpy(b->data + b->len, s, n + 1);   // +1 : copie aussi le '\0'
	b->len += n;
}

void buf_append_html(Buffer *b, const char *s){
	size_t n = strlen(s);
	if (buf_reserve(b, n * 6) < 0){return;}   // pire cas : chaque octet devient &quot;

	int written = html_escape(s, b->data + b->len, b->cap - b->len);
	if (written < 0){
		b->error = 1;
		return;
	}
	b->len += written;
}
