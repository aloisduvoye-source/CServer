-- ce que le site ajoute aux comptes du framework (cs_users : nom et mot de passe)

CREATE TABLE profiles (
    user_id INTEGER PRIMARY KEY REFERENCES cs_users(id),
    email   TEXT NOT NULL UNIQUE COLLATE NOCASE           -- privé : jamais sur les pages publiques
);

-- à part : la liste des membres n'a pas à charger les images
CREATE TABLE avatars (
    user_id INTEGER PRIMARY KEY REFERENCES cs_users(id),
    type    TEXT NOT NULL,                                 -- "image/png"... (vérifié par cs_image_type)
    data    BLOB NOT NULL,
    version INTEGER NOT NULL DEFAULT 1                     -- dans l'URL de l'image : change à chaque envoi
);
