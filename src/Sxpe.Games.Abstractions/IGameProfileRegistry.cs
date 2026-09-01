namespace Sxpe.Games;

public interface IGameProfileRegistry
{
    IReadOnlyList<IGameProfile> Profiles { get; }

    IGameProfile? Find(GameId id);

    IGameProfile? Sniff(ReadOnlySpan<byte> header);
}
