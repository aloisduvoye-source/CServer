#ifndef TODOS_H
#define TODOS_H

#include"cserver.h"

// toutes ces pages demandent d'être connecté (sinon : redirection vers /login)
void todos_list(cs_ctx *c);     // GET  /todos
void todos_create(cs_ctx *c);   // POST /todos             title=...
void todos_toggle(cs_ctx *c);   // POST /todos/:id/done    coche ou décoche
void todos_remove(cs_ctx *c);   // POST /todos/:id/delete

#endif
