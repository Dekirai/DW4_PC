using System.Diagnostics;
using DW4Online.FakeClient;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.Logging.Abstractions;

namespace DW4Online.Integration.Tests;

/// <summary>
/// Round I1a's load gate harness (SEAMLESS.md 19.17.4, FakeClient's LoadGate), without the database: its frames must be
/// what the hub forwards, or the gate measures less than it claims (the first runs sent a growing yaw, and the hub dropped
/// about half of the lobby's presence as implausible).
/// </summary>
public class LoadGateTests
{
    private sealed class Peer : IHubPeer
    {
        public int Presence;

        public void Send(byte[] frame, bool bulk = false)
        {
            if (frame[0] is Msg.Presence2Out or Msg.PresenceOut) ++Presence;
        }

        public void Close(CloseReason reason, string text)
        {
        }
    }

    [Fact]
    public void A_lobby_members_presence_stays_plausible_for_a_long_run_and_reaches_the_others()
    {
        var hub = new RoomHub(new RoomInfo("WORLD1", LoadGate.Protocol, 1, 2, 4), new RelayOptions(), new SealingOptions(), new InstanceOptions(),
            NullLogger.Instance, null);
        var now = Stopwatch.Frequency * 1000L;
        var peers = new[] { new Peer(), new Peer() };
        var a = hub.Connect(0, 1000, "a", true, peers[0], now);
        var b = hub.Connect(1, 1001, "b", false, peers[1], now);
        // (b's own presence puts it in the stage: the hub forwards to the members whose last presence names it.)
        var own = LoadGate.LobbyPresence(1, 0, "stagML11", b.Instance!.Id, 0).Frame();
        hub.Receive(b, own[0], own.AsSpan(Wire.HeaderBytes), now);
        // Five minutes at the gate's 10 Hz from a phase near the top of its random start.
        var phase = 6.2f;
        for (var k = 0; k < 3000; ++k)
        {
            phase += 0.05f;
            now += Stopwatch.Frequency / 10;
            var msg = LoadGate.LobbyPresence(0, phase, "stagML11", a.Instance!.Id, (uint)k * 100);
            Assert.Null(msg.Presence.Implausible());
            Assert.InRange(msg.Presence.Yaw, -MathF.PI, MathF.PI);
            var f = msg.Frame();
            hub.Receive(a, f[0], f.AsSpan(Wire.HeaderBytes), now);
        }
        Assert.Equal(0, hub.Counters.GetValueOrDefault("presence_implausible"));
        Assert.Equal(0, hub.Counters.GetValueOrDefault("presence_dropped"));
        Assert.Equal(3000, peers[1].Presence);
    }

    /// <summary>
    /// The gate measures the newest protocol (the integration review of r71: it ran 47 rooms, and 48's check-fingerprint
    /// pass in OnNetFp went unmeasured); a join from a checkpoint at 48 names the check tick and is checked against the
    /// donor's NETFP of it, and joins (the JOIN_REQ to the donor, the instance sealed after the JOIN_AT), at 47 as before.
    /// </summary>
    [Fact]
    public void The_gate_runs_the_newest_protocol_and_its_checkpoint_join_is_checked()
    {
        Assert.Equal(Wire.ReadyCheckProtocol, LoadGate.Protocol);
        var r = LoadGate.Join(1, _ => { });
        var classic = (Dictionary<string, object>)r["classic"];
        var checkpoint = (Dictionary<string, object>)r["checkpoint"];
        Assert.Equal((1, (int)InstanceMode.Sealed, Wire.ReadyCheckProtocol), ((int)classic["join_reqs"], (int)classic["mode_after"], (int)classic["protocol"]));
        Assert.Equal((1, (int)InstanceMode.Sealed), ((int)checkpoint["join_reqs"], (int)checkpoint["mode_after"]));
        Assert.True(checkpoint.ContainsKey("check_netfp"));
        try
        {
            LoadGate.Protocol = Wire.CheckpointProtocol;
            var old = (Dictionary<string, object>)LoadGate.Join(1, _ => { })["checkpoint"];
            Assert.Equal((1, (int)InstanceMode.Sealed, Wire.CheckpointProtocol), ((int)old["join_reqs"], (int)old["mode_after"], (int)old["protocol"]));
            Assert.False(old.ContainsKey("check_netfp"));
        }
        finally
        {
            LoadGate.Protocol = Wire.ReadyCheckProtocol;
        }
    }

    [Fact]
    public void A_run_with_frames_the_hub_refused_or_dropped_is_invalid()
    {
        Assert.Empty(LoadGate.InvalidReasons(new Dictionary<string, long> { ["delay_changes"] = 9, ["presence_dropped"] = 1 }, 1000, 0));
        Assert.Equal(["presence_implausible_yaw 7"],
            LoadGate.InvalidReasons(new Dictionary<string, long> { ["presence_implausible_yaw"] = 7 }, 1000, 0));
        Assert.Single(LoadGate.InvalidReasons(new Dictionary<string, long> { ["presence_dropped"] = 11 }, 1000, 0));
        Assert.Single(LoadGate.InvalidReasons(new Dictionary<string, long>(), 1000, 2));
    }

    [Fact]
    public void A_small_in_process_cell_runs_valid_with_the_full_fan_out()
    {
        // 20 lobby members in one stage for a second (after the 3 s warm-up): each PRESENCE2 to the 19 others.
        var r = LoadGate.Run(new LoadGate.Cell("t", 0, 0, 20, 1, 0, 0, false, 0), 1, 2, _ => { });
        Assert.True((bool)r["valid"], string.Join("; ", (List<string>)r["invalid_reasons"]));
        var sent = (double)r["presence_sent_per_second"];
        var forwarded = (double)r["frames_out_per_second"];
        Assert.InRange(sent, 150, 250);
        Assert.InRange(forwarded / sent, 18, 19.5);
    }
}
