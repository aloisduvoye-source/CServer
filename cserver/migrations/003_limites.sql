-- les actions limitées (cs_rate_limit) : une ligne par action faite, par clé
-- (une adresse IP, ou ce que le site choisit, par exemple un compte)

CREATE TABLE cs_rate_limits (
    action TEXT NOT NULL,
    key    TEXT NOT NULL,
    at     INTEGER NOT NULL                                -- timestamp Unix
);

CREATE INDEX cs_rate_limits_key ON cs_rate_limits (action, key, at);
