using System.Security.Cryptography;
using DW4Online.Domain.Abstractions;
using DW4Online.Domain.Accounts;
using DW4Online.Domain.Security;

namespace DW4Online.Domain.Services;

public sealed record AccountView(long Id, string Username, string? Email, string Role, string Status, bool MustChangePassword, bool ProfilePublic, DateTime CreatedAt, DateTime? LastLoginAt)
{
    public static AccountView From(AccountRecord a) =>
        new(a.Id, a.Username, a.Email, a.Role, a.Status, a.MustChangePassword, a.ProfilePublic, a.CreatedAt, a.LastLoginAt);
}

public sealed record SessionTicket(string Token, DateTime ExpiresAt, string Kind);

public sealed record LoginResult(SessionTicket Session, AccountView Account);

public sealed record RegisterResult(AccountView Account, string RecoveryCode, SessionTicket Session);

/// <summary>Who is calling: the request's IP (for audit and new sessions) and the device label of a new session.</summary>
public sealed record CallerInfo(byte[]? Ip, string? DeviceName);

/// <summary>Registration, login, sessions, password changes, recovery codes and the staff actions on accounts.</summary>
public sealed class AccountService(
    IAccountRepository accounts,
    ISessionRepository sessions,
    IAuditLog audit,
    PasswordHasher hasher,
    AuditNameHasher names,
    LoginThrottle throttle,
    AccountOptions options,
    TimeProvider time)
{
    public async Task<Result<RegisterResult>> RegisterAsync(string? username, string? password, string? email, string kind, CallerInfo caller, CancellationToken ct = default)
    {
        if (!string.Equals(options.Registration, "open", StringComparison.OrdinalIgnoreCase))
            return DomainError.Forbidden("registration_closed", "Registration is closed on this server.");
        if (UsernamePolicy.Check(username) is { } nameError) return DomainError.Invalid("invalid_username", nameError);
        if (PasswordPolicy.Check(password, username) is { } passwordError) return DomainError.Invalid("weak_password", passwordError);
        email = string.IsNullOrWhiteSpace(email) ? null : email.Trim();
        if (EmailPolicy.Check(email) is { } emailError) return DomainError.Invalid("invalid_email", emailError);

        var now = Clock.Now(time);
        var recovery = RecoveryCode.Create();
        long id;
        try
        {
            id = await accounts.CreateAsync(
                new NewAccount(username!, email, hasher.Hash(password!), RecoveryCode.Hash(RecoveryCode.Normalize(recovery)!), Roles.Player), now, ct);
        }
        catch (DuplicateAccountException e) when (e.Field == "email")
        {
            return DomainError.Conflict("email_taken", "Another account uses this e-mail address.");
        }
        catch (DuplicateAccountException)
        {
            return DomainError.Conflict("username_taken", "This user name is taken.");
        }
        await audit.WriteAsync("register", id, null, caller.Ip, null, now, ct);
        var account = (await accounts.GetAsync(id, ct))!;
        var ticket = await CreateSessionAsync(id, kind, caller, now, ct);
        return new RegisterResult(AccountView.From(account), recovery, ticket);
    }

    /// <summary>
    /// Logs in. Failed attempts lock the pair of user name and client address (<see cref="LoginThrottle"/>, the same for
    /// names without an account), and many failures from all addresses lock the account except for addresses it used
    /// before (<see cref="AccountOptions.MaxFailedLoginsPerAccount"/>): strangers cannot lock the owner out. Only the
    /// canonical form of the typed name is looked up (<see cref="UsernamePolicy.Canonical"/>); text that cannot be a user
    /// name is an unknown name without a database query or a password hash (no account can have it, so its timing
    /// tells nothing).
    /// </summary>
    public async Task<Result<LoginResult>> LoginAsync(string? username, string? password, string kind, CallerInfo caller, CancellationToken ct = default)
    {
        var now = Clock.Now(time);
        var key = LoginThrottle.Key(username, caller.Ip);
        if (throttle.LockedUntil(key) is { } refused) return TemporarilyLocked(refused, now);
        var name = UsernamePolicy.Canonical(username);
        var account = name is null ? null : await accounts.FindByUsernameAsync(name, ct);
        if (account is null || account.Status == AccountStatus.Deleted)
        {
            if (name is not null) hasher.VerifyDummy(password ?? "");
            throttle.Fail(key);
            // Never the typed name itself (often a password typed into the wrong field): a keyed hash of it.
            var tag = names.Tag(Truncate(username, 40));
            await audit.WriteAsync("login_unknown", null, null, caller.Ip, tag is null ? null : new { name = tag }, now, ct);
            return BadCredentials();
        }
        if (await AccountLockAsync(account, caller, now, ct) is { } locked) return locked;

        var check = hasher.Verify(account.PasswordHash, password ?? "");
        if (check == PasswordCheck.Failed)
        {
            await FailedAttemptAsync(account, "login_fail", key, caller, now, ct);
            return BadCredentials();
        }
        throttle.Reset(key);
        throttle.ResetAccount(account.Id);
        if (Blocked(account) is { } blocked) return blocked; // only told to someone who knows the password
        await accounts.RecordLoginAsync(account.Id, now, check == PasswordCheck.SuccessRehashNeeded ? hasher.Hash(password!) : null, ct);
        var ticket = await CreateSessionAsync(account.Id, kind, caller, now, ct);
        await audit.WriteAsync("login_ok", account.Id, null, caller.Ip, new { kind, device = Truncate(caller.DeviceName, 64) }, now, ct);
        var fresh = (await accounts.GetAsync(account.Id, ct))!;
        return new LoginResult(ticket, AccountView.From(fresh));
    }

    /// <summary>
    /// The session behind a bearer token; extends its sliding expiry at most every few minutes, and at once when the
    /// session is used from another address than last time (<paramref name="ip"/> becomes its last address, which the
    /// account-wide lock counts as known: <see cref="AccountLockAsync"/>).
    /// </summary>
    public async Task<AuthenticatedSession?> AuthenticateAsync(string? token, byte[]? ip = null, CancellationToken ct = default)
    {
        if (!OpaqueToken.LooksValid(token)) return null;
        var now = Clock.Now(time);
        var session = await sessions.FindAsync(OpaqueToken.Hash(token!), now, ct);
        if (session is null) return null;
        var moved = ip is { Length: > 0 } && (session.LastUsedIp is null || !ip.AsSpan().SequenceEqual(session.LastUsedIp));
        if (moved || session.LastUsedAt is null || now - session.LastUsedAt.Value >= TimeSpan.FromMinutes(options.SessionTouchMinutes))
            await sessions.TouchAsync(session.SessionId, now, now + Lifetime(session.Kind), ip is { Length: > 0 } ? ip : null, ct);
        return session;
    }

    public async Task LogoutAsync(AuthenticatedSession session, CancellationToken ct = default) =>
        await sessions.RevokeAsync(session.AccountId, session.SessionId, Clock.Now(time), ct);

    public async Task<int> LogoutEverywhereAsync(AuthenticatedSession session, CallerInfo caller, CancellationToken ct = default)
    {
        var now = Clock.Now(time);
        var count = await sessions.RevokeAllAsync(session.AccountId, null, now, ct);
        await audit.WriteAsync("logout_all", session.AccountId, session.AccountId, caller.Ip, new { count }, now, ct);
        return count;
    }

    /// <summary>Ends every session of the account except the caller's own.</summary>
    public async Task<int> LogoutOthersAsync(AuthenticatedSession session, CallerInfo caller, CancellationToken ct = default)
    {
        var now = Clock.Now(time);
        var count = await sessions.RevokeAllAsync(session.AccountId, session.SessionId, now, ct);
        await audit.WriteAsync("logout_others", session.AccountId, session.AccountId, caller.Ip, new { count }, now, ct);
        return count;
    }

    public Task<IReadOnlyList<SessionInfo>> ListSessionsAsync(long accountId, CancellationToken ct = default) =>
        sessions.ListAsync(accountId, Clock.Now(time), ct);

    public Task<bool> RevokeSessionAsync(long accountId, long sessionId, CancellationToken ct = default) =>
        sessions.RevokeAsync(accountId, sessionId, Clock.Now(time), ct);

    public async Task<AccountView?> GetAccountAsync(long accountId, CancellationToken ct = default) =>
        await accounts.GetAsync(accountId, ct) is { } a ? AccountView.From(a) : null;

    public async Task<AccountView?> FindAccountAsync(string username, CancellationToken ct = default) =>
        await accounts.FindByUsernameAsync(UsernamePolicy.Normalize(username), ct) is { } a ? AccountView.From(a) : null;

    /// <summary>
    /// The current password confirms a destructive action (account deletion, online character reset, cloud save
    /// deletion). A wrong one counts like a failed login (the caller's address locks after
    /// <see cref="AccountOptions.MaxFailedLogins"/>, the account after <see cref="AccountOptions.MaxFailedLoginsPerAccount"/>),
    /// and a locked address confirms nothing until the lock ends.
    /// </summary>
    public async Task<Result<bool>> ConfirmPasswordAsync(long accountId, string? password, CallerInfo caller, CancellationToken ct = default)
    {
        var account = await accounts.GetAsync(accountId, ct);
        if (account is null || account.Status == AccountStatus.Deleted) return DomainError.Unauthorized("unauthorized", "Not logged in.");
        if (await ConfirmAsync(account, password, caller, ct) is { } wrong) return wrong;
        return true;
    }

    /// <summary>Changes the password; every other session of the account is revoked.</summary>
    public async Task<Result<bool>> ChangePasswordAsync(AuthenticatedSession session, string? currentPassword, string? newPassword, CallerInfo caller, CancellationToken ct = default)
    {
        var account = await accounts.GetAsync(session.AccountId, ct);
        if (account is null) return DomainError.Unauthorized("unauthorized", "Not logged in.");
        if (await ConfirmAsync(account, currentPassword, caller, ct) is { } wrong) return wrong;
        if (PasswordPolicy.Check(newPassword, account.Username) is { } error) return DomainError.Invalid("weak_password", error);
        var now = Clock.Now(time);
        await accounts.SetPasswordAsync(account.Id, hasher.Hash(newPassword!), false, now, ct);
        var revoked = await sessions.RevokeAllAsync(account.Id, session.SessionId, now, ct);
        await audit.WriteAsync("password_change", account.Id, account.Id, caller.Ip, new { revoked }, now, ct);
        return true;
    }

    /// <summary>Resets a forgotten password with the one-time recovery code; returns the next recovery code.</summary>
    public async Task<Result<string>> RecoverAsync(string? username, string? code, string? newPassword, CallerInfo caller, CancellationToken ct = default)
    {
        var now = Clock.Now(time);
        var key = LoginThrottle.Key(username, caller.Ip);
        if (throttle.LockedUntil(key) is { } refused) return TemporarilyLocked(refused, now);
        var name = UsernamePolicy.Canonical(username); // as for logins: only the canonical form is looked up
        var account = name is null ? null : await accounts.FindByUsernameAsync(name, ct);
        var normalized = RecoveryCode.Normalize(code);
        if (account is null || account.Status == AccountStatus.Deleted)
        {
            throttle.Fail(key);
            return DomainError.Unauthorized("bad_recovery_code", "User name or recovery code is wrong.");
        }
        if (await AccountLockAsync(account, caller, now, ct) is { } locked) return locked;
        if (normalized is null || account.RecoveryCodeHash is null ||
            !CryptographicOperations.FixedTimeEquals(RecoveryCode.Hash(normalized), account.RecoveryCodeHash))
        {
            await FailedAttemptAsync(account, "recover_fail", key, caller, now, ct);
            return DomainError.Unauthorized("bad_recovery_code", "User name or recovery code is wrong.");
        }
        throttle.Reset(key);
        throttle.ResetAccount(account.Id);
        if (Blocked(account) is { } blocked) return blocked;
        if (PasswordPolicy.Check(newPassword, account.Username) is { } error) return DomainError.Invalid("weak_password", error);
        var next = RecoveryCode.Create();
        await accounts.SetPasswordAsync(account.Id, hasher.Hash(newPassword!), false, now, ct);
        await accounts.SetRecoveryCodeAsync(account.Id, RecoveryCode.Hash(RecoveryCode.Normalize(next)!), ct);
        await accounts.RecordFailedLoginAsync(account.Id, 0, null, ct); // clears failed attempts and the lock
        var revoked = await sessions.RevokeAllAsync(account.Id, null, now, ct);
        await audit.WriteAsync("recover_ok", account.Id, account.Id, caller.Ip, new { revoked }, now, ct);
        return next;
    }

    public async Task<Result<string>> RegenerateRecoveryCodeAsync(AuthenticatedSession session, string? currentPassword, CallerInfo caller, CancellationToken ct = default)
    {
        var account = await accounts.GetAsync(session.AccountId, ct);
        if (account is null) return DomainError.Unauthorized("unauthorized", "Not logged in.");
        if (await ConfirmAsync(account, currentPassword, caller, ct) is { } wrong) return wrong;
        var code = RecoveryCode.Create();
        var now = Clock.Now(time);
        await accounts.SetRecoveryCodeAsync(account.Id, RecoveryCode.Hash(RecoveryCode.Normalize(code)!), ct);
        await audit.WriteAsync("recovery_code_new", account.Id, account.Id, caller.Ip, null, now, ct);
        return code;
    }

    public async Task<Result<AccountView>> SetEmailAsync(AuthenticatedSession session, string? currentPassword, string? email, CallerInfo caller, CancellationToken ct = default)
    {
        var account = await accounts.GetAsync(session.AccountId, ct);
        if (account is null) return DomainError.Unauthorized("unauthorized", "Not logged in.");
        if (await ConfirmAsync(account, currentPassword, caller, ct) is { } wrong) return wrong;
        email = string.IsNullOrWhiteSpace(email) ? null : email.Trim();
        if (EmailPolicy.Check(email) is { } error) return DomainError.Invalid("invalid_email", error);
        if (!await accounts.SetEmailAsync(account.Id, email, ct))
            return DomainError.Conflict("email_taken", "Another account uses this e-mail address.");
        await audit.WriteAsync("email_change", account.Id, account.Id, caller.Ip, null, Clock.Now(time), ct);
        return AccountView.From((await accounts.GetAsync(account.Id, ct))!);
    }

    /// <summary>Public profiles show the online character and achievements on the website; private ones only to the owner and staff.</summary>
    public async Task<Result<AccountView>> SetProfilePublicAsync(AuthenticatedSession session, bool isPublic, CallerInfo caller, CancellationToken ct = default)
    {
        if (await accounts.GetAsync(session.AccountId, ct) is null) return DomainError.Unauthorized("unauthorized", "Not logged in.");
        await accounts.SetProfilePublicAsync(session.AccountId, isPublic, ct);
        await audit.WriteAsync(isPublic ? "profile_public" : "profile_private", session.AccountId, session.AccountId, caller.Ip, null, Clock.Now(time), ct);
        return AccountView.From((await accounts.GetAsync(session.AccountId, ct))!);
    }

    /// <summary>
    /// Deletes the caller's account after a password check (GDPR erasure): every personal record goes, the row stays
    /// anonymised as "deleted_&lt;id&gt;" so rooms and audit rows keep a valid reference. Take the account out of its
    /// room first (RoomService.RemoveAccountAsync).
    /// </summary>
    public async Task<Result<bool>> DeleteAccountAsync(AuthenticatedSession session, string? currentPassword, CallerInfo caller, CancellationToken ct = default)
    {
        var account = await accounts.GetAsync(session.AccountId, ct);
        if (account is null || account.Status == AccountStatus.Deleted) return DomainError.Unauthorized("unauthorized", "Not logged in.");
        if (await ConfirmAsync(account, currentPassword, caller, ct) is { } wrong) return wrong;
        if (account.Role == Roles.Admin) return DomainError.Forbidden("admin_account", "Admins cannot delete their own account; remove the admin role first.");
        await ScrubAsync(account, null, ct);
        return true;
    }

    /// <summary>Admins delete an account on request (GDPR erasure), like <see cref="DeleteAccountAsync"/>.</summary>
    public async Task<Result<bool>> AdminDeleteAccountAsync(AuthenticatedSession actor, string username, CancellationToken ct = default)
    {
        if (actor.Role != Roles.Admin) return DomainError.Forbidden("forbidden", "Only admins can delete accounts.");
        var target = await accounts.FindByUsernameAsync(UsernamePolicy.Normalize(username), ct);
        if (target is null || target.Status == AccountStatus.Deleted) return DomainError.NotFound("account_not_found", "No such account.");
        if (target.Id == actor.AccountId) return DomainError.Invalid("own_account", "Staff cannot delete their own account here.");
        if (target.Role == Roles.Admin) return DomainError.Forbidden("forbidden", "Remove the admin role before deleting an admin.");
        await ScrubAsync(target, actor.AccountId, ct);
        return true;
    }

    private async Task ScrubAsync(AccountRecord account, long? actorId, CancellationToken ct)
    {
        var name = UsernamePolicy.DeletedPrefix + account.Id.ToString(System.Globalization.CultureInfo.InvariantCulture);
        // The failed logins under this name before it existed (or typed with spaces) go with it: their tag and IP.
        await accounts.ScrubAsync(account.Id, name, names.Tag(account.Username), ct);
        // No IP and no detail: the audit row only says that the account was erased and by whom.
        await audit.WriteAsync(actorId is null ? "account_delete" : "admin_account_delete", account.Id, actorId ?? account.Id, null, null, Clock.Now(time), ct);
    }

    public Task<long> CountActiveAsync(CancellationToken ct = default) => accounts.CountActiveAsync(ct);

    // Staff ------------------------------------------------------------------------------------

    public async Task<IReadOnlyList<AccountView>> SearchAsync(string? query, int limit, CancellationToken ct = default) =>
        (await accounts.SearchAsync(query, Math.Clamp(limit, 1, 200), ct)).Select(AccountView.From).ToList();

    /// <summary>
    /// Admins only: sets a temporary password the user must change; all sessions end. Returns the temporary password
    /// once. (Whoever learns it can take the account over, so moderators cannot do this.)
    /// </summary>
    public async Task<Result<string>> AdminResetPasswordAsync(AuthenticatedSession actor, string username, CallerInfo caller, CancellationToken ct = default)
    {
        if (actor.Role != Roles.Admin) return DomainError.Forbidden("forbidden", "Only admins can reset passwords.");
        var target = await accounts.FindByUsernameAsync(UsernamePolicy.Normalize(username), ct);
        if (target is null || target.Status == AccountStatus.Deleted) return DomainError.NotFound("account_not_found", "No such account.");
        var temporary = TemporaryPassword.Create();
        var now = Clock.Now(time);
        await accounts.SetPasswordAsync(target.Id, hasher.Hash(temporary), true, now, ct);
        await accounts.RecordFailedLoginAsync(target.Id, 0, null, ct);
        throttle.ResetAccount(target.Id);
        await sessions.RevokeAllAsync(target.Id, null, now, ct);
        await audit.WriteAsync("admin_password_reset", target.Id, actor.AccountId, caller.Ip, null, now, ct);
        return temporary;
    }

    public async Task<Result<AccountView>> AdminSetStatusAsync(AuthenticatedSession actor, string username, string status, string? reason, CallerInfo caller, CancellationToken ct = default)
    {
        if (!AccountStatus.All.Contains(status)) return DomainError.Invalid("invalid_status", "Unknown account status.");
        // Deleting is its own action (admins only, with the GDPR erasure): never a status change.
        if (status == AccountStatus.Deleted)
            return DomainError.Invalid("invalid_status", "Accounts are deleted with the delete action (admins only), not with a status.");
        var target = await accounts.FindByUsernameAsync(UsernamePolicy.Normalize(username), ct);
        if (target is null || target.Status == AccountStatus.Deleted) return DomainError.NotFound("account_not_found", "No such account.");
        if (target.Id == actor.AccountId) return DomainError.Invalid("own_account", "Staff cannot change their own status.");
        if (target.Role != Roles.Player && actor.Role != Roles.Admin)
            return DomainError.Forbidden("forbidden", "Only admins can change staff accounts.");
        var now = Clock.Now(time);
        await accounts.SetStatusAsync(target.Id, status, ct);
        if (status != AccountStatus.Active) await sessions.RevokeAllAsync(target.Id, null, now, ct);
        await audit.WriteAsync("admin_status", target.Id, actor.AccountId, caller.Ip, new { status, reason = Truncate(reason, 200) }, now, ct);
        return AccountView.From((await accounts.GetAsync(target.Id, ct))!);
    }

    public async Task<Result<AccountView>> AdminSetRoleAsync(AuthenticatedSession? actor, string username, string role, CallerInfo caller, CancellationToken ct = default)
    {
        if (!Roles.All.Contains(role)) return DomainError.Invalid("invalid_role", "Unknown role.");
        if (actor is not null && actor.Role != Roles.Admin) return DomainError.Forbidden("forbidden", "Only admins can change roles.");
        var target = await accounts.FindByUsernameAsync(UsernamePolicy.Normalize(username), ct);
        if (target is null) return DomainError.NotFound("account_not_found", "No such account.");
        await accounts.SetRoleAsync(target.Id, role, ct);
        await audit.WriteAsync("admin_role", target.Id, actor?.AccountId, caller.Ip, new { role }, Clock.Now(time), ct);
        return AccountView.From((await accounts.GetAsync(target.Id, ct))!);
    }

    // ------------------------------------------------------------------------------------------

    private async Task<SessionTicket> CreateSessionAsync(long accountId, string kind, CallerInfo caller, DateTime now, CancellationToken ct)
    {
        kind = kind == SessionKind.Web ? SessionKind.Web : SessionKind.Game;
        var token = OpaqueToken.Create();
        var expires = now + Lifetime(kind);
        await sessions.CreateAsync(accountId, OpaqueToken.Hash(token), kind, Truncate(caller.DeviceName, 64), caller.Ip, now, expires, ct);
        await sessions.TrimAsync(accountId, options.MaxSessionsPerAccount, now, ct);
        return new SessionTicket(token, expires, kind);
    }

    private TimeSpan Lifetime(string kind) =>
        TimeSpan.FromDays(kind == SessionKind.Web ? options.WebSessionDays : options.GameSessionDays);

    /// <summary>
    /// A wrong password or code: counts for the pair of name and address (<paramref name="key"/>) and for the account,
    /// which is locked for everyone but known addresses at <see cref="AccountOptions.MaxFailedLoginsPerAccount"/>
    /// failures within <see cref="AccountOptions.LockoutMinutes"/> (<see cref="LoginThrottle.FailAccount"/>; one network
    /// adds at most <see cref="AccountOptions.MaxFailedLogins"/> of them). A lock in force stays: a known address's wrong
    /// password does not end it. accounts.failed_logins keeps the failures since the last success for the staff.
    /// </summary>
    private async Task FailedAttemptAsync(AccountRecord account, string evt, string key, CallerInfo caller, DateTime now, CancellationToken ct)
    {
        var addressLocked = throttle.Fail(key) is not null;
        var started = throttle.FailAccount(account.Id, caller.Ip);
        var lockedUntil = started ?? (account.LockedUntil is { } held && held > now ? held : null);
        var failed = Math.Min(account.FailedLogins + 1, ushort.MaxValue);
        await accounts.RecordFailedLoginAsync(account.Id, failed, lockedUntil, ct);
        await audit.WriteAsync(started is null ? evt : evt + "_locked", account.Id, null, caller.Ip,
            addressLocked ? new { addressLocked } : null, now, ct);
    }

    /// <summary>
    /// The account-wide lock (many failures from many addresses): an address the account created a session from or last
    /// used one from within <see cref="AccountOptions.KnownAddressDays"/> (IPv6: the same /64) may still try, any other
    /// is refused until the lock ends.
    /// </summary>
    private async Task<DomainError?> AccountLockAsync(AccountRecord account, CallerInfo caller, DateTime now, CancellationToken ct)
    {
        if (account.LockedUntil is not { } until || until <= now) return null;
        if (caller.Ip is { Length: > 0 } ip && await sessions.KnowsAddressAsync(account.Id, ip, now.AddDays(-options.KnownAddressDays), ct))
            return null;
        return TemporarilyLocked(until, now);
    }

    /// <summary>Null when the password is the account's current one; a wrong one counts as a failed attempt.</summary>
    private async Task<DomainError?> ConfirmAsync(AccountRecord account, string? password, CallerInfo caller, CancellationToken ct)
    {
        var now = Clock.Now(time);
        var key = LoginThrottle.Key(account.Username, caller.Ip);
        if (throttle.LockedUntil(key) is { } refused) return TemporarilyLocked(refused, now);
        if (await AccountLockAsync(account, caller, now, ct) is { } locked) return locked;
        if (hasher.Verify(account.PasswordHash, password ?? "") == PasswordCheck.Failed)
        {
            await FailedAttemptAsync(account, "confirm_fail", key, caller, now, ct);
            return DomainError.Invalid("wrong_password", "The current password is wrong.");
        }
        throttle.Reset(key);
        throttle.ResetAccount(account.Id);
        if (account.FailedLogins > 0 || account.LockedUntil is not null) await accounts.RecordFailedLoginAsync(account.Id, 0, null, ct);
        return null;
    }

    private static DomainError? Blocked(AccountRecord account) => account.Status switch
    {
        AccountStatus.Banned => DomainError.Forbidden("account_banned", "This account is banned."),
        AccountStatus.Locked => DomainError.Forbidden("account_locked", "This account is locked. Contact the server staff."),
        _ => null,
    };

    private static DomainError TemporarilyLocked(DateTime until, DateTime now) =>
        new(ErrorKind.Locked, "account_locked_temporarily", "Too many failed attempts. Try again later.",
            new Dictionary<string, object?> { ["retryAfterSeconds"] = (int)Math.Ceiling((until - now).TotalSeconds) });

    private static DomainError BadCredentials() => DomainError.Unauthorized("bad_credentials", "User name or password is wrong.");

    private static string? Truncate(string? s, int max) => s is null ? null : s.Length <= max ? s : s[..max];
}
