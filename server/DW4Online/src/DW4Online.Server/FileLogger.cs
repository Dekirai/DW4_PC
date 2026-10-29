using System.Collections.Concurrent;
using System.Text;
using System.Threading.Channels;

namespace DW4Online.Server;

/// <summary>
/// A minimal daily log file (logs/dw4online-yyyyMMdd.log, UTC) for the Windows service, where nobody sees the console.
/// Enabled with Logging:File:Directory; files older than Logging:File:RetainDays are deleted.
/// </summary>
internal sealed class FileLoggerProvider : ILoggerProvider
{
    private readonly string _directory;
    private readonly int _retainDays;
    private readonly LogLevel _minimum;
    private readonly Channel<string> _queue = Channel.CreateBounded<string>(new BoundedChannelOptions(10_000)
    {
        FullMode = BoundedChannelFullMode.DropOldest,
        SingleReader = true,
    });
    private readonly ConcurrentDictionary<string, FileLogger> _loggers = new();
    private readonly Task _writer;

    public FileLoggerProvider(string directory, int retainDays, LogLevel minimum)
    {
        _directory = directory;
        _retainDays = retainDays;
        _minimum = minimum;
        Directory.CreateDirectory(directory);
        _writer = Task.Run(WriteLoopAsync);
    }

    public ILogger CreateLogger(string categoryName) => _loggers.GetOrAdd(categoryName, name => new FileLogger(name, this));

    public void Dispose()
    {
        _queue.Writer.TryComplete();
        _writer.Wait(TimeSpan.FromSeconds(5));
    }

    private void Enqueue(string line) => _queue.Writer.TryWrite(line);

    private async Task WriteLoopAsync()
    {
        string? day = null;
        StreamWriter? file = null;
        try
        {
            await foreach (var line in _queue.Reader.ReadAllAsync())
            {
                var today = DateTime.UtcNow.ToString("yyyyMMdd", System.Globalization.CultureInfo.InvariantCulture);
                if (today != day)
                {
                    if (file is not null) await file.DisposeAsync();
                    day = today;
                    file = new StreamWriter(new FileStream(Path.Combine(_directory, $"dw4online-{day}.log"), FileMode.Append,
                        FileAccess.Write, FileShare.ReadWrite), new UTF8Encoding(false)) { AutoFlush = true };
                    Prune();
                }
                await file!.WriteLineAsync(line);
            }
        }
        catch (IOException)
        {
            // Logging must never take the service down.
        }
        finally
        {
            if (file is not null) await file.DisposeAsync();
        }
    }

    private void Prune()
    {
        try
        {
            foreach (var path in Directory.EnumerateFiles(_directory, "dw4online-*.log"))
                if (File.GetLastWriteTimeUtc(path) < DateTime.UtcNow.AddDays(-_retainDays)) File.Delete(path);
        }
        catch (IOException)
        {
        }
        catch (UnauthorizedAccessException)
        {
        }
    }

    private sealed class FileLogger(string category, FileLoggerProvider provider) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => logLevel != LogLevel.None && logLevel >= provider._minimum;

        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state, Exception? exception, Func<TState, Exception?, string> formatter)
        {
            if (!IsEnabled(logLevel)) return;
            var sb = new StringBuilder(160);
            sb.Append(DateTime.UtcNow.ToString("yyyy-MM-dd HH:mm:ss.fff", System.Globalization.CultureInfo.InvariantCulture))
                .Append(' ').Append(Level(logLevel)).Append(' ').Append(category).Append(": ").Append(formatter(state, exception));
            if (exception is not null) sb.AppendLine().Append(exception);
            provider.Enqueue(sb.ToString());
        }

        private static string Level(LogLevel level) => level switch
        {
            LogLevel.Trace => "TRCE",
            LogLevel.Debug => "DBUG",
            LogLevel.Information => "INFO",
            LogLevel.Warning => "WARN",
            LogLevel.Error => "FAIL",
            _ => "CRIT",
        };
    }
}
