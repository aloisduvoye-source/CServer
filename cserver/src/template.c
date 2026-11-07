#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"internal.h"

/* ---------------------------------------------------------------- variables */

void cs_vars_init(cs_vars *v){
	memset(v, 0, sizeof(*v));
}

void cs_vars_free(cs_vars *v){
	for (int i = 0; i < v->count; i++){
		free(v->items[i].value);
	}
	v->count = 0;
}

static void set_var(cs_vars *v, const char *name, const char *value, int raw){
	char *copy = strdup(value != NULL ? value : "");
	if (copy == NULL){
		v->error = 1;
		return;
	}

	// même nom : on remplace l'ancienne valeur
	for (int i = 0; i < v->count; i++){
		if (strcmp(v->items[i].name, name) == 0){
			free(v->items[i].value);
			v->items[i].value = copy;
			v->items[i].raw = raw;
			return;
		}
	}

	if (v->count == CS_VARS_MAX){
		fprintf(stderr, "cs_set(%s) : plus de %d variables\n", name, CS_VARS_MAX);
		free(copy);
		v->error = 1;
		return;
	}
	v->items[v->count].name = name;
	v->items[v->count].value = copy;
	v->items[v->count].raw = raw;
	v->count++;
}

void cs_set(cs_vars *v, const char *name, const char *text){
	set_var(v, name, text, 0);
}

void cs_set_html(cs_vars *v, const char *name, const char *html){
	set_var(v, name, html, 1);
}

void cs_set_long(cs_vars *v, const char *name, long n){
	char num[32];
	snprintf(num, sizeof(num), "%ld", n);
	set_var(v, name, num, 0);
}

// d'abord les variables du handler, puis celles que le framework fournit toujours
static const char *lookup(cs_ctx *c, const cs_vars *v, const char *name, int *raw){
	*raw = 0;
	for (int i = 0; v != NULL && i < v->count; i++){
		if (strcmp(v->items[i].name, name) == 0){
			*raw = v->items[i].raw;
			return v->items[i].value;
		}
	}
	if (strcmp(name, "site_name") == 0){
		return cs_config.name;
	}
	if (strcmp(name, "login_path") == 0){
		return cs_config.login_path;
	}
	if (strcmp(name, "signup_path") == 0){
		return cs_config.signup_path;
	}
	if (strcmp(name, "accounts") == 0){
		return cs_config.accounts ? "1" : "";
	}
	if (strcmp(name, "user") == 0){
		const cs_user *user = cs_current_user(c);
		return user != NULL ? user->name : "";
	}
	return "";
}

/* ---------------------------------------------------------------- chargement */

// un modèle n'est lu qu'une fois par requête, même s'il est rendu 100 fois (un par
// message). Chaque requête est un processus neuf (fork) : le cache repart à vide, donc
// une modification du fichier est visible dès la requête suivante.
#define CACHE_MAX 32
static struct {
	char  name[64];
	char *text;
} cache[CACHE_MAX];
static int cache_count;

static char *read_file(const char *path){
	FILE *f = fopen(path, "r");
	if (f == NULL){return NULL;}

	cs_buf b;
	cs_buf_init(&b);
	char chunk[4096];
	size_t n;
	while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0){
		buf_append_n(&b, chunk, n);
	}
	fclose(f);

	if (b.error){
		cs_buf_free(&b);
		return NULL;
	}
	return b.data;   // appartient désormais à l'appelant
}

// le fichier du site s'il existe, sinon le modèle par défaut du framework
static const char *load_template(const char *name){
	for (int i = 0; i < cache_count; i++){
		if (strcmp(cache[i].name, name) == 0){return cache[i].text;}
	}

	if (name[0] == '\0' || name[0] == '/' || strstr(name, "..") != NULL || strlen(name) >= sizeof(cache[0].name)){
		fprintf(stderr, "modèle %s : nom invalide\n", name);
		return NULL;
	}

	char path[512];
	snprintf(path, sizeof(path), "%s/%s", cs_config.templates_dir, name);
	char *text = read_file(path);

	for (const embedded_file *f = framework_templates; text == NULL && f->name != NULL; f++){
		if (strcmp(f->name, name) == 0){
			text = strdup(f->text);
		}
	}
	if (text == NULL){
		fprintf(stderr, "modèle %s : introuvable (ni %s, ni dans le framework)\n", name, path);
		return NULL;
	}

	// cache plein : on renvoie quand même le texte (il sera libéré à la fin du processus)
	if (cache_count < CACHE_MAX){
		snprintf(cache[cache_count].name, sizeof(cache[0].name), "%s", name);
		cache[cache_count].text = text;
		cache_count++;
	}
	return text;
}

void template_cache_clear(void){
	for (int i = 0; i < cache_count; i++){
		free(cache[i].text);
	}
	cache_count = 0;
}

/* ---------------------------------------------------------------- rendu */

// comme strstr, mais dans [p, end) : le texte d'une section ne finit pas par '\0'
static const char *find(const char *p, const char *end, const char *needle){
	size_t n = strlen(needle);
	for (; p + n <= end; p++){
		if (memcmp(p, needle, n) == 0){return p;}
	}
	return NULL;
}

// copie le nom d'une balise en retirant les espaces autour : "{{ nom }}" -> "nom"
static int tag_name(const char *start, const char *stop, char *out, size_t size){
	while (start < stop && *start == ' '){start++;}
	while (stop > start && stop[-1] == ' '){stop--;}
	size_t n = stop - start;
	if (n == 0 || n >= size){return -1;}
	memcpy(out, start, n);
	out[n] = '\0';
	return 0;
}

// Une balise de section ou de commentaire seule sur sa ligne (à des espaces près)
// fait disparaître la ligne entière, pour ne pas laisser de ligne vide dans le HTML.
// Renvoie 1 si c'est le cas : *begin = début de la ligne, *next = juste après son '\n'.
// text : début du modèle entier (la ligne peut commencer avant la section en cours)
static int standalone(const char *text, const char *open, const char *after, const char *end,
                      const char **begin, const char **next){
	const char *b = open;
	while (b > text && (b[-1] == ' ' || b[-1] == '\t')){b--;}
	if (b > text && b[-1] != '\n'){return 0;}   // du texte avant la balise

	const char *n = after;
	while (n < end && (*n == ' ' || *n == '\t' || *n == '\r')){n++;}
	if (n < end && *n != '\n'){return 0;}       // du texte après la balise
	if (n < end){n++;}

	*begin = b;
	*next = n;
	return 1;
}

static int render_range(cs_ctx *c, cs_buf *out, const char *text, const char *p, const char *end,
                        const cs_vars *v, const char *tpl){
	while (p < end){
		const char *open = find(p, end, "{{");
		if (open == NULL){
			buf_append_n(out, p, end - p);
			return 0;
		}

		// {{{nom}}} : insérée telle quelle
		int triple = open + 2 < end && open[2] == '{';
		const char *inner = open + (triple ? 3 : 2);
		const char *close = find(inner, end, triple ? "}}}" : "}}");
		if (close == NULL){
			fprintf(stderr, "modèle %s : balise {{ sans }}\n", tpl);
			return -1;
		}
		const char *after = close + (triple ? 3 : 2);

		// premier caractère de la balise : ! # ^ / ou le début d'un nom
		const char *k = inner;
		while (k < close && *k == ' '){k++;}
		char kind = triple ? '{' : (k < close ? *k : '\0');
		int block = kind == '!' || kind == '#' || kind == '^' || kind == '/';

		const char *line_begin, *line_next;
		int alone = block && standalone(text, open, after, end, &line_begin, &line_next);
		buf_append_n(out, p, (alone ? line_begin : open) - p);
		p = alone ? line_next : after;

		// {{! commentaire }} : ignoré, quelle que soit sa longueur
		if (kind == '!'){
			continue;
		}

		char tag[64];
		if (tag_name(inner, close, tag, sizeof(tag)) < 0){
			fprintf(stderr, "modèle %s : nom de balise vide ou trop long\n", tpl);
			return -1;
		}

		int raw;
		if (triple){
			cs_buf_append(out, lookup(c, v, tag, &raw));
			continue;
		}

		switch (tag[0]){
			case '#':   // section : affichée si la variable n'est pas vide
			case '^': { // section inversée : affichée si elle est vide
				char name[64];
				char closing[80];
				if (tag_name(tag + 1, tag + strlen(tag), name, sizeof(name)) < 0){
					fprintf(stderr, "modèle %s : {{%c}} sans nom\n", tpl, tag[0]);
					return -1;
				}
				snprintf(closing, sizeof(closing), "{{/%s}}", name);
				const char *section_end = find(p, end, closing);
				if (section_end == NULL){
					fprintf(stderr, "modèle %s : {{%c%s}} sans %s\n", tpl, tag[0], name, closing);
					return -1;
				}

				// la balise fermante seule sur sa ligne : la ligne disparaît aussi
				const char *close_after = section_end + strlen(closing);
				const char *close_begin, *close_next;
				int close_alone = standalone(text, section_end, close_after, end, &close_begin, &close_next);
				const char *body_end = close_alone ? close_begin : section_end;
				if (body_end < p){body_end = p;}

				int not_empty = lookup(c, v, name, &raw)[0] != '\0';
				if (not_empty == (tag[0] == '#')){
					if (render_range(c, out, text, p, body_end, v, tpl) < 0){return -1;}
				}
				p = close_alone ? close_next : close_after;
				break;
			}

			case '/':
				fprintf(stderr, "modèle %s : {{%s}} sans ouverture\n", tpl, tag);
				return -1;

			default: {  // variable : échappée, sauf si le handler l'a donnée avec cs_set_html
				const char *value = lookup(c, v, tag, &raw);
				if (raw){
					cs_buf_append(out, value);
				}
				else {
					cs_buf_append_html(out, value);
				}
			}
		}
	}
	return 0;
}

int template_render_text(cs_ctx *c, cs_buf *out, const char *text, size_t len, const cs_vars *v){
	return render_range(c, out, text, text, text + len, v, "(texte)");
}

int cs_render(cs_ctx *c, cs_buf *out, const char *tpl, const cs_vars *v){
	const char *text = load_template(tpl);
	if (text == NULL){return -1;}
	return render_range(c, out, text, text, text + strlen(text), v, tpl);
}

void cs_page(cs_ctx *c, int status, const char *tpl, cs_vars *v){
	cs_vars empty;
	if (v == NULL){
		cs_vars_init(&empty);
		v = &empty;
	}

	// 1. la page elle-même
	cs_buf content;
	cs_buf_init(&content);
	int ok = cs_render(c, &content, tpl, v) == 0 && !content.error;
	if (ok){
		cs_set_html(v, "content", content.data);
	}
	cs_buf_free(&content);

	// 2. placée dans le layout
	cs_buf page;
	cs_buf_init(&page);
	ok = ok && !v->error && cs_render(c, &page, "layout.html", v) == 0;
	cs_vars_free(v);

	if (!ok){
		cs_buf_free(&page);
		cs_error(c, 500);
		return;
	}
	cs_html(c, status, &page);
}
