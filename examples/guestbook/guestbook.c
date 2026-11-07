#include<string.h>
#include<ctype.h>
#include"cserver.h"
#include"guestbook.h"
#include"messages.h"

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
	cs_buf *buf;
	long    user_id;   // utilisateur connecté (0 si personne) : ses messages sont "à lui"
	int     count;     // JSON : pour savoir s'il faut une virgule
} MessageList;

static long current_user_id(cs_ctx *c){
	const cs_user *user = cs_current_user(c);
	return user != NULL ? user->id : 0;
}

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
	MessageList *list = ctx;
	cs_buf_append(list->buf, "    <article>\n        <p class=\"meta\"><strong>");
	cs_buf_append_html(list->buf, name);
	cs_buf_append(list->buf, "</strong> · ");
	cs_buf_append_html(list->buf, date);

	if (list->user_id != 0 && author_id == list->user_id){
		cs_buf_append(list->buf,
			" <form class=\"inline\" method=\"post\" action=\"/guestbook/delete\">"
			"<input type=\"hidden\" name=\"id\" value=\"");
		cs_buf_append_long(list->buf, id);
		cs_buf_append(list->buf, "\"><button class=\"link\">delete</button></form>");
	}

	cs_buf_append(list->buf, "</p>\n        <p class=\"text\">");
	cs_buf_append_html(list->buf, message);
	cs_buf_append(list->buf, "</p>\n    </article>\n");
}

void guestbook_list(cs_ctx *c){
	sqlite3 *db = cs_db(c);
	if (db == NULL){
		cs_error(c, 500);
		return;
	}

	cs_buf page;
	cs_buf_init(&page);
	cs_page_begin(c, &page, "guestbook", "/guestbook.js");
	cs_buf_append(&page, "    <h1>guestbook</h1>\n");
	cs_buf_append(&page, cs_current_user(c) != NULL ? FORM : LOGIN_INVITE);
	cs_buf_append(&page, "    <section id=\"messages\">\n");

	MessageList list = { &page, current_user_id(c), 0 };
	int count = messages_each(db, LIST_LIMIT, append_message_html, &list);
	if (count == 0){
		cs_buf_append(&page, "    <p>No messages yet. Be the first!</p>\n");
	}

	cs_buf_append(&page, "    </section>\n");
	cs_page_end(&page);

	if (count < 0){
		cs_buf_free(&page);
		cs_error(c, 500);
		return;
	}
	cs_html(c, 200, &page);
}

void guestbook_post(cs_ctx *c){
	const cs_user *user = cs_require_user(c);   // sinon : redirection vers /login
	if (user == NULL){return;}

	char message[MESSAGE_BYTES + 1];
	if (read_message(c, message) != NULL){
		cs_error(c, 400);
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
	MessageList list = { &json, current_user_id(c), 0 };
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

	// la réponse contient le message créé, tel que la base l'a enregistré
	cs_buf json;
	cs_buf_init(&json);
	MessageList list = { &json, user->id, 0 };
	if (messages_add(cs_db(c), user->id, user->name, message, append_message_json, &list) < 0){
		cs_buf_free(&json);
		cs_json_error(c, 500, "database error");
		return;
	}
	cs_buf_append(&json, "\n");
	cs_json(c, 201, &json);
}
