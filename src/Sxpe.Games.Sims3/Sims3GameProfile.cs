using Sxpe.Games;

namespace Sxpe.Games.Sims3;

public sealed class Sims3GameProfile : IGameProfile
{
    public Sims3GameProfile()
    {
        PackageCodec = new Sims3PackageCodec(this);
        Compression = new Sims3Compression();
    }

    public GameId Id => GameId.Sims3;

    public string DisplayName => "The Sims 3";

    public IReadOnlyList<string> FileExtensions { get; } =
        [".package", ".world", ".dbc", ".nhd"];

    public IPackageCodec PackageCodec { get; }

    public ICompressionCodec Compression { get; }

    /// <summary>
    /// Header sniff is filled in when the DBPF reader lands.
    /// Until then this profile never claims a file.
    /// </summary>
    public bool TrySniff(ReadOnlySpan<byte> header, out float confidence)
    {
        confidence = 0;
        return false;
    }
}
