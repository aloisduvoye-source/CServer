#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include"cserver.h"
#include"guestbook.h"
#include"messages.h"

// le formulaire limite en CARACTÈRES (maxlength="1000"),
// le serveur limite en OCTETS : en UTF-8 un caractère prend jusqu'à 4 octets
#define MESSAGE_BYTES (1000 * 4)
#define LIST_LIMIT    100

// contre le spam : au plus 30 messages par minute et par compte
#define MESSAGES_PER_MINUTE 30

// 1 si l'utilisateur peut encore poster ; la clé est son compte, pas son IP
// (plusieurs personnes peuvent partager une IP ; les comptes, eux, sont limités
// par le framework : signup_limit inscriptions par IP)
static int may_post(cs_ctx *c, const cs_user *user){
	char key[32];
	snprintf(key, sizeof(key), "user:%ld", user->id);
	return cs_rate_limit(c, "message", key, MESSAGES_PER_MINUTE, 60);
}

/* ------------------------------------------------------------------ outils */

static int is_blank(const char *s){
	for (; *s != '\0'; s++){
		if (!isspace((unsigned char)*s)){return 0;}
	}
	return 1;
}

// lit et vérifie le champ message (buffer de MESSAGE_BYTES + 1)
// renvoie NULL si tout va bien, sinon le message d'erreur
static const char *read_message(cs_ctx *c, char *message){
	// cs_form échoue aussi si la valeur ne tient pas dans le buffer (trop longue)
	if (cs_form(c, "message", message, MESSAGE_BYTES + 1) < 0 || is_blank(message)){
		return "message is required (max 1000 characters)";
	}
	if (!cs_utf8_valid(message)){
		return "invalid UTF-8";
	}
	return NULL;
}

// "42" -> 42 ; -1 si ce n'est pas un nombre positif
static long parse_id(const char *s){
	size_t n = strlen(s);
	if (n == 0 || n > 18){return -1;}
	long id = 0;
	for (; *s != '\0'; s++){
		if (!isdigit((unsigned char)*s)){return -1;}
		id = id * 10 + (*s - '0');
	}
	return id;
}

// contexte passé aux fonctions appelées pour chaque message
typedef struct {
	cs_ctx *c;
	cs_buf *buf;
	long    user_id;   // utilisateur connecté (0 si personne) : ses messages sont "à lui"
	int     count;     // JSON : pour savoir s'il faut une virgule
	int     error;     // HTML : un modèle n'a pas pu être rendu
} MessageList;

static long current_user_id(cs_ctx *c){
	const cs_user *user = cs_current_user(c);
	return user != NULL ? user->id : 0;
}

/* ------------------------------------------------------------------ version HTML */

// un message = le modèle message.html (la boucle se fait ici, en C)
static void append_message_html(void *ctx, long id, long author_id,
                                const char *name, const char *message, const char *date){
	MessageList *list = ctx;
	cs_vars v;
	cs_vars_init(&v);
	cs_set_long(&v, "id", id);
	cs_set(&v, "name", name);
	cs_set(&v, "message", message);
	cs_set(&v, "date", date);
	cs_set(&v, "mine", (list->user_id != 0 && author_id == list->user_id) ? "1" : "");

	if (v.error || cs_render(list->c, list->buf, "message.html", &v) < 0){
		list->error = 1;
	}
	cs_vars_free(&v);
}

void guestbook_list(cs_ctx *c){
	sqlite3 *db = cs_db(c);
	if (db == NULL){
		cs_error(c, 500);
		return;
	}

	cs_buf messages;
	cs_buf_init(&messages);
	MessageList list = { c, &messages, current_user_id(c), 0, 0 };
	int count = messages_each(db, LIST_LIMIT, append_message_html, &list);

	if (count < 0 || list.error || messages.error){
		cs_buf_free(&messages);
		cs_error(c, 500);
		return;
	}

	cs_vars v;
	cs_vars_init(&v);
	cs_set(&v, "title", "guestbook");
	cs_set(&v, "script", "/guestbook.js");
	cs_set_html(&v, "messages", messages.data);   // déjà du HTML : rendu par message.html
	cs_buf_free(&messages);
	cs_page(c, 200, "guestbook.html", &v);
}

void guestbook_post(cs_ctx *c){
	const cs_user *user = cs_require_user(c);   // sinon : redirection vers /login
	if (user == NULL){return;}

	char message[MESSAGE_BYTES + 1];
	if (read_message(c, message) != NULL){
		cs_error(c, 400);
		return;
	}
	if (!may_post(c, user)){
		cs_error(c, 429);
		return;
	}

	// le nom vient du compte, plus du formulaire : impossible de se faire passer pour un autre
	if (messages_add(cs_db(c), user->id, user->name, message, NULL, NULL) < 0){
		cs_error(c, 500);
		return;
	}

	// Post/Redirect/Get : le navigateur recharge la page en GET,
	// donc F5 ne renvoie pas le formulaire une deuxième fois
	cs_redirect(c, "/guestbook");
}

void guestbook_delete(cs_ctx *c){
	const cs_user *user = cs_require_user(c);
	if (user == NULL){return;}

	char id_text[32];
	long id = cs_form(c, "id", id_text, sizeof(id_text)) == 0 ? parse_id(id_text) : -1;
	int deleted = id > 0 ? messages_delete(cs_db(c), id, user->id) : 0;

	if (deleted < 0){
		cs_error(c, 500);
	}
	else if (deleted == 0){
		cs_error(c, 403);   // n'existe pas, ou appartient à quelqu'un d'autre
	}
	else {
		cs_redirect(c, "/guestbook");
	}
}

/* ------------------------------------------------------------------ version JSON */

// {"id":1,"name":"jean","message":"Salut","date":"2026-11-07 17:50","mine":true}
static void append_message_json(void *ctx, long id, long author_id,
                                const char *name, const char *message, const char *date){
	MessageList *list = ctx;
	if (list->count++ > 0){
		cs_buf_append(list->buf, ",");
	}
	cs_buf_append(list->buf, "{\"id\":");
	cs_buf_append_long(list->buf, id);
	cs_buf_append(list->buf, ",\"name\":");
	cs_buf_append_json(list->buf, name);
	cs_buf_append(list->buf, ",\"message\":");
	cs_buf_append_json(list->buf, message);
	cs_buf_append(list->buf, ",\"date\":");
	cs_buf_append_json(list->buf, date);
	cs_buf_append(list->buf, (list->user_id != 0 && author_id == list->user_id) ? ",\"mine\":true}" : ",\"mine\":false}");
}

void api_messages_list(cs_ctx *c){
	sqlite3 *db = cs_db(c);
	if (db == NULL){
		cs_json_error(c, 500, "database unavailable");
		return;
	}

	cs_buf json;
	cs_buf_init(&json);
	MessageList list = { c, &json, current_user_id(c), 0, 0 };
	cs_buf_append(&json, "[");
	int count = messages_each(db, LIST_LIMIT, append_message_json, &list);
	cs_buf_append(&json, "]\n");

	if (count < 0){
		cs_buf_free(&json);
		cs_json_error(c, 500, "database error");
		return;
	}
	cs_json(c, 200, &json);
}

void api_messages_post(cs_ctx *c){
	const cs_user *user = cs_current_user(c);
	if (user == NULL){
		cs_json_error(c, 401, "login required");   // une API ne redirige pas : elle explique
		return;
	}

	char message[MESSAGE_BYTES + 1];
	const char *error = read_message(c, message);
	if (error != NULL){
		cs_json_error(c, 400, error);
		return;
	}
	if (!may_post(c, user)){
		cs_json_error(c, 429, "too many messages, slow down");
		return;
	}

	// la réponse contient le message créé, tel que la base l'a enregistré
	cs_buf json;
	cs_buf_init(&json);
	MessageList list = { c, &json, user->id, 0, 0 };
	if (messages_add(cs_db(c), user->id, user->name, message, append_message_json, &list) < 0){
		cs_buf_free(&json);
		cs_json_error(c, 500, "database error");
		return;
	}
	cs_buf_append(&json, "\n");
	cs_json(c, 201, &json);
}
