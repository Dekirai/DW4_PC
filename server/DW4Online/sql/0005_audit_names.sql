-- DW4Online migration 0005: a failed login with an unknown user name keeps only a keyed hash of the typed name
-- (detail.name, AuditNameHasher: people often type their password into the name field). Rows written before held the
-- typed name itself, so they lose their detail.
UPDATE audit_log SET detail = NULL WHERE event = 'login_unknown';
