-- DW4Online migration 0012: friends and blocks (round I2-social, SEAMLESS.md 19.11 and 19.17.1). Three plain tables,
-- readable in HeidiSQL as they are (account ids, text enums, times; no blobs); DEPLOY.md section 9.1 has the query that
-- shows them with user names. No views: CREATE VIEW is a right the restricted service user of DEPLOY.md section 2 does
-- not have, so the views (v_friends, v_friend_requests, v_blocks) come with a later migration that brings its grant.
-- Account deletion clears all three tables for the account (AccountRepository.ScrubAsync), as the account row stays.

CREATE TABLE friendships (
  account_lo  BIGINT UNSIGNED NOT NULL COMMENT 'the smaller account id of the pair (accounts.id)',
  account_hi  BIGINT UNSIGNED NOT NULL COMMENT 'the larger account id of the pair (accounts.id)',
  created_at  DATETIME(3)     NOT NULL COMMENT 'when the request was accepted (UTC)',
  PRIMARY KEY (account_lo, account_hi),
  KEY ix_friendships_hi (account_hi),
  CONSTRAINT ck_friendships_order CHECK (account_lo < account_hi),
  CONSTRAINT fk_friendships_lo FOREIGN KEY (account_lo) REFERENCES accounts(id) ON DELETE CASCADE,
  CONSTRAINT fk_friendships_hi FOREIGN KEY (account_hi) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='one row per pair of friends; removing a friend deletes the row';

-- Accepting deletes the row and writes friendships; the sender's cancel deletes a pending row. Pending and declined
-- rows expire after Friends:RequestDays (30) from created_at and are then purged by the hourly housekeeping.
CREATE TABLE friend_requests (
  from_account_id BIGINT UNSIGNED NOT NULL COMMENT 'who sent the request (accounts.id)',
  to_account_id   BIGINT UNSIGNED NOT NULL COMMENT 'who it is for (accounts.id)',
  state           ENUM('pending','declined') NOT NULL DEFAULT 'pending'
                  COMMENT 'declined: still shown as sent to the sender until it expires; never shown to the receiver again',
  delivered       TINYINT(1)      NOT NULL DEFAULT 1
                  COMMENT '0: the receiver blocks the sender; never shown to the receiver, shown as sent to the sender',
  created_at      DATETIME(3)     NOT NULL COMMENT 'when it was sent (UTC); it expires Friends:RequestDays later',
  decided_at      DATETIME(3)     NULL     COMMENT 'when the receiver declined it',
  cancelled_at    DATETIME(3)     NULL     COMMENT 'declined rows only: the sender cancelled it (hidden from him, the decline holds until it expires)',
  PRIMARY KEY (from_account_id, to_account_id),
  KEY ix_friend_requests_to (to_account_id, state, created_at),
  KEY ix_friend_requests_created (created_at),
  CONSTRAINT ck_friend_requests_pair CHECK (from_account_id <> to_account_id),
  CONSTRAINT fk_fr_from FOREIGN KEY (from_account_id) REFERENCES accounts(id) ON DELETE CASCADE,
  CONSTRAINT fk_fr_to   FOREIGN KEY (to_account_id)   REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='open friend requests; a block is never revealed (delivered = 0)';

CREATE TABLE account_blocks (
  account_id         BIGINT UNSIGNED NOT NULL COMMENT 'who blocks (accounts.id)',
  blocked_account_id BIGINT UNSIGNED NOT NULL COMMENT 'who is blocked (accounts.id); never told',
  created_at         DATETIME(3)     NOT NULL COMMENT 'when the block was set (UTC)',
  PRIMARY KEY (account_id, blocked_account_id),
  KEY ix_blocks_blocked (blocked_account_id),
  CONSTRAINT ck_blocks_pair CHECK (account_id <> blocked_account_id),
  CONSTRAINT fk_blocks_a FOREIGN KEY (account_id)         REFERENCES accounts(id) ON DELETE CASCADE,
  CONSTRAINT fk_blocks_b FOREIGN KEY (blocked_account_id) REFERENCES accounts(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
  COMMENT='blocks: no requests, invites, joins or whereabouts between the two, both ways';
