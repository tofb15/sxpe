namespace Sxpe.Games;

/// <summary>
/// Opaque resource identity. Sims 3 uses TGI; a future profile may add fields
/// without changing the command bus.
/// </summary>
public interface IResourceIdentity : IEquatable<IResourceIdentity>
{
    GameId Game { get; }

    string ToDisplayString();
}
