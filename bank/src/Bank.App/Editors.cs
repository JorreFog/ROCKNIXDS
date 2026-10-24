using PKHeX.Core;

namespace Rocknixds.Bank.App;

/// <summary>Small helpers the editors share.</summary>
public static class Ask
{
    public static void Number(App app, string title, long current, long min, long max, Action<long> done) =>
        app.Push(new KeyboardScreen(app, $"{title} ({min}-{max})", KeyboardScreen.Kind.Number, current.ToString(), max.ToString().Length, t =>
        {
            if (long.TryParse(t, out var v))
                done(Math.Clamp(v, min, max));
        }));

    public static void Text(App app, string title, string current, int max, Action<string> done) =>
        app.Push(new KeyboardScreen(app, title, KeyboardScreen.Kind.Text, current, max, done));

    public static int Cycle(int value, int step, int count) => count <= 0 ? 0 : ((value + step) % count + count) % count;

    public static string[] StatNames(byte format) => format <= 2 ? ["HP", "Attack", "Defense", "Special", "Special", "Speed"] : ["HP", "Attack", "Defense", "Sp. Atk", "Sp. Def", "Speed"];
}

/// <summary>
/// Edits one Pokémon (or a new one for an empty slot) on a copy: every change shows on the top screen at once, with
/// PKHeX's verdict, and nothing is written until Save. A save is then written as every move is: after the session's
/// backup of the file, and a Pokémon that fails the legality check goes into a game only as the settings allow.
/// </summary>
public sealed class MonEditorScreen : FieldScreen
{
    private readonly Slot _slot;
    private readonly bool _isNew;
    private readonly string _startHash;
    private PKM _pk;
    private bool _report;
    private int _reportScroll;
    private string? _note;

    public MonEditorScreen(App app, Slot slot, PKM pk, bool isNew, string? note = null) : base(app)
    {
        _slot = slot;
        _isNew = isNew;
        _pk = pk.Clone();
        _startHash = PkmIO.Hash(_pk);
        _note = note;
    }

    private bool Changed => _isNew || PkmIO.Hash(_pk) != _startHash;

    private FilteredGameDataSource Src => EditSources.For(App, _pk);

    private static string NameOf(IReadOnlyList<ComboItem> list, int value) => list.FirstOrDefault(i => i.Value == value)?.Text ?? value.ToString();

    private void Pick(string title, IReadOnlyList<ComboItem> list, int current, Action<int> set, bool withNone = false, Action<Canvas, int>? preview = null) =>
        App.Push(new PickerScreen(App, title, EditSources.List(list, withNone), current, set, preview));

    private List<Field>? _fields;
    private PKM? _fieldsPk;
    private int _fieldsKey;

    // the list's shape follows the species (its forms, abilities, genders): built again when that changes
    protected override IReadOnlyList<Field> Fields
    {
        get
        {
            int key = _pk.Species | (_pk.Form << 16);
            if (_fields is null || !ReferenceEquals(_fieldsPk, _pk) || _fieldsKey != key)
            {
                _fields = Build();
                _fieldsPk = _pk;
                _fieldsKey = key;
            }
            return _fields;
        }
    }

    private List<Field> Build()
    {
        {
            var pk = _pk;
            byte fmt = pk.Format;
            var f = new List<Field> { Field.Head(_isNew ? "New Pokémon" : "Pokémon") };
            f.Add(new("Species", () => Names.Species(pk.Species), () => Pick("Species", Src.Species, pk.Species, s => MonEdit.SetSpecies(pk, (ushort)s),
                preview: (c, s) => SpeciesPreview(c, (ushort)s))));
            var forms = FormConverter.GetFormList(pk.Species, GameInfo.Strings.Types, GameInfo.Strings.forms, GameInfo.GenderSymbolASCII, pk.Context);
            if (forms.Length > 1)
            {
                f.Add(new("Form", () => pk.Form < forms.Length ? forms[pk.Form] : pk.Form.ToString(), null, d =>
                {
                    var form = (byte)Ask.Cycle(pk.Form, Math.Sign(d), forms.Length);
                    if (pk is PK3 && pk.Species == (ushort)Species.Unown)
                        pk.SetPIDUnown3(form);
                    pk.Form = form;
                }));
            }
            f.Add(new("Nickname", () => pk.IsNicknamed ? pk.Nickname : $"{pk.Nickname} (not nicknamed)", () => Ask.Text(App, "Nickname", pk.Nickname, pk.MaxStringLengthNickname,
                n => pk.SetNickname(n)), _ => pk.ClearNickname()));
            f.Add(new("Level", () => pk.CurrentLevel.ToString(), () => Ask.Number(App, "Level", pk.CurrentLevel, 1, 100, v => MonEdit.SetLevel(pk, (int)v)),
                d => MonEdit.SetLevel(pk, pk.CurrentLevel + d)));
            f.Add(new("Experience", () => pk.EXP.ToString("N0"), () => Ask.Number(App, "Experience", pk.EXP, 0,
                Experience.GetEXP(100, pk.PersonalInfo.EXPGrowth), v => pk.EXP = (uint)v)));
            if (fmt >= 3)
            {
                f.Add(new("Nature", () => GameInfo.Strings.Natures[(int)pk.Nature], () => Pick("Nature", Src.Natures, (int)pk.Nature, n => CommonEdits.SetNature(pk, (Nature)n), withNone: true),
                    d => CommonEdits.SetNature(pk, (Nature)Ask.Cycle((int)pk.Nature, Math.Sign(d), 25))));
                var abilities = MonEdit.Abilities(pk);
                f.Add(new("Ability", () => GameInfo.Strings.Ability[pk.Ability] + (MonEdit.AbilityIndex(pk) == 2 ? " (hidden)" : ""), null, abilities.Count < 2 ? null : d =>
                {
                    int at = abilities.ToList().FindIndex(a => a.Index == MonEdit.AbilityIndex(pk));
                    MonEdit.SetAbility(pk, abilities[Ask.Cycle(Math.Max(0, at), Math.Sign(d), abilities.Count)].Index);
                }));
            }
            if (fmt >= 2)
                f.Add(new("Held item", () => pk.HeldItem == 0 ? "None" : NameOf(Src.Items, pk.HeldItem), () => Pick("Held item", Src.Items, pk.HeldItem, i => pk.HeldItem = i, withNone: true)));
            if (fmt >= 3)
            {
                var genders = MonEdit.Genders(pk);
                f.Add(new("Gender", () => pk.Gender switch { 0 => "Male", 1 => "Female", _ => "Genderless" }, null, genders.Count < 2 ? null : d =>
                    MonEdit.SetGender(pk, genders[Ask.Cycle(genders.ToList().IndexOf(pk.Gender), Math.Sign(d), genders.Count)])));
            }
            f.Add(new("Shiny", () => pk.IsShiny ? "Yes" : "No", () => CommonEdits.SetIsShiny(pk, !pk.IsShiny)));
            if (fmt >= 3)
                f.Add(new("Ball", () => NameOf(Src.Balls, pk.Ball), () => Pick("Ball", Src.Balls, pk.Ball, b => pk.Ball = (byte)b)));
            if (fmt >= 2)
            {
                f.Add(new("Friendship", () => pk.CurrentFriendship.ToString(), () => Ask.Number(App, "Friendship", pk.CurrentFriendship, 0, 255, v => pk.CurrentFriendship = (byte)v),
                    d => pk.CurrentFriendship = (byte)Math.Clamp(pk.CurrentFriendship + d, 0, 255)));
                f.Add(new("Pokérus", () => MonEdit.PokerusState(pk) switch { 0 => "Never had it", 1 => "Infected", _ => "Cured" }, null,
                    d => MonEdit.SetPokerus(pk, Ask.Cycle(MonEdit.PokerusState(pk), Math.Sign(d), 3))));
            }

            f.Add(Field.Head("Moves (left/right: PP Ups)"));
            for (int i = 0; i < 4; i++)
            {
                int m = i;
                f.Add(new($"Move {m + 1}", () =>
                {
                    var move = MonEdit.GetMove(pk, m);
                    if (move == 0)
                        return "—";
                    int ups = MonEdit.GetPPUps(pk, m);
                    return $"{Names.Move(move)}  PP {pk.GetMovePP(move, ups)}{(ups > 0 ? $" (+{ups})" : "")}";
                }, () => Pick($"Move {m + 1}", Src.Moves, MonEdit.GetMove(pk, m), v => MonEdit.SetMove(pk, m, (ushort)v), withNone: true),
                    d => MonEdit.SetPPUps(pk, m, MonEdit.GetPPUps(pk, m) + Math.Sign(d))));
            }

            f.Add(Field.Head("Original trainer"));
            f.Add(new("Name", () => pk.OriginalTrainerName, () => Ask.Text(App, "Original trainer", pk.OriginalTrainerName, pk.MaxStringLengthTrainer, n => pk.OriginalTrainerName = n)));
            if (fmt >= 3)
                f.Add(new("Gender", () => pk.OriginalTrainerGender == 0 ? "Male" : "Female", () => pk.OriginalTrainerGender = (byte)(pk.OriginalTrainerGender ^ 1)));
            f.Add(new("ID", () => pk.TID16.ToString("00000"), () => Ask.Number(App, "Trainer ID", pk.TID16, 0, 65535, v => pk.TID16 = (ushort)v)));
            if (fmt >= 3)
                f.Add(new("Secret ID", () => pk.SID16.ToString("00000"), () => Ask.Number(App, "Secret ID", pk.SID16, 0, 65535, v => pk.SID16 = (ushort)v)));

            var stats = Ask.StatNames(fmt);
            int statCount = fmt <= 2 ? 5 : 6; // Gen 1-2: one Special
            int[] order = fmt <= 2 ? [0, 1, 2, 3, 5] : [0, 1, 2, 3, 4, 5];
            f.Add(Field.Head(fmt <= 2 ? $"DVs (0-{pk.MaxIV})" : $"IVs (0-{pk.MaxIV})"));
            for (int k = 0; k < statCount; k++)
            {
                int s = order[k];
                f.Add(new(stats[s], () => MonEdit.GetIV(pk, s).ToString(), () => Ask.Number(App, stats[s], MonEdit.GetIV(pk, s), 0, pk.MaxIV, v => MonEdit.SetIV(pk, s, (int)v)),
                    d => MonEdit.SetIV(pk, s, MonEdit.GetIV(pk, s) + d)));
            }
            f.Add(Field.Head(fmt <= 2 ? $"Stat experience (0-{pk.MaxEV})" : $"EVs (0-{pk.MaxEV} each)", value: fmt <= 2 ? null : () => $"{pk.EVTotal} of 510"));
            for (int k = 0; k < statCount; k++)
            {
                int s = order[k];
                f.Add(new(stats[s], () => MonEdit.GetEV(pk, s).ToString(), () => Ask.Number(App, stats[s], MonEdit.GetEV(pk, s), 0, pk.MaxEV, v => MonEdit.SetEV(pk, s, (int)v)),
                    d => MonEdit.SetEV(pk, s, MonEdit.GetEV(pk, s) + d * (fmt <= 2 ? 256 : 4))));
            }

            f.Add(Field.Head("Quick changes"));
            f.Add(new("Try to make it legal", () => "", MakeLegal));
            f.Add(new("All IVs to the most", () => "", () => MonEdit.MaxIVs(pk)));
            f.Add(new("Legal moves for its level", () => "", () => { pk.SetMoveset(false); pk.HealPP(); }));
            if (fmt >= 3)
                f.Add(new("Every ribbon it can have", () => "", () => RibbonApplicator.SetAllValidRibbons(pk)));
            if (fmt >= 3)
                f.Add(new("No ribbons", () => "", () => RibbonApplicator.RemoveAllValidRibbons(pk)));
            f.Add(new("Most friendship", () => "", () => pk.CurrentFriendship = 255));
            return f;
        }
    }

    private void SpeciesPreview(Canvas c, ushort species)
    {
        var p = c.P;
        c.Panel(170, 70, 300, 230, false);
        c.Mon(species, 0, 2, false, false, 180, 80, 280, 200, maxScale: 3);
        c.Text($"#{species:000}  {Names.Species(species)}", 320, 316, 26, p.Ink, Align.Center, bold: true, maxW: 600);
    }

    private void MakeLegal()
    {
        var copy = _pk.Clone();
        App.Run("Asking PKHeX for legal values...", () =>
        {
            var changed = MonEdit.TryMakeLegal(copy);
            return (changed, Legality.Check(copy));
        }, r =>
        {
            _pk = copy;
            var (changed, v) = r;
            var what = changed.Count == 0 ? "Nothing PKHeX can suggest was changed." : "Changed: " + string.Join(", ", changed) + ".";
            App.Message(v.Valid ? "Legal now" : "Still not legal", v.Valid ? what : $"{what}\n\nWhat's left: {v.Headline}");
        });
    }

    protected override IReadOnlyList<DialogChoice> Actions() =>
    [
        new("Try to make it legal", MakeLegal),
        new(_report ? "Hide the legality report" : "Legality report (X)", () => { _report = !_report; _reportScroll = 0; }),
        new("Undo every change", () => _pk = App.Mover.Read(_slot)?.Clone() ?? _pk, !_isNew),
    ];

    protected override bool Extra(InputEvent e)
    {
        if (e.Pressed(Btn.X))
        {
            _report = !_report;
            _reportScroll = 0;
            return true;
        }
        if (_report && (e.Is(Btn.L2) || e.Is(Btn.R2)))
        {
            _reportScroll = Math.Max(0, _reportScroll + (e.Btn == Btn.L2 ? -3 : 3));
            return true;
        }
        return false;
    }

    protected override void Save()
    {
        var pk = _pk.Clone();
        pk.ResetPartyStats(); // stats for its level, IVs and EVs (a party Pokémon carries them), full HP
        pk.RefreshChecksum();
        App.Run("Checking it...", () => Legality.Check(pk), v =>
        {
            if (_slot.InGame && v is { Analysed: true, Valid: false })
            {
                if (App.Cfg.BlockIllegalIntoSaves)
                {
                    App.Message("Not saved", $"It fails the legality check ({v.Headline}), and putting such Pokémon into games is switched off in the settings. Try \"Try to make it legal\", or keep it in the bank.");
                    return;
                }
                App.Confirm("This Pokémon isn't legal", $"{v.Headline}\n\nSave it into the game anyway?", "Save anyway", () => Write(pk));
                return;
            }
            Write(pk);
        });
    }

    private void Write(PKM pk)
    {
        App.Run("Saving...", () =>
        {
            App.Mover.Replace(_slot, pk, _isNew ? "CREATE" : "EDIT");
            return App.Mover.Where(_slot);
        }, where =>
        {
            App.Pop();
            App.Find<BoxScreen>()?.Refresh();
            App.ShowToast($"{MonSummary.From(pk).Title} saved{(_slot.InGame ? " to the card" : " in the bank")}.");
        }, ex => App.Message("Not saved", ex.Message));
    }

    protected override void Cancel()
    {
        if (_report)
        {
            _report = false;
            return;
        }
        if (Changed)
            App.Confirm(_isNew ? "Leave without it?" : "Throw the changes away?", _isNew ? "The new Pokémon isn't saved." : "The Pokémon stays as it was.", _isNew ? "Leave" : "Throw away", App.Pop, "Keep editing");
        else
            App.Pop();
    }

    public override bool Animating => App.PendingChecks > 0;

    public override void DrawTop(Canvas c)
    {
        var label = Current?.Label ?? "";
        Views.Header(c, App, _isNew ? "New Pokémon" : "Edit Pokémon", label);
        var v = App.VerdictFor(_pk);
        if (_report)
        {
            _reportScroll = Views.Report(c, _pk, v, _reportScroll, "L2/R2 scroll · X closes");
            return;
        }
        Views.MonDetails(c, App, _pk, v, _note ?? App.Mover.Where(_slot));
    }
}

/// <summary>The trainer: name, IDs, money, play time, badges. Written to the save with Save.</summary>
public sealed class TrainerScreen : FieldScreen
{
    private readonly SaveFile _sav;
    private string _ot;
    private byte _gender;
    private ushort _tid, _sid;
    private uint _money;
    private int _hours, _minutes, _badges;
    private readonly int _badgeCount;

    public TrainerScreen(App app) : base(app)
    {
        _sav = app.Mover.Save!.Sav;
        _ot = _sav.OT;
        _gender = _sav.Gender;
        _tid = _sav.TID16;
        _sid = _sav.SID16;
        _money = _sav.Money;
        _hours = _sav.PlayedHours;
        _minutes = _sav.PlayedMinutes;
        _badgeCount = TrainerEdit.BadgeCount(_sav);
        _badges = TrainerEdit.GetBadges(_sav);
    }

    protected override IReadOnlyList<Field> Fields
    {
        get
        {
            var f = new List<Field>
            {
                Field.Head("Trainer"),
                new("Name", () => _ot, () => Ask.Text(App, "Trainer name", _ot, _sav.MaxStringLengthTrainer, n => _ot = n)),
            };
            if (_sav.Generation >= 3)
                f.Add(new("Gender", () => _gender == 0 ? "Boy" : "Girl", () => _gender = (byte)(_gender ^ 1)));
            f.Add(new("Trainer ID", () => _tid.ToString("00000"), () => Ask.Number(App, "Trainer ID", _tid, 0, 65535, v => _tid = (ushort)v)));
            if (_sav.Generation >= 3)
                f.Add(new("Secret ID", () => _sid.ToString("00000"), () => Ask.Number(App, "Secret ID", _sid, 0, 65535, v => _sid = (ushort)v)));
            f.Add(new("Money", () => _money.ToString("N0"), () => Ask.Number(App, "Money", _money, 0, _sav.MaxMoney, v => _money = (uint)v),
                d => _money = (uint)Math.Clamp(_money + d * 1000L, 0, _sav.MaxMoney)));
            f.Add(new("Play time, hours", () => _hours.ToString(), () => Ask.Number(App, "Hours", _hours, 0, 999, v => _hours = (int)v), d => _hours = Math.Clamp(_hours + d, 0, 999)));
            f.Add(new("Play time, minutes", () => _minutes.ToString("00"), () => Ask.Number(App, "Minutes", _minutes, 0, 59, v => _minutes = (int)v), d => _minutes = Ask.Cycle(_minutes, d, 60)));
            if (_badgeCount > 0)
            {
                f.Add(Field.Head($"Badges ({System.Numerics.BitOperations.PopCount((uint)_badges)} of {_badgeCount})"));
                f.Add(new("All badges", () => "", () => _badges = (1 << _badgeCount) - 1));
                for (int i = 0; i < _badgeCount; i++)
                {
                    int b = i;
                    f.Add(new(TrainerEdit.BadgeName(_sav, b), () => (_badges & (1 << b)) != 0 ? "Yes" : "No", () => _badges ^= 1 << b));
                }
            }
            return f;
        }
    }

    protected override void Save()
    {
        App.Run("Saving...", () =>
        {
            App.Mover.EditSave("trainer", s =>
            {
                s.OT = _ot;
                if (s.Generation >= 3)
                {
                    s.Gender = _gender;
                    s.SID16 = _sid;
                }
                s.TID16 = _tid;
                s.Money = _money;
                s.PlayedHours = _hours;
                s.PlayedMinutes = _minutes;
                TrainerEdit.SetBadges(s, _badges);
            });
            return 0;
        }, _ =>
        {
            App.Pop();
            App.ShowToast("The trainer's details are saved.");
        }, ex => App.Message("Not saved", ex.Message));
    }

    protected override void Cancel() => App.Pop();

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, "Trainer", App.Mover.Save!.Entry.GameName);
        var p = c.P;
        c.Panel(16, 52, 608, 416, false, 0);
        Views.GameStripe(c, _sav.Version, 16, 52, 608);
        c.Text(_ot, 36, 76, 34, p.Ink, bold: true, maxW: 560);
        float y = 130;
        Row("ID", _sav.Generation >= 3 ? $"{_tid:00000} / {_sid:00000}" : $"{_tid:00000}");
        Row("Money", _money.ToString("N0"));
        Row("Play time", $"{_hours}:{_minutes:00}");
        if (_badgeCount > 0)
        {
            c.Text("Badges", 36, y, 17, p.Muted);
            for (int i = 0; i < _badgeCount; i++)
            {
                float bx = 170 + (i % 8) * 50, by = y - 4 + (i / 8) * 40;
                bool has = (_badges & (1 << i)) != 0;
                c.Box(bx, by, 34, 30, has ? p.Accent : p.Chip, has ? p.Edge : p.Rule, 2);
                c.Text((i % 8 + 1).ToString(), bx + 17, by + 6, 15, has ? p.Bg : p.Muted, Align.Center, bold: true);
            }
        }
        c.Paragraph("Changing the trainer ID makes the game treat its Pokémon as traded ones: they may stop obeying above a level.",
            36, 380, 568, 14, p.Muted);

        void Row(string label, string value)
        {
            c.Text(label, 36, y, 17, p.Muted);
            c.Text(value, 170, y, 19, p.Ink, maxW: 430);
            y += 40;
        }
    }
}

/// <summary>The bag, a pocket at a time: counts, items, every item. Written to the save with Save.</summary>
public sealed class BagScreen : FieldScreen
{
    private readonly SaveFile _sav;
    private readonly PlayerBag _bag;
    private readonly string[] _names;
    private int _pouch;

    public BagScreen(App app) : base(app)
    {
        _sav = app.Mover.Save!.Sav;
        _bag = _sav.Inventory;
        _names = GameInfo.Strings.GetItemStrings(_sav.Context, _sav.Version);
    }

    private InventoryPouch Pouch => _bag.Pouches[_pouch];

    private string ItemName(int id) => id < _names.Length && _names[id].Length > 0 ? _names[id] : $"Item {id}";

    private static string PouchName(InventoryType t) => t switch
    {
        InventoryType.Items => "Items",
        InventoryType.KeyItems => "Key items",
        InventoryType.TMHMs => "TMs and HMs",
        InventoryType.Medicine => "Medicine",
        InventoryType.Berries => "Berries",
        InventoryType.Balls => "Poké Balls",
        InventoryType.BattleItems => "Battle items",
        InventoryType.MailItems => "Mail",
        InventoryType.PCItems => "PC",
        _ => t.ToString(),
    };

    private int Max(int item) => Math.Max(1, _bag.GetMaxCount(Pouch.Type, item));

    protected override IReadOnlyList<Field> Fields
    {
        get
        {
            var pouch = Pouch;
            var held = pouch.Items.Where(i => i.Index != 0).ToList();
            var f = new List<Field>
            {
                Field.Head($"{PouchName(pouch.Type)}   ({_pouch + 1} of {_bag.Pouches.Count}: tap or L/R for the next)", () => Turn(1)),
            };
            foreach (var item in held)
            {
                var it = item;
                f.Add(new(ItemName(it.Index), () => $"× {it.Count}", () => ItemMenu(it), d => it.Count = Math.Clamp(it.Count + d, 1, Max(it.Index))));
            }
            if (pouch.FindIndexFirstEmptySlot() >= 0)
                f.Add(new("Add an item...", () => "", Add));
            return f;
        }
    }

    private void Turn(int d)
    {
        _pouch = Ask.Cycle(_pouch, d, _bag.Pouches.Count);
        Sel = 1;
        Scroll = 0;
    }

    private List<(int Value, string Text)> Choices(int except = 0)
    {
        var have = Pouch.Items.Where(i => i.Index != 0 && i.Index != except).Select(i => i.Index).ToHashSet();
        var list = new List<(int, string)>();
        foreach (var id in Pouch.GetAllItems())
        {
            if (id != 0 && !have.Contains(id))
                list.Add((id, ItemName(id)));
        }
        return list.OrderBy(i => i.Item2, StringComparer.OrdinalIgnoreCase).ToList();
    }

    private void Add() => App.Push(new PickerScreen(App, $"Add to {PouchName(Pouch.Type)}", Choices(), 0, id =>
    {
        int slot = Pouch.FindIndexFirstEmptySlot();
        if (slot < 0)
            return;
        Pouch.Items[slot] = Pouch.GetEmpty(id, 1);
    }));

    private void ItemMenu(InventoryItem it) => App.Menu(ItemName(it.Index),
    [
        new("How many...", () => Ask.Number(App, ItemName(it.Index), it.Count, 1, Max(it.Index), v => it.Count = (int)v)),
        new("As many as fit", () => it.Count = Max(it.Index)),
        new("Change the item...", () => App.Push(new PickerScreen(App, "Change to", Choices(it.Index), it.Index, id => it.Index = id))),
        new("Throw it away", () => { it.Clear(); Pouch.SortByEmpty(); }),
        new("Close", () => { }),
    ], 4);

    protected override IReadOnlyList<DialogChoice> Actions() =>
    [
        new("Next pocket (R)", () => Turn(1)),
        new("Previous pocket (L)", () => Turn(-1)),
        new("Every item in this pocket", () => Pouch.GiveAllItems(_bag, Pouch.Type == InventoryType.KeyItems ? 1 : Pouch.MaxCount)),
        new("Every count to the most", () => { foreach (var i in Pouch.Items.Where(i => i.Index != 0)) i.Count = Max(i.Index); }),
        new("Sort by name", () => Pouch.SortByName(_names, false)),
        new("Empty this pocket", () => App.Confirm("Empty the pocket?", $"Every item in {PouchName(Pouch.Type)} goes (when you save).", "Empty it", () => Pouch.RemoveAll())),
    ];

    protected override bool Extra(InputEvent e)
    {
        if (e.Is(Btn.L) || e.Is(Btn.R))
        {
            Turn(e.Btn == Btn.L ? -1 : 1);
            return true;
        }
        return false;
    }

    protected override void Save()
    {
        App.Run("Saving...", () =>
        {
            App.Mover.EditSave("bag", s => _bag.CopyTo(s));
            return 0;
        }, _ =>
        {
            App.Pop();
            App.ShowToast("The bag is saved.");
        }, ex => App.Message("Not saved", ex.Message));
    }

    protected override void DrawHints(Canvas c, float y)
    {
        float x = 8;
        x += c.Hint("A", "Item", x, y);
        x += c.Hint("←→", "Count", x, y);
        x += c.Hint("L/R", "Pocket", x, y);
        x += c.Hint("Y", "Actions", x, y);
        x += c.Hint("START", "Save", x, y);
        c.Hint("B", "Back", x, y);
    }

    protected override void Cancel() => App.Confirm("Leave the bag?", "Changes that weren't saved are lost.", "Leave", App.Pop, "Stay");

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, "Bag", App.Mover.Save!.Entry.GameName);
        var p = c.P;
        float x = 16;
        for (int i = 0; i < _bag.Pouches.Count; i++)
        {
            var name = PouchName(_bag.Pouches[i].Type);
            float w = c.Measure(name, 14, true) + 16;
            if (x + w > 624)
                break;
            c.Box(x, 56, w, 26, i == _pouch ? p.Accent : p.Chip, i == _pouch ? p.Edge : p.Rule, 1);
            c.Text(name, x + 8, 61, 14, i == _pouch ? p.Bg : p.Muted, bold: true);
            x += w + 6;
        }
        var pouch = Pouch;
        int used = pouch.Items.Count(i => i.Index != 0);
        c.Text(PouchName(pouch.Type), 32, 110, 30, p.Ink, bold: true);
        c.Text($"{used} of {pouch.Items.Length} spaces used · up to {pouch.MaxCount} of each", 32, 152, 17, p.Muted);
        if (Current is { IsHeader: false } f && Sel - 1 < used && Sel >= 1)
        {
            c.Text(f.Label, 32, 210, 26, p.Ink, bold: true, maxW: 576);
            c.Text(f.Value?.Invoke() ?? "", 32, 248, 20, p.Accent);
        }
        c.Paragraph("Left/right change a count, A opens an item, L/R turn the pocket. Nothing is written until Save.", 32, 400, 576, 15, p.Muted);
    }
}

/// <summary>The Pokédex: seen and caught, one species at a time or all at once. Written to the save with Save.</summary>
public sealed class DexScreen : FieldScreen
{
    private readonly SaveFile _sav;
    private readonly bool[] _seen, _caught, _seen0, _caught0;
    private readonly int _max;
    private List<Field>? _fields;

    public DexScreen(App app) : base(app)
    {
        _sav = app.Mover.Save!.Sav;
        _max = _sav.MaxSpeciesID;
        _seen = new bool[_max + 1];
        _caught = new bool[_max + 1];
        for (ushort s = 1; s <= _max; s++)
        {
            _seen[s] = DexEdit.Seen(_sav, s);
            _caught[s] = DexEdit.Caught(_sav, s);
        }
        _seen0 = (bool[])_seen.Clone();
        _caught0 = (bool[])_caught.Clone();
    }

    private int State(int s) => _caught[s] ? 2 : _seen[s] ? 1 : 0;

    private void SetState(int s, int state)
    {
        _seen[s] = state >= 1;
        _caught[s] = state == 2;
    }

    protected override IReadOnlyList<Field> Fields => _fields ??= Build();

    private List<Field> Build()
    {
        var f = new List<Field> { Field.Head($"Pokédex of {_max}: A or left/right change one") };
        for (int s = 1; s <= _max; s++)
        {
            int sp = s;
            f.Add(new($"{sp:000}  {Names.Species((ushort)sp)}", () => State(sp) switch { 2 => "Caught", 1 => "Seen", _ => "—" },
                () => SetState(sp, (State(sp) + 1) % 3), d => SetState(sp, Ask.Cycle(State(sp), Math.Sign(d), 3))));
        }
        return f;
    }

    protected override IReadOnlyList<DialogChoice> Actions() =>
    [
        new("Every species caught", () => { for (int s = 1; s <= _max; s++) SetState(s, 2); }),
        new("Every species seen", () => { for (int s = 1; s <= _max; s++) if (!_seen[s]) SetState(s, 1); }),
        new("Clear the Pokédex", () => App.Confirm("Clear the Pokédex?", "Every species is marked unseen (when you save).", "Clear", () => { for (int s = 1; s <= _max; s++) SetState(s, 0); })),
        new("Go to number...", () => Ask.Number(App, "Pokédex number", Math.Max(1, Sel), 1, _max, v => { Sel = (int)v; Scroll = Sel - 1; Fix(1); })),
    ];

    protected override bool Extra(InputEvent e)
    {
        // L/R turn a page: ten species at a time is a lot of pressing in a list of 500
        if (e.Is(Btn.L) || e.Is(Btn.R))
        {
            Sel += e.Btn == Btn.L ? -(Visible - 1) : Visible - 1;
            Fix(e.Btn == Btn.L ? -1 : 1);
            return true;
        }
        return false;
    }

    protected override void Save()
    {
        App.Run("Saving...", () =>
        {
            App.Mover.EditSave("Pokédex", s =>
            {
                for (int sp = 1; sp <= _max; sp++)
                {
                    if (_seen[sp] != _seen0[sp] || _caught[sp] != _caught0[sp])
                        DexEdit.Set(s, (ushort)sp, _seen[sp], _caught[sp]);
                }
            });
            return 0;
        }, _ =>
        {
            App.Pop();
            App.ShowToast("The Pokédex is saved.");
        }, ex => App.Message("Not saved", ex.Message));
    }

    protected override void Cancel()
    {
        bool changed = !_seen.SequenceEqual(_seen0) || !_caught.SequenceEqual(_caught0);
        if (changed)
            App.Confirm("Leave the Pokédex?", "Changes that weren't saved are lost.", "Leave", App.Pop, "Stay");
        else
            App.Pop();
    }

    protected override void DrawHints(Canvas c, float y)
    {
        float x = 8;
        x += c.Hint("A", "Change", x, y);
        x += c.Hint("L/R", "Page", x, y);
        x += c.Hint("Y", "All...", x, y);
        x += c.Hint("START", "Save", x, y);
        c.Hint("B", "Back", x, y);
    }

    public override void DrawTop(Canvas c)
    {
        int seen = _seen.Count(b => b), caught = _caught.Count(b => b);
        Views.Header(c, App, "Pokédex", $"seen {seen} · caught {caught}");
        var p = c.P;
        int s = Math.Clamp(Sel, 1, _max);
        c.Panel(16, 52, 300, 300, false);
        c.Mon((ushort)s, 0, 0, false, false, 26, 62, 280, 280, maxScale: 3, alpha: _seen[s] ? (byte)255 : (byte)70);
        c.Text($"#{s:000}", 340, 70, 22, p.Muted, bold: true);
        c.Text(Names.Species((ushort)s), 340, 100, 30, p.Ink, bold: true, maxW: 280);
        c.Text(State(s) switch { 2 => "Caught", 1 => "Seen", _ => "Not seen" }, 340, 146, 22, State(s) == 2 ? p.Good : State(s) == 1 ? p.Accent : p.Muted);
        c.Fill(340, 200, 280, 14, p.Chip);
        c.Fill(340, 200, 280f * seen / _max, 14, p.Accent.WithAlpha(120));
        c.Fill(340, 200, 280f * caught / _max, 14, p.Good);
        c.Text($"{caught * 100 / Math.Max(1, _max)}% caught, {seen * 100 / Math.Max(1, _max)}% seen", 340, 222, 16, p.Muted);
        c.Paragraph("The game also counts its own Pokédex for its diplomas and the national dex; marking species here doesn't unlock the national Pokédex.",
            16, 380, 608, 14, p.Muted);
    }
}

/// <summary>One of the game's boxes: its name and wallpaper. Written to the save with Save.</summary>
public sealed class SaveBoxScreen : FieldScreen
{
    private readonly int _box;
    private string _name;
    private int _wallpaper;
    private readonly int _wallpapers;

    public SaveBoxScreen(App app, int box) : base(app)
    {
        _box = box;
        var sav = app.Mover.Save!.Sav;
        _name = app.Mover.Save.BoxName(box);
        _wallpaper = sav is IBoxDetailWallpaper w ? w.GetBoxWallpaper(box) : -1;
        _wallpapers = sav switch
        {
            SAV3 => 16,
            SAV4DP => 16,
            SAV4 or SAV5 => 24,
            _ => 0,
        };
    }

    protected override IReadOnlyList<Field> Fields
    {
        get
        {
            var sav = App.Mover.Save!.Sav;
            var f = new List<Field> { Field.Head($"Box {_box + 1}") };
            if (sav is IBoxDetailName)
                f.Add(new("Name", () => _name, () => Ask.Text(App, "Box name", _name, 8, n => _name = n)));
            if (_wallpaper >= 0 && _wallpapers > 0)
            {
                f.Add(new("Wallpaper", () => sav.Generation == 3 && _wallpaper < GameInfo.Strings.wallpapernames.Length ? GameInfo.Strings.wallpapernames[_wallpaper] : $"{_wallpaper + 1} of {_wallpapers}",
                    null, d => _wallpaper = Ask.Cycle(_wallpaper, Math.Sign(d), _wallpapers)));
            }
            return f;
        }
    }

    protected override void Save()
    {
        App.Run("Saving...", () =>
        {
            App.Mover.EditSave($"box {_box + 1}", s =>
            {
                if (s is IBoxDetailName n)
                    n.SetBoxName(_box, _name);
                if (s is IBoxDetailWallpaper w && _wallpaper >= 0)
                    w.SetBoxWallpaper(_box, _wallpaper);
            });
            return 0;
        }, _ =>
        {
            App.Pop();
            App.Find<BoxScreen>()?.Refresh();
        }, ex => App.Message("Not saved", ex.Message));
    }

    protected override void Cancel() => App.Pop();

    public override void DrawTop(Canvas c)
    {
        Views.Header(c, App, "Box", App.Mover.Save!.Entry.GameName);
        var p = c.P;
        c.Text(_name, 320, 150, 40, p.Ink, Align.Center, bold: true, maxW: 600);
        if (_wallpaper >= 0)
            c.Text($"Wallpaper {_wallpaper + 1}", 320, 210, 20, p.Muted, Align.Center);
    }
}
