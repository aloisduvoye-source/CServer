#include<stdio.h>
#include<string.h>
#include<ctype.h>
#include"cserver.h"
#include"guestbook.h"

// le formulaire limite en CARACTÈRES (maxlength="1000"),
// le serveur limite en OCTETS : en UTF-8 un caractère prend jusqu'à 4 octets
#define MESSAGE_BYTES (1000 * 4)
#define LIST_LIMIT    100

/* ------------------------------------------------------------------ outils */

static int is_blank(const char *s){
	for (; *s != '\0'; s++){
		if (!isspace((unsigned char)*s)){return 0;}
	}
	return 1;
}

// lit et vérifie le champ message (buffer de MESSAGE_BYTES + 1)
// renvoie NULL si tout va bien, sinon le message d'erreur
static const char *read_message(Request *req, char *message){
	// query_get échoue aussi si la valeur ne tient pas dans le buffer (trop longue)
	if (query_get(req->body, "message", message, MESSAGE_BYTES + 1) < 0 || is_blank(message)){
		return "message is required (max 1000 characters)";
	}
	if (!utf8_valid(message)){
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
	Buffer *buf;
	long    user_id;   // utilisateur connecté (0 si personne) : ses messages sont "à lui"
	int     count;     // JSON : pour savoir s'il faut une virgule
} MessageCtx;

/* ------------------------------------------------------------------ version HTML */

static const char *FORM =
	"    <form id=\"guestbook-form\" method=\"post\" action=\"/guestbook\">\n"
	"        <p><textarea name=\"message\" placeholder=\"message\" maxlength=\"1000\" rows=\"4\" required></textarea></p>\n"
	"        <p><button>send</button> <span id=\"error\" class=\"error\"></span></p>\n"
	"    </form>\n";

static const char *LOGIN_INVITE =
	"    <p><a href=\"/login\">log in</a> or <a href=\"/signup\">sign up</a> to leave a message.</p>\n";

// même structure HTML que render() dans www/guestbook.js
static void append_message_html(void *ctx, long id, long author_id,
                                const char *name, const char *message, const char *date){
	MessageCtx *c = ctx;
	buf_append(c->buf, "    <article>\n        <p class=\"meta\"><strong>");
	buf_append_html(c->buf, name);
	buf_append(c->buf, "</strong> · ");
	buf_append_html(c->buf, date);

	if (c->user_id != 0 && author_id == c->user_id){
		char num[32];
		snprintf(num, sizeof(num), "%ld", id);
		buf_append(c->buf,
			" <form class=\"inline\" method=\"post\" action=\"/guestbook/delete\">"
			"<input type=\"hidden\" name=\"id\" value=\"");
		buf_append(c->buf, num);
		buf_append(c->buf, "\"><button class=\"link\">delete</button></form>");
	}

	buf_append(c->buf, "</p>\n        <p class=\"text\">");
	buf_append_html(c->buf, message);
	buf_append(c->buf, "</p>\n    </article>\n");
}

void guestbook_list(int client, Request *req){
	sqlite3 *db = db_open();
	if (db == NULL){
		send_error(client, 500);
		return;
	}

	User user;
	int logged = auth_current_user(db, req, &user);

	Buffer page;
	buf_init(&page);
	page_begin(&page, "guestbook", logged ? &user : NULL, "/guestbook.js");
	buf_append(&page, "    <h1>guestbook</h1>\n");
	buf_append(&page, logged ? FORM : LOGIN_INVITE);
	buf_append(&page, "    <section id=\"messages\">\n");

	MessageCtx ctx = { &page, logged ? user.id : 0, 0 };
	int count = db_messages_each(db, LIST_LIMIT, append_message_html, &ctx);
	if (count == 0){
		buf_append(&page, "    <p>No messages yet. Be the first!</p>\n");
	}

	buf_append(&page, "    </section>\n");
	page_end(&page);
	sqlite3_close(db);

	if (count < 0){
		buf_free(&page);
		send_error(client, 500);
		return;
	}
	page_send(client, 200, &page);
}

void guestbook_post(int client, Request *req){
	sqlite3 *db = db_open();
	if (db == NULL){
		send_error(client, 500);
		return;
	}

	User user;
	if (!auth_current_user(db, req, &user)){
		sqlite3_close(db);
		send_redirect(client, "/login", NULL);
		return;
	}

	char message[MESSAGE_BYTES + 1];
	if (read_message(req, message) != NULL){
		sqlite3_close(db);
		send_error(client, 400);
		return;
	}

	// le nom vient du compte, plus du formulaire : impossible de se faire passer pour un autre
	int ok = db_message_add(db, user.id, user.name, message, NULL, NULL) == 0;
	sqlite3_close(db);

	if (!ok){
		send_error(client, 500);
		return;
	}

	// Post/Redirect/Get : le navigateur recharge la page en GET,
	// donc F5 ne renvoie pas le formulaire une deuxième fois
	send_redirect(client, "/guestbook", NULL);
}

void guestbook_delete(int client, Request *req){
	sqlite3 *db = db_open();
	if (db == NULL){
		send_error(client, 500);
		return;
	}

	User user;
	if (!auth_current_user(db, req, &user)){
		sqlite3_close(db);
		send_redirect(client, "/login", NULL);
		return;
	}

	char id_text[32];
	long id = query_get(req->body, "id", id_text, sizeof(id_text)) == 0 ? parse_id(id_text) : -1;
	int deleted = id > 0 ? db_message_delete(db, id, user.id) : 0;
	sqlite3_close(db);

	if (deleted < 0){
		send_error(client, 500);
	}
	else if (deleted == 0){
		send_error(client, 403);   // n'existe pas, ou appartient à quelqu'un d'autre
	}
	else {
		send_redirect(client, "/guestbook", NULL);
	}
}

/* ------------------------------------------------------------------ version JSON */

// {"id":1,"name":"jean","message":"Salut","date":"2026-11-07 17:50","mine":true}
static void append_message_json(void *ctx, long id, long author_id,
                                const char *name, const char *message, const char *date){
	MessageCtx *c = ctx;
	char num[32];
	snprintf(num, sizeof(num), "%ld", id);

	if (c->count++ > 0){
		buf_append(c->buf, ",");
	}
	buf_append(c->buf, "{\"id\":");
	buf_append(c->buf, num);
	buf_append(c->buf, ",\"name\":");
	buf_append_json(c->buf, name);
	buf_append(c->buf, ",\"message\":");
	buf_append_json(c->buf, message);
	buf_append(c->buf, ",\"date\":");
	buf_append_json(c->buf, date);
	buf_append(c->buf, (c->user_id != 0 && author_id == c->user_id) ? ",\"mine\":true}" : ",\"mine\":false}");
}

// {"error":"..."}
static void send_json_error(int client, int status, const char *error){
	Buffer json;
	buf_init(&json);
	buf_append(&json, "{\"error\":");
	buf_append_json(&json, error);
	buf_append(&json, "}\n");

	if (json.error){
		send_error(client, 500);
	}
	else {
		send_response(client, status, "application/json", json.data, json.len);
	}
	buf_free(&json);
}

void api_messages_list(int client, Request *req){
	sqlite3 *db = db_open();
	if (db == NULL){
		send_json_error(client, 500, "database unavailable");
		return;
	}

	User user;
	int logged = auth_current_user(db, req, &user);

	Buffer json;
	buf_init(&json);
	MessageCtx ctx = { &json, logged ? user.id : 0, 0 };
	buf_append(&json, "[");
	int count = db_messages_each(db, LIST_LIMIT, append_message_json, &ctx);
	buf_append(&json, "]\n");
	sqlite3_close(db);

	if (count < 0 || json.error){
		send_json_error(client, 500, "database error");
	}
	else {
		send_response(client, 200, "application/json", json.data, json.len);
	}
	buf_free(&json);
}

void api_messages_post(int client, Request *req){
	sqlite3 *db = db_open();
	if (db == NULL){
		send_json_error(client, 500, "database unavailable");
		return;
	}

	User user;
	if (!auth_current_user(db, req, &user)){
		sqlite3_close(db);
		send_json_error(client, 401, "login required");
		return;
	}

	char message[MESSAGE_BYTES + 1];
	const char *error = read_message(req, message);
	if (error != NULL){
		sqlite3_close(db);
		send_json_error(client, 400, error);
		return;
	}

	// la réponse contient le message créé, tel que la base l'a enregistré
	Buffer json;
	buf_init(&json);
	MessageCtx ctx = { &json, user.id, 0 };
	int ok = db_message_add(db, user.id, user.name, message, append_message_json, &ctx) == 0;
	sqlite3_close(db);
	buf_append(&json, "\n");

	if (!ok || json.error){
		send_json_error(client, 500, "database error");
	}
	else {
		send_response(client, 201, "application/json", json.data, json.len);
	}
	buf_free(&json);
}
