#ifndef GUESTBOOK_H
#define GUESTBOOK_H

#include"http.h"

// version HTML (fonctionne sans JavaScript)
void guestbook_list(int client, Request *req);      // GET  /guestbook
void guestbook_post(int client, Request *req);      // POST /guestbook -> 303

// version JSON (utilisée par www/guestbook.js)
void api_messages_list(int client, Request *req);   // GET  /api/messages -> 200 [...]
void api_messages_post(int client, Request *req);   // POST /api/messages -> 201 {...} ou 400 {"error":...}

#endif
