using System.IO.Compression;
using System.Runtime.InteropServices;

namespace Rocknixds.Bank.App;

/// <summary>
/// ROCKNIXDS Bank &amp; Trade: a Pokémon bank, legality checker and trading app for the RG DS's two screens.
/// <code>
/// rocknixds-bank [--config FILE] [--layout auto|side|stack|plus|sidex2] [--assets DIR]
///                [--script FILE --shots DIR]   (renders without a display: screenshots for the docs and the tests)
/// </code>
/// </summary>
public static class Program
{
    public const string AppId = "rocknixds-bank";
    public static string PadDescription { get; private set; } = "";

    private static StreamWriter? _log;

    public static int Main(string[] args)
    {
        string? configPath = null, layoutMode = null, assets = null, script = null, shots = null;
        for (int i = 0; i < args.Length; i++)
        {
            string Next() => i + 1 < args.Length ? args[++i] : throw new ArgumentException($"{args[i]} needs a value");
            switch (args[i])
            {
                case "--config": configPath = Next(); break;
                case "--layout": layoutMode = Next(); break;
                case "--assets": assets = Next(); break;
                case "--script": script = Next(); break;
                case "--shots": shots = Next(); break;
                case "--demo":
                    configPath = Demo.Make(Next());
                    Console.WriteLine($"demo sandbox: {configPath}");
                    break;
                case "--version":
                    Console.WriteLine($"rocknixds-bank {typeof(Program).Assembly.GetName().Version?.ToString(3)}");
                    return 0;
                case "--help" or "-h":
                    Console.WriteLine("rocknixds-bank [--config FILE] [--layout auto|side|stack|plus] [--assets DIR] [--script FILE --shots DIR] [--demo DIR]");
                    return 0;
                default:
                    Console.Error.WriteLine($"unknown option {args[i]}");
                    return 2;
            }
        }

        var cfg = BankConfig.Load(configPath);
        OpenLog(cfg);
        Log($"start {typeof(Program).Assembly.GetName().Version?.ToString(3)} on {RuntimeInformation.OSDescription} {RuntimeInformation.OSArchitecture}");
        Names.SetLanguage(cfg.Language);
        try
        {
            return Run(cfg, layoutMode, assets, script, shots);
        }
        catch (Exception ex)
        {
            Log($"fatal: {ex}");
            Console.Error.WriteLine(ex);
            return 1;
        }
        finally
        {
            _log?.Dispose();
        }
    }

    private static void OpenLog(BankConfig cfg)
    {
        try
        {
            var dir = Path.Combine(cfg.DataFolder, "logs");
            Directory.CreateDirectory(dir);
            var path = Path.Combine(dir, "rocknixds-bank.log");
            if (File.Exists(path) && new FileInfo(path).Length > 512 * 1024)
                File.Move(path, path + ".1", true);
            _log = new StreamWriter(path, append: true) { AutoFlush = true };
        }
        catch (Exception)
        {
            _log = null;
        }
    }

    public static void Log(string line)
    {
        var l = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {line}";
        lock (typeof(Program))
        {
            try { _log?.WriteLine(l); } catch (IOException) { }
        }
        if (Environment.GetEnvironmentVariable("ROCKNIXDS_BANK_VERBOSE") is not null)
            Console.Error.WriteLine(l);
    }

    private static string FindAssets(string? given)
    {
        var candidates = new List<string?>
        {
            given,
            Environment.GetEnvironmentVariable("ROCKNIXDS_BANK_ASSETS"),
            Path.Combine(AppContext.BaseDirectory, "assets"),
        };
        // a development checkout: bank/assets above the build output
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d is not null; d = d.Parent)
            candidates.Add(Path.Combine(d.FullName, "assets"));
        foreach (var c in candidates)
        {
            if (c is not null && File.Exists(Path.Combine(c, "fonts", "PixelifySans-Regular.ttf")))
                return c;
        }
        throw new FileNotFoundException("the app's assets folder (fonts/, sprites/) wasn't found; pass --assets DIR");
    }

    private static readonly string[] FallbackFonts =
    [
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/emulationstation/resources/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    ];

    private static unsafe int Run(BankConfig cfg, string? layoutMode, string? assetsArg, string? script, string? shots)
    {
        var assets = FindAssets(assetsArg);
        bool headless = script is not null;
        if (headless)
        {
            Sdl.SetHint("SDL_VIDEODRIVER", "offscreen"); // a hint, not an environment variable: see below
            layoutMode ??= "side";
        }
        // The window's app_id under sway is the executable's name, rocknixds-bank (the launcher also exports
        // SDL_VIDEO_WAYLAND_WMCLASS). The environment is never changed from in here: setenv() races with the runtime's
        // threads reading it, which crashed the arm64 build.

        var input = new Input(cfg.SwapAB);
        bool evdev = !headless && cfg.PadDevice.Length > 0 && input.StartEvdev(cfg.PadDevice);
        Sdl.SetHint("SDL_TOUCH_MOUSE_EVENTS", "0");
        Sdl.SetHint("SDL_MOUSE_TOUCH_EVENTS", "0");
        Sdl.SetHint("SDL_APP_ID", AppId);
        Sdl.SetHint("SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR", "0");
        uint flags = Sdl.INIT_VIDEO | Sdl.INIT_EVENTS | (evdev || headless ? 0 : Sdl.INIT_JOYSTICK | Sdl.INIT_GAMECONTROLLER);
        if (Sdl.Init(flags) != 0)
            throw new InvalidOperationException("SDL_Init: " + Sdl.GetError());
        try
        {
            var layout = Layout.Detect(layoutMode, Log);
            Log($"SDL {Sdl.Version}, video {Sdl.VideoDriver}, layout: {layout.Description} ({layout.WindowW}x{layout.WindowH})");
            var window = Sdl.CreateWindow("ROCKNIXDS Bank", Sdl.WINDOWPOS_UNDEFINED, Sdl.WINDOWPOS_UNDEFINED, layout.WindowW, layout.WindowH,
                Sdl.WINDOW_SHOWN | (layout.SwayOrigin is null ? 0 : Sdl.WINDOW_BORDERLESS));
            if (window == IntPtr.Zero)
                throw new InvalidOperationException("SDL_CreateWindow: " + Sdl.GetError());
            var renderer = headless ? IntPtr.Zero : Sdl.CreateRenderer(window, -1, Sdl.RENDERER_ACCELERATED | Sdl.RENDERER_PRESENTVSYNC);
            if (renderer == IntPtr.Zero)
                renderer = Sdl.CreateRenderer(window, -1, Sdl.RENDERER_SOFTWARE);
            if (renderer == IntPtr.Zero)
                throw new InvalidOperationException("SDL_CreateRenderer: " + Sdl.GetError());
            Sdl.ShowCursor(layout.SwayOrigin is null && !headless ? 1 : 0);
            layout.PlaceUnderSway(Log);

            if (!evdev && !headless)
                input.OpenSdlPads();
            PadDescription = input.PadDescription;
            Log($"pad: {PadDescription}");

            var fallbacks = FallbackFonts.ToList();
            using var regular = new Font(renderer, Path.Combine(assets, "fonts", "PixelifySans-Regular.ttf"), fallbacks);
            using var medium = new Font(renderer, Path.Combine(assets, "fonts", "PixelifySans-Medium.ttf"), fallbacks);
            using var sprites = new Pictures(renderer, Path.Combine(assets, "sprites"));
            var top = new Canvas(renderer, regular, medium, sprites);
            var bottom = new Canvas(renderer, regular, medium, sprites);

            var app = new App(cfg, Log);
            // the terms of use first: read and accepted once per version, before anything else opens
            if (LegalTerms.IsAccepted(cfg))
                app.Push(new LoadingScreen(app));
            else
                app.Push(new TermsScreen(app, () => app.Replace(new LoadingScreen(app))));

            input.WindowSize = () =>
            {
                Sdl.GetWindowSize(window, out var w, out var h);
                return (w, h);
            };
            input.WindowToBottom = (x, y) =>
            {
                Sdl.GetWindowSize(window, out var w, out var h);
                return layout.WindowToBottom(x, y, w, h);
            };
            input.Wake = () =>
            {
                var wake = new Sdl.Event { Type = 0x8000 }; // SDL_USEREVENT
                Sdl.PushEvent(ref wake);
            };

            var harness = script is null ? null : new Harness(File.ReadAllLines(script), shots ?? ".", input);
            bool redraw = true;
            uint lastFrame = 0;
            while (!app.QuitRequested)
            {
                uint now = Sdl.GetTicks();
                app.Now = now;
                int timeout = app.Animating || redraw ? 16 : 500;
                if (input.NextRepeat(now) is { } rep)
                    timeout = Math.Min(timeout, (int)Math.Max(1, rep - now));
                if (harness is not null)
                    timeout = 0;
                if (Sdl.WaitEventTimeout(out var ev, timeout) != 0)
                {
                    do
                    {
                        input.HandleSdl(ev, now);
                        if (ev.Type == Sdl.WINDOWEVENT)
                            redraw = true;
                    } while (Sdl.PollEvent(out ev) != 0);
                }
                now = Sdl.GetTicks();
                app.Now = now;
                harness?.Step(app, now);
                foreach (var e in input.Drain(now))
                {
                    if (e.Kind == InputKind.Quit && app.Top is BoxScreen && app.Dialog is null)
                    {
                        app.QuitRequested = true;
                        break;
                    }
                    app.Handle(e);
                    redraw = true;
                }
                app.Update();
                if (!redraw && !app.Animating && now - lastFrame < 1000)
                    continue;
                Draw(renderer, layout, app, top, bottom);
                if (harness is not null && harness.WantsShot is { } name)
                    harness.Shot(renderer, layout, name);
                Sdl.RenderPresent(renderer);
                lastFrame = now;
                redraw = false;
                if (harness is { Done: true })
                    break;
            }
            Log("quit");
            app.Trade?.Session.Leave("The partner closed the app.");
            Thread.Sleep(app.Trade is null ? 0 : 300); // let the goodbye go out
            input.Dispose();
            Sdl.DestroyRenderer(renderer);
            Sdl.DestroyWindow(window);
            return 0;
        }
        finally
        {
            Sdl.Quit();
        }
    }

    private static void Draw(IntPtr renderer, Layout layout, App app, Canvas top, Canvas bottom)
    {
        Sdl.GetRendererOutputSize(renderer, out var ow, out _);
        float rs = layout.WindowW > 0 ? (float)ow / layout.WindowW : 1; // HiDPI: renderer pixels per window pixel
        Sdl.SetRenderDrawColor(renderer, 0, 0, 0, 255);
        Sdl.RenderClear(renderer);
        top.P = bottom.P = app.Palette;
        top.Ticks = bottom.Ticks = app.Now;
        top.Begin((int)(layout.Top.X * rs), (int)(layout.Top.Y * rs), layout.TopScale * rs);
        app.Draw(top, bottom, drawTop: true);
        top.End();
        bottom.Begin((int)(layout.Bottom.X * rs), (int)(layout.Bottom.Y * rs), layout.BottomScale * rs);
        app.Draw(top, bottom, drawTop: false);
        bottom.End();
    }

    /// <summary>
    /// Scripted runs for screenshots: one command per line, "press A", "hold Right 400", "tap 120 200", "type K7P2QX",
    /// "wait 500", "shot name". Comments start with #.
    /// </summary>
    private sealed class Harness(string[] lines, string dir, Input input)
    {
        private int _line;
        private uint _waitUntil;

        public string? WantsShot { get; private set; }
        public bool Done { get; private set; }

        public void Step(App app, uint now)
        {
            if (WantsShot is not null || now < _waitUntil || app.Busy)
                return;
            while (_line < lines.Length)
            {
                var l = lines[_line++].Trim();
                if (l.Length == 0 || l.StartsWith('#'))
                    continue;
                var parts = l.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                switch (parts[0])
                {
                    case "press":
                        var b = Enum.Parse<Btn>(parts[1], true);
                        input.Inject(new InputEvent(InputKind.Press, b));
                        input.Inject(new InputEvent(InputKind.Release, b));
                        _waitUntil = now + 120;
                        return;
                    case "tap":
                        float x = float.Parse(parts[1]), y = float.Parse(parts[2]);
                        input.Inject(new InputEvent(InputKind.TouchDown, X: x, Y: y));
                        input.Inject(new InputEvent(InputKind.TouchUp, X: x, Y: y));
                        _waitUntil = now + 120;
                        return;
                    case "type":
                        input.Inject(new InputEvent(InputKind.Text, Text: string.Join(' ', parts.Skip(1))));
                        _waitUntil = now + 60;
                        return;
                    case "waitfile":
                        if (!File.Exists(parts[1]))
                        {
                            _line--;
                            _waitUntil = now + 100;
                            return;
                        }
                        continue;
                    case "typefile":
                        input.Inject(new InputEvent(InputKind.Text, Text: File.ReadAllText(parts[1]).Trim()));
                        _waitUntil = now + 60;
                        return;
                    case "wait":
                        _waitUntil = now + uint.Parse(parts[1]);
                        return;
                    case "shot":
                        WantsShot = parts[1];
                        return;
                    case "quit":
                        Done = true;
                        return;
                }
            }
            Done = true;
        }

        public unsafe void Shot(IntPtr renderer, Layout layout, string name)
        {
            WantsShot = null;
            Sdl.GetRendererOutputSize(renderer, out var w, out var h);
            var pixels = new byte[w * h * 4];
            fixed (byte* p = pixels)
                Sdl.RenderReadPixels(renderer, IntPtr.Zero, Sdl.PIXELFORMAT_ABGR8888, p, w * 4);
            Directory.CreateDirectory(dir);
            Png.Write(Path.Combine(dir, name + ".png"), pixels, w, h);
            Log($"screenshot {name}");
        }
    }
}

/// <summary>A minimal PNG writer (RGBA, no filtering): screenshots without another library.</summary>
public static class Png
{
    private static readonly uint[] Crc = MakeCrc();

    private static uint[] MakeCrc()
    {
        var t = new uint[256];
        for (uint n = 0; n < 256; n++)
        {
            uint c = n;
            for (int k = 0; k < 8; k++)
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }

    public static void Write(string path, byte[] rgba, int w, int h)
    {
        using var fs = File.Create(path);
        fs.Write([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
        var ihdr = new byte[13];
        System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(ihdr, w);
        System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(ihdr.AsSpan(4), h);
        ihdr[8] = 8;
        ihdr[9] = 6;
        Chunk(fs, "IHDR", ihdr);
        using var raw = new MemoryStream();
        using (var z = new ZLibStream(raw, CompressionLevel.Optimal, true))
        {
            for (int y = 0; y < h; y++)
            {
                z.WriteByte(0);
                z.Write(rgba, y * w * 4, w * 4);
            }
        }
        Chunk(fs, "IDAT", raw.ToArray());
        Chunk(fs, "IEND", []);
    }

    private static void Chunk(Stream s, string type, byte[] data)
    {
        var len = new byte[4];
        System.Buffers.Binary.BinaryPrimitives.WriteInt32BigEndian(len, data.Length);
        s.Write(len);
        var t = System.Text.Encoding.ASCII.GetBytes(type);
        s.Write(t);
        s.Write(data);
        uint c = 0xFFFFFFFF;
        foreach (var b in t.Concat(data))
            c = Crc[(c ^ b) & 0xFF] ^ (c >> 8);
        var crc = new byte[4];
        System.Buffers.Binary.BinaryPrimitives.WriteUInt32BigEndian(crc, c ^ 0xFFFFFFFF);
        s.Write(crc);
    }
}
