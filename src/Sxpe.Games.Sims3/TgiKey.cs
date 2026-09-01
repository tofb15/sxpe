using Sxpe.Games;

namespace Sxpe.Games.Sims3;

/// <summary>Sims 3 Type/Group/Instance identity.</summary>
public readonly record struct TgiKey(uint Type, uint Group, ulong Instance) : IResourceIdentity
{
    public GameId Game => GameId.Sims3;

    public string ToDisplayString() =>
        $"{Type:X8}-{Group:X8}-{Instance:X16}";

    public bool Equals(IResourceIdentity? other) =>
        other is TgiKey tgi && this == tgi;
}
