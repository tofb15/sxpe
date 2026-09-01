namespace Sxpe.Games;

/// <summary>
/// Reads and writes a package for one <see cref="IGameProfile"/>.
/// Core must not assume DBPF/RefPack layout.
/// </summary>
public interface IPackageCodec
{
    IGameProfile Profile { get; }
}
