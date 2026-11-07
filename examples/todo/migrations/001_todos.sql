-- les tâches : chacune appartient à un utilisateur du framework

CREATE TABLE todos (
    id         INTEGER PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES cs_users(id),
    title      TEXT NOT NULL,
    done       INTEGER NOT NULL DEFAULT 0,               -- 0 ou 1
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);

-- toutes les requêtes filtrent par utilisateur
CREATE INDEX todos_user ON todos (user_id);
