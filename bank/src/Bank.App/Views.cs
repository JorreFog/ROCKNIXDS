using PKHeX.Core;

namespace Rocknixds.Bank.App;

/// <summary>Pieces drawn by several screens.</summary>
public static class Views
{
    private static string _battery = "";
    private static uint _batteryRead;

    /// <summary>The top screen's title band, with the time and the battery.</summary>
    public static void Header(Canvas c, App app, string title, string? right = null)
    {
        var p = c.P;
        c.Fill(0, 0, Canvas.W, 40, p.Band);
        c.Fill(0, 40, Canvas.W, 2, p.Low);
        c.Fill(16, 14, 12, 12, p.Red);
        c.Text(title, 38, 9, 20, p.Ink, bold: true, maxW: 380);
        if (app.Now - _batteryRead > 30000 || _batteryRead == 0)
        {
            _batteryRead = app.Now + 1;
            _battery = ReadBattery();
        }
        var status = $"{DateTime.Now:HH:mm}" + (_battery.Length > 0 ? $"   {_battery}" : "");
        c.Text(right ?? status, 624, 11, 17, p.Muted, Align.Right);
    }

    private static string ReadBattery()
    {
        try
        {
            foreach (var dir in Directory.EnumerateDirectories("/sys/class/power_supply"))
            {
                var type = Path.Combine(dir, "type");
                var cap = Path.Combine(dir, "capacity");
                if (File.Exists(type) && File.ReadAllText(type).Trim() == "Battery" && File.Exists(cap))
                    return File.ReadAllText(cap).Trim() + "%";
            }
        }
        catch (Exception)
        {
            // no battery
        }
        return "";
    }

    public static Color VerdictColor(Palette p, LegalityVerdict? v) =>
        v is null ? p.Muted : !v.Analysed ? p.Warn : v.Valid ? p.Good : p.Bad;

    /// <summary>The legality badge: "✓ Legal", "✗ Not legal", "Checking...".</summary>
    public static float Badge(Canvas c, LegalityVerdict? v, float x, float y, int size = 15)
    {
        var p = c.P;
        var col = VerdictColor(p, v);
        string label = v is null ? "Checking" : !v.Analysed ? "Unknown" : v.Valid ? "Legal" : "Not legal";
        float w = c.Measure(label, size, true) + 34;
        c.Box(x, y, w, size + 11, col, col, 1);
        var ink = p.IsDark ? Color.Hex(0x0b1016) : Color.Hex(0xffffff);
        if (v is { Analysed: true, Valid: true })
            c.Icon(Icons.Check, x + 7, y + (size + 11 - 12) / 2f, 2, ink);
        else if (v is { Analysed: true })
            c.Icon(Icons.Cross, x + 7, y + (size + 11 - 14) / 2f, 2, ink);
        else
            c.Icon(Icons.Warn, x + 10, y + (size + 11 - 12) / 2f, 2, ink);
        c.Text(label, x + 26, y + 5, size, ink, bold: true);
        return w;
    }

    /// <summary>The full picture of one Pokémon on the top screen.</summary>
    public static void MonDetails(Canvas c, App app, PKM pk, LegalityVerdict? v, string? where)
    {
        var p = c.P;
        var s = MonSummary.From(pk);

        // sprite
        c.Panel(16, 52, 210, 186, false);
        c.Mon(pk, 24, 60, 194, 162, maxScale: 3);
        if (where is not null)
            c.Text(where, 121, 214, 13, p.Muted, Align.Center, maxW: 196);

        // names
        float x = 242, y = 52;
        var title = s.Title;
        float tw = c.Text(title, x, y, 28, p.Ink, bold: true, maxW: 300);
        float ix = x + tw + 10;
        if (!s.IsEgg)
        {
            c.GenderIcon(s.Gender, ix, y + 9, 2.5f);
            if (s.Gender < 2)
                ix += 22;
        }
        if (s.IsShiny)
            c.Icon(Icons.Star, ix, y + 8, 2.5f, Color.Hex(0xffc93c));
        y += 38;
        var line2 = s.IsEgg ? "Egg" : (s.IsNicknamed ? $"{s.SpeciesName}   " : "") + $"Lv {s.Level}";
        float lw = c.Text(line2, x, y, 19, p.Ink);
        c.Tag(s.Format, x + lw + 12, y, p.Accent);
        y += 30;
        if (!s.IsEgg)
        {
            var natAbility = string.Join("  ·  ", new[] { s.Nature, s.Ability }.Where(t => t.Length > 0));
            if (natAbility.Length > 0)
            {
                c.Text(natAbility, x, y, 17, p.Ink, maxW: 382);
                y += 25;
            }
        }
        c.Text(s.Item.Length > 0 ? $"Holds {s.Item}" : "No held item", x, y, 17, s.Item.Length > 0 ? p.Ink : p.Muted, maxW: 382);
        y += 25;
        c.Text($"OT {s.OT}   ID {s.TrainerId}", x, y, 17, p.Ink, maxW: 382);
        y += 25;
        var origin = s.Met.Length > 0 ? $"{s.Origin} · {s.Met}" : s.Origin;
        c.Text(origin, x, y, 15, p.Muted, maxW: 382);
        y += 21;
        if (s.Ball.Length > 0)
            c.Text($"{s.Ball}{(s.Language.Length > 0 ? " · " + s.Language : "")}", x, y, 15, p.Muted, maxW: 382);

        // legality
        c.Fill(16, 246, 608, 2, p.Rule);
        float bw = Badge(c, v, 16, 256);
        string verdictText = v is null ? "PKHeX is checking this Pokémon..." : v.Valid ? "Passes PKHeX's legality checks." : v.Headline;
        c.Text(verdictText, 16 + bw + 12, 259, 15, v is { Valid: false } ? p.Bad : p.Muted, maxW: 608 - bw - 12 - 90);
        c.Text("X: report", 624, 259, 14, p.Muted, Align.Right);

        // moves
        for (int i = 0; i < 4; i++)
        {
            float mx = 16 + (i % 2) * 308, my = 292 + (i / 2) * 44;
            c.Box(mx, my, 300, 38, p.Chip, p.Rule, 2);
            var m = i < s.Moves.Length ? s.Moves[i] : "—";
            c.Text(m, mx + 12, my + 9, 17, i < s.Moves.Length ? p.Ink : p.Muted, maxW: 276);
        }

        // stats
        if (!s.IsEgg)
            StatTable(c, s, pk.Format, 16, 384);
    }

    private static void StatTable(Canvas c, MonSummary s, byte format, float x, float y)
    {
        var p = c.P;
        string[] heads = ["HP", "Atk", "Def", "SpA", "SpD", "Spe"];
        float labelW = 70, colW = (608 - labelW) / 6;
        for (int i = 0; i < 6; i++)
            c.Text(heads[i], x + labelW + colW * i + colW / 2, y, 14, p.Muted, Align.Center, bold: true);
        string ivLabel = format <= 2 ? "DV" : "IV", evLabel = format <= 2 ? "Stat Exp" : "EV";
        var rows = new (string Label, int[] Values)[] { (ivLabel, s.IVs), (evLabel, s.EVs), ("Stat", s.Stats) };
        for (int r = 0; r < rows.Length; r++)
        {
            float ry = y + 20 + r * 24;
            if (r % 2 == 0)
                c.Fill(x, ry - 2, 608, 23, p.Chip);
            c.Text(rows[r].Label, x + 8, ry + 1, 14, p.Muted);
            for (int i = 0; i < 6; i++)
            {
                int val = rows[r].Values[i];
                var col = r == 0 && ((format > 2 && val == 31) || (format <= 2 && val == 15)) ? p.Good : p.Ink;
                c.Text(val.ToString(), x + labelW + colW * i + colW / 2, ry + 1, 15, col, Align.Center);
            }
        }
    }

    /// <summary>A Pokémon put up for trade: one of the two cards on the trade's top screen.</summary>
    public static void OfferCard(Canvas c, float x, float y, float w, float h, string heading, Trade.TradeOffer? o,
        LegalityVerdict? v, bool accepted, string? partnerCheck, bool partnerValid)
    {
        var p = c.P;
        c.Panel(x, y, w, h, accepted, 36);
        c.Text(heading, x + 14, y + 9, 16, p.Ink, bold: true, maxW: w - 28 - (accepted ? 90 : 0));
        if (accepted)
        {
            float tw = c.Measure("Accepted", 13, true) + 10;
            c.Tag("Accepted", x + w - 14 - tw, y + 9, p.Good, 13, filled: true);
        }
        if (o is null)
        {
            c.Text("Nothing offered yet", x + w / 2, y + h / 2 - 10, 17, p.Muted, Align.Center);
            return;
        }
        var s = o.Summary;
        c.Mon(o.Pk, x + 10, y + 44, w - 20, 120, maxScale: 2);
        float ty = y + 170;
        float tw2 = c.Text(s.Title, x + 14, ty, 21, p.Ink, bold: true, maxW: w - 60);
        if (!s.IsEgg)
            c.GenderIcon(s.Gender, x + 14 + tw2 + 8, ty + 6, 2);
        if (s.IsShiny)
            c.Icon(Icons.Star, x + w - 34, ty + 4, 2.5f, Color.Hex(0xffc93c));
        ty += 30;
        c.Text($"{(s.IsNicknamed ? s.SpeciesName + "  " : "")}Lv {s.Level}", x + 14, ty, 16, p.Ink, maxW: w - 80);
        c.Tag(s.Format, x + w - 14 - c.Measure(s.Format, 13, true) - 10, ty, p.Accent);
        ty += 26;
        c.Text($"OT {s.OT} ({s.TrainerId})", x + 14, ty, 15, p.Muted, maxW: w - 28);
        ty += 21;
        c.Text(s.Origin, x + 14, ty, 15, p.Muted, maxW: w - 28);
        ty += 21;
        if (s.Item.Length > 0)
        {
            c.Text($"Holds {s.Item}", x + 14, ty, 15, p.Muted, maxW: w - 28);
        }
        ty += 26;
        Badge(c, v, x + 14, ty, 14);
        ty += 30;
        if (v is { Valid: false })
            c.Paragraph(v.Headline, x + 14, ty, w - 28, 13, p.Bad, maxLines: 2, lineH: 17);
        else if (partnerCheck is not null)
            c.Text(partnerCheck, x + 14, ty, 13, partnerValid ? p.Muted : p.Bad, maxW: w - 28);
    }

    /// <summary>A save's details (the save picker's and the box screen's top screen).</summary>
    public static void SaveDetails(Canvas c, App app, SaveEntry e, IReadOnlyList<PKM>? party)
    {
        var p = c.P;
        c.Panel(16, 52, 608, 416, false, 0);
        GameStripe(c, e.Version, 16, 52, 608);
        c.Text(e.GameName, 36, 72, 30, p.Ink, bold: true, maxW: 570);
        c.Text(e.FileTitle, 36, 112, 15, p.Muted, maxW: 570);
        float y = 150;
        Row("Trainer", $"{e.Trainer}   (ID {e.TrainerId})");
        Row("Play time", e.PlayTime);
        Row("In boxes", $"{e.Pokemon} Pokémon in {e.BoxCount} boxes");
        Row("Generation", $"{e.Generation}");
        Row("Saved", e.Modified.ToLocalTime().ToString("yyyy-MM-dd  HH:mm"));
        Row("File", Path.GetFileName(e.Path) + "  in " + e.Folder);
        if (party is { Count: > 0 })
        {
            c.Text("Party", 36, y + 8, 16, p.Muted);
            for (int i = 0; i < party.Count; i++)
                c.Mon(party[i], 130 + i * 80, y, 76, 64, maxScale: 1);
        }

        void Row(string label, string value)
        {
            c.Text(label, 36, y, 16, p.Muted);
            c.Text(value, 160, y, 17, p.Ink, maxW: 450);
            y += 34;
        }
    }

    /// <summary>The game's colour, as a stripe (HeartGold gold, SoulSilver silver...).</summary>
    public static void GameStripe(Canvas c, GameVersion v, float x, float y, float w)
    {
        c.Fill(x + 2, y + 2, w - 4, 8, GameColor(v));
    }

    public static Color GameColor(GameVersion v) => v switch
    {
        GameVersion.RD or GameVersion.RBY or GameVersion.R or GameVersion.FR or GameVersion.OR => Color.Hex(0xd9443b),
        GameVersion.BU or GameVersion.S or GameVersion.AS => Color.Hex(0x3b6fd9),
        GameVersion.GN or GameVersion.LG or GameVersion.E => Color.Hex(0x3fae5a),
        GameVersion.YW => Color.Hex(0xf2c230),
        GameVersion.GD or GameVersion.GS or GameVersion.HG or GameVersion.HGSS => Color.Hex(0xd9a520),
        GameVersion.SI or GameVersion.SS => Color.Hex(0xa9b4c2),
        GameVersion.C => Color.Hex(0x5ad1e0),
        GameVersion.D or GameVersion.DP => Color.Hex(0x6f8fe8),
        GameVersion.P => Color.Hex(0xe88fb8),
        GameVersion.Pt or GameVersion.DPPt => Color.Hex(0x8a8f99),
        GameVersion.B or GameVersion.BW or GameVersion.B2 or GameVersion.B2W2 => Color.Hex(0x3a3f47),
        GameVersion.W or GameVersion.W2 => Color.Hex(0xe8e8e8),
        _ => Color.Hex(0x7d8b9c),
    };
}
