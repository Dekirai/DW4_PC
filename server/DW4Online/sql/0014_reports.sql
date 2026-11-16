-- DW4Online migration 0014: the players' reports (round I1b, SEAMLESS.md 19.16, 19.17.1, Report player). A player reports
-- another from the game's player card (the relay's REPORT) or POST /api/v1/reports, with a reason from a fixed list (no
-- free text); staff work them off on the website's Admin > Reports (open -> done, audited report_handled; reopen audited
-- report_reopened). A reporter's rows go when his account is deleted; rows about a deleted account keep its (anonymous) id
-- for staff. Every column carries a COMMENT (HeidiSQL's table editor shows it); enums are text, there are no blobs.
-- (Built by round I1b's moderation lane for the then unreleased 0013; 0013 went live with DW4Online 0.1.7, so its part is
-- this migration. v_staff_audit stays 0013's: replacing a view needs the right DROP, which the service user of DEPLOY.md
-- section 2 does not have; report_reopened rows are in audit_log and on the website's audit list.)
-- The view needs the right CREATE VIEW (DEPLOY.md 9.2 granted it for 0013); the migrator checks the line below first.
-- Requires: CREATE VIEW

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

-- The players' reports with the names (19.17.1): newest first in HeidiSQL's Data tab when sorted by created_at.
CREATE SQL SECURITY INVOKER VIEW v_player_reports AS
  SELECT r.id, r.created_at, r.state, rp.username AS reporter, rd.username AS reported, r.reason, r.source, r.instance_id,
         hs.username AS handled_by, r.handled_at, r.reporter_account_id, r.reported_account_id
    FROM player_reports r
    JOIN accounts rp ON rp.id = r.reporter_account_id
    JOIN accounts rd ON rd.id = r.reported_account_id
    LEFT JOIN accounts hs ON hs.id = r.handled_by;
