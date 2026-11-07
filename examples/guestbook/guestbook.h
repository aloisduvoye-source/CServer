#ifndef GUESTBOOK_H
#define GUESTBOOK_H

#include"cserver.h"

// version HTML (fonctionne sans JavaScript)
void guestbook_list(cs_ctx *c);      // GET  /guestbook
void guestbook_post(cs_ctx *c);      // POST /guestbook -> 303 (connecté)
void guestbook_delete(cs_ctx *c);    // POST /guestbook/delete id=N -> 303 ou 403

// version JSON (utilisée par www/guestbook.js)
void api_messages_list(cs_ctx *c);   // GET  /api/messages -> 200 [...]
void api_messages_post(cs_ctx *c);   // POST /api/messages -> 201 {...} ou 400 {"error":...}

#endif
