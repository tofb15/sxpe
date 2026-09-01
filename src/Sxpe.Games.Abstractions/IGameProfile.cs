namespace Sxpe.Games;

/// <summary>
/// One game family's formats, extensions, and codecs.
/// v1 ships <c>Sxpe.Games.Sims3</c> only.
/// </summary>
public interface IGameProfile
{
    GameId Id { get; }

    string DisplayName { get; }

    IReadOnlyList<string> FileExtensions { get; }

    IPackageCodec PackageCodec { get; }

    ICompressionCodec Compression { get; }

    /// <summary>
    /// Returns whether this profile claims <paramref name="header"/>.
    /// Confidence is 0..1. v1: if no profile claims the file, refuse it.
    /// </summary>
    bool TrySniff(ReadOnlySpan<byte> header, out float confidence);
}
