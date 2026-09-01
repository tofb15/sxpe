using Sxpe.Games;

namespace Sxpe.Games.Sims3;

/// <summary>Placeholder DBPF codec. Real I/O is a later milestone.</summary>
public sealed class Sims3PackageCodec : IPackageCodec
{
    public Sims3PackageCodec(IGameProfile profile)
    {
        Profile = profile;
    }

    public IGameProfile Profile { get; }
}
