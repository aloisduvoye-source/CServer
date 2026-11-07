#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"internal.h"

// s'assure qu'il reste au moins `extra` octets libres (+ la place du '\0')
static int buf_reserve(cs_buf *b, size_t extra){
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

void cs_buf_init(cs_buf *b){
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
	b->error = 0;
	if (buf_reserve(b, 0) == 0){
		b->data[0] = '\0';
	}
}

void cs_buf_free(cs_buf *b){
	free(b->data);
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

void cs_buf_append(cs_buf *b, const char *s){
	size_t n = strlen(s);
	if (buf_reserve(b, n) < 0){return;}
	memcpy(b->data + b->len, s, n + 1);   // +1 : copie aussi le '\0'
	b->len += n;
}

void cs_buf_append_html(cs_buf *b, const char *s){
	size_t n = strlen(s);
	if (buf_reserve(b, n * 6) < 0){return;}   // pire cas : chaque octet devient &quot;

	int written = cs_html_escape(s, b->data + b->len, b->cap - b->len);
	if (written < 0){
		b->error = 1;
		return;
	}
	b->len += written;
}

void cs_buf_append_json(cs_buf *b, const char *s){
	static const char hex[] = "0123456789abcdef";

	// pire cas : chaque octet devient \u00XX (6 octets), + les 2 guillemets
	size_t n = strlen(s);
	if (buf_reserve(b, n * 6 + 2) < 0){return;}

	char *out = b->data + b->len;
	*out++ = '"';
	for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++){
		switch (*p){
			case '"':  *out++ = '\\'; *out++ = '"';  break;
			case '\\': *out++ = '\\'; *out++ = '\\'; break;
			case '\n': *out++ = '\\'; *out++ = 'n';  break;
			case '\r': *out++ = '\\'; *out++ = 'r';  break;
			case '\t': *out++ = '\\'; *out++ = 't';  break;
			default:
				if (*p < 0x20){   // autres caractères de contrôle : \u00XX
					*out++ = '\\'; *out++ = 'u'; *out++ = '0'; *out++ = '0';
					*out++ = hex[*p >> 4];
					*out++ = hex[*p & 0xF];
				}
				else {            // le reste (accents UTF-8 compris) passe tel quel
					*out++ = *p;
				}
		}
	}
	*out++ = '"';
	*out = '\0';
	b->len = out - b->data;
}

void cs_buf_append_long(cs_buf *b, long n){
	char num[32];
	snprintf(num, sizeof(num), "%ld", n);
	cs_buf_append(b, num);
}
