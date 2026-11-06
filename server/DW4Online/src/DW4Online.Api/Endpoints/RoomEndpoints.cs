using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Routing;

namespace DW4Online.Api.Endpoints;

/// <summary>
/// The room era's endpoints are gone (round I1, SEAMLESS.md 19.18, 19.22): /rooms and everything below it answer 410
/// rooms_retired for one release, for every method and without authentication, so a room client that slipped past the
/// version gate learns why. The world's endpoints are <see cref="WorldEndpoints"/>.
/// </summary>
public static class RoomEndpoints
{
    public static void Map(RouteGroupBuilder v1)
    {
        static IResult Retired() => ApiProblems.Of(StatusCodes.Status410Gone, "rooms_retired",
            "Rooms are retired: the online world replaced them. Please update the game.");
        v1.Map("/rooms", Retired).AllowAnonymous();
        v1.Map("/rooms/{**rest}", (string? rest) => Retired()).AllowAnonymous();
    }
}
