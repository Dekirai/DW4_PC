-- DW4Online migration 0008: the online story of an account (SEAMLESS.md 8.4). A room's story is its owner's row; only
-- the room service writes it (the relay merges the members' progress and saves each new revision, newer revisions
-- only). GET /online-character composes the character's record from it; PUT ignores the uploaded story bytes, so a
-- guest's own story is untouched by playing in someone else's room. counters = BASE_COUNTER as 16 x i32 little endian.
CREATE TABLE online_story (
  account_id   BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  revision     INT UNSIGNED NOT NULL,
  flags        BINARY(1024) NOT NULL,
  folder       BINARY(12)   NOT NULL,
  counters     BINARY(64)   NOT NULL,
  updated_at   DATETIME(3)  NOT NULL,
  CONSTRAINT fk_online_story_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
