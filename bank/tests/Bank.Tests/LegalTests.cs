using Xunit;

namespace Rocknixds.Bank.Tests;

/// <summary>The terms of use the app asks for: the repository's LEGAL.md, and when they count as accepted.</summary>
public class LegalTests
{
    private static string RepoLegal()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
        {
            var f = Path.Combine(dir.FullName, "LEGAL.md");
            if (File.Exists(f) && Directory.Exists(Path.Combine(dir.FullName, "bank")))
                return File.ReadAllText(f);
        }
        throw new FileNotFoundException("LEGAL.md");
    }

    [Fact]
    public void TheAppShowsTheTermsFromTheRepository()
    {
        Assert.Equal(RepoLegal().ReplaceLineEndings(), LegalTerms.Text.ReplaceLineEndings());
        Assert.Equal(1, LegalTerms.Version);
    }

    [Fact]
    public void TheTermsSayWhatTheyMustSay()
    {
        var t = LegalTerms.Text;
        Assert.Contains("does not support, encourage or condone piracy", t);
        Assert.Contains("original cartridges or discs that you own", t);
        Assert.Contains("belong to their owners", t);
        Assert.Contains("not affiliated with", t);
        Assert.Contains("C-435/12", t);
        Assert.Contains("Article 2(2)(c) GDPR", t);
        Assert.Contains("cannot be excluded or limited under the law", t);
        Assert.Contains("add no restriction on your rights under those licences", t);
    }

    [Fact]
    public void AcceptingIsRememberedUntilANewVersion()
    {
        var cfg = TradeTests.Cfg("Jorre");
        Assert.False(LegalTerms.IsAccepted(cfg));
        LegalTerms.Accept(cfg);
        Assert.True(LegalTerms.IsAccepted(cfg));
        Assert.Equal(LegalTerms.Version, cfg.TermsAccepted);
        Assert.Matches(@"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ$", cfg.TermsAcceptedAt);

        cfg.TermsAccepted = LegalTerms.Version - 1;     // accepted an older version
        Assert.False(LegalTerms.IsAccepted(cfg));
    }

    [Fact]
    public void AcceptanceSurvivesARestart()
    {
        var dir = Directory.CreateTempSubdirectory("terms");
        try
        {
            var path = Path.Combine(dir.FullName, "bank.json");
            var cfg = TradeTests.Cfg("Jorre").WithPath(path);
            LegalTerms.Accept(cfg);
            cfg.Save();
            Assert.True(LegalTerms.IsAccepted(BankConfig.Load(path)));
        }
        finally
        {
            dir.Delete(true);
        }
    }
}
