using System.Reflection;
using System.Runtime.InteropServices;

namespace Rocknixds.Bank.App;

/// <summary>The few SDL2 calls the app uses. SDL2 is part of ROCKNIX (and of every desktop Linux).</summary>
public static unsafe partial class Sdl
{
    private const string Lib = "SDL2";

    static Sdl()
    {
        NativeLibrary.SetDllImportResolver(typeof(Sdl).Assembly, (name, asm, path) =>
        {
            if (name != Lib)
                return IntPtr.Zero;
            foreach (var candidate in new[] { "libSDL2-2.0.so.0", "libSDL2-2.0.so", "libSDL2.so", "SDL2.dll", "libSDL2.dylib" })
            {
                if (NativeLibrary.TryLoad(candidate, asm, path, out var h))
                    return h;
            }
            return IntPtr.Zero;
        });
    }

    public const uint INIT_VIDEO = 0x20, INIT_JOYSTICK = 0x200, INIT_GAMECONTROLLER = 0x2000, INIT_EVENTS = 0x4000;
    public const uint WINDOW_SHOWN = 0x4, WINDOW_BORDERLESS = 0x10, WINDOW_RESIZABLE = 0x20;
    public const uint RENDERER_SOFTWARE = 0x1, RENDERER_ACCELERATED = 0x2, RENDERER_PRESENTVSYNC = 0x4, RENDERER_TARGETTEXTURE = 0x8;
    public const uint PIXELFORMAT_ABGR8888 = 0x16762004; // bytes R, G, B, A in memory on little-endian: stb_image's RGBA
    public const int TEXTUREACCESS_STATIC = 0, TEXTUREACCESS_TARGET = 2;
    public const int BLENDMODE_NONE = 0, BLENDMODE_BLEND = 1;
    public const int ScaleModeNearest = 0, ScaleModeLinear = 1;
    public const int WINDOWPOS_UNDEFINED = 0x1FFF0000;

    // event types
    public const uint QUIT = 0x100, WINDOWEVENT = 0x200, KEYDOWN = 0x300, KEYUP = 0x301, TEXTINPUT = 0x303;
    public const uint MOUSEMOTION = 0x400, MOUSEBUTTONDOWN = 0x401, MOUSEBUTTONUP = 0x402, MOUSEWHEEL = 0x403;
    public const uint JOYAXISMOTION = 0x600, JOYHATMOTION = 0x602, JOYBUTTONDOWN = 0x603, JOYBUTTONUP = 0x604;
    public const uint JOYDEVICEADDED = 0x605, JOYDEVICEREMOVED = 0x606;
    public const uint CONTROLLERAXISMOTION = 0x650, CONTROLLERBUTTONDOWN = 0x651, CONTROLLERBUTTONUP = 0x652;
    public const uint FINGERDOWN = 0x700, FINGERUP = 0x701, FINGERMOTION = 0x702;
    public const uint TOUCH_MOUSEID = 0xFFFFFFFF;

    public const byte WINDOWEVENT_EXPOSED = 3, WINDOWEVENT_SIZE_CHANGED = 6;

    [StructLayout(LayoutKind.Sequential)]
    public struct Rect(int x, int y, int w, int h)
    {
        public int X = x, Y = y, W = w, H = h;
    }

    /// <summary>SDL_Event: the union, with the fields of the events read here at their offsets.</summary>
    [StructLayout(LayoutKind.Explicit, Size = 56)]
    public struct Event
    {
        [FieldOffset(0)] public uint Type;
        // SDL_WindowEvent
        [FieldOffset(12)] public byte WindowEvent;
        // SDL_KeyboardEvent
        [FieldOffset(13)] public byte KeyRepeat;
        [FieldOffset(16)] public int Scancode;
        [FieldOffset(20)] public int Sym;
        [FieldOffset(24)] public ushort KeyMod;
        // SDL_TextInputEvent: char text[32] at 12
        [FieldOffset(12)] public fixed byte Text[32];
        // SDL_JoyButtonEvent / SDL_JoyHatEvent / SDL_JoyAxisEvent / SDL_ControllerButtonEvent
        [FieldOffset(8)] public int Which;
        [FieldOffset(12)] public byte JButton;
        [FieldOffset(13)] public byte JState;
        [FieldOffset(16)] public short JAxisValue;
        // SDL_MouseButtonEvent / SDL_MouseMotionEvent
        [FieldOffset(12)] public uint MouseWhich;
        [FieldOffset(16)] public byte MouseButton;
        [FieldOffset(20)] public int MouseX;
        [FieldOffset(24)] public int MouseY;
        // SDL_MouseWheelEvent
        [FieldOffset(16)] public int WheelX;
        [FieldOffset(20)] public int WheelY;
        // SDL_TouchFingerEvent
        [FieldOffset(16)] public long FingerId;
        [FieldOffset(24)] public float FingerX;
        [FieldOffset(28)] public float FingerY;
    }

    [LibraryImport(Lib, EntryPoint = "SDL_Init")] public static partial int Init(uint flags);
    [LibraryImport(Lib, EntryPoint = "SDL_Quit")] public static partial void Quit();
    [LibraryImport(Lib, EntryPoint = "SDL_GetError")] private static partial IntPtr GetErrorPtr();
    public static string GetError() => Marshal.PtrToStringUTF8(GetErrorPtr()) ?? "";
    [LibraryImport(Lib, EntryPoint = "SDL_SetHint", StringMarshalling = StringMarshalling.Utf8)] public static partial int SetHint(string name, string value);
    [LibraryImport(Lib, EntryPoint = "SDL_GetTicks")] public static partial uint GetTicks();
    [LibraryImport(Lib, EntryPoint = "SDL_GetCurrentVideoDriver")] private static partial IntPtr GetCurrentVideoDriverPtr();
    public static string VideoDriver => Marshal.PtrToStringUTF8(GetCurrentVideoDriverPtr()) ?? "";
    [LibraryImport(Lib, EntryPoint = "SDL_GetVersion")] public static partial void GetVersion(byte* v);

    [LibraryImport(Lib, EntryPoint = "SDL_CreateWindow", StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr CreateWindow(string title, int x, int y, int w, int h, uint flags);
    [LibraryImport(Lib, EntryPoint = "SDL_DestroyWindow")] public static partial void DestroyWindow(IntPtr window);
    [LibraryImport(Lib, EntryPoint = "SDL_GetWindowSize")] public static partial void GetWindowSize(IntPtr window, out int w, out int h);
    [LibraryImport(Lib, EntryPoint = "SDL_SetWindowSize")] public static partial void SetWindowSize(IntPtr window, int w, int h);
    [LibraryImport(Lib, EntryPoint = "SDL_ShowCursor")] public static partial int ShowCursor(int toggle);
    [LibraryImport(Lib, EntryPoint = "SDL_StartTextInput")] public static partial void StartTextInput();
    [LibraryImport(Lib, EntryPoint = "SDL_StopTextInput")] public static partial void StopTextInput();

    [LibraryImport(Lib, EntryPoint = "SDL_CreateRenderer")] public static partial IntPtr CreateRenderer(IntPtr window, int index, uint flags);
    [LibraryImport(Lib, EntryPoint = "SDL_DestroyRenderer")] public static partial void DestroyRenderer(IntPtr renderer);
    [LibraryImport(Lib, EntryPoint = "SDL_GetRendererOutputSize")] public static partial int GetRendererOutputSize(IntPtr renderer, out int w, out int h);
    [LibraryImport(Lib, EntryPoint = "SDL_SetRenderDrawColor")] public static partial int SetRenderDrawColor(IntPtr renderer, byte r, byte g, byte b, byte a);
    [LibraryImport(Lib, EntryPoint = "SDL_SetRenderDrawBlendMode")] public static partial int SetRenderDrawBlendMode(IntPtr renderer, int mode);
    [LibraryImport(Lib, EntryPoint = "SDL_RenderClear")] public static partial int RenderClear(IntPtr renderer);
    [LibraryImport(Lib, EntryPoint = "SDL_RenderPresent")] public static partial void RenderPresent(IntPtr renderer);
    [LibraryImport(Lib, EntryPoint = "SDL_RenderFillRect")] public static partial int RenderFillRect(IntPtr renderer, in Rect rect);
    [LibraryImport(Lib, EntryPoint = "SDL_RenderCopy")] public static partial int RenderCopy(IntPtr renderer, IntPtr texture, in Rect src, in Rect dst);
    [LibraryImport(Lib, EntryPoint = "SDL_RenderSetClipRect")] public static partial int RenderSetClipRect(IntPtr renderer, in Rect rect);
    [LibraryImport(Lib, EntryPoint = "SDL_RenderSetClipRect")] public static partial int RenderSetClipRect(IntPtr renderer, IntPtr rect);
    [LibraryImport(Lib, EntryPoint = "SDL_RenderReadPixels")] public static partial int RenderReadPixels(IntPtr renderer, IntPtr rect, uint format, void* pixels, int pitch);

    [LibraryImport(Lib, EntryPoint = "SDL_CreateTexture")] public static partial IntPtr CreateTexture(IntPtr renderer, uint format, int access, int w, int h);
    [LibraryImport(Lib, EntryPoint = "SDL_UpdateTexture")] public static partial int UpdateTexture(IntPtr texture, in Rect rect, void* pixels, int pitch);
    [LibraryImport(Lib, EntryPoint = "SDL_UpdateTexture")] public static partial int UpdateTexture(IntPtr texture, IntPtr rect, void* pixels, int pitch);
    [LibraryImport(Lib, EntryPoint = "SDL_SetTextureBlendMode")] public static partial int SetTextureBlendMode(IntPtr texture, int mode);
    [LibraryImport(Lib, EntryPoint = "SDL_SetTextureColorMod")] public static partial int SetTextureColorMod(IntPtr texture, byte r, byte g, byte b);
    [LibraryImport(Lib, EntryPoint = "SDL_SetTextureAlphaMod")] public static partial int SetTextureAlphaMod(IntPtr texture, byte a);
    [LibraryImport(Lib, EntryPoint = "SDL_SetTextureScaleMode")] public static partial int SetTextureScaleMode(IntPtr texture, int mode);
    [LibraryImport(Lib, EntryPoint = "SDL_DestroyTexture")] public static partial void DestroyTexture(IntPtr texture);

    [LibraryImport(Lib, EntryPoint = "SDL_PollEvent")] public static partial int PollEvent(out Event e);
    [LibraryImport(Lib, EntryPoint = "SDL_PushEvent")] public static partial int PushEvent(ref Event e);
    [LibraryImport(Lib, EntryPoint = "SDL_WaitEventTimeout")] public static partial int WaitEventTimeout(out Event e, int timeout);

    [LibraryImport(Lib, EntryPoint = "SDL_NumJoysticks")] public static partial int NumJoysticks();
    [LibraryImport(Lib, EntryPoint = "SDL_JoystickOpen")] public static partial IntPtr JoystickOpen(int index);
    [LibraryImport(Lib, EntryPoint = "SDL_JoystickNameForIndex")] private static partial IntPtr JoystickNameForIndexPtr(int index);
    public static string JoystickNameForIndex(int i) => Marshal.PtrToStringUTF8(JoystickNameForIndexPtr(i)) ?? "";
    [LibraryImport(Lib, EntryPoint = "SDL_IsGameController")] public static partial int IsGameController(int index);
    [LibraryImport(Lib, EntryPoint = "SDL_GameControllerOpen")] public static partial IntPtr GameControllerOpen(int index);
    [LibraryImport(Lib, EntryPoint = "SDL_RWFromFile", StringMarshalling = StringMarshalling.Utf8)] public static partial IntPtr RWFromFile(string file, string mode);
    [LibraryImport(Lib, EntryPoint = "SDL_GameControllerAddMappingsFromRW")] public static partial int GameControllerAddMappingsFromRW(IntPtr rw, int freerw);

    // keys
    public const int K_RETURN = 13, K_ESCAPE = 27, K_BACKSPACE = 8, K_TAB = 9, K_SPACE = 32;
    public const int K_RIGHT = 0x4000004F, K_LEFT = 0x40000050, K_DOWN = 0x40000051, K_UP = 0x40000052;
    public const int K_PAGEUP = 0x4000004B, K_PAGEDOWN = 0x4000004E, K_F1 = 0x4000003A, K_F12 = 0x40000045;

    // controller buttons
    public const byte CB_A = 0, CB_B = 1, CB_X = 2, CB_Y = 3, CB_BACK = 4, CB_GUIDE = 5, CB_START = 6, CB_LEFTSHOULDER = 9,
        CB_RIGHTSHOULDER = 10, CB_DPAD_UP = 11, CB_DPAD_DOWN = 12, CB_DPAD_LEFT = 13, CB_DPAD_RIGHT = 14;

    public static string Version
    {
        get
        {
            byte* v = stackalloc byte[3];
            GetVersion(v);
            return $"{v[0]}.{v[1]}.{v[2]}";
        }
    }
}
