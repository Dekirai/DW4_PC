using System.Globalization;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using DW4Online.Server;
using DW4Online.Tests;
using Microsoft.AspNetCore.Builder;
using Microsoft.Extensions.Configuration;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace DW4Online.Integration.Tests;

/// <summary>
/// The real host (ServerApp: Kestrel, the relay, the background services, the file log and on Windows the event log)
/// started as a service under a fake Service Control Manager, from ServerStartup on: what the SCM and the log see when
/// the start fails after the database step, when a background service stops the host, and on a clean stop. No database
/// is needed (Database:AutoMigrate off).
/// </summary>
public class ServiceHostTests
{
    private static readonly TimeSpan Tick = TimeSpan.FromMilliseconds(20);

    /// <summary>
    /// A port in use: the start ends with ExitHostFailed and no STOPPED (restart actions), and the LAST FAIL line of
    /// the file log names the port (Kestrel's endpoint, or the relay's port, whose bind failed later and only stopped
    /// the host before). Before, the host disposed its log providers before that line: the event log provider threw
    /// out of ServerStartup (an unhandled crash) and the file log dropped the line.
    /// </summary>
    [Theory]
    [InlineData("http")]
    [InlineData("relay")]
    public async Task A_port_in_use_ends_the_start_without_STOPPED_and_the_last_FAIL_line_names_it(string busy)
    {
        using var holder = new TcpListener(IPAddress.Loopback, TestPorts.Next());
        holder.Start();
        var port = ((IPEndPoint)holder.LocalEndpoint).Port;
        var logs = NewLogDirectory();
        using var scm = new FakeScm();
        try
        {
            var app = Build(scm, logs, new Dictionary<string, string?>
            {
                ["Kestrel:Endpoints:Http:Url"] = $"http://127.0.0.1:{(busy == "http" ? port : TestPorts.Next())}",
                ["Relay:Enabled"] = "true",
                ["Relay:ListenAddress"] = "127.0.0.1",
                ["Relay:Port"] = (busy == "relay" ? port : TestPorts.Next()).ToString(CultureInfo.InvariantCulture),
            });

            Assert.Equal(ServerStartup.ExitHostFailed, await ServerStartup.RunAsync(app, asService: true));

            Assert.All(scm.Statuses(), s => Assert.Equal(ServiceState.StartPending, s.State));
            var last = LastFail(await ReadLogAsync(logs));
            Assert.StartsWith("The host failed: Failed to bind ", last);
            Assert.Contains($"127.0.0.1:{port}", last);
            Assert.Contains("address already in use", last);
            Assert.EndsWith("The service ends now; its restart actions start it again.", last);
        }
        finally
        {
            DeleteLogs(logs);
        }
    }

    /// <summary>A setting the start checks before the database (here Relay:TicketKey): STOPPED with error 13, no restart.</summary>
    [Fact]
    public async Task A_relay_ticket_key_that_is_not_base64_stops_the_start_with_error_13()
    {
        var logs = NewLogDirectory();
        using var scm = new FakeScm();
        try
        {
            var app = Build(scm, logs, new Dictionary<string, string?>
            {
                ["Relay:Enabled"] = "true",
                ["Relay:ListenAddress"] = "127.0.0.1",
                ["Relay:Port"] = TestPorts.Next().ToString(CultureInfo.InvariantCulture),
                ["Relay:TicketKey"] = "<output of DW4Online.Server.exe gen-relay-key>",
            });

            Assert.Equal(ServerStartup.ExitStartRefused, await ServerStartup.RunAsync(app, asService: true));

            var statuses = scm.Statuses();
            Assert.Equal(new ServiceStatus(ServiceState.Stopped, 0, WindowsServiceLifetime.ErrorServiceSpecificError,
                ServerStartup.ExitStartRefused, 0, 0), statuses[^1]);
            var last = LastFail(await ReadLogAsync(logs));
            Assert.StartsWith("Start failed: Relay:TicketKey is not valid base64", last);
            Assert.EndsWith("The service stops (no restart): fix the cause, then Start-Service.", last);
        }
        finally
        {
            DeleteLogs(logs);
        }
    }

    /// <summary>
    /// A background service that fails after RUNNING stops the host (BackgroundServiceExceptionBehavior.StopHost): the
    /// service reports nothing after RUNNING and the process ends with ExitHostFailed, so the SCM runs the restart
    /// actions. Before, it reported STOPPED with exit code 0, a clean stop, and the server stayed off.
    /// </summary>
    [Fact]
    public async Task A_background_service_that_fails_after_RUNNING_ends_the_process_without_STOPPED()
    {
        var logs = NewLogDirectory();
        using var scm = new FakeScm();
        try
        {
            var app = Build(scm, logs, [], services => services.AddHostedService<FailingAfterStart>());

            Assert.Equal(ServerStartup.ExitHostFailed, await ServerStartup.RunAsync(app, asService: true).WaitAsync(FakeScm.Timeout));

            var statuses = scm.Statuses();
            Assert.Equal(ServiceState.Running, statuses[^1].State);
            Assert.DoesNotContain(statuses, s => s.State is ServiceState.StopPending or ServiceState.Stopped);
            var log = await ReadLogAsync(logs);
            Assert.Contains("background failure for the test", log);
            var last = LastFail(log);
            Assert.StartsWith("The host stopped although the Service Control Manager did not ask it to", last);
            Assert.EndsWith("The service ends now; its restart actions start it again.", last);
        }
        finally
        {
            DeleteLogs(logs);
        }
    }

    /// <summary>The real host under the fake SCM: START_PENDING, RUNNING once it listens, STOP, STOPPED with exit code 0.</summary>
    [Fact]
    public async Task A_service_start_and_stop_with_the_real_host()
    {
        var logs = NewLogDirectory();
        using var scm = new FakeScm();
        try
        {
            var app = Build(scm, logs, []);
            var run = ServerStartup.RunAsync(app, asService: true);
            await scm.WaitUntil(s => s.Any(x => x.State == ServiceState.Running));
            Assert.Equal(0, scm.Send(ServiceControlCode.Stop));
            Assert.Equal(0, await run.WaitAsync(FakeScm.Timeout));

            var statuses = scm.Statuses();
            Assert.Equal(new ServiceStatus(ServiceState.Stopped, 0, 0, 0, 0, 0), statuses[^1]);
            Assert.Equal(ServiceState.StopPending, statuses[^2].State);
            var log = await ReadLogAsync(logs);
            Assert.Contains("Service start: connected to the Service Control Manager", log);
            Assert.Contains("Service control STOP: stopping", log);
            Assert.DoesNotContain(" FAIL ", log);
        }
        finally
        {
            DeleteLogs(logs);
        }
    }

    private sealed class FailingAfterStart(IHostApplicationLifetime lifetime) : BackgroundService
    {
        protected override async Task ExecuteAsync(CancellationToken stop)
        {
            var started = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            using (lifetime.ApplicationStarted.Register(() => started.TrySetResult()))
                await started.Task.WaitAsync(stop);
            await Task.Delay(100, stop);
            throw new InvalidOperationException("background failure for the test");
        }
    }

    private static WebApplication Build(FakeScm scm, string logs, Dictionary<string, string?> settings, Action<IServiceCollection>? services = null) =>
        ServerApp.Build(["--environment", "Testing"], configure: builder =>
        {
            var all = new Dictionary<string, string?>
            {
                // Never contacted with AutoMigrate off; the room sweeper's start-up query fails at once (a warning).
                ["Database:ConnectionString"] = "Server=127.0.0.1;Port=1;Database=x;User ID=x;Password=x;Connection Timeout=2",
                ["Database:AutoMigrate"] = "false",
                ["Relay:Enabled"] = "false",
                ["Relay:TicketKey"] = Convert.ToBase64String(RandomNumberGenerator.GetBytes(32)),
                ["Web:DataProtectionKeysDirectory"] = "memory",
                ["Kestrel:Endpoints:Http:Url"] = $"http://127.0.0.1:{TestPorts.Next()}",
                ["Logging:LogLevel:Default"] = "Information",
                ["Logging:File:Directory"] = logs,
            };
            foreach (var (k, v) in settings) all[k] = v;
            builder.Configuration.AddInMemoryCollection(all);
            builder.Services.AddSingleton<IHostLifetime>(sp => new WindowsServiceLifetime(scm,
                sp.GetRequiredService<IHostApplicationLifetime>(), sp.GetRequiredService<ILogger<WindowsServiceLifetime>>(), Tick));
            services?.Invoke(builder.Services);
        });

    private static string NewLogDirectory() => Path.Combine(Path.GetTempPath(), "dw4online-it-" + Guid.NewGuid().ToString("N")[..8]);

    private static void DeleteLogs(string directory)
    {
        if (Directory.Exists(directory)) Directory.Delete(directory, true);
    }

    /// <summary>The message of the log's last FAIL line (after "&lt;time&gt; FAIL &lt;category&gt;: ").</summary>
    private static string LastFail(string log)
    {
        var line = log.Split('\n').Select(l => l.TrimEnd('\r')).Last(l => l.Contains(" FAIL ", StringComparison.Ordinal));
        var category = line.IndexOf(" FAIL ", StringComparison.Ordinal) + " FAIL ".Length;
        return line[(line.IndexOf(": ", category, StringComparison.Ordinal) + 2)..];
    }

    private static async Task<string> ReadLogAsync(string directory)
    {
        var file = Assert.Single(Directory.GetFiles(directory, "dw4online-*.log"));
        await using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        return await new StreamReader(stream).ReadToEndAsync();
    }
}
