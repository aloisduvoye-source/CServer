// Formulaires : lire les champs et les fichiers envoyés par un POST.
//
// Deux formats, selon l'en-tête Content-Type de la requête :
//   - "application/x-www-form-urlencoded" (le défaut) : "name=jean&message=bonjour",
//     comme la query ;
//   - "multipart/form-data" (<form enctype="multipart/form-data">, obligatoire pour
//     envoyer un fichier) : le corps est découpé en parties, séparées par une
//     « frontière » choisie par le navigateur et annoncée dans le Content-Type :
//
//       Content-Type: multipart/form-data; boundary=XyZ
//
//       --XyZ
//       Content-Disposition: form-data; name="csrf"
//
//       4f2a...
//       --XyZ
//       Content-Disposition: form-data; name="avatar"; filename="moi.png"
//       Content-Type: image/png
//
//       <les octets du fichier, n'importe lesquels, '\0' compris>
//       --XyZ--
//
// Tout vient du visiteur : longueurs, noms, guillemets... rien n'est supposé bien formé.
#include<stdio.h>
#include<string.h>
#include<strings.h>
#include"internal.h"

#define BOUNDARY_MAX 70    // la limite de la norme (RFC 2046)
#define PART_HEAD_MAX 1024 // en-têtes d'une partie : quelques lignes suffisent

typedef struct {
	char        name[256];
	char        filename[256];
	int         has_filename;   // filename="..." présent (même vide)
	char        type[128];
	const char *data;           // dans le corps de la requête
	size_t      len;
} part;

// memmem, sans dépendre de _GNU_SOURCE
static const char *find_bytes(const char *hay, size_t hay_len, const char *needle, size_t n){
	if (n == 0 || hay_len < n){return NULL;}
	const char *end = hay + hay_len - n;
	for (const char *p = hay; p <= end; p++){
		p = memchr(p, needle[0], (size_t)(end - p) + 1);
		if (p == NULL){return NULL;}
		if (memcmp(p, needle, n) == 0){return p;}
	}
	return NULL;
}

// "multipart/form-data; boundary=XyZ" -> "XyZ" ; 0, ou -1 si ce n'est pas du multipart
static int boundary_of(const char *content_type, char *out, size_t size){
	static const char type[] = "multipart/form-data";
	size_t n = sizeof(type) - 1;
	if (strncasecmp(content_type, type, n) != 0){return -1;}

	const char *p = content_type + n;
	if (*p != ';' && *p != ' ' && *p != '\t'){return -1;}   // "multipart/form-dataX"
	while ((p = strchr(p, ';')) != NULL){
		p++;
		while (*p == ' ' || *p == '\t'){p++;}
		if (strncasecmp(p, "boundary=", 9) != 0){continue;}
		p += 9;

		size_t len;
		if (*p == '"'){
			p++;
			const char *end = strchr(p, '"');
			if (end == NULL){return -1;}
			len = (size_t)(end - p);
		}
		else {
			len = strcspn(p, "; \t");
		}
		if (len == 0 || len > BOUNDARY_MAX || len >= size){return -1;}
		memcpy(out, p, len);
		out[len] = '\0';
		return 0;
	}
	return -1;
}

// copie [src, src+len) dans out ; -1 si ça ne tient pas
static int copy_value(const char *src, size_t len, char *out, size_t size){
	if (len >= size){return -1;}
	memcpy(out, src, len);
	out[len] = '\0';
	return 0;
}

// Content-Disposition: form-data; name="avatar"; filename="moi.png"
// Renvoie -1 si la ligne n'est pas utilisable (pas form-data, nom trop long...).
static int parse_disposition(const char *v, part *pt){
	while (*v == ' ' || *v == '\t'){v++;}
	if (strncasecmp(v, "form-data", 9) != 0){return -1;}
	v += 9;

	int has_name = 0;
	while (*v != '\0'){
		// "; clé=valeur" ; la valeur peut être entre guillemets (et contenir des ';')
		while (*v == ';' || *v == ' ' || *v == '\t'){v++;}
		if (*v == '\0'){break;}
		const char *key = v;
		size_t key_len = strcspn(v, "=;");
		v += key_len;
		if (*v != '='){continue;}          // un paramètre sans valeur : ignoré
		v++;

		const char *val;
		size_t val_len;
		if (*v == '"'){
			val = v + 1;
			const char *end = strchr(val, '"');
			if (end == NULL){return -1;}
			val_len = (size_t)(end - val);
			v = end + 1;
		}
		else {
			val = v;
			val_len = strcspn(v, "; \t");
			v += val_len;
		}

		if (key_len == 4 && strncasecmp(key, "name", 4) == 0){
			if (copy_value(val, val_len, pt->name, sizeof(pt->name)) < 0){return -1;}
			has_name = 1;
		}
		else if (key_len == 8 && strncasecmp(key, "filename", 8) == 0){
			// trop long : coupé (le nom ne sert qu'à être affiché)
			if (val_len >= sizeof(pt->filename)){val_len = sizeof(pt->filename) - 1;}
			copy_value(val, val_len, pt->filename, sizeof(pt->filename));
			pt->has_filename = 1;
		}
	}
	return has_name ? 0 : -1;
}

// les en-têtes d'une partie : [head, head+len), lignes séparées par "\r\n"
static int parse_part_head(const char *head, size_t len, part *pt){
	char buf[PART_HEAD_MAX];
	if (len >= sizeof(buf) || memchr(head, '\0', len) != NULL){return -1;}
	memcpy(buf, head, len);
	buf[len] = '\0';

	int has_disposition = 0;
	for (char *line = buf; line != NULL; ){
		char *next = strstr(line, "\r\n");
		if (next != NULL){*next = '\0'; next += 2;}

		if (strncasecmp(line, "Content-Disposition:", 20) == 0){
			if (parse_disposition(line + 20, pt) < 0){return -1;}
			has_disposition = 1;
		}
		else if (strncasecmp(line, "Content-Type:", 13) == 0){
			const char *v = line + 13;
			while (*v == ' ' || *v == '\t'){v++;}
			size_t n = strcspn(v, " \t");
			if (n >= sizeof(pt->type)){n = sizeof(pt->type) - 1;}
			copy_value(v, n, pt->type, sizeof(pt->type));
		}
		line = next;
	}
	return has_disposition ? 0 : -1;
}

// cherche la partie `name` (fichier ou non selon want_file). 0 si trouvée, -1 sinon
// (absente, ou corps mal formé : on ne va pas plus loin que l'endroit abîmé)
static int multipart_find(const cs_request *req, const char *boundary, const char *name,
                          int want_file, part *out){
	// délimiteur : "\r\n--XyZ" ; le premier peut être tout au début du corps, sans "\r\n"
	char delim[BOUNDARY_MAX + 5];
	size_t dlen = (size_t)snprintf(delim, sizeof(delim), "\r\n--%s", boundary);

	const char *body = req->body;
	const char *end = body + req->body_len;
	const char *cur;
	if (req->body_len >= dlen - 2 && memcmp(body, delim + 2, dlen - 2) == 0){
		cur = body + dlen - 2;
	}
	else {
		const char *d = find_bytes(body, req->body_len, delim, dlen);
		if (d == NULL){return -1;}
		cur = d + dlen;
	}

	while (1){
		// juste après un délimiteur : "--" (c'était le dernier) ou "\r\n" (une partie suit)
		if (end - cur >= 2 && cur[0] == '-' && cur[1] == '-'){return -1;}
		while (cur < end && (*cur == ' ' || *cur == '\t')){cur++;}
		if (end - cur < 2 || cur[0] != '\r' || cur[1] != '\n'){return -1;}
		cur += 2;

		const char *head_end = find_bytes(cur, (size_t)(end - cur), "\r\n\r\n", 4);
		if (head_end == NULL){return -1;}
		const char *data = head_end + 4;
		const char *data_end = find_bytes(data, (size_t)(end - data), delim, dlen);
		if (data_end == NULL){return -1;}

		part pt;
		memset(&pt, 0, sizeof(pt));
		if (parse_part_head(cur, (size_t)(head_end - cur), &pt) == 0
		 && strcmp(pt.name, name) == 0 && pt.has_filename == want_file){
			pt.data = data;
			pt.len = (size_t)(data_end - data);
			*out = pt;
			return 0;
		}
		cur = data_end + dlen;
	}
}

int form_get(const cs_request *req, const char *key, char *out, size_t size){
	if (size == 0){return -1;}
	char boundary[BOUNDARY_MAX + 1];
	if (boundary_of(req->content_type, boundary, sizeof(boundary)) < 0){
		// un formulaire classique a le même format que la query : "a=1&b=2"
		return cs_params_get(req->body, key, out, size);
	}

	part pt;
	if (multipart_find(req, boundary, key, 0, &pt) < 0){return -1;}
	// un champ texte : ni '\0' au milieu (la chaîne serait coupée), ni trop long
	if (memchr(pt.data, '\0', pt.len) != NULL){return -1;}
	return copy_value(pt.data, pt.len, out, size);
}

int form_file(const cs_request *req, const char *key, cs_file *file){
	char boundary[BOUNDARY_MAX + 1];
	if (boundary_of(req->content_type, boundary, sizeof(boundary)) < 0){return -1;}

	part pt;
	if (multipart_find(req, boundary, key, 1, &pt) < 0){return -1;}
	// <input type="file"> laissé vide : le navigateur envoie filename="" sans contenu
	if (pt.filename[0] == '\0' && pt.len == 0){return -1;}

	memset(file, 0, sizeof(*file));
	file->data = pt.data;
	file->len = pt.len;
	memcpy(file->filename, pt.filename, sizeof(file->filename));
	memcpy(file->type, pt.type, sizeof(file->type));
	return 0;
}

/* ---------------------------------------------------------------- images */

// le type d'après les premiers octets (la « signature » du format), pas d'après le nom
// ni le type annoncé, que le visiteur choisit librement
const char *cs_image_type(const void *data, size_t len){
	const unsigned char *d = data;
	if (d == NULL){return NULL;}
	if (len >= 8 && memcmp(d, "\x89PNG\r\n\x1a\n", 8) == 0)                {return "image/png";}
	if (len >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF)          {return "image/jpeg";}
	if (len >= 6 && (memcmp(d, "GIF87a", 6) == 0 || memcmp(d, "GIF89a", 6) == 0)){return "image/gif";}
	if (len >= 12 && memcmp(d, "RIFF", 4) == 0 && memcmp(d + 8, "WEBP", 4) == 0){return "image/webp";}
	return NULL;   // SVG compris : il peut contenir du JavaScript
}
