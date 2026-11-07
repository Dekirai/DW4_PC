-- DW4Online migration 0013: the world (round I1a, SEAMLESS.md 19.17.1 and 19.22). Rooms give way to one world per
-- server: the online preferences (the host settings a player's instances start with, the default visibility and the
-- social switches), the history of instances and their members, when an account was last online, the instance an
-- achievement or a speedrun time came from, the story history's reason 'mirror' (round I2), the players' reports
-- (round I1b, Report player) and views with user names for HeidiSQL. rooms and room_members stay read-only for one release (a cleanup migration drops them).
-- Players who were only guests keep an empty story (question 36 (b)): nothing of a room's story is copied to them.
-- Every table and column carries a COMMENT (HeidiSQL's table editor shows it); enums are text, there are no blobs.
-- The views need the right CREATE VIEW, which the restricted service user of DEPLOY.md section 2 gets in section 9.2
-- before this release (root has it); the migrator checks the line below before it starts and changes nothing without it.
-- Requires: CREATE VIEW

-- One row per account that changed a default; a missing row means the defaults (RoomSettings.Default's: camera 0,
-- light 1, fast loading off, no randomizer). The difficulty is not here: it is the box's story's (19.8.7).
CREATE TABLE online_prefs (
  account_id         BIGINT UNSIGNED  NOT NULL PRIMARY KEY COMMENT 'the account (accounts.id)',
  default_visibility ENUM('public','friends','private') NOT NULL DEFAULT 'public'
                     COMMENT 'the visibility of the next instance this player hosts: the last one he chose (Q9)',
  invites            ENUM('anyone','friends') NOT NULL DEFAULT 'anyone' COMMENT 'who may invite him into a party (round I4)',
  whereabouts        ENUM('friends','nobody') NOT NULL DEFAULT 'friends'
                     COMMENT 'who sees where he plays and when he was last online: his friends, or nobody',
  friend_toasts      TINYINT(1)       NOT NULL DEFAULT 1 COMMENT '1: the game shows a toast when a friend comes online',
  camera_mode        TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'host setting: the title option camera_mode (0 or 1)',
  light_mode         TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'host setting: the title option light_mode (0 or 1; 1 is the new game default)',
  fast_loading       TINYINT(1)       NOT NULL DEFAULT 0 COMMENT 'host setting: the PC fast loading (1 on); it changes tick counts',
  randomizer_seed    BIGINT UNSIGNED  NULL COMMENT 'host setting: the randomizer master seed (1..4294967295); NULL = randomizer off',
  randomizer_modes   INT UNSIGNED     NOT NULL DEFAULT 0
                     COMMENT 'host setting: bit 0 drops shuffled, bits 1-2 enemies (0 off, 1 each, 2 per wave), bits 3-4 zone shuffle (0 off, 1, 2)',
  last_box           TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'the story box the player played last (1 until round D)',
  updated_at         DATETIME(3)      NOT NULL COMMENT 'when the row was last written (UTC)',
  CONSTRAINT ck_prefs_camera CHECK (camera_mode <= 1),
  CONSTRAINT ck_prefs_light CHECK (light_mode <= 1),
  CONSTRAINT ck_prefs_seed CHECK (randomizer_seed IS NULL OR randomizer_seed BETWEEN 1 AND 4294967295),
  CONSTRAINT ck_prefs_modes CHECK (randomizer_modes <= 31 AND (randomizer_modes >> 1) & 3 <> 3 AND (randomizer_modes >> 3) & 3 <> 3),
  CONSTRAINT ck_prefs_box CHECK (last_box BETWEEN 1 AND 4),
  CONSTRAINT fk_prefs FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='the online preferences per account; a missing row = the defaults';

-- Written by the relay's history queue (off its lock); kept DW4Online's World:HistoryDays (90) after the start, then
-- deleted by the hourly housekeeping with their members. A hosted row keeps its host's id after a deletion (the id then
-- names the anonymous deleted_<id> account).
CREATE TABLE instance_history (
  id              BIGINT UNSIGNED  NOT NULL AUTO_INCREMENT PRIMARY KEY COMMENT 'the history id (never the relay''s run-local wid)',
  host_account_id BIGINT UNSIGNED  NOT NULL COMMENT 'who hosted it (accounts.id); the host never changes',
  host_box        TINYINT UNSIGNED NOT NULL COMMENT 'the host''s story box (1 until round D)',
  first_stage     VARCHAR(16)      NOT NULL COMMENT 'the stage folder the host entered from home (its area is the instance''s)',
  visibility      ENUM('public','friends','private') NOT NULL COMMENT 'at the start; later changes are audit rows instance_visibility',
  difficulty      TINYINT UNSIGNED NOT NULL COMMENT '0 Normal, 1 Hard, 2 Very Hard (the host''s box)',
  story_phase     TINYINT UNSIGNED NOT NULL COMMENT 'the host''s story phase at the start',
  settings        VARCHAR(200)     NOT NULL COMMENT 'the canonical settings text (DW4R2-settings/1;...)',
  started_at      DATETIME(3)      NOT NULL COMMENT 'when the host''s first stage group started (UTC)',
  ended_at        DATETIME(3)      NULL COMMENT 'when it ended (UTC); NULL while it runs',
  end_reason      ENUM('host_left','host_defeated','host_lost','staff','empty','server_stop') NULL
                  COMMENT 'why it ended: the host went home, was defeated, lost his connection beyond the grace; staff; nobody left; the server stopped',
  peak_players    TINYINT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'the most players at once (spectators never count)',
  KEY ix_ih_host (host_account_id, started_at),
  KEY ix_ih_started (started_at),
  KEY ix_ih_open (ended_at),
  CONSTRAINT ck_ih_box CHECK (host_box BETWEEN 1 AND 4),
  CONSTRAINT ck_ih_difficulty CHECK (difficulty <= 2),
  CONSTRAINT ck_ih_peak CHECK (peak_players BETWEEN 1 AND 4),
  CONSTRAINT fk_ih_host FOREIGN KEY (host_account_id) REFERENCES accounts(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='one row per instance a player hosted; feeds the admin pages and Recently played with';

CREATE TABLE instance_history_members (
  instance_id  BIGINT UNSIGNED  NOT NULL COMMENT 'the instance (instance_history.id)',
  account_id   BIGINT UNSIGNED  NOT NULL COMMENT 'the member (accounts.id)',
  box          TINYINT UNSIGNED NOT NULL COMMENT 'the member''s story box (1 until round D)',
  role         ENUM('host','guest','staff','spectator') NOT NULL
               COMMENT 'staff: a moderator or admin playing as a guest; spectator: watching (admin pages and audit only, never shown to players)',
  joined_at    DATETIME(3)      NOT NULL COMMENT 'when the member entered the instance (UTC); a member may come back (a new row)',
  left_at      DATETIME(3)      NULL COMMENT 'when the member left (UTC); NULL while inside',
  leave_reason ENUM('left','home','removed','ended','lost') NULL
               COMMENT 'left the world, went home, was removed, the instance ended, the connection was lost for good',
  shared_story TINYINT(1)       NOT NULL DEFAULT 0 COMMENT '1: the member got mirrored story progress (round I2, 19.8.6)',
  PRIMARY KEY (instance_id, account_id, joined_at),
  KEY ix_ihm_account (account_id, joined_at),
  CONSTRAINT ck_ihm_box CHECK (box BETWEEN 1 AND 4),
  CONSTRAINT fk_ihm_instance FOREIGN KEY (instance_id) REFERENCES instance_history(id) ON DELETE CASCADE,
  CONSTRAINT fk_ihm_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='who was in which instance and when; account deletion removes the account''s rows';

-- Report player (round I1b, 19.16, 19.26 idea 5): a player's report about another, with a reason from a fixed list (no
-- free text); staff work them off on the website's /admin/reports (state open -> done, audited report_handled). Written by
-- the report service (the game's REPORT through the relay, POST /api/v1/reports); a reporter's rows go when his account
-- is deleted, rows about a deleted account keep its (anonymous) id for staff.
CREATE TABLE player_reports (
  id                  BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY COMMENT 'the report id (the website''s /admin/reports)',
  reporter_account_id BIGINT UNSIGNED NOT NULL COMMENT 'who reported (accounts.id)',
  reported_account_id BIGINT UNSIGNED NOT NULL COMMENT 'whom he reported (accounts.id)',
  reason      ENUM('griefing','cheating','name','other') NOT NULL
              COMMENT 'the reason the reporter picked from the fixed list: griefing, cheating, an offensive name, other (no free text)',
  source      ENUM('game','web') NOT NULL DEFAULT 'game' COMMENT 'game: the player card in the game (the relay); web: POST /api/v1/reports',
  instance_id BIGINT UNSIGNED NULL
              COMMENT 'the instance both played in (instance_history.id) when they shared one; NULL when not, or after the history was pruned',
  created_at  DATETIME(3)     NOT NULL COMMENT 'when the report came (UTC)',
  state       ENUM('open','done') NOT NULL DEFAULT 'open' COMMENT 'open: waits for staff; done: a moderator or admin handled it',
  handled_by  BIGINT UNSIGNED NULL COMMENT 'the moderator or admin who marked it done (accounts.id); NULL while open',
  handled_at  DATETIME(3)     NULL COMMENT 'when it was marked done (UTC); NULL while open',
  KEY ix_reports_state (state, created_at),
  KEY ix_reports_reported (reported_account_id, created_at),
  KEY ix_reports_reporter (reporter_account_id, created_at),
  CONSTRAINT ck_reports_self CHECK (reporter_account_id <> reported_account_id),
  CONSTRAINT ck_reports_handled CHECK ((state = 'open') = (handled_at IS NULL)),
  CONSTRAINT fk_reports_reporter FOREIGN KEY (reporter_account_id) REFERENCES accounts(id),
  CONSTRAINT fk_reports_reported FOREIGN KEY (reported_account_id) REFERENCES accounts(id),
  CONSTRAINT fk_reports_instance FOREIGN KEY (instance_id) REFERENCES instance_history(id) ON DELETE SET NULL,
  CONSTRAINT fk_reports_staff FOREIGN KEY (handled_by) REFERENCES accounts(id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='Report player (round I1b): what players reported about others; staff handle them on /admin/reports';

ALTER TABLE accounts
  ADD COLUMN last_seen_at DATETIME(3) NULL COMMENT 'when the account was last in the world (UTC; friends see it by the whereabouts preference)' AFTER last_login_at;
ALTER TABLE achievement_unlocks
  ADD COLUMN instance_id BIGINT UNSIGNED NULL COMMENT 'the instance it happened in (instance_history.id); rows before round I1 have room_id instead' AFTER room_id;
ALTER TABLE speedrun_times
  ADD COLUMN instance_id BIGINT UNSIGNED NULL COMMENT 'the instance of the run (instance_history.id); rows before round I1 have room_id instead' AFTER room_id;
-- A world entry keeps the story before the session's first change as 'session' (as opening a room did); 'mirror' is a
-- mirrored clear of round I2 (19.8.6). No reason 'migration': nothing is copied (question 36 (b)).
ALTER TABLE online_story_history
  MODIFY reason ENUM('room','reset','admin_restore','session','mirror') NOT NULL
  COMMENT 'room: an instance (a room before round I1) wrote it; reset; admin_restore; session: the story a world session started from; mirror: shared progress (round I2)';

-- The settings of rooms carry over (19.22 step 4): each host's newest room settings fill his online_prefs (camera,
-- light, fast loading, randomizer seed and modes); everyone else has the defaults (no row).
INSERT INTO online_prefs (account_id, camera_mode, light_mode, fast_loading, randomizer_seed, randomizer_modes, updated_at)
SELECT r.host_account_id,
       IF(JSON_VALUE(r.settings, '$.cameraMode') = '1', 1, 0),
       IF(JSON_VALUE(r.settings, '$.lightMode') = '0', 0, 1),
       IF(JSON_EXTRACT(r.settings, '$.fastLoading') = 'true', 1, 0),
       IF(JSON_EXTRACT(r.settings, '$.randomizer.enabled') = 'true'
            AND JSON_VALUE(r.settings, '$.randomizer.seed') REGEXP '^[0-9]{1,10}$'
            AND CAST(JSON_VALUE(r.settings, '$.randomizer.seed') AS UNSIGNED) BETWEEN 1 AND 4294967295,
          CAST(JSON_VALUE(r.settings, '$.randomizer.seed') AS UNSIGNED), NULL),
       IF(JSON_EXTRACT(r.settings, '$.randomizer.enabled') = 'true',
          IF(JSON_EXTRACT(r.settings, '$.randomizer.drops') = 'true', 1, 0)
          | (CASE JSON_VALUE(r.settings, '$.randomizer.enemies') WHEN '1' THEN 1 WHEN '2' THEN 2 ELSE 0 END << 1)
          | (CASE JSON_VALUE(r.settings, '$.randomizer.zones') WHEN '1' THEN 1 WHEN '2' THEN 2 ELSE 0 END << 3), 0),
       UTC_TIMESTAMP(3)
  FROM rooms r
  JOIN accounts a ON a.id = r.host_account_id AND a.status <> 'deleted'
 WHERE r.settings IS NOT NULL AND JSON_VALID(r.settings)
   AND r.id = (SELECT MAX(r2.id) FROM rooms r2 WHERE r2.host_account_id = r.host_account_id AND r2.settings IS NOT NULL);

-- Views with user names (HeidiSQL: select one and open its Data tab). SQL SECURITY INVOKER: whoever reads one needs
-- SELECT on the tables (root and dw4backup have it), and a restored backup works without the user that created them.
CREATE SQL SECURITY INVOKER VIEW v_friends AS
  SELECT a.username AS player, b.username AS friend, f.created_at AS friends_since, f.account_lo, f.account_hi
    FROM friendships f JOIN accounts a ON a.id = f.account_lo JOIN accounts b ON b.id = f.account_hi;

CREATE SQL SECURITY INVOKER VIEW v_friend_requests AS
  SELECT a.username AS from_player, b.username AS to_player, r.state, r.delivered, r.created_at, r.decided_at, r.cancelled_at,
         r.from_account_id, r.to_account_id
    FROM friend_requests r JOIN accounts a ON a.id = r.from_account_id JOIN accounts b ON b.id = r.to_account_id;

CREATE SQL SECURITY INVOKER VIEW v_blocks AS
  SELECT a.username AS player, b.username AS blocks_player, k.created_at, k.account_id, k.blocked_account_id
    FROM account_blocks k JOIN accounts a ON a.id = k.account_id JOIN accounts b ON b.id = k.blocked_account_id;

-- The area of the first stage as the loading cards name it (data/online/areas.json; the Fortress is Death Valley's,
-- decision 39), the duration in minutes and the players by name (spectators are left out).
CREATE SQL SECURITY INVOKER VIEW v_instance_history AS
  SELECT h.id, u.username AS host, h.host_box,
         CASE
           WHEN h.first_stage LIKE 'stag00%' OR h.first_stage LIKE 'stag01%' OR h.first_stage LIKE 'dng10%'
             OR h.first_stage LIKE 'staggf%' OR h.first_stage = 'stagbs01' THEN 'Death Valley'
           WHEN h.first_stage LIKE 'stag02%' OR h.first_stage LIKE 'dng20%' OR h.first_stage = 'stagbs02' THEN 'Dry Land'
           WHEN h.first_stage LIKE 'stag03%' OR h.first_stage LIKE 'dng30%' OR h.first_stage LIKE 'dng31%'
             OR h.first_stage = 'stagbs03' THEN 'Venom Jungle'
           WHEN h.first_stage LIKE 'stag04%' OR h.first_stage LIKE 'dng41%' OR h.first_stage LIKE 'dngq%'
             OR h.first_stage IN ('stagbs04', 'stagbs05', 'stagbs06') THEN 'Machine Pit'
           ELSE h.first_stage
         END AS area,
         h.first_stage, h.visibility,
         CASE h.difficulty WHEN 0 THEN 'Normal' WHEN 1 THEN 'Hard' ELSE 'Very Hard' END AS difficulty,
         h.story_phase, h.settings, h.started_at, h.ended_at,
         TIMESTAMPDIFF(MINUTE, h.started_at, h.ended_at) AS minutes,
         h.end_reason, h.peak_players,
         (SELECT GROUP_CONCAT(DISTINCT p.username ORDER BY p.username SEPARATOR ', ')
            FROM instance_history_members m JOIN accounts p ON p.id = m.account_id
           WHERE m.instance_id = h.id AND m.role <> 'spectator') AS players,
         h.host_account_id
    FROM instance_history h JOIN accounts u ON u.id = h.host_account_id;

-- The players' reports with the names (19.17.1): newest first in HeidiSQL's Data tab when sorted by created_at.
CREATE SQL SECURITY INVOKER VIEW v_player_reports AS
  SELECT r.id, r.created_at, r.state, rp.username AS reporter, rd.username AS reported, r.reason, r.source, r.instance_id,
         hs.username AS handled_by, r.handled_at, r.reporter_account_id, r.reported_account_id
    FROM player_reports r
    JOIN accounts rp ON rp.id = r.reporter_account_id
    JOIN accounts rd ON rd.id = r.reported_account_id
    LEFT JOIN accounts hs ON hs.id = r.handled_by;

-- The staff rows of the audit log (19.14): what moderators and admins did and looked at, with both names.
CREATE SQL SECURITY INVOKER VIEW v_staff_audit AS
  SELECT l.id, l.at, l.event, s.username AS staff, p.username AS player, l.detail, l.actor_id, l.account_id
    FROM audit_log l
    LEFT JOIN accounts s ON s.id = l.actor_id
    LEFT JOIN accounts p ON p.id = l.account_id
   WHERE l.event IN ('staff_join', 'staff_join_override', 'staff_spectate_start', 'staff_spectate_end', 'staff_end_instance',
                     'staff_remove', 'instance_visibility', 'instance_remove', 'report_handled', 'report_reopened', 'room_close_admin',
                     'admin_role', 'admin_status', 'admin_password_reset', 'admin_unlock_revoke', 'character_restore', 'story_restore');
