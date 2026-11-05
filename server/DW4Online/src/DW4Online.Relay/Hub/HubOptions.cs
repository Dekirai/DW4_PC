namespace DW4Online.Relay.Hub;

/// <summary>Sealing (configuration section "Sealing", SEAMLESS.md 6.4).</summary>
public sealed class SealingOptions
{
    /// <summary>Upper bound of a member's input delay d (ticks).</summary>
    public int MaxDelayTicks { get; set; } = 15;
    /// <summary>The share of late pads in the delay rule's ten-second window above which d rises, when they came in at least
    /// three different seconds (2 %: DelayController.LateTarget).</summary>
    public double LateTarget { get; set; } = 0.02;
    /// <summary>Ticks added to the largest member horizon when the server stamps JOIN, LEAVE or INTENT.</summary>
    public int EventMarginTicks { get; set; } = 2;
}

/// <summary>Instance decisions (configuration section "Instances", SEAMLESS.md 3.4, 6.6).</summary>
public sealed class InstanceOptions
{
    /// <summary>How long a parked group waits for a group that announced (HEADING) the same target.</summary>
    public int CoArrivalHoldMs { get; set; } = 3000;
    /// <summary>How long an arriving group waits for a target that is not joinable for a while (its stage load and entry scene: 23 s
    /// measured from a NEW in stag0101, SEAMLESS.md 3.4).</summary>
    public int JoinHoldSeconds { get; set; } = 30;
    /// <summary>A JOIN (and a resync) is offered only when the slowest member catches up within this many seconds.</summary>
    public int MaxCatchUpSeconds { get; set; } = 30;
    /// <summary>Round J (SEAMLESS.md 6.13.5): a joiner gets a checkpoint of the target instead of its whole log (false: every
    /// join and resync takes the classic path, for a released build whose tick state is known to miss a field).</summary>
    public bool CheckpointJoins { get; set; } = true;
    /// <summary>A stored checkpoint this young serves a join at once; an older one only after the donor did not answer.</summary>
    public int FreshCheckpointMs { get; set; } = 2000;
    /// <summary>How long a join waits for the donor's checkpoint before it takes the newest stored one or the classic path.</summary>
    public int CheckpointWaitMs { get; set; } = 1000;
    /// <summary>A joiner's READY from a checkpoint waits this long for the members' fingerprint of its tick (round J's review);
    /// then it is taken unchecked.</summary>
    public int ReadyFingerprintWaitMs { get; set; } = 2000;
    /// <summary>Checkpoints kept per instance.</summary>
    public int KeepCheckpoints { get; set; } = 2;
    /// <summary>A desync this many ticks after a rebuild from a checkpoint makes the instance's checkpoint path suspect.</summary>
    public int CheckpointSuspectTicks { get; set; } = 1200;
    /// <summary>While an instance is joinable its donor uploads a checkpoint this often (the newest stored ones serve a join
    /// whose donor does not answer in time, and every resync); 0: only on demand.</summary>
    public int CheckpointPeriodicMs { get; set; } = 30000;
    /// <summary>The replay rate (steps/s) assumed for a member that has not reported one (RATE).</summary>
    public int DefaultReplayRate { get; set; } = 3000;
    /// <summary>A HEADING counts for co-arrivals this long after it arrived.</summary>
    public int HeadingValidMs { get; set; } = 5000;
    /// <summary>STORY messages a member may send per minute beyond <see cref="StoryBurst"/>; more are refused (reason rate) and sent again.</summary>
    public int StoryMessagesPerMinute { get; set; } = 60;
    public int StoryBurst { get; set; } = 10;
    /// <summary>Phase steps, flags, key items and counter growth count only from a visit the member has been in this long (the
    /// client sends them again meanwhile, reason wait); a visit gives one phase step and one key item, a further one is
    /// earned and merged in a later visit (SEAMLESS.md 8.7).</summary>
    public double StoryMinVisitSeconds { get; set; } = 10;
    /// <summary>A guest's phase steps count at least this far apart in a room session (an earlier one is earned and merged
    /// when due); the original's steps follow whole dungeons. The owner's own steps are not spaced (SEAMLESS.md 8.7).</summary>
    public double StoryPhaseStepSeconds { get; set; } = 300;
    /// <summary>A guest's key items count at least this far apart in a room session (an earlier one is earned and merged when
    /// due); not the owner's.</summary>
    public double StoryKeySeconds { get; set; } = 60;
    /// <summary>A guest's persistent flags count this many at once, then one more every <see cref="StoryFlagSeconds"/> in a
    /// room session (the rest is earned and merged when due); not the owner's, not the room's clear flag.</summary>
    public int StoryFlagBurst { get; set; } = 8;
    /// <summary>See <see cref="StoryFlagBurst"/>; 0: flags are not spaced.</summary>
    public double StoryFlagSeconds { get; set; } = 30;
    /// <summary>The counter lines one member may open in a room session; more are refused (limit).</summary>
    public int StoryLinesPerMember { get; set; } = 256;
    /// <summary>After the first BARRIER of a sealed instance, the others' reports are awaited this long.</summary>
    public int BarrierReportSeconds { get; set; } = 5;
    /// <summary>Players in one instance (the engine's four slots).</summary>
    public int PlayersPerInstance { get; set; } = 4;
    /// <summary>The highest replay rate (steps/s) a member may report (RATE, READY): a forged rate cannot buy a join on a long log.</summary>
    public int MaxReplayRate { get; set; } = 20000;
    /// <summary>A solo member's LOG may run this many ticks ahead of the wall clock since its visit began (host step bursts).</summary>
    public int LogLeadTicks { get; set; } = 600;
    /// <summary>A visit's solo log stops growing after this many hours (further LOG is dropped, the instance takes no joins).</summary>
    public double MaxVisitHours { get; set; } = 12;
    /// <summary>A sealed visit's members whose canonical snapshot hash has not come after this many seconds leave it (6.8).</summary>
    public int SnapHashSeconds { get; set; } = 60;
    /// <summary>JOIN_REQ asks to a solo leader that does not answer before the joiner falls back to NEW.</summary>
    public int JoinRequestTries { get; set; } = 3;
    /// <summary>A fork reported by members of both sides forks after this wait even when not every member confirmed it.</summary>
    public int ForkConfirmMs { get; set; } = 2000;
    /// <summary>A member is resynced at most once in this many seconds (a second lost vote meanwhile is only counted).</summary>
    public int ResyncCooldownSeconds { get; set; } = 10;
    /// <summary>RESUME sends at most this many ticks again (a longer gap is no reconnect: the member is not resumed).</summary>
    public int MaxResumeTicks { get; set; } = 3600;
    /// <summary>A member's BARRIER that arrives alone (a straggler, a client that went on by itself) is decided at most once in
    /// this many milliseconds (every decision may build a state transfer; an honest client loads a stage in between).</summary>
    public int LoneBarrierMs { get; set; } = 1000;
    /// <summary>Sealed ticks waiting beyond the log for the solo leader's LOG before J (after JOIN_AT): more than this many
    /// seconds of them, or the leader gone, and the gap can never close (the joiners go back to their own stage as NEW).</summary>
    public int MaxAheadSeconds { get; set; } = 15;
    /// <summary>The resyncs a member gets within <see cref="ResyncWindowMinutes"/>; one more lost vote and it continues alone (6.9).</summary>
    public int ResyncLimit { get; set; } = 3;
    public int ResyncWindowMinutes { get; set; } = 10;
}
