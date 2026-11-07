-- préfixe cs_ : un site peut avoir sa propre table "users" ou "sessions".
-- SQLite met à jour tout seul les REFERENCES qui pointaient vers ces tables.

ALTER TABLE users RENAME TO cs_users;
ALTER TABLE sessions RENAME TO cs_sessions;
ALTER TABLE login_failures RENAME TO cs_login_failures;
