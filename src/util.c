#include<string.h>
#include"util.h"

static int hex_value(char c){
	if (c >= '0' && c <= '9'){return c - '0';}
	if (c >= 'a' && c <= 'f'){return c - 'a' + 10;}
	if (c >= 'A' && c <= 'F'){return c - 'A' + 10;}
	return -1;
}

int url_decode(const char *src, size_t src_len, char *dst, size_t dst_size, int plus_as_space){
	if (dst_size == 0){return -1;}

	size_t j = 0;
	for (size_t i = 0; i < src_len; i++){
		char c = src[i];

		if (c == '%'){
			if (i + 2 >= src_len){return -1;}   // "%" ou "%A" en fin de chaîne
			int hi = hex_value(src[i+1]);
			int lo = hex_value(src[i+2]);
			if (hi < 0 || lo < 0){return -1;}   // "%ZZ"
			c = (char)(hi * 16 + lo);
			if (c == '\0'){return -1;}          // "%00" couperait la chaîne
			i += 2;
		}
		else if (c == '+' && plus_as_space){
			c = ' ';
		}

		if (j + 1 >= dst_size){return -1;}      // garder la place du '\0'
		dst[j++] = c;
	}

	dst[j] = '\0';
	return (int)j;
}

int query_get(const char *query, const char *key, char *out, size_t out_size){
	const char *p = query;

	while (*p != '\0'){
		// une paire va de p jusqu'au prochain '&' (ou la fin)
		const char *end = strchr(p, '&');
		if (end == NULL){end = p + strlen(p);}

		const char *eq = memchr(p, '=', end - p);
		const char *key_end = (eq != NULL) ? eq : end;

		char name[256];
		if (url_decode(p, key_end - p, name, sizeof(name), 1) >= 0 && strcmp(name, key) == 0){
			if (eq == NULL){             // "?debug" sans valeur
				out[0] = '\0';
				return 0;
			}
			return url_decode(eq + 1, end - (eq + 1), out, out_size, 1) < 0 ? -1 : 0;
		}

		p = (*end == '&') ? end + 1 : end;
	}

	return -1;
}
