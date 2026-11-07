-- comptes utilisateurs, sessions et échecs de connexion
-- IF NOT EXISTS : les bases créées avant les migrations ont déjà ces tables

CREATE TABLE IF NOT EXISTS users (
    id            INTEGER PRIMARY KEY,
    name          TEXT NOT NULL UNIQUE COLLATE NOCASE,     -- "Jean" et "jean" : même nom
    password_hash TEXT NOT NULL,
    created_at    TEXT NOT NULL DEFAULT (datetime('now'))
);

CREATE TABLE IF NOT EXISTS sessions (
    token      TEXT PRIMARY KEY,
    user_id    INTEGER NOT NULL REFERENCES users(id),
    expires_at INTEGER NOT NULL                            -- timestamp Unix
);

CREATE TABLE IF NOT EXISTS login_failures (
    ip TEXT NOT NULL,
    at INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS login_failures_ip ON login_failures (ip, at);
