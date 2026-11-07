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

static const char *PAGE_START =
	"<!DOCTYPE html>\n"
	"<html lang=\"en\">\n"
	"<head>\n"
	"    <meta charset=\"UTF-8\">\n"
	"    <title>Guestbook</title>\n"
	"    <style>\n"
	"        body {background: #000; color: #e5e5e5; font-family: monospace; margin: 3rem; max-width: 40rem;}\n"
	"        a {color: #e5e5e5;}\n"
	"        input, textarea, button {background: #111; color: #e5e5e5; border: 1px solid #444; font: inherit; padding: .4rem;}\n"
	"        textarea {width: 100%; box-sizing: border-box;}\n"
	"        article {border-top: 1px solid #333; padding: .5rem 0;}\n"
	"        .meta {color: #888;}\n"
	"        .text {white-space: pre-wrap; overflow-wrap: anywhere;}\n"
	"    </style>\n"
	"</head>\n"
	"<body>\n"
	"    <h1>guestbook</h1>\n"
	"    <form method=\"post\" action=\"/guestbook\">\n"
	"        <p><input name=\"name\" placeholder=\"name\" maxlength=\"50\" required></p>\n"
	"        <p><textarea name=\"message\" placeholder=\"message\" maxlength=\"1000\" rows=\"4\" required></textarea></p>\n"
	"        <p><button>send</button></p>\n"
	"    </form>\n";

static const char *PAGE_END =
	"    <p><a href=\"/\">back</a></p>\n"
	"</body>\n"
	"</html>\n";

// appelée par db_messages_each() pour chaque message
static void append_message(void *ctx, const char *name, const char *message, const char *date){
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
	int count = db_messages_each(db, LIST_LIMIT, append_message, &page);
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

static int is_blank(const char *s){
	for (; *s != '\0'; s++){
		if (!isspace((unsigned char)*s)){return 0;}
	}
	return 1;
}

void guestbook_post(int client, Request *req){
	char name[NAME_BYTES + 1];
	char message[MESSAGE_BYTES + 1];

	// query_get échoue aussi si la valeur ne tient pas dans le buffer (trop longue)
	if (query_get(req->body, "name", name, sizeof(name)) < 0 || is_blank(name)
	 || query_get(req->body, "message", message, sizeof(message)) < 0 || is_blank(message)){
		send_error(client, 400);
		return;
	}

	sqlite3 *db = db_open();
	int ok = (db != NULL) && db_message_add(db, name, message) == 0;
	sqlite3_close(db);

	if (!ok){
		send_error(client, 500);
		return;
	}

	// Post/Redirect/Get : le navigateur recharge la page en GET,
	// donc F5 ne renvoie pas le formulaire une deuxième fois
	send_redirect(client, "/guestbook");
}
