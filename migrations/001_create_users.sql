-- 001_create_users.sql
-- User accounts for JWT-based authentication.

CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE users (
    id           UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    username     VARCHAR(64) UNIQUE NOT NULL,
    password_hash TEXT NOT NULL,
    created_at   TIMESTAMPTZ DEFAULT NOW()
);

-- Seed one admin user: username=admin, password=admin123
-- Uses pgcrypto crypt() with gen_salt('bf', 10) for bcrypt hashing.
INSERT INTO users (username, password_hash)
VALUES ('admin', crypt('admin123', gen_salt('bf', 10)));
