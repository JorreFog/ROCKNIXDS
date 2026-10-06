using System.Diagnostics;
using System.Text.Json;

namespace Rocknixds.Bank.App;

/// <summary>
/// Where the two screens are. On the RG DS, sway lays the panels out side by side: DSI-2 (top) at x 0 and DSI-1
/// (bottom) next to it, each 640x480 (1024x768 on the RG DS Plus). The app opens one window over both, as the ROCKNIXDS
/// menu does, and draws each screen into its half.
/// </summary>
public sealed record Layout(int WindowW, int WindowH, PanelRect Top, PanelRect Bottom, string Description)
{
    public float TopScale => Top.H / 480f;
    public float BottomScale => Bottom.H / 480f;

    /// <summary>The sway output layout, or a desktop window (side by side, stacked, or one screen above the other).</summary>
    public static Layout Detect(string? mode, Action<string> log)
    {
        if (mode is null or "auto" && TrySway(log) is { } sway)
            return sway;
        int s = 1;
        if (mode is not null && mode.EndsWith("x2", StringComparison.Ordinal))
        {
            s = 2;
            mode = mode[..^2];
        }
        return mode switch
        {
            "stack" => new Layout(640 * s, 960 * s, new(0, 0, 640 * s, 480 * s), new(0, 480 * s, 640 * s, 480 * s), "stacked window"),
            "plus" => new Layout(2048, 768, new(0, 0, 1024, 768), new(1024, 0, 1024, 768), "RG DS Plus size window"),
            _ => new Layout(1280 * s, 480 * s, new(0, 0, 640 * s, 480 * s), new(640 * s, 0, 640 * s, 480 * s), "side-by-side window"),
        };
    }

    private static Layout? TrySway(Action<string> log)
    {
        if (Environment.GetEnvironmentVariable("WAYLAND_DISPLAY") is null && Environment.GetEnvironmentVariable("SWAYSOCK") is null)
            return null;
        var json = Swaymsg("-t get_outputs", log);
        if (json is null)
            return null;
        try
        {
            using var doc = JsonDocument.Parse(json);
            var outputs = new Dictionary<string, PanelRect>();
            foreach (var o in doc.RootElement.EnumerateArray())
            {
                if (!o.TryGetProperty("active", out var active) || !active.GetBoolean())
                    continue;
                var r = o.GetProperty("rect");
                outputs[o.GetProperty("name").GetString() ?? "?"] = new PanelRect(
                    r.GetProperty("x").GetInt32(), r.GetProperty("y").GetInt32(), r.GetProperty("width").GetInt32(), r.GetProperty("height").GetInt32());
            }
            var topName = Environment.GetEnvironmentVariable("ROCKNIXDS_BANK_TOP") ?? "DSI-2";
            var bottomName = Environment.GetEnvironmentVariable("ROCKNIXDS_BANK_BOTTOM") ?? "DSI-1";
            if (outputs.TryGetValue(topName, out var top) && outputs.TryGetValue(bottomName, out var bottom))
            {
                int x0 = Math.Min(top.X, bottom.X), y0 = Math.Min(top.Y, bottom.Y);
                int x1 = Math.Max(top.X + top.W, bottom.X + bottom.W), y1 = Math.Max(top.Y + top.H, bottom.Y + bottom.H);
                var l = new Layout(x1 - x0, y1 - y0, top with { X = top.X - x0, Y = top.Y - y0 }, bottom with { X = bottom.X - x0, Y = bottom.Y - y0 },
                    $"sway: {topName} {top.W}x{top.H} top, {bottomName} {bottom.W}x{bottom.H} bottom") { SwayOrigin = (x0, y0) };
                return l;
            }
            if (outputs.Count == 1)
            {
                // one screen: both canvases one above the other, scaled to fit
                var only = outputs.Values.First();
                int h = only.H / 2, w = h * 4 / 3;
                int x = (only.W - w) / 2;
                return new Layout(only.W, only.H, new(x, 0, w, h), new(x, h, w, h), "sway: one screen") { SwayOrigin = (only.X, only.Y) };
            }
            log($"sway outputs: {string.Join(", ", outputs.Keys)}; no {topName} + {bottomName}");
        }
        catch (Exception ex)
        {
            log($"sway outputs unreadable: {ex.Message}");
        }
        return null;
    }

    public (int X, int Y)? SwayOrigin { get; init; }

    /// <summary>Under sway: floats the window over both panels (as the ROCKNIXDS menu's window) once it exists.</summary>
    public void PlaceUnderSway(Action<string> log)
    {
        if (SwayOrigin is not { } o)
            return;
        var cmd = $"[app_id=\"{Program.AppId}\"] floating enable, border none, resize set {WindowW} {WindowH}, move absolute position {o.X} {o.Y}, focus";
        new Thread(() =>
        {
            for (int i = 0; i < 40; i++)
            {
                var r = Swaymsg($"'{cmd}'", log);
                if (r is not null && r.Contains("\"success\": true", StringComparison.Ordinal))
                {
                    log("placed the window over both panels");
                    return;
                }
                Thread.Sleep(100);
            }
            log("couldn't place the window with swaymsg");
        }) { IsBackground = true }.Start();
    }

    private static string? Swaymsg(string args, Action<string> log)
    {
        try
        {
            var psi = new ProcessStartInfo("/bin/sh", ["-c", "swaymsg " + args]) { RedirectStandardOutput = true, RedirectStandardError = true };
            using var p = Process.Start(psi);
            if (p is null)
                return null;
            var output = p.StandardOutput.ReadToEnd();
            p.WaitForExit(3000);
            return p.ExitCode == 0 || output.Length > 0 ? output : null;
        }
        catch (Exception ex)
        {
            log($"swaymsg: {ex.Message}");
            return null;
        }
    }

    public (float X, float Y)? WindowToBottom(float wx, float wy, int winW, int winH)
    {
        // the window may have been scaled by the compositor: map through the requested size
        float sx = winW > 0 ? (float)WindowW / winW : 1, sy = winH > 0 ? (float)WindowH / winH : 1;
        float x = wx * sx, y = wy * sy;
        if (x < Bottom.X || y < Bottom.Y || x >= Bottom.X + Bottom.W || y >= Bottom.Y + Bottom.H)
            return null;
        return ((x - Bottom.X) / BottomScale, (y - Bottom.Y) / BottomScale);
    }
}

public readonly record struct PanelRect(int X, int Y, int W, int H);
