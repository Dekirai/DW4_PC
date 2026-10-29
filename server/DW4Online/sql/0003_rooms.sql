-- DW4Online migration 0003: the room service.
-- A room keeps running when its owner (the host, whose online save holds the room's story) leaves: the story freezes
-- until the owner returns (story_frozen_at). Rooms close when the last member leaves ('empty'), when the owner closes
-- them ('host_closed'), by staff ('admin') or at a service start ('server_restart'); 'host_left' / 'host_timeout' stay
-- in the list for rows written before this migration. randomizer copies the host's room setting, so randomizer
-- unlocks go to their own ruleset whatever the client reports.

ALTER TABLE rooms
  MODIFY close_reason ENUM('host_left','host_timeout','empty','admin','server_restart','host_closed') NULL,
  ADD COLUMN randomizer TINYINT(1) NOT NULL DEFAULT 0 AFTER ranked,
  ADD COLUMN story_frozen_at DATETIME(3) NULL AFTER started_at;

-- Rarity on the achievements page counts unlocks per achievement.
CREATE INDEX ix_unlocks_rarity ON achievement_unlocks (achievement_id, flagged, account_id);
