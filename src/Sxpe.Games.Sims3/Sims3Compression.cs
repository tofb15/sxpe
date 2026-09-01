using Sxpe.Games;

namespace Sxpe.Games.Sims3;

/// <summary>
/// Placeholder for RefPack/QFS. Real codec is a later milestone.
/// </summary>
public sealed class Sims3Compression : ICompressionCodec
{
    public string Name => "RefPack";

    public bool TryDecompress(ReadOnlySpan<byte> input, Span<byte> output, out int bytesWritten)
    {
        bytesWritten = 0;
        return false;
    }

    public bool TryCompress(ReadOnlySpan<byte> input, Span<byte> output, out int bytesWritten)
    {
        bytesWritten = 0;
        return false;
    }
}
