#include"page.h"
#include"http.h"
#include"config.h"

void page_begin(Buffer *b, const char *title, const User *user, const char *script){
	buf_append(b,
		"<!DOCTYPE html>\n"
		"<html lang=\"en\">\n"
		"<head>\n"
		"    <meta charset=\"UTF-8\">\n"
		"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
		"    <title>");
	buf_append_html(b, title);
	buf_append(b, " · ");
	buf_append_html(b, cs_config.name);
	buf_append(b,
		"</title>\n"
		"    <link rel=\"stylesheet\" href=\"/style.css\">\n");
	if (script != NULL){
		buf_append(b, "    <script src=\"");
		buf_append(b, script);
		buf_append(b, "\" defer></script>\n");
	}
	buf_append(b,
		"</head>\n"
		"<body>\n"
		"    <nav><a href=\"/\">home</a> <a href=\"/about.html\">about</a> <a href=\"/guestbook\">guestbook</a>\n"
		"        <span class=\"user\">");

	if (user != NULL){
		// déconnexion en POST : un simple lien (GET) pourrait être déclenché
		// par n'importe quelle page, ou par le préchargement du navigateur
		buf_append_html(b, user->name);
		buf_append(b,
			" <form class=\"inline\" method=\"post\" action=\"/logout\">"
			"<button class=\"link\">log out</button></form>");
	}
	else {
		buf_append(b, "<a href=\"/login\">log in</a> <a href=\"/signup\">sign up</a>");
	}

	buf_append(b, "</span></nav>\n");
}

void page_end(Buffer *b){
	buf_append(b,
		"</body>\n"
		"</html>\n");
}

void page_send(int client, int status, Buffer *b){
	if (b->error){
		send_error(client, 500);
	}
	else {
		send_response(client, status, "text/html; charset=utf-8", b->data, b->len);
	}
	buf_free(b);
}
