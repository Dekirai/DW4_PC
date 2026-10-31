using DW4Online.Data.Migrations;

namespace DW4Online.Tests;

/// <summary>
/// The migrator's warning about MariaDB's native AIO on Windows before 10.6 (DDL that hangs for good: XAMPP's 10.4.32,
/// the server's MariaDB). 'migrate' prints it, the service logs it as WARN.
/// </summary>
public class MigratorWarningTests
{
    [Theory]
    [InlineData("10.4.32-MariaDB", "Win64", "ON", true)]   // XAMPP 8.x as installed (my.ini without the third line)
    [InlineData("10.4.32-MariaDB", "Win64", "1", true)]
    [InlineData("10.5.27-MariaDB", "Win64", "ON", true)]
    [InlineData("10.4.32-MariaDB", "Win64", "OFF", false)] // innodb_use_native_aio=0
    [InlineData("10.4.32-MariaDB", "Win64", "0", false)]
    [InlineData("10.6.21-MariaDB", "Win64", "ON", false)]  // 10.6 replaced that AIO code
    [InlineData("11.4.5-MariaDB", "Win64", "ON", false)]   // the MSI of DEPLOY.md section 1
    [InlineData("10.4.32-MariaDB", "Linux", "ON", false)]
    [InlineData("8.0.36", "Win64", "ON", false)]           // MySQL
    [InlineData("", "", "", false)]                        // variables missing
    public void Native_AIO_is_flagged_only_on_MariaDB_before_10_6_on_Windows(string version, string os, string aio, bool flagged)
    {
        var warning = Migrator.NativeAioWarning(version, os, aio);
        Assert.Equal(flagged, warning is not null);
        if (warning is null) return;
        Assert.StartsWith($"MariaDB {version} on Windows runs with innodb_use_native_aio=ON.", warning);
        Assert.Contains("innodb_use_native_aio=0 at the end of the [mysqld] section", warning);
        Assert.Contains("DEPLOY.md section 1.1", warning);
    }
}
