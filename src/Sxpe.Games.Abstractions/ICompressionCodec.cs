namespace Sxpe.Games;

/// <summary>
/// Per-profile compression. Sims 3 uses RefPack/QFS. A later profile
/// (e.g. Sims 4) would supply a different codec. Not implemented in this assembly.
/// </summary>
public interface ICompressionCodec
{
    string Name { get; }

    bool TryDecompress(ReadOnlySpan<byte> input, Span<byte> output, out int bytesWritten);

    bool TryCompress(ReadOnlySpan<byte> input, Span<byte> output, out int bytesWritten);
}
