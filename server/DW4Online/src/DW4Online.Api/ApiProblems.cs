using DW4Online.Domain;
using Microsoft.AspNetCore.Http;

namespace DW4Online.Api;

/// <summary>
/// Errors as RFC 9457 problem details: "type" is urn:dw4online:problem:&lt;code&gt;, "code" repeats the stable code,
/// "title" is an English message for people; extra fields (currentRevision, issues, retryAfterSeconds) sit beside them.
/// </summary>
public static class ApiProblems
{
    public static int Status(ErrorKind kind) => kind switch
    {
        ErrorKind.Invalid => StatusCodes.Status422UnprocessableEntity,
        ErrorKind.Unauthorized => StatusCodes.Status401Unauthorized,
        ErrorKind.Forbidden => StatusCodes.Status403Forbidden,
        ErrorKind.NotFound => StatusCodes.Status404NotFound,
        ErrorKind.Conflict => StatusCodes.Status409Conflict,
        ErrorKind.PreconditionRequired => StatusCodes.Status428PreconditionRequired,
        ErrorKind.Locked => StatusCodes.Status423Locked,
        ErrorKind.Outdated => StatusCodes.Status426UpgradeRequired,
        ErrorKind.TooLarge => StatusCodes.Status413PayloadTooLarge,
        ErrorKind.Unavailable => StatusCodes.Status503ServiceUnavailable,
        _ => StatusCodes.Status500InternalServerError,
    };

    public static IResult From(DomainError error)
    {
        var extensions = new Dictionary<string, object?> { ["code"] = error.Code };
        if (error.Extra is not null)
            foreach (var (key, value) in error.Extra) extensions[key] = value;
        return Results.Problem(statusCode: Status(error.Kind), title: error.Message, type: "urn:dw4online:problem:" + error.Code,
            extensions: extensions);
    }

    public static IResult Of(int status, string code, string message) =>
        Results.Problem(statusCode: status, title: message, type: "urn:dw4online:problem:" + code,
            extensions: new Dictionary<string, object?> { ["code"] = code });

    /// <summary>For middleware that writes directly (authentication challenges, rate-limit rejections).</summary>
    public static async Task Write(HttpContext context, int status, string code, string message, IDictionary<string, object?>? extra = null)
    {
        if (context.Response.HasStarted) return;
        context.Response.StatusCode = status;
        context.Response.ContentType = "application/problem+json";
        var body = new Dictionary<string, object?>
        {
            ["type"] = "urn:dw4online:problem:" + code,
            ["title"] = message,
            ["status"] = status,
            ["code"] = code,
        };
        if (extra is not null)
            foreach (var (key, value) in extra) body[key] = value;
        await context.Response.WriteAsJsonAsync(body, ApiJson.Options, "application/problem+json", context.RequestAborted);
    }

    /// <summary>Maps a result to 200 OK (or the given success) and errors to problem details.</summary>
    public static IResult Ok<T>(Result<T> result, Func<T, IResult>? success = null) =>
        result.Ok ? success?.Invoke(result.Value!) ?? Results.Ok(result.Value) : From(result.Error!);
}

public static class ApiJson
{
    public static readonly System.Text.Json.JsonSerializerOptions Options = new(System.Text.Json.JsonSerializerDefaults.Web);
}
