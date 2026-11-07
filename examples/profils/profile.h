#ifndef PROFILE_H
#define PROFILE_H

// les pages : liste des membres, profil public, son profil (e-mail, avatar), l'image

#include"cserver.h"

void members_page(cs_ctx *c);            // GET  /
void member_page(cs_ctx *c);             // GET  /u/:name
void profile_page(cs_ctx *c);            // GET  /profile
void profile_email(cs_ctx *c);           // POST /profile/email
void profile_avatar(cs_ctx *c);          // POST /profile/avatar          (multipart)
void profile_avatar_delete(cs_ctx *c);   // POST /profile/avatar/delete
void avatar_image(cs_ctx *c);            // GET  /avatars/:id

#endif
