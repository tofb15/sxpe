using Sxpe.Games;
using Sxpe.Games.Sims3;

namespace Sxpe.Core;

/// <summary>
/// v1 registers The Sims 3 only. Unknown sniffs return null (caller must refuse).
/// </summary>
public sealed class GameProfileRegistry : IGameProfileRegistry
{
    public GameProfileRegistry()
        : this([new Sims3GameProfile()])
    {
    }

    public GameProfileRegistry(IEnumerable<IGameProfile> profiles)
    {
        Profiles = profiles.ToArray();
    }

    public IReadOnlyList<IGameProfile> Profiles { get; }

    public IGameProfile? Find(GameId id) =>
        Profiles.FirstOrDefault(p => p.Id == id);

    public IGameProfile? Sniff(ReadOnlySpan<byte> header)
    {
        IGameProfile? best = null;
        var bestScore = 0f;
        foreach (var profile in Profiles)
        {
            if (profile.TrySniff(header, out var score) && score > bestScore)
            {
                best = profile;
                bestScore = score;
            }
        }

        return best;
    }
}
