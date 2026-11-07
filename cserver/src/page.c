#include"internal.h"

void cs_page_begin(cs_ctx *c, cs_buf *b, const char *title, const char *script){
	cs_buf_append(b,
		"<!DOCTYPE html>\n"
		"<html lang=\"en\">\n"
		"<head>\n"
		"    <meta charset=\"UTF-8\">\n"
		"    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
		"    <title>");
	cs_buf_append_html(b, title);
	cs_buf_append(b, " · ");
	cs_buf_append_html(b, cs_config.name);
	cs_buf_append(b,
		"</title>\n"
		"    <link rel=\"stylesheet\" href=\"/style.css\">\n");
	if (script != NULL){
		cs_buf_append(b, "    <script src=\"");
		cs_buf_append(b, script);
		cs_buf_append(b, "\" defer></script>\n");
	}
	cs_buf_append(b,
		"</head>\n"
		"<body>\n"
		"    <nav><a href=\"/\">home</a> <a href=\"/about.html\">about</a> <a href=\"/guestbook\">guestbook</a>\n"
		"        <span class=\"user\">");

	const cs_user *user = cs_current_user(c);
	if (user != NULL){
		// déconnexion en POST : un simple lien (GET) pourrait être déclenché
		// par n'importe quelle page, ou par le préchargement du navigateur
		cs_buf_append_html(b, user->name);
		cs_buf_append(b,
			" <form class=\"inline\" method=\"post\" action=\"/logout\">"
			"<button class=\"link\">log out</button></form>");
	}
	else {
		cs_buf_append(b, "<a href=\"/login\">log in</a> <a href=\"/signup\">sign up</a>");
	}

	cs_buf_append(b, "</span></nav>\n");
}

void cs_page_end(cs_buf *b){
	cs_buf_append(b,
		"</body>\n"
		"</html>\n");
}
