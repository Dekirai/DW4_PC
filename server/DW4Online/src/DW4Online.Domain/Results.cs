namespace DW4Online.Domain;

/// <summary>How the HTTP layer reports an error (problem details status).</summary>
public enum ErrorKind
{
    Invalid,              // 400 / 422
    Unauthorized,         // 401
    Forbidden,            // 403
    NotFound,             // 404
    Conflict,             // 409
    PreconditionRequired, // 428
    Locked,               // 423
    Outdated,             // 426
    TooLarge,             // 413
    Unavailable,          // 503
}

/// <summary>A failed operation: a stable machine code, an English message and optional extra fields.</summary>
public sealed record DomainError(ErrorKind Kind, string Code, string Message, IReadOnlyDictionary<string, object?>? Extra = null)
{
    public static DomainError Invalid(string code, string message, IReadOnlyDictionary<string, object?>? extra = null) => new(ErrorKind.Invalid, code, message, extra);
    public static DomainError NotFound(string code, string message) => new(ErrorKind.NotFound, code, message);
    public static DomainError Conflict(string code, string message, IReadOnlyDictionary<string, object?>? extra = null) => new(ErrorKind.Conflict, code, message, extra);
    public static DomainError Forbidden(string code, string message) => new(ErrorKind.Forbidden, code, message);
    public static DomainError Unauthorized(string code, string message) => new(ErrorKind.Unauthorized, code, message);
}

/// <summary>Either a value or a <see cref="DomainError"/>.</summary>
public readonly struct Result<T>
{
    private Result(T? value, DomainError? error)
    {
        Value = value;
        Error = error;
    }

    public T? Value { get; }
    public DomainError? Error { get; }
    public bool Ok => Error is null;

    public static implicit operator Result<T>(T value) => new(value, null);
    public static implicit operator Result<T>(DomainError error) => new(default, error);
}
