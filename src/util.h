#ifndef UTIL_H
#define UTIL_H

#include<stddef.h>

// décode les %XX (et les '+' en espaces si plus_as_space), refuse %00
// renvoie la longueur décodée, ou -1 si invalide ou trop long pour dst
int url_decode(const char *src, size_t src_len, char *dst, size_t dst_size, int plus_as_space);

// cherche key dans "a=1&b=2" et écrit sa valeur décodée dans out
// renvoie 0 si trouvée, -1 sinon
int query_get(const char *query, const char *key, char *out, size_t out_size);

// remplace < > & " ' par leurs entités HTML (&lt; &gt; ...)
// renvoie la longueur écrite, ou -1 si dst est trop petit
int html_escape(const char *src, char *dst, size_t dst_size);

// taille de buffer suffisante pour échapper n octets (pire cas : " -> &quot;, x6)
#define HTML_ESCAPE_MAX(n) ((n) * 6 + 1)

// 1 si s est de l'UTF-8 bien formé, 0 sinon (ex : "%FF" décodé)
int utf8_valid(const char *s);

#endif
