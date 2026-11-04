-- DW4Online migration 0011: the online story history keeps the story every room session started from (round 59). A
-- member that forged a revision a second pushed every honest revision out of a history that kept only the last 100, and
-- staff could no longer put the owner's story back. Now opening a room keeps the owner's story as it was then (reason
-- session), and the history keeps the newest revisions in full and older ones thinned out by time
-- (OnlineCharacter:StoryHistoryRevisions, StoryHistoryMinutes, StoryHistoryDays, StoryHistorySessions; SEAMLESS.md 8.7).
ALTER TABLE online_story_history
  MODIFY reason ENUM('room','reset','admin_restore','session') NOT NULL;
