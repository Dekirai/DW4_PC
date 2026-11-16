using System.Buffers.Binary;
using DW4Online.Relay;
using DW4Online.Relay.Hub;
using DW4Online.Relay.Protocol;
using Microsoft.Extensions.DependencyInjection;

namespace DW4Online.Integration.Tests;

/// <summary>
/// Round I1b, the arrival prompt over loopback with the real server and a private database (SEAMLESS.md 19.7, 19.24's server
/// integration): the gate form's PLAN round trip with its CHOICE carried by the BARRIER's plan id; a PLAN without an answer (the relay's
/// test hook drops it) whose barrier is decided as a loading-screen arrival, held without a time limit and answered with *Back to the
/// lobby*; the loading-screen prompt forgotten at a disconnect.
/// </summary>
public class WorldPromptTests
{
    private static async Task<(TestHost Host, int Port)> StartAsync(Dictionary<string, string?>? more = null)
    {
        var settings = new Dictionary<string, string?> { ["Instances:StoryMinVisitSeconds"] = "1" };
        foreach (var (k, v) in more ?? []) settings[k] = v;
        var host = await TestHost.StartAsync(RelaySettings.On(settings), fakeWorld: false);
        var port = await host.App.Services.GetRequiredService<RelayHub>().Started.WaitAsync(TimeSpan.FromSeconds(10));
        return (host, port);
    }

    private static string Ticket(TestHost host, long account) =>
        host.App.Services.GetRequiredService<DW4Online.Domain.Services.WorldService>()
            .EnterAsync(new DW4Online.Domain.Abstractions.AuthenticatedSession { AccountId = account }, null, null, null).GetAwaiter().GetResult().Value!.Ticket;

    /// <summary>a hosts a public instance at stag0101 whose stage group is joinable and no longer starting.</summary>
    private static async Task<BarrierOkMsg> HostAsync(Member a)
    {
        await a.SendAsync(new BarrierMsg(a.Inst, a.Visit, 0, "stag0101", 0, 1, 1, 5, [1], 0, BarrierMsg.PlanNewInstance).Frame());
        var ok = BarrierOkMsg.Parse(await a.NextAsync(Msg.BarrierOk));
        a.Enter(ok);
        await a.SendAsync(new SnapshotMsg(a.Inst, a.Visit, 4, 0, [1, 2, 3, 4]).Frame());
        await a.SendAsync(new SnapHashMsg(a.Inst, a.Visit, 0xAA).Frame());
        await a.SendAsync(new LogMsg(a.Inst, a.Visit, 0, Enumerable.Range(0, 60).Select(_ => new Pad(0, 0, 0, 0, 0)).ToArray()).Frame());
        await a.SendAsync(new StateMsg(a.Inst, a.Visit, 0, StateMsg.Joinable | StateMsg.MembershipSafe | StateMsg.LoadEnded, 5, StateReason.None).Frame());
        await Task.Delay(11_000); // (World:StartingSeconds: a starting instance would take the arrival as a co-arrival, 19.7.6)
        return ok;
    }

    [DbFact]
    public async Task The_gate_forms_plan_is_answered_and_the_choice_goes_with_the_barrier()
    {
        var (host, port) = await StartAsync();
        await using var scope = host;
        var (_, _, hostId) = await host.RegisterAsync("PromptHost");
        var (_, _, guestId) = await host.RegisterAsync("PromptGuest");
        await using var a = await Member.ConnectAsync(port, Ticket(host, hostId));
        await using var b = await Member.ConnectAsync(port, Ticket(host, guestId));
        var okA = await HostAsync(a);
        // b holds its confirming press at the gate: PLAN, OPTIONS within a round trip.
        await b.SendAsync(new PlanMsg(5, PlanPurpose.Gate, "stag0101", 0, 5, 1).Frame());
        var options = OptionsMsg.Parse(await b.NextAsync(Msg.Options));
        Assert.Equal((5, (byte)0, okA.World.Wid, (byte)1, "PromptHost"), (options.Plan, options.Preselected, options.Rows[0].Wid, options.Rows[0].Here,
            options.Rows[0].HostName));
        // A: CHOICE, then the press and the departure's BARRIER with the plan id.
        await b.SendAsync(new ChoiceMsg(5, 0, 0, ChoiceKind.Join, okA.World.Wid, Visibility.Public, 0, ChoiceMsg.DefaultBackPoint).Frame());
        await b.SendAsync(new BarrierMsg(b.Inst, b.Visit, 0, "stag0101", 0, 1, 1, 5, [2], 0, 5).Frame());
        var okB = BarrierOkMsg.Parse(await b.NextAsync(Msg.BarrierOk));
        Assert.Equal((BarrierDecision.Join, ArrivalKind.AutoJoin, okA.World.Wid, StoryMode.Replace), (okB.Decision, okB.World.Arrival, okB.World.Wid,
            okB.World.StoryMode));
        Assert.DoesNotContain(b.Skipped, f => f.Type == Msg.BarrierWait && BarrierWaitMsg.Parse(f.Payload).Reason == WaitReason.Choice);
    }

    [DbFact]
    public async Task A_plan_without_an_answer_becomes_a_loading_screen_prompt_without_a_time_limit_and_back_takes_it_home()
    {
        var (host, port) = await StartAsync(new() { ["World:TestDropPlans"] = "true" });
        await using var scope = host;
        var (_, _, hostId) = await host.RegisterAsync("DropHost");
        var (_, _, guestId) = await host.RegisterAsync("DropGuest");
        await using var a = await Member.ConnectAsync(port, Ticket(host, hostId));
        await using var b = await Member.ConnectAsync(port, Ticket(host, guestId));
        await HostAsync(a);
        await b.SendAsync(new PlanMsg(1, PlanPurpose.Gate, "stag0101", 0, 5, 1).Frame());
        await Task.Delay(1_100); // (World:PlanWaitMs: the client's press goes out without an answer)
        await b.SendAsync(new BarrierMsg(b.Inst, b.Visit, 0, "stag0101", 0, 1, 1, 5, [2]).Frame());
        var wait = BarrierWaitMsg.Parse(await b.NextAsync(Msg.BarrierWait));
        Assert.Equal(WaitReason.Choice, wait.Reason);
        var options = OptionsMsg.Parse(await b.NextAsync(Msg.Options));
        Assert.Equal((0, b.Inst, b.Visit), (options.Plan, options.Inst, options.Visit));
        Assert.DoesNotContain(b.Skipped, f => f.Type == Msg.Options); // (the PLAN itself was never answered)
        // No time limit: it still waits after the client's five seconds and the relay's join hold.
        await Task.Delay(6_000);
        await b.SendAsync(new ChoiceMsg(0, b.Inst, b.Visit, ChoiceKind.Back, 0, Visibility.Public, 0, ChoiceMsg.DefaultBackPoint).Frame());
        var ok = BarrierOkMsg.Parse(await b.NextAsync(Msg.BarrierOk));
        Assert.Equal((BarrierDecision.Dissolve, "stagML11", (byte)3, 0u), (ok.Decision, ok.World.RedirectStage, ok.World.RedirectPoint, ok.World.Wid));
    }

    [DbFact]
    public async Task The_loading_screen_prompt_is_forgotten_at_a_disconnect()
    {
        var (host, port) = await StartAsync();
        await using var scope = host;
        var (_, _, hostId) = await host.RegisterAsync("LossHost");
        var (_, _, guestId) = await host.RegisterAsync("LossGuest");
        await using var a = await Member.ConnectAsync(port, Ticket(host, hostId));
        await HostAsync(a);
        var b = await Member.ConnectAsync(port, Ticket(host, guestId));
        await b.SendAsync(new BarrierMsg(b.Inst, b.Visit, 0, "stag0101", 0, 1, 1, 5, [2]).Frame());
        Assert.Equal(WaitReason.Choice, BarrierWaitMsg.Parse(await b.NextAsync(Msg.BarrierWait)).Reason);
        var hub = host.App.Services.GetRequiredService<RelayHub>();
        Assert.Equal(HoldKind.Prompt, hub.Locked((h, _) => h.MemberOf(guestId)!.Arrival?.Hold));
        await b.DisposeAsync(); // (the connection lost)
        var forgotten = false;
        for (var k = 0; k < 100 && !forgotten; ++k)
        {
            await Task.Delay(50);
            forgotten = hub.Locked((h, _) => h.MemberOf(guestId)!.Arrival is null);
        }
        Assert.True(forgotten);
    }

    /// <summary>
    /// Decision 49 (SEAMLESS.md 19.7.6) through the real admission: b remembers Private (online_prefs, the website's setting). a's new
    /// instance is starting when b PLANs at the same gate: not "starting" for b (a stranger) but nothing to choose, and b's BARRIER of
    /// that plan makes b's own Private instance.
    /// </summary>
    [DbFact]
    public async Task A_remembered_private_default_gets_no_starting_flag_for_a_strangers_starting_instance_and_lands_in_its_own()
    {
        var (host, port) = await StartAsync();
        await using var scope = host;
        var (_, _, hostId) = await host.RegisterAsync("StartingHost");
        var (_, _, guestId) = await host.RegisterAsync("PrivateGuest");
        var world = host.App.Services.GetRequiredService<DW4Online.Domain.Services.WorldService>();
        Assert.True((await world.SavePrefsAsync(guestId, DW4Online.Domain.World.OnlinePrefs.Default with { DefaultVisibility = "private" })).Ok);
        await using var a = await Member.ConnectAsync(port, Ticket(host, hostId));
        await using var b = await Member.ConnectAsync(port, Ticket(host, guestId));
        await a.SendAsync(new BarrierMsg(a.Inst, a.Visit, 0, "stag0101", 0, 1, 1, 5, [1], 0, BarrierMsg.PlanNewInstance).Frame());
        var okA = BarrierOkMsg.Parse(await a.NextAsync(Msg.BarrierOk));
        Assert.Equal((BarrierDecision.New, Visibility.Public), (okA.Decision, okA.World.Visibility));
        await b.SendAsync(new PlanMsg(2, PlanPurpose.Gate, "stag0101", 0, 5, 1).Frame());
        var options = OptionsMsg.Parse(await b.NextAsync(Msg.Options));
        Assert.Equal((OptionsMsg.Nothing, 0, Visibility.Private), (options.Flags, options.Rows.Count, options.Visibility));
        await b.SendAsync(new BarrierMsg(b.Inst, b.Visit, 0, "stag0101", 0, 1, 1, 5, [2], 0, 2).Frame());
        var okB = BarrierOkMsg.Parse(await b.NextAsync(Msg.BarrierOk));
        Assert.Equal((BarrierDecision.New, ArrivalKind.NewInstance, Visibility.Private), (okB.Decision, okB.World.Arrival, okB.World.Visibility));
        Assert.NotEqual(okA.World.Wid, okB.World.Wid);
    }

    /// <summary>
    /// Decision 49's staleness guard through the gateway: a *New instance, Private* choice is remembered in memory at once and written
    /// through the history queue (held back here); a live re-check that reads the account meanwhile (still Public there) keeps Private in
    /// memory. Once the write is done, the website's newer setting (Friends) is taken as before.
    /// </summary>
    [DbFact]
    public async Task A_recheck_that_read_the_account_before_the_remembered_choice_was_written_keeps_the_choice_in_memory()
    {
        var (host, port) = await StartAsync();
        await using var scope = host;
        var (_, _, id) = await host.RegisterAsync("GuardPlayer");
        var gateway = host.App.Services.GetRequiredService<DW4Online.Server.WorldGateway>();
        var hub = host.App.Services.GetRequiredService<RelayHub>();
        var world = host.App.Services.GetRequiredService<DW4Online.Domain.Services.WorldService>();
        var notifier = host.App.Services.GetRequiredService<DW4Online.Domain.Abstractions.IWorldNotifier>();
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var write = gateway.RememberVisibility!;
        gateway.RememberVisibility = async (account, v) =>
        {
            await release.Task;
            await write(account, v);
        };
        Visibility Default() => hub.Locked((h, _) => h.MemberOf(id)!.DefaultVisibility);
        long Kept() => hub.Locked((h, _) => h.Counters.GetValueOrDefault("admit_visibility_kept"));
        static async Task Until(Func<bool> done)
        {
            for (var k = 0; k < 100 && !done(); ++k) await Task.Delay(50);
            Assert.True(done());
        }

        await using var m = await Member.ConnectAsync(port, Ticket(host, id));
        await m.SendAsync(new PlanMsg(1, PlanPurpose.Gate, "stag0101", 0, 5, 1).Frame());
        Assert.Equal(OptionsMsg.Nothing, OptionsMsg.Parse(await m.NextAsync(Msg.Options)).Flags);
        await m.SendAsync(new ChoiceMsg(1, 0, 0, ChoiceKind.New, 0, Visibility.Private, 0, ChoiceMsg.DefaultBackPoint).Frame());
        await m.SendAsync(new ChoiceMsg(1, 0, 0, ChoiceKind.Back, 0, Visibility.Private, 0, ChoiceMsg.DefaultBackPoint).Frame()); // (B)
        await Until(() => Default() == Visibility.Private);
        notifier.PrefsChanged(id); // (a re-check: it reads online_prefs, still Public)
        await Until(() => Kept() == 1);
        Assert.Equal(Visibility.Private, Default());
        Assert.Equal("public", (await world.GetPrefsAsync(id)).DefaultVisibility);
        release.SetResult();
        Assert.True(await gateway.Queue.DrainAsync(TimeSpan.FromSeconds(5)));
        Assert.Equal("private", (await world.GetPrefsAsync(id)).DefaultVisibility);
        // The website's newer setting after the write: taken (the guard ended).
        Assert.True((await world.SavePrefsAsync(id, DW4Online.Domain.World.OnlinePrefs.Default with { DefaultVisibility = "friends" })).Ok);
        await Until(() => Default() == Visibility.Friends);
        Assert.Equal(1, Kept());
    }

    /// <summary>A world member over a socket that keeps the frames it skipped while waiting for another type.</summary>
    private sealed class Member : IAsyncDisposable
    {
        private readonly RelayConnection _c;
        public uint Inst { get; private set; }
        public uint Visit { get; private set; }
        public WelcomeMsg Welcome { get; private set; } = null!;
        public List<(byte Type, byte[] Payload)> Skipped { get; } = [];

        private Member(RelayConnection c) => _c = c;

        public static async Task<Member> ConnectAsync(int port, string ticket)
        {
            var m = new Member(await RelayConnection.OpenAsync(port, $"DW4R2 {ticket} {Wire.WorldProtocol} 1 0"));
            Assert.StartsWith("OK ", await m._c.ReadLineAsync());
            m.Welcome = WelcomeMsg.Parse(await m.NextAsync(Msg.Welcome));
            (m.Inst, m.Visit) = (m.Welcome.Inst, m.Welcome.Visit);
            return m;
        }

        public void Enter(BarrierOkMsg ok) => (Inst, Visit) = (ok.NewInst, ok.NewVisit);

        public async Task SendAsync(byte[] frame) => await _c.Stream.WriteAsync(frame);

        public async Task<byte[]> NextAsync(byte type)
        {
            for (var k = 0; k < 400; ++k)
            {
                var header = await _c.ReadExactlyAsync(Wire.HeaderBytes);
                var payload = await _c.ReadExactlyAsync((int)BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(1)));
                if (header[0] == type) return payload;
                Skipped.Add((header[0], payload));
            }
            throw new InvalidOperationException("no frame " + Msg.Name(type));
        }

        public ValueTask DisposeAsync() => _c.DisposeAsync();
    }
}
