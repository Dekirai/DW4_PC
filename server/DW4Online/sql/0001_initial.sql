-- DW4Online migration 0001: initial schema.
-- Reviewed from the groundwork spike (validated there on a MariaDB 10.4.32 scratch instance); target MariaDB 11.4 LTS.
-- InnoDB, utf8mb4 with an explicit collation (MariaDB 11.x changed the default). All times are UTC DATETIME(3);
-- the service always writes them explicitly. No INET6 / UUID column types, so the script also runs on 10.4 for local tests.
-- The startup migrator (src/DW4Online.Data/Migrations) runs each file once and records it in schema_version.

CREATE TABLE schema_version (
  version      INT UNSIGNED NOT NULL PRIMARY KEY,
  description  VARCHAR(200) NOT NULL,
  checksum     BINARY(32)   NOT NULL,                  -- SHA-256 of the migration file as applied
  applied_at   DATETIME(3)  NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Accounts ----------------------------------------------------------------------------------------------------
CREATE TABLE accounts (
  id                   BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  username             VARCHAR(20)  NOT NULL,              -- as typed: login name and profile URL
  username_norm        VARCHAR(20)  NOT NULL,              -- lower case, unique
  email                VARCHAR(254) NULL,                  -- optional, only for password resets once SMTP exists
  email_norm           VARCHAR(254) NULL,                  -- lower case, unique (several NULLs allowed)
  email_verified_at    DATETIME(3)  NULL,
  password_hash        VARCHAR(255) NOT NULL,              -- $pbkdf2-sha512$i=<iterations>$<salt>$<hash>
  must_change_password TINYINT(1)   NOT NULL DEFAULT 0,    -- set by an admin reset (temporary password)
  password_changed_at  DATETIME(3)  NULL,
  recovery_code_hash   BINARY(32)   NULL,                  -- SHA-256 of the one-time recovery code
  role                 ENUM('player','moderator','admin') NOT NULL DEFAULT 'player',
  status               ENUM('active','locked','banned','deleted') NOT NULL DEFAULT 'active',
  failed_logins        SMALLINT UNSIGNED NOT NULL DEFAULT 0,
  locked_until         DATETIME(3)  NULL,                  -- temporary lock after too many failed logins
  profile_public       TINYINT(1)   NOT NULL DEFAULT 1,
  created_at           DATETIME(3)  NOT NULL,
  last_login_at        DATETIME(3)  NULL,
  UNIQUE KEY ux_accounts_username (username_norm),
  UNIQUE KEY ux_accounts_email (email_norm),
  CONSTRAINT ck_accounts_username CHECK (username_norm REGEXP BINARY '^[a-z0-9_-]{3,20}$')
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Opaque session tokens (game client: bearer header; website: cookie). Only the SHA-256 of a token is stored.
CREATE TABLE sessions (
  id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  account_id    BIGINT UNSIGNED NOT NULL,
  token_hash    BINARY(32)    NOT NULL,
  kind          ENUM('game','web') NOT NULL,
  device_name   VARCHAR(64)   NULL,                      -- "DW4_PC 0.48.0 / KEVIN-PC"
  created_ip    VARBINARY(16) NULL,                      -- IPv4 or IPv6 bytes
  created_at    DATETIME(3)   NOT NULL,
  last_used_at  DATETIME(3)   NULL,
  expires_at    DATETIME(3)   NOT NULL,                  -- sliding: game 30 days, web 14 days
  revoked_at    DATETIME(3)   NULL,
  UNIQUE KEY ux_sessions_token (token_hash),
  KEY ix_sessions_account (account_id, revoked_at),
  KEY ix_sessions_expires (expires_at),
  CONSTRAINT fk_sessions_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Optional e-mail flows (verification / password reset), only used once SMTP is configured.
CREATE TABLE email_tokens (
  token_hash   BINARY(32)  NOT NULL PRIMARY KEY,
  account_id   BIGINT UNSIGNED NOT NULL,
  purpose      ENUM('verify','reset') NOT NULL,
  created_at   DATETIME(3) NOT NULL,
  expires_at   DATETIME(3) NOT NULL,
  used_at      DATETIME(3) NULL,
  CONSTRAINT fk_email_tokens_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE account_bans (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  account_id  BIGINT UNSIGNED NOT NULL,
  reason      VARCHAR(255) NOT NULL,
  banned_by   BIGINT UNSIGNED NULL,
  created_at  DATETIME(3)  NOT NULL,
  expires_at  DATETIME(3)  NULL,                         -- NULL = permanent
  lifted_at   DATETIME(3)  NULL,
  KEY ix_bans_account (account_id),
  CONSTRAINT fk_bans_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
  CONSTRAINT fk_bans_by FOREIGN KEY (banned_by) REFERENCES accounts(id) ON DELETE SET NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Online character ----------------------------------------------------------------------------------------------
-- One per account, starts from zero, independent of local saves. record = one 3316-byte character record exactly
-- as on the card (include/dw4/save_data.hpp kRecordSize). The summary columns are filled from the SERVER's own
-- decode (DW4Online.Domain.Save), never taken from the client.
CREATE TABLE online_characters (
  account_id     BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  record         BLOB         NOT NULL,
  record_sha256  BINARY(32)   NOT NULL,
  revision       INT UNSIGNED NOT NULL,                  -- optimistic concurrency (ETag / If-Match)
  unique_id      INT UNSIGNED NOT NULL,                  -- UNIQUE: the character's id on the card
  digimon        VARCHAR(32)  NOT NULL,                  -- DIGIMONNAME, e.g. p_agumon
  species_row    TINYINT      NOT NULL,                  -- shinka row 0..15
  player_name    VARCHAR(16)  NOT NULL,                  -- PLAYERNAME in its ASCII form
  level          SMALLINT UNSIGNED NOT NULL,
  highest_level  SMALLINT UNSIGNED NOT NULL,             -- the highest level of any reached form
  hp INT NOT NULL, max_hp INT NOT NULL, mp INT NOT NULL, max_mp INT NOT NULL,
  bits           INT NOT NULL,
  bank_bits      INT NOT NULL,
  forms_reached  TINYINT UNSIGNED NOT NULL,
  cards          TINYINT UNSIGNED NOT NULL,
  clears         TINYINT UNSIGNED NOT NULL,              -- bit 0 Normal, 1 Hard, 2 Very Hard (BASE_FLAG 406-408)
  play_seconds   BIGINT UNSIGNED NOT NULL DEFAULT 0,     -- the port's play time without loading (client report)
  flagged        TINYINT(1)   NOT NULL DEFAULT 0,        -- a plausibility warning was raised; for moderators
  client_version VARCHAR(32)  NULL,
  created_at     DATETIME(3)  NOT NULL,
  updated_at     DATETIME(3)  NOT NULL,
  CONSTRAINT ck_online_record_size CHECK (LENGTH(record) = 3316),
  CONSTRAINT fk_online_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
  KEY ix_online_level (highest_level)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Every written online character revision, the current one included (restore after a bad upload, rollback after
-- cheating); the service keeps the newest 21. Revisions keep counting across a reset, so a new character continues
-- after the last kept revision.
CREATE TABLE online_character_history (
  id             BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  account_id     BIGINT UNSIGNED NOT NULL,
  revision       INT UNSIGNED NOT NULL,
  record         BLOB         NOT NULL,
  record_sha256  BINARY(32)   NOT NULL,
  level          SMALLINT UNSIGNED NOT NULL,
  play_seconds   BIGINT UNSIGNED NOT NULL,
  reason         ENUM('create','upload','admin_restore') NOT NULL,
  saved_at       DATETIME(3)  NOT NULL,                  -- when this revision was written
  UNIQUE KEY ux_och_rev (account_id, revision),
  CONSTRAINT ck_och_record_size CHECK (LENGTH(record) = 3316),
  CONSTRAINT fk_och_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Cloud save ----------------------------------------------------------------------------------------------------
-- One private backup of the LOCAL save per account (saves/savedata.dat = GCI bytes, or the raw 90112-byte file).
-- Never shown on the website, never used for achievements.
CREATE TABLE cloud_saves (
  account_id     BIGINT UNSIGNED NOT NULL PRIMARY KEY,
  revision       INT UNSIGNED NOT NULL,
  format         ENUM('gci','raw') NOT NULL,
  data           MEDIUMBLOB   NOT NULL,                  -- 90176 bytes today; the API caps uploads at 1 MiB
  size_bytes     INT UNSIGNED NOT NULL,
  sha256         BINARY(32)   NOT NULL,
  slot_summary   LONGTEXT     NULL CHECK (slot_summary IS NULL OR JSON_VALID(slot_summary)), -- boxes for the owner's UI
  device_name    VARCHAR(64)  NULL,
  client_version VARCHAR(32)  NULL,
  uploaded_at    DATETIME(3)  NOT NULL,
  CONSTRAINT ck_cloud_size CHECK (size_bytes = LENGTH(data) AND size_bytes <= 1048576),
  CONSTRAINT fk_cloud_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- The older revisions (the service keeps the newest 5).
CREATE TABLE cloud_save_history (
  id             BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  account_id     BIGINT UNSIGNED NOT NULL,
  revision       INT UNSIGNED NOT NULL,
  format         ENUM('gci','raw') NOT NULL,
  data           MEDIUMBLOB   NOT NULL,
  size_bytes     INT UNSIGNED NOT NULL,
  sha256         BINARY(32)   NOT NULL,
  slot_summary   LONGTEXT     NULL CHECK (slot_summary IS NULL OR JSON_VALID(slot_summary)),
  device_name    VARCHAR(64)  NULL,
  client_version VARCHAR(32)  NULL,
  uploaded_at    DATETIME(3)  NOT NULL,
  UNIQUE KEY ux_csh_rev (account_id, revision),
  CONSTRAINT ck_csh_size CHECK (size_bytes = LENGTH(data) AND size_bytes <= 1048576),
  CONSTRAINT fk_csh_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Achievements --------------------------------------------------------------------------------------------------
-- Definitions are data seeded by migrations; ids and codes never change once released.
CREATE TABLE achievements (
  id           SMALLINT UNSIGNED NOT NULL PRIMARY KEY,
  code         VARCHAR(48)  NOT NULL,                    -- story_briefing
  name         VARCHAR(80)  NOT NULL,
  description  VARCHAR(255) NOT NULL,
  category     ENUM('story','boss','challenge','level','digivolution','technique','collection','quest','combat',
                    'support','time','multiplayer') NOT NULL,
  tier         ENUM('bronze','silver','gold','platinum') NOT NULL,
  points       SMALLINT UNSIGNED NOT NULL,
  kind         ENUM('event','stat','state') NOT NULL,    -- reported by the game / counter / online character decode
  goal         BIGINT UNSIGNED NULL,                     -- threshold or limit (level, count, seconds)
  stat_key     VARCHAR(48)  NULL,                        -- player_stats key of a 'stat' achievement
  hidden       TINYINT(1)   NOT NULL DEFAULT 0,          -- masked until unlocked (spoilers)
  sort_order   SMALLINT     NOT NULL,
  active       TINYINT(1)   NOT NULL DEFAULT 1,
  UNIQUE KEY ux_achievements_code (code),
  CONSTRAINT ck_achievements_stat CHECK ((kind = 'stat') = (stat_key IS NOT NULL))
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- One row per account, achievement and ruleset: randomizer runs are their own category.
CREATE TABLE achievement_unlocks (
  account_id     BIGINT UNSIGNED NOT NULL,
  achievement_id SMALLINT UNSIGNED NOT NULL,
  ruleset        ENUM('standard','randomizer') NOT NULL,
  unlocked_at    DATETIME(3)  NOT NULL,
  room_id        BIGINT UNSIGNED NULL,                   -- the room it happened in
  players        TINYINT UNSIGNED NULL,                  -- players present (the website labels multi-player unlocks)
  client_version VARCHAR(32)  NULL,
  flagged        TINYINT(1)   NOT NULL DEFAULT 0,        -- came from a flagged upload: hidden from rankings until reviewed
  evidence       LONGTEXT     NULL CHECK (evidence IS NULL OR JSON_VALID(evidence)),
  PRIMARY KEY (account_id, achievement_id, ruleset),
  KEY ix_unlocks_achievement (achievement_id, ruleset),
  CONSTRAINT fk_unlocks_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE,
  CONSTRAINT fk_unlocks_achievement FOREIGN KEY (achievement_id) REFERENCES achievements(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Counters behind progress achievements and the profile (enemies_defeated, play_seconds, ...).
CREATE TABLE player_stats (
  account_id  BIGINT UNSIGNED NOT NULL,
  stat_key    VARCHAR(48)  NOT NULL,
  value       BIGINT UNSIGNED NOT NULL DEFAULT 0,
  updated_at  DATETIME(3)  NOT NULL,
  PRIMARY KEY (account_id, stat_key),
  CONSTRAINT fk_stats_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Best story times: in-game time without loading screens, per board and ruleset.
CREATE TABLE speedrun_times (
  account_id   BIGINT UNSIGNED NOT NULL,
  board        ENUM('story_normal','story_hard','story_veryhard') NOT NULL,
  ruleset      ENUM('standard','randomizer') NOT NULL,
  igt_ms       BIGINT UNSIGNED NOT NULL,
  room_id      BIGINT UNSIGNED NULL,
  achieved_at  DATETIME(3)  NOT NULL,
  PRIMARY KEY (account_id, board, ruleset),
  KEY ix_speedrun_board (board, ruleset, igt_ms),
  CONSTRAINT fk_speedrun_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Rooms ---------------------------------------------------------------------------------------------------------
-- The live registry (heartbeats, relay sockets) lives in server memory; this table is the durable record (website
-- "rooms online", history, achievement evidence). Rows still open / playing at service start are closed with
-- reason 'server_restart'. Up to 10 players per room, at most 4 per stage instance (game side).
CREATE TABLE rooms (
  id               BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  public_code      CHAR(6)      NOT NULL,                -- short join code shown in game and on the website
  name             VARCHAR(32)  NOT NULL,
  host_account_id  BIGINT UNSIGNED NOT NULL,
  has_password     TINYINT(1)   NOT NULL DEFAULT 0,      -- the password hash itself lives only in memory
  max_players      TINYINT UNSIGNED NOT NULL,
  protocol         INT UNSIGNED NOT NULL,                -- netplay kProtocol (src/game/netplay.cpp)
  data_hash        BIGINT UNSIGNED NOT NULL,             -- game data + mods hash (the HELLO check)
  client_version   VARCHAR(32)  NOT NULL,
  settings         LONGTEXT     NULL CHECK (settings IS NULL OR JSON_VALID(settings)), -- difficulty, randomizer seed
  ranked           TINYINT(1)   NOT NULL DEFAULT 1,      -- 0 with unknown mods: nothing counts for achievements
  status           ENUM('open','playing','closed') NOT NULL DEFAULT 'open',
  close_reason     ENUM('host_left','host_timeout','empty','admin','server_restart') NULL,
  created_at       DATETIME(3)  NOT NULL,
  started_at       DATETIME(3)  NULL,
  closed_at        DATETIME(3)  NULL,
  open_code        CHAR(6) AS (IF(closed_at IS NULL, public_code, NULL)) PERSISTENT, -- unique only among open rooms
  UNIQUE KEY ux_rooms_open_code (open_code),
  KEY ix_rooms_status (status, created_at),
  CONSTRAINT ck_rooms_players CHECK (max_players BETWEEN 2 AND 10),
  CONSTRAINT fk_rooms_host FOREIGN KEY (host_account_id) REFERENCES accounts(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE room_members (
  room_id      BIGINT UNSIGNED NOT NULL,
  account_id   BIGINT UNSIGNED NOT NULL,
  slot         TINYINT UNSIGNED NOT NULL,                -- 0 = host
  joined_at    DATETIME(3)  NOT NULL,
  left_at      DATETIME(3)  NULL,
  leave_reason ENUM('left','kicked','timeout','room_closed') NULL,
  PRIMARY KEY (room_id, account_id, joined_at),
  KEY ix_members_account (account_id),
  CONSTRAINT fk_members_room FOREIGN KEY (room_id) REFERENCES rooms(id) ON DELETE CASCADE,
  CONSTRAINT fk_members_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Audit (logins, failed logins, uploads, staff actions). Pruned after 180 days by the service.
CREATE TABLE audit_log (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  at          DATETIME(3)   NOT NULL,
  account_id  BIGINT UNSIGNED NULL,
  actor_id    BIGINT UNSIGNED NULL,
  event       VARCHAR(40)   NOT NULL,                    -- login_ok, login_fail, cloud_upload, character_flagged ...
  ip          VARBINARY(16) NULL,
  detail      LONGTEXT      NULL CHECK (detail IS NULL OR JSON_VALID(detail)),
  KEY ix_audit_account (account_id, at),
  KEY ix_audit_event (event, at),
  KEY ix_audit_at (at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
