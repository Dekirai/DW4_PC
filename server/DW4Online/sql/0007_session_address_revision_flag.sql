-- DW4Online migration 0007: the address a session was last used from, and the flag of each kept revision.
--
-- sessions.last_used_ip: the account-wide lock lets addresses the account really uses log in (AccountService,
-- KnownAddressDays). A game token used every day from a new address made only its creation address known; now the
-- address of its last use counts too. Existing sessions start with their creation address.
ALTER TABLE sessions
  ADD COLUMN last_used_ip VARBINARY(16) NULL AFTER created_ip;

UPDATE sessions SET last_used_ip = created_ip;

-- online_character_history.flagged: whether the revision came from a flagged upload (a flagged character stays flagged,
-- so every later revision of it is flagged too). A staff restore takes the restored revision's flag and play time, so an
-- honest character restored after a reset is not flagged by its next upload, and a flagged one does not lose its flag.
ALTER TABLE online_character_history
  ADD COLUMN flagged TINYINT(1) NOT NULL DEFAULT 0 AFTER play_seconds;

-- Revisions written before: those of a live character take its flag (the history cannot tell when the flag came, so
-- an early revision of a flagged character counts as flagged), those of a reset character the flag its reset noted in
-- the security log (character_reset, written at the same time as ended_at).
UPDATE online_character_history h JOIN online_characters o ON o.account_id = h.account_id
  SET h.flagged = o.flagged
  WHERE h.ended_at IS NULL;

UPDATE online_character_history h JOIN audit_log l
    ON l.account_id = h.account_id AND l.event = 'character_reset' AND l.at = h.ended_at
  SET h.flagged = 1
  WHERE JSON_UNQUOTE(JSON_EXTRACT(l.detail, '$.flagged')) = 'true';
