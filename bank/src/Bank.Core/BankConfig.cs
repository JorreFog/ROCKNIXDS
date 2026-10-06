using System.Text.Json;
using System.Text.Json.Serialization;

namespace Rocknixds.Bank;

/// <summary>
/// The app's settings: <c>/storage/.config/rocknixds/bank.json</c> on the handheld, <c>~/.config/rocknixds-bank/bank.json</c>
/// elsewhere. Every field has a default, so a missing or partial file works.
/// </summary>
public sealed class BankConfig
{
    /// <summary>Folders searched for game saves (DraStic .dsv, .sav, .srm and the other formats PKHeX reads).</summary>
    public List<string> SaveFolders { get; set; } = [];

    /// <summary>The bank, save backups, the import/export folders and the history log. On the handheld it is in the
    /// roms share, so it can be copied off over the network like the games.</summary>
    public string DataFolder { get; set; } = "";

    /// <summary>Boxes in the bank, 30 Pokémon each.</summary>
    public int BankBoxes { get; set; } = 40;

    /// <summary>TCP port a hosted trade listens on. The LAN announcement uses the next port (UDP).</summary>
    public int TradePort { get; set; } = 47900;

    /// <summary>Let handhelds outside the local network join a trade room (a public IPv6 address, a forwarded port).
    /// Off: only private, link-local and VPN (100.64/10) addresses may connect.</summary>
    public bool TradeAllowAnyAddress { get; set; }

    /// <summary>The name trade partners see. Empty: the handheld's host name.</summary>
    public string TrainerName { get; set; } = "";

    /// <summary>Language of species, move and item names (PKHeX's: en, ja, fr, it, de, es, ko, zh-Hans, zh-Hant).</summary>
    public string Language { get; set; } = "en";

    /// <summary>Refuse to put a Pokémon that fails the legality check into a game. Off: the move asks first.</summary>
    public bool BlockIllegalIntoSaves { get; set; }

    /// <summary>Refuse a trade partner's Pokémon that fails the legality check. Off: accepting it asks first.</summary>
    public bool BlockIllegalTrades { get; set; }

    /// <summary>Trade evolutions (Kadabra, Haunter, Onix with a Metal Coat, Karrablast for Shelmet...) when a trade arrives.</summary>
    public bool TradeEvolutions { get; set; } = true;

    /// <summary>Backups kept per save file. One is made the first time a session changes that save.</summary>
    public int BackupsPerSave { get; set; } = 10;

    /// <summary>auto (follows the ROCKNIXDS Pixel theme), dark or light.</summary>
    public string Palette { get; set; } = "auto";

    /// <summary>The version of the terms of use (LEGAL.md) accepted on this handheld, and when; 0: not yet.</summary>
    public int TermsAccepted { get; set; }
    public string TermsAcceptedAt { get; set; } = "";

    /// <summary>The save that was open last; it opens again at the next start.</summary>
    public string LastSave { get; set; } = "";

    /// <summary>The address last typed to join a trade room.</summary>
    public string LastTradeAddress { get; set; } = "";

    /// <summary>Swap the A and B buttons (for pads labelled the other way round).</summary>
    public bool SwapAB { get; set; }

    /// <summary>The pad's evdev name. The RG DS's is retrogame_joypad; empty: SDL's gamepads only.</summary>
    public string PadDevice { get; set; } = "retrogame_joypad";

    /// <summary>Folder names never searched for saves (case-insensitive).</summary>
    public List<string> SkipFolders { get; set; } =
        ["images", "videos", "manuals", "savestates", "downloaded_images", "bios", "screenshots", "thumbnails", "media", "backup-bank"];

    public static bool OnHandheld => Directory.Exists("/storage/roms") && Directory.Exists("/storage/.config");

    public static string DefaultConfigPath => OnHandheld
        ? "/storage/.config/rocknixds/bank.json"
        : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".config", "rocknixds-bank", "bank.json");

    [JsonIgnore]
    public string ConfigPath { get; private set; } = DefaultConfigPath;

    [JsonIgnore]
    public string BankFolder => Path.Combine(DataFolder, "bank");
    [JsonIgnore]
    public string BackupFolder => Path.Combine(DataFolder, "backups");
    [JsonIgnore]
    public string ImportFolder => Path.Combine(DataFolder, "import");
    [JsonIgnore]
    public string ExportFolder => Path.Combine(DataFolder, "export");
    /// <summary>This handheld's identity key for trading: next to the settings, out of the shared roms folder.</summary>
    [JsonIgnore]
    public string IdentityPath => Path.Combine(Path.GetDirectoryName(Path.GetFullPath(ConfigPath))!, "bank-identity.key");

    /// <summary>The handhelds traded with before (trust on first use).</summary>
    [JsonIgnore]
    public string TrainersPath => Path.Combine(DataFolder, "trainers.json");

    [JsonIgnore]
    public string HistoryPath => Path.Combine(DataFolder, "history.log");

    [JsonIgnore]
    public string EffectiveTrainerName
    {
        get
        {
            if (!string.IsNullOrWhiteSpace(TrainerName))
                return TrainerName.Trim();
            try { return Environment.MachineName; } catch { return "ROCKNIXDS"; }
        }
    }

    /// <summary>Loads the settings, filling the defaults in. A broken file is kept aside and the defaults are used.</summary>
    public static BankConfig Load(string? path = null)
    {
        path ??= Environment.GetEnvironmentVariable("ROCKNIXDS_BANK_CONFIG") ?? DefaultConfigPath;
        BankConfig cfg;
        try
        {
            cfg = File.Exists(path)
                ? JsonSerializer.Deserialize(File.ReadAllText(path), BankJson.Default.BankConfig) ?? new BankConfig()
                : new BankConfig();
        }
        catch (Exception)
        {
            try { File.Move(path, path + ".broken", true); } catch { /* keep going with the defaults */ }
            cfg = new BankConfig();
        }
        cfg.ConfigPath = path;
        cfg.FillDefaults();
        return cfg;
    }

    public void FillDefaults()
    {
        if (SaveFolders.Count == 0)
        {
            SaveFolders = OnHandheld
                ? ["/storage/roms"]
                : [Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "roms")];
        }
        if (string.IsNullOrWhiteSpace(DataFolder))
        {
            DataFolder = OnHandheld
                ? "/storage/roms/rocknixds-bank"
                : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".local", "share", "rocknixds-bank");
        }
        BankBoxes = Math.Clamp(BankBoxes, 1, 999);
        BackupsPerSave = Math.Clamp(BackupsPerSave, 1, 1000);
        if (TradePort is < 1024 or > 65534)
            TradePort = 47900;
    }

    public void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(ConfigPath)!);
        FileUtil.WriteAtomic(ConfigPath, System.Text.Encoding.UTF8.GetBytes(JsonSerializer.Serialize(this, ReadableJson.Context.BankConfig)));
    }

    /// <summary>Uses <paramref name="path"/> for <see cref="Save"/>; the tests keep their settings in a temp folder.</summary>
    public BankConfig WithPath(string path)
    {
        ConfigPath = path;
        return this;
    }
}

internal static class ReadableJson
{
    /// <summary>The settings file is meant to be edited by hand too: "Jorre's RG DS", not "Jorre\u0027s RG DS".</summary>
    public static readonly BankJson Context = new(new JsonSerializerOptions(BankJson.Default.Options)
    {
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    });
}

[JsonSourceGenerationOptions(WriteIndented = true, PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.Never, ReadCommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true)]
[JsonSerializable(typeof(BankConfig))]
[JsonSerializable(typeof(BankBoxNames))]
internal sealed partial class BankJson : JsonSerializerContext;

internal sealed class BankBoxNames
{
    public List<string> Names { get; set; } = [];
}
