namespace DW4Online.Server;

/// <summary>
/// The test stack's ordered stop (tools/online_test/run.py, the world's restart scenarios W24 and W25 of SEAMLESS.md
/// 19.24): with Testing:StopFile set, the service stops as a service stop does (every hosted service's StopAsync: the
/// relay closes the world's connections) once that file exists. A process started without a console cannot be sent
/// Ctrl+C or Ctrl+Break, so the harness writes the file instead. Not registered without the setting.
/// </summary>
internal sealed class StopFileWatcher(string path, IHostApplicationLifetime lifetime, ILogger<StopFileWatcher> log) : BackgroundService
{
    protected override async Task ExecuteAsync(CancellationToken stop)
    {
        try
        {
            while (!stop.IsCancellationRequested)
            {
                if (File.Exists(path))
                {
                    log.LogInformation("The stop file {Path} appeared: stopping in order", path);
                    try
                    {
                        File.Delete(path);
                    }
                    catch (IOException)
                    {
                    }
                    lifetime.StopApplication();
                    return;
                }
                await Task.Delay(200, stop);
            }
        }
        catch (OperationCanceledException)
        {
        }
    }
}
