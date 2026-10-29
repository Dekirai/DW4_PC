namespace DW4Online.Domain.Services;

/// <summary>UTC "now" truncated to the milliseconds MariaDB's DATETIME(3) keeps.</summary>
public static class Clock
{
    public static DateTime Now(TimeProvider time)
    {
        var ticks = time.GetUtcNow().UtcTicks;
        return new DateTime(ticks - ticks % TimeSpan.TicksPerMillisecond, DateTimeKind.Utc);
    }
}
