namespace DW4Online.Relay.Hub;

/// <summary>Sealing (configuration section "Sealing", SEAMLESS.md 6.4).</summary>
public sealed class SealingOptions
{
    /// <summary>Upper bound of a member's input delay d (ticks).</summary>
    public int MaxDelayTicks { get; set; } = 15;
    /// <summary>The share of late pads above which d rises (0.1 %).</summary>
    public double LateTarget { get; set; } = 0.001;
    /// <summary>Ticks added to the largest member horizon when the server stamps JOIN, LEAVE or INTENT.</summary>
    public int EventMarginTicks { get; set; } = 2;
}

/// <summary>Instance decisions (configuration section "Instances", SEAMLESS.md 3.4, 6.6).</summary>
public sealed class InstanceOptions
{
    /// <summary>How long a parked group waits for a group that announced (HEADING) the same target.</summary>
    public int CoArrivalHoldMs { get; set; } = 3000;
    /// <summary>How long an arriving group waits for a target that is not joinable for a while (its entry scene).</summary>
    public int JoinHoldSeconds { get; set; } = 15;
    /// <summary>A JOIN (and a resync) is offered only when the slowest member catches up within this many seconds.</summary>
    public int MaxCatchUpSeconds { get; set; } = 30;
    /// <summary>The replay rate (steps/s) assumed for a member that has not reported one (RATE).</summary>
    public int DefaultReplayRate { get; set; } = 3000;
    /// <summary>A HEADING counts for co-arrivals this long after it arrived.</summary>
    public int HeadingValidMs { get; set; } = 5000;
    /// <summary>After the first BARRIER of a sealed instance, the others' reports are awaited this long.</summary>
    public int BarrierReportSeconds { get; set; } = 5;
    /// <summary>Players in one instance (the engine's four slots).</summary>
    public int PlayersPerInstance { get; set; } = 4;
}
