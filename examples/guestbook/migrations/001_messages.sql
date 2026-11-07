-- les messages du livre d'or
-- IF NOT EXISTS : les bases créées avant les migrations ont déjà cette table

CREATE TABLE IF NOT EXISTS messages (
    id         INTEGER PRIMARY KEY,
    user_id    INTEGER REFERENCES cs_users(id),            -- NULL : ancien message anonyme
    name       TEXT NOT NULL,
    message    TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))     -- en UTC
);
