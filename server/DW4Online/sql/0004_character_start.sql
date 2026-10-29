-- DW4Online migration 0004: when the account's current online character could have started. A new online character
-- starts from zero, so its first upload's play time must fit into the time since the account was made or since the
-- last reset of the online character (online_reset_at, set by every reset); records that do not fit are flagged.
ALTER TABLE accounts
  ADD COLUMN online_reset_at DATETIME(3) NULL AFTER created_at;
