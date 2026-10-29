using System.Security.Cryptography;
using System.Text.Json;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Save;

namespace DW4Online.Domain.Services;

public sealed record CloudSaveOverview(CloudSaveMeta Latest, IReadOnlyList<SaveBoxSummary> Boxes, IReadOnlyList<CloudSaveMeta> History);

/// <summary>
/// The private cloud backup of the local save: one per account, the latest plus a few older revisions.
/// Never shown on the website and never used for achievements or profiles (it is freely editable).
/// </summary>
public sealed class CloudSaveService(ICloudSaveRepository saves, IAuditLog audit, CloudSaveOptions options, TimeProvider time)
{
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    public async Task<CloudSaveOverview?> GetAsync(long accountId, CancellationToken ct = default)
    {
        var meta = await saves.GetMetaAsync(accountId, ct);
        if (meta is null) return null;
        var boxes = meta.SlotSummary is null ? [] : JsonSerializer.Deserialize<List<SaveBoxSummary>>(meta.SlotSummary, Json) ?? [];
        return new CloudSaveOverview(meta, boxes, await saves.ListHistoryAsync(accountId, ct));
    }

    public Task<CloudSaveData?> DownloadAsync(long accountId, int? revision, CancellationToken ct = default) =>
        saves.GetDataAsync(accountId, revision, ct);

    /// <summary>
    /// Stores a new backup. <paramref name="sha256Hex"/> must match the body. <paramref name="expectedRevision"/>
    /// null with <paramref name="createOnly"/> creates the first backup; otherwise it must match the stored revision.
    /// </summary>
    public async Task<Result<CloudSaveMeta>> UploadAsync(long accountId, byte[] data, string? sha256Hex, int? expectedRevision, bool createOnly,
        string? deviceName, string? clientVersion, byte[]? ip, CancellationToken ct = default)
    {
        if (data.Length > options.MaxBytes)
            return new DomainError(ErrorKind.TooLarge, "payload_too_large", $"A cloud save holds at most {options.MaxBytes} bytes.");
        if (expectedRevision is null && !createOnly)
            return new DomainError(ErrorKind.PreconditionRequired, "precondition_required",
                "Send If-Match with the current revision, or If-None-Match: * for the first upload.");
        var sha = SHA256.HashData(data);
        if (sha256Hex is null || !TryHex(sha256Hex, out var claimed) || !CryptographicOperations.FixedTimeEquals(claimed, sha))
            return DomainError.Invalid("sha256_mismatch", "X-Sha256 is missing or does not match the uploaded bytes.");
        var info = LocalSaveInspector.Inspect(data, out var error);
        if (info is null) return DomainError.Invalid("invalid_save", error ?? "Not a Digimon World 4 save.");

        var now = Clock.Now(time);
        var write = new CloudSaveWrite(accountId, info.Format, data, sha, JsonSerializer.Serialize(info.Boxes, Json),
            Truncate(deviceName, 64), Truncate(clientVersion, 32), now);
        var outcome = await saves.SaveAsync(write, expectedRevision, options.HistoryRevisions, ct);
        switch (outcome.Status)
        {
            case WriteStatus.Conflict when expectedRevision is null:
                return DomainError.Conflict("cloud_save_exists", "A cloud save exists; upload with its revision.", Rev(outcome.Revision));
            case WriteStatus.Conflict:
                return DomainError.Conflict("revision_conflict", "The cloud save changed in the meantime.", Rev(outcome.Revision));
            case WriteStatus.NotFound:
                return DomainError.NotFound("no_cloud_save", "There is no cloud save yet.");
        }
        await audit.WriteAsync("cloud_upload", accountId, accountId, ip, new { revision = outcome.Revision, size = data.Length }, now, ct);
        return (await saves.GetMetaAsync(accountId, ct))!;
    }

    /// <summary>Makes a kept older revision the latest again (as a new revision).</summary>
    public async Task<Result<CloudSaveMeta>> RestoreAsync(long accountId, int revision, byte[]? ip, CancellationToken ct = default)
    {
        var current = await saves.GetMetaAsync(accountId, ct);
        if (current is null) return DomainError.NotFound("no_cloud_save", "There is no cloud save yet.");
        if (revision == current.Revision) return current;
        var old = await saves.GetDataAsync(accountId, revision, ct);
        if (old is null) return DomainError.NotFound("revision_not_found", "No such kept revision.");
        return await UploadAsync(accountId, old.Data, Convert.ToHexString(old.Sha256), current.Revision, false, current.DeviceName,
            current.ClientVersion, ip, ct);
    }

    public async Task<bool> DeleteAsync(long accountId, byte[]? ip, CancellationToken ct = default)
    {
        var done = await saves.DeleteAsync(accountId, ct);
        if (done) await audit.WriteAsync("cloud_delete", accountId, accountId, ip, null, Clock.Now(time), ct);
        return done;
    }

    private static bool TryHex(string hex, out byte[] bytes)
    {
        bytes = [];
        if (hex.Length != 64) return false;
        try
        {
            bytes = Convert.FromHexString(hex);
            return true;
        }
        catch (FormatException)
        {
            return false;
        }
    }

    private static Dictionary<string, object?> Rev(int revision) => new() { ["currentRevision"] = revision };

    private static string? Truncate(string? s, int max) => s is null ? null : s.Length <= max ? s : s[..max];
}
