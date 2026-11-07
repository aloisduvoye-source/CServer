#include"router.h"
#include"auth.h"
#include"guestbook.h"

// ajouter une page = ajouter une ligne ici
const Route routes[] = {
	{ "GET",  "/guestbook",        guestbook_list },
	{ "POST", "/guestbook",        guestbook_post },
	{ "POST", "/guestbook/delete", guestbook_delete },
	{ "GET",  "/api/messages",     api_messages_list },
	{ "POST", "/api/messages",     api_messages_post },
	{ "GET",  "/signup",           auth_signup_page },
	{ "POST", "/signup",           auth_signup },
	{ "GET",  "/login",            auth_login_page },
	{ "POST", "/login",            auth_login },
	{ "POST", "/logout",           auth_logout },
};

const size_t routes_count = sizeof(routes) / sizeof(routes[0]);
