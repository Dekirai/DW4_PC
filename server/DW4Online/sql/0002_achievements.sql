-- DW4Online migration 0002: the achievement definitions (groundwork list of 72, work/notes/online_understand_journal.jsonl,
-- topic "client"). Ids and codes are stable: never renumber or reuse them; retire one with active = 0 in a new migration.
-- kind: event = reported by the game when it happens; stat = the server unlocks it when player_stats.<stat_key> reaches goal;
-- state = the server unlocks it from its own decode of an online character upload (DW4Online.Domain AchievementRules).
-- goal for event kinds is a limit the server checks: speed_* seconds of in-game time, lowlevel_* the highest level.
-- Points: bronze 10, silver 25, gold 50, platinum 100.

INSERT INTO achievements (id, code, name, description, category, tier, points, kind, goal, stat_key, hidden, sort_order) VALUES
-- Story
( 1, 'story_briefing',      'New Recruit',                  'Receive your first briefing.',                                        'story',        'bronze',   10, 'event', NULL, NULL, 0,  10),
( 2, 'story_gate_keys',     'Security Clearance',           'Obtain both gate keys.',                                              'story',        'bronze',   10, 'event', NULL, NULL, 0,  20),
( 3, 'story_leomon',        'Gate Breaker',                 'Open the way past Leomon.',                                           'story',        'bronze',   10, 'event', NULL, NULL, 0,  30),
( 4, 'boss_apocalymon',     'Tower Toppler',                'Defeat Apocalymon.',                                                  'story',        'silver',   25, 'event', NULL, NULL, 0,  40),
( 5, 'story_restore',       'System Restore',               'Restore the system.',                                                 'story',        'bronze',   10, 'event', NULL, NULL, 0,  50),
( 6, 'story_plugs',         'Unplugged',                    'Pull all the plugs.',                                                 'story',        'bronze',   10, 'event', NULL, NULL, 0,  60),
( 7, 'boss_belphemon',      'Sloth Slayer',                 'Defeat Belphemon.',                                                   'story',        'silver',   25, 'event', NULL, NULL, 0,  70),
( 8, 'story_maze',          'Fortress Navigator',           'Find your way through the fortress maze.',                           'story',        'bronze',   10, 'event', NULL, NULL, 0,  80),
( 9, 'boss_lucemon',        'Fallen Angel',                 'Defeat Lucemon.',                                                     'story',        'silver',   25, 'event', NULL, NULL, 1,  90),
(10, 'boss_lordknightmon',  'Knight Fall',                  'Defeat LordKnightmon.',                                               'story',        'silver',   25, 'event', NULL, NULL, 1, 100),
(11, 'boss_daemon',         'Demon Lord',                   'Defeat Daemon.',                                                      'story',        'gold',     50, 'event', NULL, NULL, 1, 110),
(12, 'clear_normal',        'Digital World Saved',          'Finish the story on Normal.',                                         'story',        'gold',     50, 'event', NULL, NULL, 0, 120),
(13, 'clear_hard',          'Hardened',                     'Finish the story on Hard.',                                           'story',        'gold',     50, 'event', NULL, NULL, 0, 130),
(14, 'clear_veryhard',      'Legend of the Digital World',  'Finish the story on Very Hard.',                                      'story',        'platinum',100, 'event', NULL, NULL, 0, 140),
-- Bosses
(15, 'boss_first',          'Boss Breaker',                 'Defeat your first boss.',                                             'boss',         'bronze',   10, 'event', NULL, NULL, 0, 150),
(16, 'boss_dungeon_all',    'Key Keeper',                   'Defeat all nine dungeon bosses.',                                     'boss',         'gold',     50, 'event', NULL, NULL, 0, 160),
-- Challenges
(17, 'nodmg_boss',          'Untouchable',                  'Defeat a story boss without taking damage.',                          'challenge',    'gold',     50, 'event', NULL, NULL, 0, 170),
(18, 'nodmg_daemon_vh',     'Flawless Demon Lord',          'Defeat Daemon on Very Hard without taking damage.',                   'challenge',    'platinum',100, 'event', NULL, NULL, 1, 180),
(19, 'deathless_clear',     'Never Fallen',                 'Finish the story without ever being defeated.',                      'challenge',    'platinum',100, 'event', NULL, NULL, 0, 190),
(20, 'solo_veryhard',       'Lone Tamer',                   'Finish the story on Very Hard alone.',                               'challenge',    'platinum',100, 'event', NULL, NULL, 0, 200),
(21, 'lowlevel_apocalymon', 'Underdog',                     'Defeat Apocalymon at level 8 or lower.',                             'challenge',    'gold',     50, 'event',    8, NULL, 0, 210),
(22, 'lowlevel_daemon',     'Minimalist',                   'Defeat Daemon at level 60 or lower.',                                'challenge',    'platinum',100, 'event',   60, NULL, 1, 220),
(23, 'speed_6h',            'Swift Tamer',                  'Finish the story within 6 hours of play time (loading excluded).',   'challenge',    'gold',     50, 'event', 21600, NULL, 0, 230),
(24, 'speed_3h',            'Speed Demon',                  'Finish the story within 3 hours of play time (loading excluded).',   'challenge',    'platinum',100, 'event', 10800, NULL, 0, 240),
(25, 'rookie_clear',        'Rookie Pride',                 'Finish the story using only rookie forms.',                          'challenge',    'platinum',100, 'event', NULL, NULL, 0, 250),
(26, 'quest_vh',            'Quest Veteran',                'Complete the three Very Hard quests.',                               'challenge',    'gold',     50, 'event', NULL, NULL, 0, 260),
-- Levels and forms
(27, 'lv_10',               'Getting Stronger',             'Reach level 10.',                                                     'level',        'bronze',   10, 'state',   10, NULL, 0, 270),
(28, 'lv_50',               'Seasoned Partner',             'Reach level 50.',                                                     'level',        'bronze',   10, 'state',   50, NULL, 0, 280),
(29, 'lv_99',               'Veteran Tamer',                'Reach level 99.',                                                     'level',        'silver',   25, 'state',   99, NULL, 0, 290),
(30, 'lv_200',              'Beyond the Limit',             'Reach level 200.',                                                    'level',        'silver',   25, 'state',  200, NULL, 0, 300),
(31, 'lv_500',              'Unstoppable',                  'Reach level 500.',                                                    'level',        'gold',     50, 'state',  500, NULL, 0, 310),
(32, 'lv_999',              'Digital Deity',                'Reach level 999.',                                                    'level',        'platinum',100, 'state',  999, NULL, 0, 320),
(33, 'form_evolve',         'Digivolution!',                'Digivolve for the first time.',                                       'digivolution', 'bronze',   10, 'event', NULL, NULL, 0, 330),
(34, 'form_rookies',        'Rookie Squad',                 'Unlock all four rookie forms.',                                       'digivolution', 'silver',   25, 'state', NULL, NULL, 0, 340),
(35, 'form_mega',           'Mega Power',                   'Unlock a digivolved form.',                                           'digivolution', 'bronze',   10, 'state', NULL, NULL, 0, 350),
(36, 'form_all16',          'Complete Evolution',           'Unlock all 16 forms.',                                                'digivolution', 'gold',     50, 'state', NULL, NULL, 0, 360),
(37, 'form_susanoomon',     'Susanoomon',                   'Unlock Susanoomon.',                                                  'digivolution', 'gold',     50, 'state', NULL, NULL, 1, 370),
(38, 'form_master',         'Form Master',                  'Raise all 16 forms to level 99.',                                     'digivolution', 'platinum',100, 'state',   99, NULL, 0, 380),
-- Techniques
(39, 'tech_first',          'Technique!',                   'Land a hit with a technique.',                                        'technique',    'bronze',   10, 'event', NULL, NULL, 0, 390),
(40, 'tech_full',           'Full Repertoire',              'Learn all six techniques of a form.',                                 'technique',    'silver',   25, 'event', NULL, NULL, 0, 400),
(41, 'tech_rank5',          'Grandmaster Technique',        'Raise a technique to its highest rank.',                              'technique',    'gold',     50, 'event', NULL, NULL, 0, 410),
(42, 'weapon_virtuoso',     'Weapon Virtuoso',              'Raise a weapon skill to its highest level.',                          'technique',    'gold',     50, 'event', NULL, NULL, 0, 420),
-- Items, cards, bits
(43, 'card_first',          'Collector',                    'Collect your first card.',                                            'collection',   'bronze',   10, 'state',    1, NULL, 0, 430),
(44, 'card_rare',           'Lucky Draw',                   'Pull a rare card from a booster.',                                    'collection',   'silver',   25, 'event', NULL, NULL, 0, 440),
(45, 'card_25',             'Half Album',                   'Collect 25 different cards.',                                         'collection',   'silver',   25, 'state',   25, NULL, 0, 450),
(46, 'card_50',             'Complete Album',               'Collect all 50 cards.',                                               'collection',   'platinum',100, 'state',   50, NULL, 0, 460),
(47, 'item_rank5',          'Master Craft',                 'Own a rank 5 device.',                                                'collection',   'silver',   25, 'state',    5, NULL, 0, 470),
(48, 'item_pink',           'Legendary Find',               'Own a device with a value of 1000 or more.',                          'collection',   'gold',     50, 'state', 1000, NULL, 0, 480),
(49, 'bits_1m',             'Millionaire',                  'Hold 1,000,000 bits in your wallet and the bank together.',           'collection',   'silver',   25, 'state', 1000000, NULL, 0, 490),
(50, 'bits_max',            'Bit Tycoon',                   'Hold 9,999,999 bits.',                                                'collection',   'gold',     50, 'state', 9999999, NULL, 0, 500),
(51, 'numemon_free',        'Numemon Liberator',            'Free all ten Numemon.',                                               'collection',   'silver',   25, 'event', NULL, NULL, 0, 510),
(52, 'hoarder',             'Hoarder',                      'Fill all 36 slots of the device folder.',                             'collection',   'bronze',   10, 'state',   36, NULL, 0, 520),
-- Quests
(53, 'quest_first',         'Side Job',                     'Complete your first quest.',                                          'quest',        'bronze',   10, 'event', NULL, NULL, 0, 530),
(54, 'quest_all',           'Quest Master',                 'Complete all eight quests.',                                          'quest',        'gold',     50, 'event', NULL, NULL, 0, 540),
-- Combat (online counters)
(55, 'kills_100',           'Monster Hunter',               'Defeat 100 enemies.',                                                 'combat',       'bronze',   10, 'stat',   100, 'enemies_defeated', 0, 550),
(56, 'kills_1000',          'Exterminator',                 'Defeat 1,000 enemies.',                                               'combat',       'silver',   25, 'stat',  1000, 'enemies_defeated', 0, 560),
(57, 'kills_10000',         'Legion Breaker',               'Defeat 10,000 enemies.',                                              'combat',       'gold',     50, 'stat', 10000, 'enemies_defeated', 0, 570),
(58, 'kills_100000',        'Digital Reaper',               'Defeat 100,000 enemies.',                                             'combat',       'platinum',100, 'stat', 100000, 'enemies_defeated', 0, 580),
(59, 'crit_1000',           'Critical Mass',                'Land 1,000 critical hits.',                                           'combat',       'silver',   25, 'stat',  1000, 'critical_hits', 0, 590),
(60, 'numemon_hunter',      'Numemon Hunter',               'Defeat 500 Numemon.',                                                 'combat',       'silver',   25, 'stat',   500, 'numemon_defeated', 0, 600),
-- Defeat and support
(61, 'first_defeat',        'Ouch',                         'Get defeated for the first time.',                                    'support',      'bronze',   10, 'event', NULL, NULL, 0, 610),
(62, 'raise_ally',          'Second Chance',                'Revive another player.',                                              'support',      'bronze',   10, 'event', NULL, NULL, 0, 620),
(63, 'capsule_recover',     'Recovery',                     'Recover your own bit capsule.',                                       'support',      'bronze',   10, 'event', NULL, NULL, 0, 630),
-- Play time (online, loading excluded)
(64, 'time_10h',            'Dedicated',                    'Play online for 10 hours.',                                           'time',         'bronze',   10, 'stat',  36000, 'play_seconds', 0, 640),
(65, 'time_100h',           'Devoted',                      'Play online for 100 hours.',                                          'time',         'gold',     50, 'stat', 360000, 'play_seconds', 0, 650),
(66, 'time_500h',           'Lifer',                        'Play online for 500 hours.',                                          'time',         'platinum',100, 'stat', 1800000, 'play_seconds', 0, 660),
-- Multiplayer
(67, 'mp_couch',            'Couch Co-op',                  'Defeat a boss with at least two players on one machine.',             'multiplayer',  'bronze',   10, 'event', NULL, NULL, 0, 670),
(68, 'mp_fullhouse',        'Full House',                   'Defeat a boss with four players.',                                    'multiplayer',  'silver',   25, 'event', NULL, NULL, 0, 680),
(69, 'online_join',         'Connected',                    'Join an online room.',                                                'multiplayer',  'bronze',   10, 'event', NULL, NULL, 0, 690),
(70, 'online_host',         'Room Host',                    'Host a room with at least three guests.',                             'multiplayer',  'silver',   25, 'event', NULL, NULL, 0, 700),
(71, 'online_ten',          'Party of Ten',                 'Play in a room with ten players.',                                    'multiplayer',  'gold',     50, 'event', NULL, NULL, 0, 710),
(72, 'online_raid',         'Raid Night',                   'Defeat a story boss with at least two online players in the room.',   'multiplayer',  'silver',   25, 'event', NULL, NULL, 0, 720);
