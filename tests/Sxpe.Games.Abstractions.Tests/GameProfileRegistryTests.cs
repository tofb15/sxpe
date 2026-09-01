using Sxpe.Core;
using Sxpe.Games;
using Sxpe.Games.Sims3;
using Xunit;

namespace Sxpe.Games.Tests;

public sealed class GameProfileRegistryTests
{
    [Fact]
    public void V1_registers_only_Sims3()
    {
        var registry = new GameProfileRegistry();
        Assert.Single(registry.Profiles);
        Assert.Equal(GameId.Sims3, registry.Profiles[0].Id);
        Assert.NotNull(registry.Find(GameId.Sims3));
        Assert.Null(registry.Find(GameId.Unknown));
    }

    [Fact]
    public void GameId_has_no_other_families_in_v1()
    {
        var names = Enum.GetNames<GameId>();
        Assert.Contains("Unknown", names);
        Assert.Contains("Sims3", names);
        Assert.DoesNotContain("Sims4", names);
        Assert.Equal(2, names.Length);
    }

    [Fact]
    public void Sims3_lists_expected_extensions()
    {
        var profile = new Sims3GameProfile();
        Assert.Equal("The Sims 3", profile.DisplayName);
        Assert.Contains(".package", profile.FileExtensions);
        Assert.Contains(".world", profile.FileExtensions);
        Assert.Contains(".dbc", profile.FileExtensions);
        Assert.Contains(".nhd", profile.FileExtensions);
    }

    [Fact]
    public void Sniff_without_codec_does_not_claim_bytes()
    {
        var registry = new GameProfileRegistry();
        byte[] header = [0x44, 0x42, 0x50, 0x46];
        Assert.Null(registry.Sniff(header));
    }
}
