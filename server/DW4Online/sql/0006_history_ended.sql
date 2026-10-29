-- DW4Online migration 0006: when a kept revision's character ended (a reset). The revisions of a reset character stay
-- OnlineCharacter:ResetHistoryDays after THAT reset (the privacy page promises 30 days), also when the next character
-- is reset again within those days; the hourly housekeeping deletes by ended_at.
ALTER TABLE online_character_history
  ADD COLUMN ended_at DATETIME(3) NULL AFTER saved_at,
  ADD KEY ix_och_ended (ended_at);

-- Revisions written up to an account's last reset belonged to a character that ended then.
UPDATE online_character_history h JOIN accounts a ON a.id = h.account_id
  SET h.ended_at = a.online_reset_at
  WHERE a.online_reset_at IS NOT NULL AND h.saved_at <= a.online_reset_at;
