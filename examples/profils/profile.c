#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<ctype.h>
#include"profile.h"
#include"members.h"

// contre le spam : au plus 20 changements d'avatar par heure et par compte
#define AVATARS_PER_HOUR 20

/* ---------------------------------------------------------------- avatars en HTML */

// l'avatar d'un membre (modèle avatar.html) : son image, ou l'initiale de son nom.
// size : "small" ou "large" (classes CSS). Renvoie 0, ou -1 si le modèle a échoué.
static int render_avatar(cs_ctx *c, cs_buf *out, const member *m, const char *size){
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "size", size);
	if (m->avatar > 0){
		// la version dans l'URL : une nouvelle image a une nouvelle adresse, le
		// navigateur ne réaffiche pas l'ancienne qu'il a gardée en cache
		char url[64];
		snprintf(url, sizeof(url), "/avatars/%ld?v=%ld", m->id, m->avatar);
		cs_set(&v, "url", url);
	}
	char initial[2] = { (char)toupper((unsigned char)m->name[0]), '\0' };
	cs_set(&v, "initial", initial);

	int r = v.error ? -1 : cs_render(c, out, "avatar.html", &v);
	cs_vars_free(&v);
	return r;
}

// la même chose, directement dans une variable de modèle ; 0 ou -1
static int set_avatar(cs_ctx *c, cs_vars *v, const member *m, const char *size){
	cs_buf html;
	cs_buf_init(&html);
	int r = render_avatar(c, &html, m, size);
	if (r == 0 && !html.error){cs_set_html(v, "avatar", html.data);}
	else {r = -1;}
	cs_buf_free(&html);
	return r;
}

/* ---------------------------------------------------------------- pages publiques */

typedef struct {
	cs_ctx *c;
	cs_buf *buf;
	int     error;
} member_list;

// un membre = le modèle member.html (la boucle se fait ici, en C)
static void append_member(void *ctx, const member *m){
	member_list *list = ctx;
	cs_buf avatar;
	cs_buf_init(&avatar);
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "name", m->name);
	cs_set(&v, "since", m->since);
	if (render_avatar(list->c, &avatar, m, "small") < 0 || avatar.error){list->error = 1;}
	else {cs_set_html(&v, "avatar", avatar.data);}
	cs_buf_free(&avatar);

	if (v.error || cs_render(list->c, list->buf, "member.html", &v) < 0){list->error = 1;}
	cs_vars_free(&v);
}

void members_page(cs_ctx *c){
	sqlite3 *db = cs_db(c);
	if (db == NULL){
		cs_error(c, 500);
		return;
	}

	cs_buf rows;
	cs_buf_init(&rows);
	member_list list = { c, &rows, 0 };
	int count = members_each(db, append_member, &list);
	if (count < 0 || list.error || rows.error){
		cs_buf_free(&rows);
		cs_error(c, 500);
		return;
	}

	cs_vars v;
	cs_vars_init(&v);
	cs_set_long(&v, "count", count);
	cs_set_html(&v, "members", rows.data != NULL ? rows.data : "");
	cs_buf_free(&rows);
	cs_page(c, 200, "index.html", &v);
}

void member_page(cs_ctx *c){
	char name[64];
	member m;
	sqlite3 *db = cs_db(c);
	if (db == NULL){
		cs_error(c, 500);
		return;
	}
	int found = cs_param(c, "name", name, sizeof(name)) == 0 ? member_by_name(db, name, &m) : 0;
	if (found < 0){
		cs_error(c, 500);
		return;
	}
	if (found == 0){
		cs_error(c, 404);
		return;
	}

	const cs_user *me = cs_current_user(c);
	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", m.name);
	cs_set(&v, "name", m.name);
	cs_set(&v, "since", m.since);
	cs_set(&v, "me", me != NULL && me->id == m.id ? "1" : "");
	// pas d'e-mail ici : la page est publique
	if (set_avatar(c, &v, &m, "large") < 0){
		cs_vars_free(&v);
		cs_error(c, 500);
		return;
	}
	cs_page(c, 200, "user.html", &v);
}

/* ---------------------------------------------------------------- son profil */

// la page /profile, avec un message (done) ou une erreur ; email : la valeur à
// afficher dans le champ (NULL : celle de la base)
static void profile_form(cs_ctx *c, int status, const char *done, const char *error, const char *email){
	const cs_user *user = cs_current_user(c);
	sqlite3 *db = cs_db(c);
	member m;
	if (user == NULL || db == NULL || member_by_id(db, user->id, &m) != 1){
		cs_error(c, 500);
		return;
	}

	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "my profile");
	cs_set(&v, "name", m.name);
	cs_set(&v, "email", email != NULL ? email : m.email);
	cs_set(&v, "since", m.since);
	cs_set(&v, "has_avatar", m.avatar > 0 ? "1" : "");
	cs_set_long(&v, "avatar_max_kb", AVATAR_MAX / 1024);
	if (done != NULL){cs_set(&v, "done", done);}
	if (error != NULL){cs_set(&v, "error", error);}
	if (set_avatar(c, &v, &m, "large") < 0){
		cs_vars_free(&v);
		cs_error(c, 500);
		return;
	}
	cs_page(c, status, "profile.html", &v);
}

void profile_page(cs_ctx *c){
	if (cs_require_user(c) == NULL){return;}   // sinon : redirection vers /login

	// après une modification (Post/Redirect/Get) : /profile?done=email...
	// Le message est choisi ici, jamais recopié de l'URL
	char done[16];
	const char *message = NULL;
	if (cs_query(c, "done", done, sizeof(done)) == 0){
		if (strcmp(done, "email") == 0)  {message = "email saved";}
		if (strcmp(done, "avatar") == 0) {message = "avatar saved";}
		if (strcmp(done, "removed") == 0){message = "avatar removed";}
	}
	profile_form(c, 200, message, NULL, NULL);
}

void profile_email(cs_ctx *c){
	const cs_user *user = cs_require_user(c);
	if (user == NULL){return;}

	char email[EMAIL_MAX + 2];
	if (cs_form(c, "email", email, sizeof(email)) < 0){email[0] = '\0';}
	if (!email_valid(email)){
		profile_form(c, 400, NULL, "invalid email address", email);
		return;
	}
	switch (profile_set_email(cs_db(c), user->id, email)){
		case 1:  cs_redirect(c, "/profile?done=email"); break;
		case 0:  profile_form(c, 409, NULL, "this email is already used", email); break;
		default: cs_error(c, 500);
	}
}

void profile_avatar(cs_ctx *c){
	const cs_user *user = cs_require_user(c);
	if (user == NULL){return;}

	// le formulaire est en multipart/form-data : cs_form_file lit le fichier, et le
	// framework a déjà vérifié le jeton CSRF (un champ du même formulaire)
	cs_file file;
	if (cs_form_file(c, "avatar", &file) < 0){
		profile_form(c, 400, NULL, "choose an image", NULL);
		return;
	}
	if (file.len > AVATAR_MAX){
		profile_form(c, 413, NULL, "image too large (max 256 KB)", NULL);
		return;
	}
	// le type vient du contenu, pas du nom du fichier ni de ce qu'annonce le navigateur
	const char *type = cs_image_type(file.data, file.len);
	if (type == NULL){
		profile_form(c, 415, NULL, "only PNG, JPEG, GIF or WebP images", NULL);
		return;
	}

	char key[32];
	snprintf(key, sizeof(key), "user:%ld", user->id);
	if (!cs_rate_limit(c, "avatar", key, AVATARS_PER_HOUR, 3600)){
		profile_form(c, 429, NULL, "too many changes, try again later", NULL);
		return;
	}

	if (avatar_set(cs_db(c), user->id, type, file.data, file.len) < 0){
		cs_error(c, 500);
		return;
	}
	cs_redirect(c, "/profile?done=avatar");
}

void profile_avatar_delete(cs_ctx *c){
	const cs_user *user = cs_require_user(c);
	if (user == NULL){return;}
	if (avatar_delete(cs_db(c), user->id) < 0){
		cs_error(c, 500);
		return;
	}
	cs_redirect(c, "/profile?done=removed");
}

/* ---------------------------------------------------------------- l'image */

void avatar_image(cs_ctx *c){
	long id = cs_param_id(c, "id");
	sqlite3 *db = cs_db(c);
	char type[32];
	char *data = NULL;
	size_t len = 0;
	int found = id > 0 && db != NULL ? avatar_get(db, id, type, sizeof(type), &data, &len) : 0;
	if (found < 0){
		cs_error(c, 500);
		return;
	}
	if (found == 0){
		cs_error(c, 404);
		return;
	}

	// gardée un jour par le navigateur (l'URL change avec chaque nouvelle image)
	cs_header(c, "Cache-Control: public, max-age=86400");
	// ouverte seule dans un onglet, l'image ne peut rien charger ni exécuter : une
	// protection de plus, au cas où un fichier piégé passerait pour une image
	cs_header(c, "Content-Security-Policy: default-src 'none'");
	cs_send(c, 200, type, data, len);
	free(data);
}
