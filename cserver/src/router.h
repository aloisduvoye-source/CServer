#ifndef ROUTER_H
#define ROUTER_H

#include<stddef.h>
#include"http.h"

typedef void (*Handler)(int client, Request *req);

typedef struct {
	const char *method;
	const char *path;
	Handler     handler;
} Route;

// table des routes, fournie par l'application (ex : examples/guestbook/routes.c)
extern const Route  routes[];
extern const size_t routes_count;

// appelle le handler qui correspond à (méthode, chemin), sinon sert un fichier
void route(int client, Request *req);

#endif
