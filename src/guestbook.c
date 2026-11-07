#include<stdio.h>
#include<ctype.h>
#include"guestbook.h"
#include"buffer.h"
#include"db.h"
#include"util.h"

// le formulaire limite en CARACTÈRES (maxlength="50" et "1000" dans PAGE_START),
// le serveur limite en OCTETS : en UTF-8 un caractère prend jusqu'à 4 octets
#define NAME_BYTES    (50 * 4)
#define MESSAGE_BYTES (1000 * 4)
#define LIST_LIMIT    100

/* ------------------------------------------------------------------ formulaire */

static int is_blank(const char *s){
	for (; *s != '\0'; s++){
		if (!isspace((unsigned char)*s)){return 0;}
	}
	return 1;
}

// lit et vérifie le formulaire (name[NAME_BYTES+1], message[MESSAGE_BYTES+1])
// renvoie NULL si tout va bien, sinon le message d'erreur
static const char *read_form(Request *req, char *name, char *message){
	// query_get échoue aussi si la valeur ne tient pas dans le buffer (trop longue)
	if (query_get(req->body, "name", name, NAME_BYTES + 1) < 0 || is_blank(name)){
		return "name is required (max 50 characters)";
	}
	if (query_get(req->body, "message", message, MESSAGE_BYTES + 1) < 0 || is_blank(message)){
		return "message is required (max 1000 characters)";
	}
	if (!utf8_valid(name) || !utf8_valid(message)){
		return "invalid UTF-8";
	}
	return NULL;
}

/* ------------------------------------------------------------------ version HTML */

static const char *PAGE_START =
	"<!DOCTYPE html>\n"
	"<html lang=\"en\">\n"
	"<head>\n"
	"    <meta charset=\"UTF-8\">\n"
	"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
	"    <title>guestbook · cServer</title>\n"
	"    <link rel=\"stylesheet\" href=\"/style.css\">\n"
	"    <script src=\"/guestbook.js\" defer></script>\n"
	"</head>\n"
	"<body>\n"
	"    <nav><a href=\"/\">home</a> <a href=\"/about.html\">about</a> <a href=\"/guestbook\">guestbook</a></nav>\n"
	"    <h1>guestbook</h1>\n"
	"    <form id=\"guestbook-form\" method=\"post\" action=\"/guestbook\">\n"
	"        <p><input name=\"name\" placeholder=\"name\" maxlength=\"50\" required></p>\n"
	"        <p><textarea name=\"message\" placeholder=\"message\" maxlength=\"1000\" rows=\"4\" required></textarea></p>\n"
	"        <p><button>send</button> <span id=\"error\" class=\"error\"></span></p>\n"
	"    </form>\n"
	"    <section id=\"messages\">\n";

static const char *PAGE_END =
	"    </section>\n"
	"</body>\n"
	"</html>\n";

// même structure HTML que render() dans www/guestbook.js
static void append_message_html(void *ctx, long id, const char *name, const char *message, const char *date){
	(void)id;
	Buffer *page = ctx;
	buf_append(page, "    <article>\n        <p class=\"meta\"><strong>");
	buf_append_html(page, name);
	buf_append(page, "</strong> · ");
	buf_append_html(page, date);
	buf_append(page, "</p>\n        <p class=\"text\">");
	buf_append_html(page, message);
	buf_append(page, "</p>\n    </article>\n");
}

void guestbook_list(int client, Request *req){
	(void)req;
	sqlite3 *db = db_open();
	if (db == NULL){
		send_error(client, 500);
		return;
	}

	Buffer page;
	buf_init(&page);
	buf_append(&page, PAGE_START);
	int count = db_messages_each(db, LIST_LIMIT, append_message_html, &page);
	if (count == 0){
		buf_append(&page, "    <p>No messages yet. Be the first!</p>\n");
	}
	buf_append(&page, PAGE_END);
	sqlite3_close(db);

	if (count < 0 || page.error){
		send_error(client, 500);
	}
	else {
		send_response(client, 200, "text/html; charset=utf-8", page.data, page.len);
	}
	buf_free(&page);
}

void guestbook_post(int client, Request *req){
	char name[NAME_BYTES + 1];
	char message[MESSAGE_BYTES + 1];
	if (read_form(req, name, message) != NULL){
		send_error(client, 400);
		return;
	}

	sqlite3 *db = db_open();
	int ok = (db != NULL) && db_message_add(db, name, message, NULL, NULL) == 0;
	sqlite3_close(db);

	if (!ok){
		send_error(client, 500);
		return;
	}

	// Post/Redirect/Get : le navigateur recharge la page en GET,
	// donc F5 ne renvoie pas le formulaire une deuxième fois
	send_redirect(client, "/guestbook");
}

/* ------------------------------------------------------------------ version JSON */

typedef struct {
	Buffer *buf;
	int     count;   // pour savoir s'il faut une virgule avant l'objet suivant
} JsonList;

// {"id":1,"name":"Jean","message":"Salut","date":"2026-11-07 17:50"}
static void append_message_json(void *ctx, long id, const char *name, const char *message, const char *date){
	JsonList *list = ctx;
	char num[32];
	snprintf(num, sizeof(num), "%ld", id);

	if (list->count++ > 0){
		buf_append(list->buf, ",");
	}
	buf_append(list->buf, "{\"id\":");
	buf_append(list->buf, num);
	buf_append(list->buf, ",\"name\":");
	buf_append_json(list->buf, name);
	buf_append(list->buf, ",\"message\":");
	buf_append_json(list->buf, message);
	buf_append(list->buf, ",\"date\":");
	buf_append_json(list->buf, date);
	buf_append(list->buf, "}");
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
	(void)req;
	sqlite3 *db = db_open();
	if (db == NULL){
		send_json_error(client, 500, "database unavailable");
		return;
	}

	Buffer json;
	buf_init(&json);
	JsonList list = { &json, 0 };
	buf_append(&json, "[");
	int count = db_messages_each(db, LIST_LIMIT, append_message_json, &list);
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
	char name[NAME_BYTES + 1];
	char message[MESSAGE_BYTES + 1];
	const char *error = read_form(req, name, message);
	if (error != NULL){
		send_json_error(client, 400, error);
		return;
	}

	// la réponse contient le message créé, tel que la base l'a enregistré
	Buffer json;
	buf_init(&json);
	JsonList list = { &json, 0 };

	sqlite3 *db = db_open();
	int ok = (db != NULL) && db_message_add(db, name, message, append_message_json, &list) == 0;
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
