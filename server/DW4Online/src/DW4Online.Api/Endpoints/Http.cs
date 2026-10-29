using DW4Online.Api.Auth;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Http.Features;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

/// <summary>ETag / If-Match handling for revisioned resources (the revision number as a strong ETag).</summary>
public static class Revisions
{
    public static string ETag(int revision) => $"\"{revision}\"";

    /// <summary>The revision in If-Match ("5", W/"5" or 5), or null when absent; false when present but malformed.</summary>
    public static bool TryIfMatch(HttpRequest request, out int? revision)
    {
        revision = null;
        var value = request.Headers.IfMatch.ToString().Trim();
        if (value.Length == 0) return true;
        if (value.StartsWith("W/", StringComparison.Ordinal)) value = value[2..];
        value = value.Trim('"');
        if (!int.TryParse(value, System.Globalization.NumberStyles.None, null, out var r) || r < 1) return false;
        revision = r;
        return true;
    }

    public static bool IfNoneMatchAny(HttpRequest request) => request.Headers.IfNoneMatch.ToString().Trim() == "*";

    public static IResult BadPrecondition() =>
        ApiProblems.Of(StatusCodes.Status400BadRequest, "invalid_if_match", "If-Match must carry a revision number.");
}

public static class Bodies
{
    /// <summary>Reads at most <paramref name="max"/> bytes; null when the body is larger.</summary>
    public static async Task<byte[]?> ReadBoundedAsync(HttpContext ctx, int max)
    {
        if (ctx.Features.Get<IHttpMaxRequestBodySizeFeature>() is { IsReadOnly: false } limit)
            limit.MaxRequestBodySize = max + 1;
        if (ctx.Request.ContentLength > max) return null;
        using var buffer = new MemoryStream();
        var chunk = new byte[16384];
        try
        {
            int read;
            while ((read = await ctx.Request.Body.ReadAsync(chunk, ctx.RequestAborted)) > 0)
            {
                if (buffer.Length + read > max) return null;
                buffer.Write(chunk, 0, read);
            }
        }
        catch (BadHttpRequestException e) when (e.StatusCode == StatusCodes.Status413PayloadTooLarge)
        {
            return null;
        }
        return buffer.ToArray();
    }
}

/// <summary>After an admin reset the temporary password must be changed before anything else.</summary>
public sealed class PasswordChangeFilter : IEndpointFilter
{
    public async ValueTask<object?> InvokeAsync(EndpointFilterInvocationContext context, EndpointFilterDelegate next)
    {
        if (context.HttpContext.SessionOrNull() is { MustChangePassword: true })
            return ApiProblems.Of(StatusCodes.Status403Forbidden, "password_change_required", "Change the temporary password first.");
        return await next(context);
    }
}

public static class EndpointExtensions
{
    public static RouteGroupBuilder RequirePasswordCurrent(this RouteGroupBuilder group)
    {
        group.AddEndpointFilter<PasswordChangeFilter>();
        return group;
    }
}
