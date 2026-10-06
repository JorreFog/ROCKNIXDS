using System.Text.RegularExpressions;

namespace Rocknixds.Bank;

/// <summary>The terms of use: LEGAL.md at the top of the repository, built into the app.</summary>
public static partial class LegalTerms
{
    private static readonly Lazy<string> TextLazy = new(() =>
    {
        using var s = typeof(LegalTerms).Assembly.GetManifestResourceStream("LEGAL.md")
                      ?? throw new InvalidOperationException("LEGAL.md isn't built in");
        using var r = new StreamReader(s);
        return r.ReadToEnd();
    });

    public static string Text => TextLazy.Value;

    /// <summary>The "**Version N**" line under the title.</summary>
    public static int Version => VersionLine().Match(Text) is { Success: true } m ? int.Parse(m.Groups[1].Value) : 1;

    public static bool IsAccepted(BankConfig cfg) => cfg.TermsAccepted >= Version;

    public static void Accept(BankConfig cfg)
    {
        cfg.TermsAccepted = Version;
        cfg.TermsAcceptedAt = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ");
    }

    [GeneratedRegex(@"\*\*Version (\d+)\*\*")]
    private static partial Regex VersionLine();
}
