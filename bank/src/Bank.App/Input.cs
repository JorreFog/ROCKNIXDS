using System.Collections.Concurrent;
using System.Runtime.InteropServices;

namespace Rocknixds.Bank.App;

public enum Btn { None, Up, Down, Left, Right, A, B, X, Y, L, R, L2, R2, Select, Start, Menu }

public enum InputKind { Press, Repeat, Release, TouchDown, TouchMove, TouchUp, Text, Quit }

/// <summary>One input, in the bottom screen's units for touches.</summary>
public readonly record struct InputEvent(InputKind Kind, Btn Btn = Btn.None, float X = 0, float Y = 0, string? Text = null)
{
    public bool Is(Btn b) => (Kind == InputKind.Press || Kind == InputKind.Repeat) && Btn == b;
    public bool Pressed(Btn b) => Kind == InputKind.Press && Btn == b;
}

/// <summary>
/// Buttons from the handheld's pad, a gamepad or the keyboard, and touches on the bottom screen, as one stream. On the
/// RG DS the pad is read from its evdev node (retrogame_joypad): the button codes are fixed there, so A is A whatever
/// SDL's controller database says.
/// </summary>
public sealed class Input : IDisposable
{
    private const int RepeatDelay = 330, RepeatEvery = 70;

    private readonly ConcurrentQueue<(Btn Btn, bool Down)> _padQueue = new();
    private readonly Queue<InputEvent> _events = new();
    private readonly Dictionary<Btn, uint> _held = [];
    private readonly HashSet<Btn> _down = [];
    private readonly bool _swapAB;
    private Thread? _evdev;
    private volatile bool _stop;
    private long _activeFinger = -1;

    /// <summary>Maps a window pixel to the bottom screen's units; null when outside it.</summary>
    public Func<float, float, (float X, float Y)?>? WindowToBottom { get; set; }
    public Func<(int W, int H)>? WindowSize { get; set; }

    public string PadDescription { get; private set; } = "keyboard";
    public bool UsesEvdev => _evdev is not null;

    public Input(bool swapAB) => _swapAB = swapAB;

    /// <summary>Finds the handheld's pad among the evdev devices. Call before SDL_Init, so SDL needn't open joysticks.</summary>
    public bool StartEvdev(string deviceName)
    {
        if (!OperatingSystem.IsLinux())
            return false;
        string? node = null;
        try
        {
            foreach (var dir in Directory.EnumerateDirectories("/sys/class/input", "event*"))
            {
                var namePath = Path.Combine(dir, "device", "name");
                if (File.Exists(namePath) && File.ReadAllText(namePath).Trim() == deviceName)
                {
                    node = "/dev/input/" + Path.GetFileName(dir);
                    break;
                }
            }
        }
        catch (Exception)
        {
            return false;
        }
        if (node is null)
            return false;
        FileStream fs;
        try
        {
            fs = new FileStream(node, FileMode.Open, FileAccess.Read, FileShare.ReadWrite, 1, FileOptions.None);
        }
        catch (Exception)
        {
            return false;
        }
        PadDescription = $"{deviceName} ({node})";
        _evdev = new Thread(() => ReadEvdev(fs)) { IsBackground = true, Name = "evdev pad" };
        _evdev.Start();
        return true;
    }

    /// <summary>Called from the evdev thread: wakes the main loop.</summary>
    public Action? Wake { get; set; }

    private void ReadEvdev(FileStream fs)
    {
        int size = IntPtr.Size == 8 ? 24 : 16; // struct input_event: timeval, u16 type, u16 code, s32 value
        var buf = new byte[size * 16];
        int hatX = 0, hatY = 0;
        try
        {
            while (!_stop)
            {
                int n = fs.Read(buf, 0, buf.Length);
                if (n <= 0)
                    break;
                for (int off = 0; off + size <= n; off += size)
                {
                    ushort type = BitConverter.ToUInt16(buf, off + size - 8);
                    ushort code = BitConverter.ToUInt16(buf, off + size - 6);
                    int value = BitConverter.ToInt32(buf, off + size - 4);
                    if (type == 1 && value != 2) // EV_KEY, not autorepeat
                    {
                        var b = code switch
                        {
                            304 => Btn.A, 305 => Btn.B, 307 => Btn.X, 308 => Btn.Y,
                            310 => Btn.L, 311 => Btn.R, 312 => Btn.L2, 313 => Btn.R2,
                            314 => Btn.Select, 315 => Btn.Start, 316 => Btn.Menu,
                            544 => Btn.Up, 545 => Btn.Down, 546 => Btn.Left, 547 => Btn.Right,
                            _ => Btn.None,
                        };
                        if (b != Btn.None)
                            _padQueue.Enqueue((b, value != 0));
                    }
                    else if (type == 3 && code is 16 or 17) // EV_ABS, ABS_HAT0X / ABS_HAT0Y
                    {
                        int v = Math.Sign(value);
                        if (code == 16)
                        {
                            Hat(ref hatX, v, Btn.Left, Btn.Right);
                        }
                        else
                        {
                            Hat(ref hatY, v, Btn.Up, Btn.Down);
                        }
                    }
                }
                Wake?.Invoke();
            }
        }
        catch (Exception)
        {
            // the pad went away
        }
    }

    private void Hat(ref int state, int v, Btn neg, Btn pos)
    {
        if (state == v)
            return;
        if (state < 0) _padQueue.Enqueue((neg, false));
        if (state > 0) _padQueue.Enqueue((pos, false));
        if (v < 0) _padQueue.Enqueue((neg, true));
        if (v > 0) _padQueue.Enqueue((pos, true));
        state = v;
    }

    private static readonly string[] ControllerDbs =
    [
        "/storage/.config/SDL-GameControllerDB/gamecontrollerdb.txt",
        "/usr/config/SDL-GameControllerDB/gamecontrollerdb.txt",
        "/usr/share/SDL-GameControllerDB/gamecontrollerdb.txt",
    ];

    /// <summary>Opens SDL gamepads (when there's no evdev pad).</summary>
    public void OpenSdlPads()
    {
        foreach (var db in ControllerDbs.Prepend(Environment.GetEnvironmentVariable("SDL_GAMECONTROLLERCONFIG_FILE") ?? ""))
        {
            if (db.Length > 0 && File.Exists(db))
                Sdl.GameControllerAddMappingsFromRW(Sdl.RWFromFile(db, "rb"), 1);
        }
        for (int i = 0; i < Sdl.NumJoysticks(); i++)
            OpenSdlPad(i);
    }

    private readonly HashSet<int> _controllers = [];

    private void OpenSdlPad(int index)
    {
        var name = Sdl.JoystickNameForIndex(index);
        if (Sdl.IsGameController(index) != 0 && Sdl.GameControllerOpen(index) != IntPtr.Zero)
        {
            PadDescription = $"{name} (SDL controller)";
            return;
        }
        if (Sdl.JoystickOpen(index) != IntPtr.Zero)
            PadDescription = $"{name} (SDL joystick)";
    }

    public void HandleSdl(in Sdl.Event e, uint now)
    {
        switch (e.Type)
        {
            case Sdl.QUIT:
                _events.Enqueue(new InputEvent(InputKind.Quit));
                break;
            case Sdl.KEYDOWN when e.KeyRepeat == 0:
            case Sdl.KEYUP:
            {
                var b = KeyToBtn(e.Sym);
                if (b != Btn.None)
                    Set(b, e.Type == Sdl.KEYDOWN, now);
                break;
            }
            case Sdl.TEXTINPUT:
            {
                unsafe
                {
                    fixed (byte* t = e.Text)
                    {
                        var s = Marshal.PtrToStringUTF8((IntPtr)t);
                        if (!string.IsNullOrEmpty(s))
                            _events.Enqueue(new InputEvent(InputKind.Text, Text: s));
                    }
                }
                break;
            }
            case Sdl.JOYDEVICEADDED when !UsesEvdev:
                OpenSdlPad(e.Which);
                break;
            case Sdl.CONTROLLERBUTTONDOWN:
            case Sdl.CONTROLLERBUTTONUP:
            {
                _controllers.Add(e.Which);
                var b = e.JButton switch
                {
                    Sdl.CB_A => Btn.A, Sdl.CB_B => Btn.B, Sdl.CB_X => Btn.X, Sdl.CB_Y => Btn.Y,
                    Sdl.CB_BACK => Btn.Select, Sdl.CB_START => Btn.Start, Sdl.CB_GUIDE => Btn.Menu,
                    Sdl.CB_LEFTSHOULDER => Btn.L, Sdl.CB_RIGHTSHOULDER => Btn.R,
                    Sdl.CB_DPAD_UP => Btn.Up, Sdl.CB_DPAD_DOWN => Btn.Down, Sdl.CB_DPAD_LEFT => Btn.Left, Sdl.CB_DPAD_RIGHT => Btn.Right,
                    _ => Btn.None,
                };
                if (b != Btn.None)
                    Set(b, e.Type == Sdl.CONTROLLERBUTTONDOWN, now);
                break;
            }
            case Sdl.JOYBUTTONDOWN when !_controllers.Contains(e.Which):
            case Sdl.JOYBUTTONUP when !_controllers.Contains(e.Which):
            {
                // a pad SDL has no mapping for: the RG DS's order (padkey.py): A B X Y L R L2 R2 Select Start Menu
                var b = e.JButton switch
                {
                    0 => Btn.A, 1 => Btn.B, 2 => Btn.X, 3 => Btn.Y, 4 => Btn.L, 5 => Btn.R, 6 => Btn.L2, 7 => Btn.R2,
                    8 => Btn.Select, 9 => Btn.Start, 10 => Btn.Menu,
                    _ => Btn.None,
                };
                if (b != Btn.None)
                    Set(b, e.Type == Sdl.JOYBUTTONDOWN, now);
                break;
            }
            case Sdl.JOYHATMOTION when !_controllers.Contains(e.Which):
            {
                int v = e.JState; // SDL_HAT_UP 1, RIGHT 2, DOWN 4, LEFT 8
                Set(Btn.Up, (v & 1) != 0, now);
                Set(Btn.Right, (v & 2) != 0, now);
                Set(Btn.Down, (v & 4) != 0, now);
                Set(Btn.Left, (v & 8) != 0, now);
                break;
            }
            case Sdl.FINGERDOWN:
            case Sdl.FINGERMOTION:
            case Sdl.FINGERUP:
            {
                if (WindowSize is null || WindowToBottom is null)
                    break;
                if (e.Type == Sdl.FINGERDOWN)
                {
                    if (_activeFinger != -1)
                        break; // one finger at a time
                    _activeFinger = e.FingerId;
                }
                else if (e.FingerId != _activeFinger)
                {
                    break;
                }
                var (w, h) = WindowSize();
                var p = WindowToBottom(e.FingerX * w, e.FingerY * h);
                var kind = e.Type == Sdl.FINGERDOWN ? InputKind.TouchDown : e.Type == Sdl.FINGERUP ? InputKind.TouchUp : InputKind.TouchMove;
                if (kind == InputKind.TouchUp)
                    _activeFinger = -1;
                if (p is { } q)
                    _events.Enqueue(new InputEvent(kind, X: q.X, Y: q.Y));
                else if (kind == InputKind.TouchUp)
                    _events.Enqueue(new InputEvent(kind, X: -1, Y: -1));
                break;
            }
            case Sdl.MOUSEBUTTONDOWN when e.MouseWhich != Sdl.TOUCH_MOUSEID && e.MouseButton == 1:
            case Sdl.MOUSEBUTTONUP when e.MouseWhich != Sdl.TOUCH_MOUSEID && e.MouseButton == 1:
            case Sdl.MOUSEMOTION when e.MouseWhich != Sdl.TOUCH_MOUSEID && _mouseDown:
            {
                if (WindowToBottom is null)
                    break;
                var kind = e.Type == Sdl.MOUSEBUTTONDOWN ? InputKind.TouchDown : e.Type == Sdl.MOUSEBUTTONUP ? InputKind.TouchUp : InputKind.TouchMove;
                _mouseDown = kind != InputKind.TouchUp && (kind == InputKind.TouchDown || _mouseDown);
                var p = WindowToBottom(e.MouseX, e.MouseY);
                if (p is { } q)
                    _events.Enqueue(new InputEvent(kind, X: q.X, Y: q.Y));
                else if (kind == InputKind.TouchUp)
                    _events.Enqueue(new InputEvent(kind, X: -1, Y: -1));
                break;
            }
            case Sdl.MOUSEWHEEL:
                if (e.WheelY != 0)
                {
                    var b = e.WheelY > 0 ? Btn.Up : Btn.Down;
                    _events.Enqueue(new InputEvent(InputKind.Press, b));
                    _events.Enqueue(new InputEvent(InputKind.Release, b));
                }
                break;
        }
    }

    private bool _mouseDown;

    private static Btn KeyToBtn(int sym) => sym switch
    {
        Sdl.K_UP => Btn.Up, Sdl.K_DOWN => Btn.Down, Sdl.K_LEFT => Btn.Left, Sdl.K_RIGHT => Btn.Right,
        'z' or Sdl.K_RETURN => Btn.A, 'x' or Sdl.K_ESCAPE => Btn.B, 's' => Btn.X, 'a' => Btn.Y,
        'q' or Sdl.K_PAGEUP => Btn.L, 'w' or Sdl.K_PAGEDOWN => Btn.R,
        Sdl.K_BACKSPACE or Sdl.K_TAB => Btn.Select, Sdl.K_SPACE => Btn.Start, Sdl.K_F1 => Btn.Menu,
        _ => Btn.None,
    };

    /// <summary>Feeds a scripted input (the screenshot harness).</summary>
    public void Inject(InputEvent e) => _events.Enqueue(e);

    private void Set(Btn b, bool down, uint now)
    {
        if (_swapAB)
            b = b switch { Btn.A => Btn.B, Btn.B => Btn.A, _ => b };
        if (down)
        {
            if (!_down.Add(b))
                return;
            _held[b] = now + RepeatDelay;
            _events.Enqueue(new InputEvent(InputKind.Press, b));
            if (b is Btn.Start or Btn.Select && _down.Contains(Btn.Start) && _down.Contains(Btn.Select))
                _events.Enqueue(new InputEvent(InputKind.Quit)); // the ROCKNIXDS quit chord
        }
        else
        {
            if (!_down.Remove(b))
                return;
            _held.Remove(b);
            _events.Enqueue(new InputEvent(InputKind.Release, b));
        }
    }

    public bool IsDown(Btn b) => _down.Contains(b);

    /// <summary>Everything since the last call, with key repeats for the d-pad and shoulders.</summary>
    public List<InputEvent> Drain(uint now)
    {
        while (_padQueue.TryDequeue(out var p))
            Set(p.Btn, p.Down, now);
        foreach (var b in _held.Keys.ToList())
        {
            if (b is not (Btn.Up or Btn.Down or Btn.Left or Btn.Right or Btn.L or Btn.R))
                continue;
            if (now >= _held[b])
            {
                _events.Enqueue(new InputEvent(InputKind.Repeat, b));
                _held[b] = now + RepeatEvery;
            }
        }
        var list = new List<InputEvent>(_events);
        _events.Clear();
        return list;
    }

    /// <summary>When the next key repeat is due (for the main loop's wait), or null.</summary>
    public uint? NextRepeat(uint now)
    {
        uint? next = null;
        foreach (var (b, t) in _held)
        {
            if (b is Btn.Up or Btn.Down or Btn.Left or Btn.Right or Btn.L or Btn.R)
                next = next is null ? t : Math.Min(next.Value, t);
        }
        return next;
    }

    public void Dispose() => _stop = true;
}
