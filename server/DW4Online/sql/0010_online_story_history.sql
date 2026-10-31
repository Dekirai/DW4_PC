-- DW4Online migration 0010: the history of the accounts' online stories (round 56 review). Every revision written to
-- online_story is also kept here (the last OnlineCharacter:StoryHistoryRevisions per account) with why it was written:
-- a room's merge (room), the reset that goes with an online character's reset (reset: an empty story), or a staff
-- restore of a kept revision (admin_restore). Staff can put a kept revision back when a forged or faulty merge damaged
-- an owner's story. Account deletion removes the rows with the account's other data.
CREATE TABLE online_story_history (
  id          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  account_id  BIGINT UNSIGNED NOT NULL,
  revision    INT UNSIGNED NOT NULL,
  flags       VARBINARY(1024) NOT NULL,
  folder      BINARY(12)   NOT NULL,
  counters    BINARY(64)   NOT NULL,
  reason      ENUM('room','reset','admin_restore') NOT NULL,
  saved_at    DATETIME(3)  NOT NULL,
  KEY ix_online_story_history_account (account_id, revision),
  CONSTRAINT fk_online_story_history_account FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- The stories saved before this migration start their history.
INSERT INTO online_story_history (account_id, revision, flags, folder, counters, reason, saved_at)
  SELECT account_id, revision, flags, folder, counters, 'room', updated_at FROM online_story;
