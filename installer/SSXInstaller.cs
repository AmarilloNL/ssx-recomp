// SSX (2012) PC port - installer.
//
// Downloads this project's source (no game code or data), then runs
// scripts\build-ssx.ps1 against the player's own SSX ISO, which installs the
// build tools, recompiles the game on this PC and produces the playable folder.
//
// Built with the C# compiler that ships with Windows (.NET Framework 4.x, C# 5):
//   installer\build-installer.ps1

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

[assembly: AssemblyTitle("SSX PC Port")]
[assembly: AssemblyDescription("Builds the SSX (2012) PC port from your own Xbox 360 disc image")]
[assembly: AssemblyProduct("SSX PC Port")]
[assembly: AssemblyCompany("AmarilloNL")]
[assembly: AssemblyCopyright("Copyright (c) 2026 AmarilloNL, MIT License")]
[assembly: AssemblyVersion("1.8.0.0")]
[assembly: AssemblyFileVersion("1.8.0.0")]
[assembly: AssemblyInformationalVersion("1.8.0")]

// Windows shell link (.lnk) interfaces, for the desktop shortcut.
[ComImport, Guid("00021401-0000-0000-C000-000000000046")] class ShellLink { }

[ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("000214F9-0000-0000-C000-000000000046")]
interface IShellLinkW {
  void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder file, int max, IntPtr data, int flags);
  void GetIDList(out IntPtr list);
  void SetIDList(IntPtr list);
  void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder name, int max);
  void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string name);
  void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder dir, int max);
  void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string dir);
  void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder args, int max);
  void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string args);
  void GetHotkey(out short hotkey);
  void SetHotkey(short hotkey);
  void GetShowCmd(out int cmd);
  void SetShowCmd(int cmd);
  void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder path, int max, out int index);
  void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string path, int index);
  void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string path, int reserved);
  void Resolve(IntPtr hwnd, int flags);
  void SetPath([MarshalAs(UnmanagedType.LPWStr)] string file);
}

static class Program {
  [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

  // ssx-recomp.exe is the one program the player starts, from the folder SSX lives in: it builds
  // the game into source\ next to itself, and everything the game keeps (saves, DLC, music,
  // shader cache, settings) stays in that folder too. It carries this port's source code (embedded
  // by build-installer.ps1), so building never downloads the port: only the Update button goes
  // online for the port, and replaces this exe with the latest release's, which then rebuilds.
  // Without a built game it opens the setup; with one it opens on the Play page. Any other
  // arguments, or --play, start the game straight away with them (minus --play).
  [STAThread]
  static int Main(string[] args) {
    Install.RemoveReplacedExe();
    if (args.Length > 0 && Array.IndexOf(HeadlessOptions, args[0].Split('=')[0]) >= 0) return Headless(args);
    string folder = Install.Folder;
    // --rebuild: started by the Update button after replacing this exe; rebuilds with the ISO the game uses.
    bool rebuild = args.Length > 0 && args[0] == "--rebuild";
    bool setup = rebuild || (args.Length > 0 && args[0] == "--setup");
    if (!setup && args.Length > 0 && Install.Built(folder)) {
      try {
        Install.StartGame(folder, Array.FindAll(args, a => a != "--play"));
        return 0;
      } catch (Exception ex) {
        MessageBox.Show("SSX couldn't be started: " + ex.Message, "SSX", MessageBoxButtons.OK, MessageBoxIcon.Warning);
      }
    }
    try { SetProcessDPIAware(); } catch (Exception) { }
    Application.EnableVisualStyles();
    Application.SetCompatibleTextRenderingDefault(false);
    Application.Run(new InstallerForm(folder, setup, rebuild));
    return 0;
  }

  static readonly string[] HeadlessOptions = {
    "--game", "--iso", "--folder", "--title-update", "--dlc", "--portable", "--no-menu", "--prepare", "--accept-vs-license" };

  const string Usage = "Usage: ssx-recomp.exe [--prepare] --game <your SSX .iso, or its extracted folder> " +
                       "[--folder <folder>] [--title-update <file>] [--dlc <file>]...";

  // Without a window, for a launcher: builds as the window does (into <folder>, by default the one
  // ssx-recomp.exe is in; the game ends up in <folder>\source\dist\SSX\ssx.exe; no desktop shortcut),
  // the build's output on stdout. Options take their value after a space or after = (--dlc=<file>).
  // --game (or --iso): the disc image, or the extracted disc's folder or its default.xex.
  // Exit codes: 0 the game is built, 2 wrong options, 1 something unexpected, else the builder's
  // (scripts\build-ssx.ps1): 10-13 the copy of the game, 20-22 the title update, 30-31 the build
  // tools, 40 a download, 50 the build.
  // --prepare (RecompLauncher's setup step, recomplauncher.json): prints only "progress <0-1> <step>"
  // lines (the build's output still goes to build-ssx.log) and, when it fails, the reason as its
  // last line; a --title-update or --dlc file that isn't there is skipped: the player didn't pick it.
  // --accept-vs-license: the setup screen's required tick for the Visual Studio Build Tools' license
  // (RecompLauncher enforces it); accepted, nothing more to do here.
  static int Headless(string[] args) {
    string iso = null, titleUpdate = null;
    string folder = Install.Folder;
    var dlc = new List<string>();
    bool prepare = false;
    for (int i = 0; i < args.Length; i++) {
      string name = args[i], value = null;
      int eq = name.IndexOf('=');
      if (name.StartsWith("--") && eq > 2) {
        value = name.Substring(eq + 1);
        name = name.Substring(0, eq);
      } else if (Array.IndexOf(new[] { "--game", "--iso", "--folder", "--title-update", "--dlc" }, name) >= 0) {
        if (i + 1 >= args.Length) { Console.Error.WriteLine(Usage); return 2; }
        value = args[++i];
      }
      if (name == "--iso" || name == "--game") iso = value;
      else if (name == "--folder") folder = value;
      else if (name == "--title-update") titleUpdate = value;
      else if (name == "--dlc") dlc.Add(value);
      else if (name == "--prepare" && value == null) prepare = true;
      else if (name == "--accept-vs-license" && value == null) { }
      else if (name == "--portable") { }  // always portable now; kept so older command lines still work
      else if (name == "--no-menu") { }  // (the Linux installer's option: one launcher command line for both)
      else { Console.Error.WriteLine(Usage); return 2; }
    }
    if (iso == null) { Console.Error.WriteLine(Usage); return 2; }
    if (!File.Exists(iso) && !Directory.Exists(iso)) { Console.WriteLine("Your copy of SSX wasn't found: " + iso); return 10; }
    if (prepare && titleUpdate != null && !File.Exists(titleUpdate)) titleUpdate = null;
    if (prepare) dlc.RemoveAll(f => !File.Exists(f));
    if (titleUpdate != null && !File.Exists(titleUpdate)) { Console.WriteLine("Your title update wasn't found: " + titleUpdate); return 20; }
    Action<string> line = delegate(string text) { if (text != null) Console.WriteLine(text); };
    PrepareProgress progress = prepare ? new PrepareProgress() : null;
    if (prepare) line = progress.OnLine;
    try {
      folder = Path.GetFullPath(folder);
      InstallerForm.CopyDlc(dlc.ToArray(), Install.UserData(folder), line);
      int code = InstallerForm.Build(Path.GetFullPath(iso), folder,
                                     titleUpdate != null ? Path.GetFullPath(titleUpdate) : null, line, null);
      if (code == 0 && Install.Built(folder)) {
        Install.MarkBuilt(folder);
        return 0;
      }
      // The builder's reason (its ERROR line), last: RecompLauncher shows the last line with the code.
      Console.WriteLine(progress != null && progress.Error != null
                            ? progress.Error
                            : "The build failed. The full log is build-ssx.log in " + Path.Combine(folder, "source"));
      return code >= 10 ? code : 50;
    } catch (Exception ex) {
      Console.Error.WriteLine(ex.ToString());
      Console.WriteLine("Setting up SSX failed: " + ex.Message);
      return 1;
    }
  }
}

// --prepare's output: the build's steps and its ninja counts as RecompLauncher progress lines
// ("progress 0.42 Recompile the Xbox 360 code"), the same progress the setup window shows.
class PrepareProgress {
  int stage;
  float shown = -1;
  // The builder's last "ERROR: ..." line, without the prefix: why it stopped.
  public string Error;

  public void OnLine(string text) {
    if (text == null) return;
    if (text.StartsWith("ERROR: ")) { Error = text.Substring(7).Trim(); return; }
    string name = stage == 2 ? InstallerForm.ToolOf(text) : null;
    if (name != null) {
      Console.WriteLine(string.Format(System.Globalization.CultureInfo.InvariantCulture,
                                      "progress {0:0.000} Installing {1}: Windows may ask for permission",
                                      Math.Max(shown, InstallerForm.StageStart[2]), name));
      return;
    }
    float frac = 0;
    if (text.StartsWith("==> ")) {
      int s = InstallerForm.StageOf(text.Substring(4));
      if (s <= stage && shown >= 0) return;
      if (s > stage) stage = s;
    } else {
      Match m = InstallerForm.NinjaStep.Match(text);
      if (!m.Success || (stage != 4 && stage != 5)) return;
      float total = float.Parse(m.Groups[2].Value);
      if (total < 20) return;
      frac = float.Parse(m.Groups[1].Value) / total;
    }
    float value = Math.Min(.99f, InstallerForm.StageStart[stage] +
                                 (InstallerForm.StageStart[stage + 1] - InstallerForm.StageStart[stage]) * frac);
    if (value < shown + .005f) return;  // a line for each half percent at most
    shown = value;
    Console.WriteLine(string.Format(System.Globalization.CultureInfo.InvariantCulture, "progress {0:0.000} {1}",
                                    value, InstallerForm.Stages[stage]));
  }
}

// The SSX folder is the one ssx-recomp.exe is in: the build goes to source\ there, the game is
// source\dist\SSX\ssx.exe, and it always runs portable (a portable.txt next to ssx.exe), so its
// saves, DLC, music, shader cache and settings stay in this folder too. Nothing is written
// anywhere else, and the folder can be moved or copied as a whole.
static class Install {
  public static string Self { get { return Path.GetFullPath(Application.ExecutablePath); } }
  public static string Folder { get { return Path.GetDirectoryName(Self); } }

  public static string GameDir(string folder) { return Path.Combine(folder, "source\\dist\\SSX"); }
  public static string GameExe(string folder) { return Path.Combine(GameDir(folder), "ssx.exe"); }
  public static bool Built(string folder) { return File.Exists(GameExe(folder)); }

  // Makes the game portable (the game reads portable.txt, src/ssx_app.h) and returns where it keeps
  // its saves, DLC and music: the userdata folder next to ssx.exe.
  public static string UserData(string folder) {
    string game = GameDir(folder), marker = Path.Combine(game, "portable.txt");
    if (!File.Exists(marker)) {
      Directory.CreateDirectory(game);
      File.WriteAllText(marker, "SSX keeps its saves, DLC, music and shader cache in the userdata folder here.\r\n");
    }
    return Path.Combine(game, "userdata");
  }

  // Files and folders in the SSX folder other than ssx-recomp.exe and what it makes there: the
  // setup warns when this exe was started from a busy folder such as Downloads.
  public static int Others(string folder) {
    int n = 0;
    try {
      foreach (string entry in Directory.EnumerateFileSystemEntries(folder)) {
        string name = Path.GetFileName(entry);
        if (!string.Equals(entry, Self, StringComparison.OrdinalIgnoreCase) &&
            !string.Equals(name, "source", StringComparison.OrdinalIgnoreCase) &&
            !name.EndsWith(".iso", StringComparison.OrdinalIgnoreCase)) n++;
      }
    } catch (Exception) { }
    return n;
  }

  // ---- The port's source, carried in this exe ----------------------------------------------

  // Which commit of the port this exe carries (build-installer.ps1 embeds source.zip and its id).
  public static string SourceId {
    get {
      using (Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream("source-id.txt")) {
        return s == null ? null : new StreamReader(s).ReadToEnd().Trim();
      }
    }
  }

  static string Stamp(string folder, string name) { return Path.Combine(folder, "source\\" + name); }
  static string ReadStamp(string path) {
    try { return File.Exists(path) ? File.ReadAllText(path).Trim() : null; } catch (Exception) { return null; }
  }

  // The game is built, but from other source than this exe carries (an update replaced this exe,
  // or the game was built by an older installer): building again brings the game up to date.
  public static bool NeedsRebuild(string folder) {
    return Built(folder) && ReadStamp(Stamp(folder, ".ssx-built")) != SourceId;
  }

  public static void MarkBuilt(string folder) {
    try { File.WriteAllText(Stamp(folder, ".ssx-built"), SourceId ?? ""); } catch (Exception) { }
  }

  // Unpacks the source this exe carries into <folder>\source, unless it is already there.
  // Earlier build output (tools, out, dist) stays, so a rebuild only redoes what changed.
  public static void UnpackSource(string folder, Action<string> onLine) {
    string source = Path.Combine(folder, "source"), stamp = Stamp(folder, ".ssx-source");
    if (SourceId != null && ReadStamp(stamp) == SourceId && File.Exists(Path.Combine(source, "scripts\\build-ssx.ps1"))) {
      onLine("The port's source (" + SourceId + ") is already unpacked.");
      return;
    }
    using (Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream("source.zip")) {
      if (s == null) {
        throw new InvalidOperationException("This ssx-recomp.exe carries no source code: build it with " +
                                            "installer\\build-installer.ps1, which embeds it.");
      }
      using (var archive = new ZipArchive(s)) {
        foreach (var entry in archive.Entries) {  // one top-level folder, left out
          int slash = entry.FullName.IndexOf('/');
          string rel = slash >= 0 ? entry.FullName.Substring(slash + 1) : entry.FullName;
          if (rel.Length == 0) continue;
          string path = Path.Combine(source, rel.Replace('/', '\\'));
          if (rel.EndsWith("/")) { Directory.CreateDirectory(path); continue; }
          Directory.CreateDirectory(Path.GetDirectoryName(path));
          // Unchanged files keep their time; changed ones get the current time, not the commit's:
          // the build only recompiles files newer than its last build, and a release's commit can
          // be older than that build (the player built after the commit was made).
          if (SameContent(entry, path)) continue;
          entry.ExtractToFile(path, true);
          File.SetLastWriteTimeUtc(path, DateTime.UtcNow);
        }
      }
    }
    File.WriteAllText(stamp, SourceId ?? "");
    onLine("Unpacked the port's source (" + SourceId + ").");
  }

  static bool SameContent(ZipArchiveEntry entry, string path) {
    try {
      var file = new FileInfo(path);
      if (!file.Exists || file.Length != entry.Length) return false;
      using (Stream a = entry.Open())
      using (Stream b = file.OpenRead()) {
        byte[] x = new byte[65536], y = new byte[65536];
        int n;
        while ((n = a.Read(x, 0, x.Length)) > 0) {
          for (int got = 0, m; got < n; got += m) {
            m = b.Read(y, got, n - got);
            if (m == 0) return false;
          }
          for (int i = 0; i < n; i++) if (x[i] != y[i]) return false;
        }
        return b.ReadByte() < 0;
      }
    } catch (Exception) {
      return false;
    }
  }

  // ---- Updates (only from the Update button) ------------------------------------------------

  const string LatestReleaseApi = "https://api.github.com/repos/AmarilloNL/ssx-recomp/releases/latest";

  // This exe's version, e.g. 1.6.0 (the releases are tagged v1.6.0).
  public static string AppVersion {
    get {
      var a = (AssemblyInformationalVersionAttribute)Attribute.GetCustomAttribute(
          Assembly.GetExecutingAssembly(), typeof(AssemblyInformationalVersionAttribute));
      return a != null ? a.InformationalVersion : "0.0.0";
    }
  }

  // The latest release's tag, and the download of its ssx-recomp.exe (null when it has none).
  public static string LatestRelease(out string exeUrl) {
    ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
    using (var web = new WebClient()) {
      web.Headers[HttpRequestHeader.UserAgent] = "ssx-recomp";
      string json = web.DownloadString(LatestReleaseApi);
      Match tag = Regex.Match(json, "\"tag_name\"\\s*:\\s*\"([^\"]+)\"");
      Match exe = Regex.Match(json, "\"browser_download_url\"\\s*:\\s*\"(https://[^\"]+/ssx-recomp\\.exe)\"");
      exeUrl = exe.Success ? exe.Groups[1].Value : null;
      return tag.Success ? tag.Groups[1].Value : null;
    }
  }

  static Version ParseVersion(string text) {
    Version v;
    return text != null && Version.TryParse(text.TrimStart('v', 'V'), out v) ? v : null;
  }

  public static bool IsNewer(string tag) {
    Version latest = ParseVersion(tag), own = ParseVersion(AppVersion);
    return latest != null && own != null && latest > own;
  }

  // Downloads a release's ssx-recomp.exe next to this one (as ssx-recomp.exe.new) and checks it
  // is that version's program. Returns its path.
  public static string DownloadUpdate(string url, string tag) {
    string file = Self + ".new";
    ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
    using (var web = new WebClient()) {
      web.Headers[HttpRequestHeader.UserAgent] = "ssx-recomp";
      web.DownloadFile(url, file);
    }
    string version = FileVersionInfo.GetVersionInfo(file).ProductVersion;
    if (ParseVersion(version) == null || ParseVersion(version) != ParseVersion(tag)) {
      File.Delete(file);
      throw new InvalidDataException("the download isn't ssx-recomp.exe " + tag);
    }
    return file;
  }

  // Puts newExe in target's place. A running exe can be renamed, so target may be this exe: it
  // becomes target.old, which the new exe deletes when it starts.
  public static void Replace(string newExe, string target) {
    string old = target + ".old";
    if (File.Exists(old)) File.Delete(old);
    File.Move(target, old);
    try {
      File.Move(newExe, target);
    } catch (Exception) {
      File.Move(old, target);
      throw;
    }
  }

  // Deletes the exe an update replaced (it may still be closing).
  public static void RemoveReplacedExe() {
    string old = Self + ".old";
    if (!File.Exists(old)) return;
    var thread = new Thread(delegate() {
      for (int i = 0; i < 20 && File.Exists(old); i++) {
        try { File.Delete(old); } catch (Exception) { Thread.Sleep(250); }
      }
    });
    thread.IsBackground = true;
    thread.Start();
  }

  public static void StartGame(string folder, string[] args) {
    UserData(folder);  // also an install built before the game was always portable
    string exe = GameExe(folder);
    var arguments = new StringBuilder();
    foreach (string arg in args) arguments.Append(arguments.Length > 0 ? " " : "").Append(Quote(arg));
    Process.Start(new ProcessStartInfo(exe, arguments.ToString()) {
      WorkingDirectory = Path.GetDirectoryName(exe), UseShellExecute = false });
  }

  // One command-line argument, quoted the way the C runtime splits them again.
  static string Quote(string arg) {
    if (arg.Length > 0 && arg.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return arg;
    var s = new StringBuilder("\"");
    int slashes = 0;
    foreach (char c in arg) {
      if (c == '\\') { slashes++; continue; }
      s.Append('\\', c == '"' ? slashes * 2 + 1 : slashes).Append(c);
      slashes = 0;
    }
    return s.Append('\\', slashes * 2).Append('"').ToString();
  }

  // A copy of the game the builder takes: a disc image (.iso), or an extracted disc (its folder
  // with default.xex, or that default.xex). An extracted disc is the whole disc: a folder with only
  // default.xex (like source\game-dump\files, the build's own copy) has none of the game's data, and
  // the game started from it crashes at once (issue #23).
  public static bool IsGameCopy(string path) {
    if (string.IsNullOrEmpty(path)) return false;
    try {
      if (Path.GetFileName(path).Equals("default.xex", StringComparison.OrdinalIgnoreCase) && File.Exists(path)) {
        path = Path.GetDirectoryName(path);
      } else if (File.Exists(path)) {
        return true;
      }
      return Directory.Exists(path) && File.Exists(Path.Combine(path, "default.xex")) &&
             File.Exists(Path.Combine(path, "data0.big"));
    } catch (Exception) {
      return false;
    }
  }

  // default.xex without the rest of its disc next to it (see IsGameCopy).
  public static bool IsLoneXex(string path) {
    try {
      return Path.GetFileName(path).Equals("default.xex", StringComparison.OrdinalIgnoreCase) && File.Exists(path) &&
             !File.Exists(Path.Combine(Path.GetDirectoryName(path), "data0.big"));
    } catch (Exception) {
      return false;
    }
  }

  // The game's settings point at the copy it was built from: the setup suggests it again when
  // updating. Null if there is none.
  public static string IsoOf(string folder) {
    try {
      string toml = Path.Combine(GameDir(folder), "ssx.toml");
      if (!File.Exists(toml)) return null;
      foreach (string line in File.ReadAllLines(toml)) {
        Match m = Regex.Match(line, "^\\s*game_data_root\\s*=\\s*\"(.*)\"\\s*$");
        if (!m.Success) continue;
        // A TOML string escapes \ and ", but take a path written without escapes as well.
        string raw = m.Groups[1].Value, path = Regex.Replace(raw, "\\\\([\\\\\"])", "$1");
        if (!IsGameCopy(path)) path = raw;
        if (Directory.Exists(path)) path = Path.Combine(path, "default.xex");
        return (path.EndsWith(".iso", StringComparison.OrdinalIgnoreCase) ||
                Path.GetFileName(path).Equals("default.xex", StringComparison.OrdinalIgnoreCase)) && IsGameCopy(path)
                   ? path : null;
      }
    } catch (Exception) { }
    return null;
  }
}

// Theme: colours, DPI scaling and shape helpers shared by the custom controls.
static class Th {
  public static readonly Color Bg = Color.FromArgb(11, 18, 27);
  public static readonly Color Rail = Color.FromArgb(14, 23, 34);
  public static readonly Color Surface = Color.FromArgb(19, 30, 43);
  public static readonly Color Surface2 = Color.FromArgb(27, 41, 57);
  public static readonly Color Surface3 = Color.FromArgb(35, 52, 71);
  public static readonly Color Border = Color.FromArgb(40, 58, 78);
  public static readonly Color Accent = Color.FromArgb(92, 209, 245);
  public static readonly Color AccentHover = Color.FromArgb(132, 224, 250);
  public static readonly Color AccentDown = Color.FromArgb(62, 178, 222);
  public static readonly Color Ink = Color.FromArgb(6, 20, 30);
  public static readonly Color Text = Color.FromArgb(232, 242, 250);
  public static readonly Color Dim = Color.FromArgb(150, 172, 190);
  public static readonly Color Faint = Color.FromArgb(96, 118, 138);
  public static readonly Color Warn = Color.FromArgb(255, 196, 77);
  public static readonly Color Good = Color.FromArgb(110, 231, 168);
  public static readonly Color Bad = Color.FromArgb(255, 112, 112);
  public static readonly Color Caption = Color.FromArgb(14, 36, 52);

  public static readonly float Scale;
  static Th() {
    using (var g = Graphics.FromHwnd(IntPtr.Zero)) Scale = g.DpiX / 96f;
  }
  public static int D(float v) { return (int)Math.Round(v * Scale); }

  public static GraphicsPath Round(RectangleF r, float radius) {
    float d = Math.Min(radius * 2, Math.Min(r.Width, r.Height));
    var p = new GraphicsPath();
    if (d <= 0) { p.AddRectangle(r); return p; }
    p.AddArc(r.X, r.Y, d, d, 180, 90);
    p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
    p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
    p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
    p.CloseFigure();
    return p;
  }

  public static void Tick(Graphics g, Color c, float cx, float cy, float size, float width) {
    using (var pen = new Pen(c, width) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
      g.DrawLines(pen, new[] { new PointF(cx - size * 0.45f, cy + size * 0.02f),
                               new PointF(cx - size * 0.12f, cy + size * 0.33f),
                               new PointF(cx + size * 0.48f, cy - size * 0.32f) });
  }

  public static Font F(float pt, FontStyle style) { return new Font("Segoe UI", pt, style); }
  public static Font Semi(float pt) { return new Font("Segoe UI Semibold", pt); }

  public static Label Label(string text, Font font, Color color, int x, int y, int w, int h) {
    return new Label { Text = text, Font = font, ForeColor = color, BackColor = Bg,
                       Location = new Point(x, y), Size = new Size(w, h), UseMnemonic = false };
  }
}

class Painted : Control {
  public Painted() {
    SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
             ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
  }
}

// Header artwork: night gradient, peaks with snow caps and a board trail (original art).
class Hero : Painted {
  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.SmoothingMode = SmoothingMode.AntiAlias;
    float w = Width, h = Height;
    Func<float, float, PointF> P = (x, y) => new PointF(x * w, y * h);

    using (var bg = new LinearGradientBrush(new RectangleF(0, 0, w, h), Th.Caption, Th.Bg, 90f))
      g.FillRectangle(bg, 0, 0, w, h);
    using (var glow = new GraphicsPath()) {
      glow.AddEllipse(w * 0.52f, -h * 0.9f, w * 0.62f, h * 2.1f);
      using (var b = new PathGradientBrush(glow)) {
        b.CenterColor = Color.FromArgb(60, Th.Accent);
        b.SurroundColors = new[] { Color.FromArgb(0, Th.Accent) };
        g.FillPath(b, glow);
      }
    }
    var rnd = new Random(11);
    for (int i = 0; i < 46; i++) {
      float sx = w * (0.42f + 0.58f * (float)rnd.NextDouble()), sy = h * 0.55f * (float)rnd.NextDouble();
      float sz = Th.Scale * (rnd.Next(2) == 0 ? 1.2f : 2f);
      using (var b = new SolidBrush(Color.FromArgb(rnd.Next(50, 170), 220, 240, 255))) g.FillEllipse(b, sx, sy, sz, sz);
    }

    var back = new[] { P(.40f, 1.02f), P(.53f, .46f), P(.60f, .60f), P(.71f, .17f), P(.80f, .44f),
                       P(.88f, .30f), P(1.02f, .56f), P(1.02f, 1.02f) };
    using (var b = new LinearGradientBrush(new RectangleF(0, h * .17f, w, h * .85f),
                                           Color.FromArgb(34, 88, 116), Color.FromArgb(14, 30, 44), 90f))
      g.FillPolygon(b, back);
    var front = new[] { P(.49f, 1.02f), P(.64f, .52f), P(.70f, .64f), P(.83f, .25f), P(.95f, .62f),
                        P(1.02f, .48f), P(1.02f, 1.02f) };
    using (var b = new LinearGradientBrush(new RectangleF(0, h * .25f, w, h * .78f),
                                           Color.FromArgb(96, 206, 240), Color.FromArgb(16, 54, 78), 90f))
      g.FillPolygon(b, front);
    using (var snow = new SolidBrush(Color.FromArgb(240, 248, 253)))
    using (var dimSnow = new SolidBrush(Color.FromArgb(170, 220, 236, 245))) {
      Cap(g, dimSnow, P(.71f, .17f), P(.60f, .60f), P(.80f, .44f), .30f);
      Cap(g, dimSnow, P(.53f, .46f), P(.40f, 1.02f), P(.60f, .60f), .22f);
      Cap(g, snow, P(.83f, .25f), P(.70f, .64f), P(.95f, .62f), .34f);
      Cap(g, snow, P(.64f, .52f), P(.49f, 1.02f), P(.70f, .64f), .26f);
    }
    using (var pen = new Pen(Color.FromArgb(215, 245, 251, 255), Th.D(3)) { StartCap = LineCap.Round, EndCap = LineCap.Round })
      g.DrawBezier(pen, P(.57f, 1.0f), P(.70f, .72f), P(.79f, 1.04f), P(.98f, .74f));
    using (var line = new LinearGradientBrush(new RectangleF(0, h - 2, w, 2), Th.Accent, Color.FromArgb(0, Th.Accent), 0f))
      g.FillRectangle(line, 0, h - Th.D(2), w, Th.D(2));

    g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
    using (var title = new Font("Segoe UI Black", 32f, FontStyle.Italic))
    using (var pill = Th.Semi(9f))
    using (var sub = Th.F(10.5f, FontStyle.Regular))
    using (var white = new SolidBrush(Th.Text))
    using (var accent = new SolidBrush(Th.Accent))
    using (var dim = new SolidBrush(Th.Dim)) {
      float x = Th.D(36), y = Th.D(20);
      g.DrawString("SSX", title, white, x, y);
      SizeF ts = g.MeasureString("SSX", title);
      SizeF ps = g.MeasureString("PC PORT", pill);
      var pr = new RectangleF(x + ts.Width + Th.D(4), y + ts.Height / 2 - ps.Height / 2 - Th.D(3),
                              ps.Width + Th.D(14), ps.Height + Th.D(6));
      using (var path = Th.Round(pr, pr.Height / 2))
      using (var pen = new Pen(Th.Accent, Th.Scale * 1.4f)) g.DrawPath(pen, path);
      g.DrawString("PC PORT", pill, accent, pr.X + Th.D(7), pr.Y + Th.D(3));
      g.DrawString("Native Windows version, recompiled on your PC from your own Xbox 360 disc", sub, dim,
                   x + Th.D(3), y + ts.Height + Th.D(2));
    }
  }

  static PointF Lerp(PointF a, PointF b, float t) { return new PointF(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t); }

  static void Cap(Graphics g, Brush b, PointF peak, PointF left, PointF right, float f) {
    PointF l = Lerp(peak, left, f), r = Lerp(peak, right, f);
    g.FillPolygon(b, new[] { peak, r, Lerp(Lerp(l, r, .72f), peak, .12f), Lerp(Lerp(l, r, .5f), peak, -.08f),
                             Lerp(Lerp(l, r, .3f), peak, .28f), l });
  }
}

class StepRail : Painted {
  public string[] Items = new string[0];
  public int Current;

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Th.Rail);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    using (var pen = new Pen(Th.Border)) g.DrawLine(pen, Width - 1, 0, Width - 1, Height);
    using (var f = Th.Semi(8f)) TextRenderer.DrawText(g, "SETUP", f, new Point(Th.D(28), Th.D(28)), Th.Faint);

    float cx = Th.D(41), r = Th.D(13);
    for (int i = 0; i < Items.Length; i++) {
      float cy = Th.D(80) + i * Th.D(58);
      bool done = i < Current, cur = i == Current;
      if (i < Items.Length - 1)
        using (var pen = new Pen(done ? Color.FromArgb(150, Th.Accent) : Th.Border, Th.D(2)))
          g.DrawLine(pen, cx, cy + r + Th.D(4), cx, cy + Th.D(58) - r - Th.D(4));
      var circle = new RectangleF(cx - r, cy - r, 2 * r, 2 * r);
      if (done) {
        using (var b = new SolidBrush(Th.Accent)) g.FillEllipse(b, circle);
        Th.Tick(g, Th.Ink, cx, cy, r * 0.95f, Th.Scale * 2.2f);
      } else {
        using (var b = new SolidBrush(cur ? Color.FromArgb(40, Th.Accent) : Th.Rail)) g.FillEllipse(b, circle);
        using (var pen = new Pen(cur ? Th.Accent : Th.Border, Th.Scale * 1.8f)) g.DrawEllipse(pen, circle);
        using (var f = Th.Semi(9f))
          TextRenderer.DrawText(g, (i + 1).ToString(), f, Rectangle.Round(circle), cur ? Th.Accent : Th.Faint,
                                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
      }
      using (var f = cur ? Th.Semi(10.5f) : Th.F(10.5f, FontStyle.Regular))
        TextRenderer.DrawText(g, Items[i], f, new Rectangle(Th.D(66), (int)(cy - r), Width - Th.D(70), (int)(2 * r)),
                              cur ? Th.Text : done ? Th.Dim : Th.Faint, TextFormatFlags.VerticalCenter);
    }
  }
}

class FlatButton : Button {
  public bool Primary;
  bool hover, down;

  public FlatButton(string text, bool primary) {
    Text = text;
    Primary = primary;
    SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
    Cursor = Cursors.Hand;
    Font = Th.Semi(10f);
  }

  protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
  protected override void OnMouseLeave(EventArgs e) { hover = down = false; Invalidate(); base.OnMouseLeave(e); }
  protected override void OnMouseDown(MouseEventArgs e) { down = true; Invalidate(); base.OnMouseDown(e); }
  protected override void OnMouseUp(MouseEventArgs e) { down = false; Invalidate(); base.OnMouseUp(e); }
  protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Parent != null ? Parent.BackColor : Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    Color fill, border, text;
    if (!Enabled) {
      fill = Th.Surface; border = Th.Surface2; text = Th.Faint;
    } else if (Primary) {
      fill = down ? Th.AccentDown : hover ? Th.AccentHover : Th.Accent; border = fill; text = Th.Ink;
    } else {
      fill = down ? Th.Surface : hover ? Th.Surface3 : Th.Surface2; border = hover ? Th.Accent : Th.Border; text = Th.Text;
    }
    var rect = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
    using (var path = Th.Round(rect, Th.D(7)))
    using (var b = new SolidBrush(fill))
    using (var pen = new Pen(border, Th.Scale)) {
      g.FillPath(b, path);
      g.DrawPath(pen, path);
    }
    if (Focused && ShowFocusCues) {
      var ring = RectangleF.Inflate(rect, -Th.D(3), -Th.D(3));
      using (var path = Th.Round(ring, Th.D(5)))
      using (var pen = new Pen(Primary ? Color.FromArgb(120, Th.Ink) : Color.FromArgb(150, Th.Accent), Th.Scale))
        g.DrawPath(pen, path);
    }
    TextRenderer.DrawText(g, Text, Font, ClientRectangle, text,
                          TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
  }
}

class Check : CheckBox {
  bool hover;

  public Check(string text) {
    Text = text;
    SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
    Cursor = Cursors.Hand;
    Font = Th.F(10.5f, FontStyle.Regular);
  }

  protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
  protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
  protected override void OnCheckedChanged(EventArgs e) { Invalidate(); base.OnCheckedChanged(e); }

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Parent != null ? Parent.BackColor : Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    float s = Th.D(20);
    var box = new RectangleF(1, (Height - s) / 2, s, s);
    using (var path = Th.Round(box, Th.D(5))) {
      using (var b = new SolidBrush(Checked ? Th.Accent : Th.Surface)) g.FillPath(b, path);
      using (var pen = new Pen(Checked || hover || Focused ? Th.Accent : Th.Border, Th.Scale * 1.4f)) g.DrawPath(pen, path);
    }
    if (Checked) Th.Tick(g, Th.Ink, box.X + s / 2, box.Y + s / 2, s * 0.62f, Th.Scale * 2.2f);
    int tx = (int)s + Th.D(12);
    TextRenderer.DrawText(g, Text, Font, new Rectangle(tx, 0, Width - tx, Height), Th.Text,
                          TextFormatFlags.VerticalCenter | TextFormatFlags.WordBreak);
  }
}

// Rounded input box hosting a borderless TextBox.
class Field : Panel {
  public readonly TextBox Box = new TextBox();

  public Field() {
    DoubleBuffered = true;
    BackColor = Th.Surface;
    Box.BorderStyle = BorderStyle.None;
    Box.BackColor = Th.Surface;
    Box.ForeColor = Th.Text;
    Box.Font = Th.F(10.5f, FontStyle.Regular);
    Controls.Add(Box);
    Box.GotFocus += delegate { Invalidate(); };
    Box.LostFocus += delegate { Invalidate(); };
    Cursor = Cursors.IBeam;
  }

  protected override void OnClick(EventArgs e) { Box.Focus(); base.OnClick(e); }

  protected override void OnLayout(LayoutEventArgs e) {
    base.OnLayout(e);
    int pad = Th.D(12);
    Box.Location = new Point(pad, (Height - Box.Height) / 2);
    Box.Width = Width - 2 * pad;
  }

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Parent != null ? Parent.BackColor : Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    using (var path = Th.Round(new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f), Th.D(7)))
    using (var b = new SolidBrush(Th.Surface))
    using (var pen = new Pen(Box.Focused ? Th.Accent : Th.Border, Th.Scale * 1.2f)) {
      g.FillPath(b, path);
      g.DrawPath(pen, path);
    }
  }
}

// Rounded panel with a coloured edge, a title and wrapped body text.
class Card : Painted {
  public string Title = "", Body = "";
  public Color Tone = Th.Accent;
  readonly Font titleFont = Th.Semi(10.5f), bodyFont = Th.F(10f, FontStyle.Regular);

  public int FitHeight(int width) {
    int inner = width - Th.D(44);
    int t = TextRenderer.MeasureText(Title, titleFont, new Size(inner, 1000), TextFormatFlags.WordBreak).Height;
    int b = TextRenderer.MeasureText(Body, bodyFont, new Size(inner, 1000), TextFormatFlags.WordBreak).Height;
    return Th.D(18) + t + Th.D(6) + b + Th.D(18);
  }

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Parent != null ? Parent.BackColor : Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    using (var path = Th.Round(new RectangleF(0, 0, Width - 1, Height - 1), Th.D(10))) {
      using (var b = new SolidBrush(Th.Surface)) g.FillPath(b, path);
      g.SetClip(path);
      using (var b = new SolidBrush(Tone)) g.FillRectangle(b, 0, 0, Th.D(4), Height);
      g.ResetClip();
      using (var pen = new Pen(Color.FromArgb(70, Tone), Th.Scale)) g.DrawPath(pen, path);
    }
    int x = Th.D(24), inner = Width - Th.D(44);
    int th = TextRenderer.MeasureText(Title, titleFont, new Size(inner, 1000), TextFormatFlags.WordBreak).Height;
    TextRenderer.DrawText(g, Title, titleFont, new Rectangle(x, Th.D(18), inner, th), Tone, TextFormatFlags.WordBreak);
    TextRenderer.DrawText(g, Body, bodyFont, new Rectangle(x, Th.D(24) + th, inner, Height), Th.Text,
                          TextFormatFlags.WordBreak);
  }
}

class Bullets : Painted {
  public string[] Items = new string[0];
  readonly Font font = Th.F(10.5f, FontStyle.Regular);

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    for (int i = 0; i < Items.Length; i++) {
      int y = i * Th.D(30);
      float r = Th.D(9), cy = y + Th.D(15);
      using (var b = new SolidBrush(Color.FromArgb(45, Th.Accent))) g.FillEllipse(b, 0, cy - r, 2 * r, 2 * r);
      Th.Tick(g, Th.Accent, r, cy, r * 0.95f, Th.Scale * 1.8f);
      TextRenderer.DrawText(g, Items[i], font, new Rectangle(Th.D(30), y, Width - Th.D(30), Th.D(30)), Th.Text,
                            TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
    }
  }
}

class StageList : Painted {
  public string[] Items = new string[0];
  public int Current;
  public bool Failed;
  public int Pulse;

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    for (int i = 0; i < Items.Length; i++) {
      int y = i * Th.D(34);
      float r = Th.D(9), cx = r + 1, cy = y + Th.D(17);
      var circle = new RectangleF(cx - r, cy - r, 2 * r, 2 * r);
      bool done = i < Current, cur = i == Current;
      Color textColor = Th.Faint;
      if (done) {
        using (var b = new SolidBrush(Th.Accent)) g.FillEllipse(b, circle);
        Th.Tick(g, Th.Ink, cx, cy, r, Th.Scale * 1.9f);
        textColor = Th.Dim;
      } else if (cur && Failed) {
        using (var b = new SolidBrush(Th.Bad)) g.FillEllipse(b, circle);
        using (var pen = new Pen(Th.Ink, Th.Scale * 1.9f)) {
          float k = r * 0.38f;
          g.DrawLine(pen, cx - k, cy - k, cx + k, cy + k);
          g.DrawLine(pen, cx - k, cy + k, cx + k, cy - k);
        }
        textColor = Th.Bad;
      } else if (cur) {
        using (var pen = new Pen(Th.Accent, Th.Scale * 1.8f)) g.DrawEllipse(pen, circle);
        int a = 110 + (int)(110 * Math.Sin(Pulse * 0.25));
        float k = r * 0.48f;
        using (var b = new SolidBrush(Color.FromArgb(a, Th.Accent))) g.FillEllipse(b, cx - k, cy - k, 2 * k, 2 * k);
        textColor = Th.Text;
      } else {
        using (var pen = new Pen(Th.Border, Th.Scale * 1.6f)) g.DrawEllipse(pen, circle);
      }
      using (var f = cur ? Th.Semi(10.5f) : Th.F(10.5f, FontStyle.Regular))
        TextRenderer.DrawText(g, Items[i], f, new Rectangle(Th.D(32), y, Width - Th.D(32), Th.D(34)), textColor,
                              TextFormatFlags.VerticalCenter);
    }
  }
}

class Bar : Painted {
  public float Value;
  public bool Failed;
  public int Phase;

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    float h = Height - 1;
    using (var track = Th.Round(new RectangleF(0, 0, Width - 1, h), h / 2))
    using (var b = new SolidBrush(Th.Surface2)) g.FillPath(b, track);
    float fw = Math.Max(h, (Width - 1) * Math.Min(1f, Value));
    if (Value <= 0) return;
    var fill = new RectangleF(0, 0, fw, h);
    using (var path = Th.Round(fill, h / 2)) {
      if (Failed) {
        using (var b = new SolidBrush(Th.Bad)) g.FillPath(b, path);
        return;
      }
      using (var b = new LinearGradientBrush(new RectangleF(0, 0, fw + 1, h), Color.FromArgb(38, 140, 196), Th.Accent, 0f))
        g.FillPath(b, path);
      if (Value < 1f) {
        float band = Th.D(140), x = (Phase * Th.D(7)) % (fw + band) - band;
        g.SetClip(path);
        using (var b = new LinearGradientBrush(new RectangleF(x, 0, band, h), Color.Transparent, Color.Transparent, 0f)) {
          var blend = new ColorBlend(3);
          blend.Colors = new[] { Color.FromArgb(0, 255, 255, 255), Color.FromArgb(110, 255, 255, 255), Color.FromArgb(0, 255, 255, 255) };
          blend.Positions = new[] { 0f, 0.5f, 1f };
          b.InterpolationColors = blend;
          g.FillRectangle(b, x, 0, band, h);
        }
        g.ResetClip();
      }
    }
  }
}

class Badge : Painted {
  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    var r = new RectangleF(1, 1, Width - 3, Height - 3);
    using (var b = new LinearGradientBrush(r, Th.AccentHover, Color.FromArgb(38, 140, 196), 60f)) g.FillEllipse(b, r);
    Th.Tick(g, Th.Ink, Width / 2f, Height / 2f, Width * 0.42f, Th.Scale * 3.2f);
  }
}

class InstallerForm : Form {
  const string RepoUrl = "https://github.com/AmarilloNL/ssx-recomp";

  [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

  internal static readonly string[] Stages = {
    "Unpack the port's source code", "Check your ISO", "Install the build tools",
    "Download the recompiler SDK", "Recompile the Xbox 360 code", "Build the PC version" };
  // Where each stage starts on the overall bar (the last entry is the end).
  internal static readonly float[] StageStart = { 0f, .02f, .04f, .10f, .18f, .36f, 1f };
  internal static readonly Regex NinjaStep = new Regex(@"^\[(\d+)/(\d+)\]");
  static readonly Regex ToolLine =
      new Regex(@"^\s+installing (.+?)(?: \(.*\))?(?: with winget| from its official download)?(?:\.\.\.|\.)?$");

  // The build tool a build-ssx.ps1 line says it's installing (CMake, Ninja, LLVM, Visual Studio's
  // Build Tools), or null: it can take many minutes and Windows asks for permission (UAC), so the
  // build tools step says which one.
  internal static string ToolOf(string text) {
    Match m = ToolLine.Match(text);
    if (!m.Success) return null;
    string name = m.Groups[1].Value;
    if (name == "Kitware.CMake" || name == "cmake") return "CMake";
    if (name == "Ninja-build.Ninja" || name == "ninja") return "Ninja";
    if (name == "LLVM.LLVM") return "LLVM";
    return name;
  }

  readonly StepRail rail = new StepRail();
  readonly Panel[] pages = new Panel[4];
  readonly FlatButton backButton = new FlatButton("Back", false);
  readonly FlatButton nextButton = new FlatButton("Next", true);

  readonly Field isoField = new Field();
  readonly Field folderField = new Field();
  Label isoHint, folderHint;
  // DLC packages from the player's own console, copied to the userdata\dlc folder next to the game
  // when the install starts; the game installs them from there on its next start.
  readonly List<string> dlcFiles = new List<string>();
  // The game's title update package, if picked: build-ssx.ps1 builds the game from it (and keeps
  // doing so on later updates).
  string titleUpdateFile;
  Label dlcHint;

  Label installTitle, installStatus, percentLabel, elapsedLabel;
  LinkLabel detailsLink;
  readonly Bar bar = new Bar();
  readonly StageList stageList = new StageList();
  readonly TextBox logBox = new TextBox();

  Label doneSub;
  readonly Check shortcutCheck = new Check("Create a desktop shortcut");
  // The SSX folder: where this exe is (Install.Folder).
  readonly string folder;

  readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
  readonly object sync = new object();
  readonly StringBuilder pendingLog = new StringBuilder();
  int stage;
  float stageFrac;
  // The build tool being installed in the build tools step (ToolOf), for the status line.
  string tool;
  Stopwatch clock = new Stopwatch();
  Process buildProcess;
  bool running, failed, cancelled, shortcutMade;
  int page;

  // folder: the SSX folder. With the game built there the window opens on the Play page, or with
  // setup on the Your game page (rebuild: and starts building at once); otherwise on Welcome.
  public InstallerForm(string folder, bool setup, bool rebuild) {
    this.folder = folder;
    Text = "SSX PC Port";
    AutoScaleMode = AutoScaleMode.None;
    ClientSize = new Size(Th.D(900), Th.D(690));
    FormBorderStyle = FormBorderStyle.FixedSingle;
    MaximizeBox = false;
    StartPosition = FormStartPosition.CenterScreen;
    BackColor = Th.Bg;
    ForeColor = Th.Text;
    Font = Th.F(10f, FontStyle.Regular);
    Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);  // ssx.ico, embedded at compile time

    int heroH = Th.D(140), railW = Th.D(220), footH = Th.D(64);
    int contentH = ClientSize.Height - heroH - footH;
    Controls.Add(new Hero { Bounds = new Rectangle(0, 0, ClientSize.Width, heroH) });
    rail.Items = new[] { "Welcome", "Your game", "Install", "Play" };
    rail.Bounds = new Rectangle(0, heroH, railW, contentH);
    Controls.Add(rail);

    for (int i = 0; i < pages.Length; i++) {
      pages[i] = new Panel { Bounds = new Rectangle(railW, heroH, ClientSize.Width - railW, contentH),
                             BackColor = Th.Bg, Visible = false };
      Controls.Add(pages[i]);
    }
    BuildWelcome(pages[0]);
    BuildGame(pages[1]);
    BuildInstall(pages[2]);
    BuildDone(pages[3]);
    BuildFooter(heroH + contentH, footH);
    string iso = Install.IsoOf(folder);
    if (iso != null) isoField.Box.Text = iso;

    timer.Interval = 80;
    timer.Tick += delegate { OnTick(); };
    if (!Install.Built(folder)) {
      ShowPage(0);
    } else if (setup) {
      ShowPage(1);
      // After an update: rebuild right away from the ISO the game was built from.
      if (rebuild && iso != null) Shown += delegate { if (Valid()) StartBuild(); };
    } else {
      ShowPlayStatus();
      ShowPage(3);
    }
  }

  void ShowPlayStatus() {
    doneSub.Text = Install.NeedsRebuild(folder)
        ? "Version " + Install.AppVersion + " isn't built yet: click Add DLC / rebuild, then Rebuild."
        : "Version " + Install.AppVersion + ", in " + folder;
  }

  protected override void OnHandleCreated(EventArgs e) {
    base.OnHandleCreated(e);
    try {
      int on = 1;
      DwmSetWindowAttribute(Handle, 20, ref on, 4);  // dark title bar
      int color = Th.Caption.R | (Th.Caption.G << 8) | (Th.Caption.B << 16);
      DwmSetWindowAttribute(Handle, 35, ref color, 4);  // caption colour (Windows 11)
    } catch (Exception) { }
  }

  int PageWidth { get { return pages[0].Width - 2 * Th.D(40); } }

  void BuildWelcome(Panel p) {
    int x = Th.D(40), w = PageWidth;
    p.Controls.Add(Th.Label("Welcome", Th.Semi(17f), Th.Text, x, Th.D(26), w, Th.D(40)));
    p.Controls.Add(Th.Label("This sets up a native Windows version of SSX (2012). The game's Xbox 360 code is " +
                            "recompiled to run directly on your PC: no emulator involved.",
                            Th.F(10.5f, FontStyle.Regular), Th.Dim, x, Th.D(70), w, Th.D(46)));
    p.Controls.Add(new Bullets {
      Items = new[] { "Up to 300 fps, or match your monitor's refresh rate",
                      "Sharper rendering, 2x resolution scale and AMD FSR upscaling",
                      "Settings menu in game: F1, or Back + Start on a controller",
                      "Keyboard and controller, DLC from your console, and your own music" },
      Bounds = new Rectangle(x, Th.D(122), w, Th.D(120)) });
    var card = new Card {
      Title = "You need your own copy of the game",
      Body = "This installer contains no game code, graphics, music or other EA content, and downloads none. " +
             "You need SSX (USA) for Xbox 360, dumped to an ISO from your own disc.",
      Tone = Th.Warn };
    card.Bounds = new Rectangle(x, Th.D(254), w, card.FitHeight(w));
    p.Controls.Add(card);
    // The setup installs Microsoft's build tools when they're missing: say whose license that accepts.
    const string tools = "A PC without Microsoft's Visual Studio Build Tools gets them installed: by installing " +
                         "SSX you accept the Visual Studio license terms.";
    var license = new LinkLabel { Text = tools, Font = Th.F(9.5f, FontStyle.Regular), BackColor = Th.Bg,
                                  ForeColor = Th.Faint, LinkColor = Th.Accent, ActiveLinkColor = Th.AccentHover,
                                  Bounds = new Rectangle(x, card.Bottom + Th.D(14), w, Th.D(40)) };
    license.LinkArea = new LinkArea(tools.IndexOf("Visual Studio license terms"), "Visual Studio license terms".Length);
    license.LinkClicked += delegate { Process.Start("https://visualstudio.microsoft.com/license-terms/"); };
    p.Controls.Add(license);
  }

  void BuildGame(Panel p) {
    int x = Th.D(40), w = PageWidth;
    p.Controls.Add(Th.Label("Your game", Th.Semi(17f), Th.Text, x, Th.D(26), w, Th.D(40)));
    p.Controls.Add(Th.Label("Pick the ISO you made from your own SSX disc. SSX is built in the folder this program is in.",
                            Th.F(10.5f, FontStyle.Regular), Th.Dim, x, Th.D(70), w, Th.D(24)));

    isoHint = AddPathRow(p, "Your SSX: disc image (.iso) or extracted default.xex", isoField, Th.D(104), "Browse...", BrowseIso);
    // The folder is where ssx-recomp.exe is: to use another, move ssx-recomp.exe there.
    folderHint = AddPathRow(p, "SSX folder", folderField, Th.D(198), "Open",
                            delegate { Process.Start(new ProcessStartInfo("explorer.exe", "\"" + folder + "\"")); });
    folderField.Box.Text = folder;
    folderField.Box.ReadOnly = true;
    folderField.Cursor = folderField.Box.Cursor = Cursors.Default;

    int dlcY = Th.D(292), addW = Th.D(112);
    string caption = "DLC and title update (optional)";
    var captionFont = Th.Semi(10f);
    int captionW = TextRenderer.MeasureText(caption, captionFont).Width;
    p.Controls.Add(Th.Label(caption, captionFont, Th.Text, x, dlcY + Th.D(8), captionW, Th.D(24)));
    var where = new LinkLabel { Text = "Where do I find these?", Font = Th.F(9.5f, FontStyle.Regular), BackColor = Th.Bg,
                                LinkColor = Th.Accent, ActiveLinkColor = Th.Accent, AutoSize = true,
                                Location = new Point(x + captionW + Th.D(8), dlcY + Th.D(9)) };
    where.LinkClicked += delegate { Process.Start(RepoUrl + "#adding-dlc"); };
    p.Controls.Add(where);
    var addDlc = new FlatButton("Add files...", false) { Bounds = new Rectangle(x + w - addW, dlcY, addW, Th.D(36)) };
    addDlc.Click += BrowseDlc;
    p.Controls.Add(addDlc);
    // Three lines: the help text, or up to six package names and what happens with them.
    dlcHint = Th.Label("", Th.F(9.5f, FontStyle.Regular), Th.Faint, x + Th.D(2), dlcY + Th.D(40), w, Th.D(60));
    dlcHint.AutoEllipsis = true;
    p.Controls.Add(dlcHint);
    SetHint(dlcHint, DlcHelp, Th.Faint);

    UpdateHints();
  }

  Label AddPathRow(Panel p, string caption, Field field, int y, string button, EventHandler browse) {
    int x = Th.D(40), w = PageWidth, browseW = Th.D(112), fieldH = Th.D(40);
    p.Controls.Add(Th.Label(caption, Th.Semi(10f), Th.Text, x, y, w, Th.D(24)));
    field.Bounds = new Rectangle(x, y + Th.D(26), w - browseW - Th.D(10), fieldH);
    field.Box.TextChanged += delegate { UpdateHints(); UpdateFooter(); };
    p.Controls.Add(field);
    var browseButton = new FlatButton(button, false) {
      Bounds = new Rectangle(x + w - browseW, y + Th.D(26), browseW, fieldH) };
    browseButton.Click += browse;
    p.Controls.Add(browseButton);
    var hint = Th.Label("", Th.F(9.5f, FontStyle.Regular), Th.Faint, x + Th.D(2), y + Th.D(70), w, Th.D(22));
    p.Controls.Add(hint);
    return hint;
  }

  void BuildInstall(Panel p) {
    int x = Th.D(40), w = PageWidth;
    installTitle = Th.Label("Installing SSX", Th.Semi(17f), Th.Text, x, Th.D(26), w - Th.D(130), Th.D(40));
    percentLabel = Th.Label("0%", Th.Semi(17f), Th.Accent, x + w - Th.D(130), Th.D(26), Th.D(130), Th.D(40));
    percentLabel.TextAlign = ContentAlignment.TopRight;
    installStatus = Th.Label("", Th.F(10.5f, FontStyle.Regular), Th.Dim, x, Th.D(70), w, Th.D(24));
    installStatus.AutoEllipsis = true;
    bar.Bounds = new Rectangle(x, Th.D(104), w, Th.D(10));
    elapsedLabel = Th.Label("", Th.F(9.5f, FontStyle.Regular), Th.Faint, x, Th.D(122), w / 2, Th.D(22));
    detailsLink = new LinkLabel { Text = "Show details", Font = Th.F(9.5f, FontStyle.Regular), BackColor = Th.Bg,
                                  LinkColor = Th.Accent, ActiveLinkColor = Th.AccentHover,
                                  LinkBehavior = LinkBehavior.HoverUnderline, TextAlign = ContentAlignment.TopRight,
                                  Bounds = new Rectangle(x + w / 2, Th.D(122), w / 2, Th.D(22)) };
    detailsLink.LinkClicked += delegate { ShowLog(!logBox.Visible); };

    stageList.Items = Stages;
    stageList.Bounds = new Rectangle(x, Th.D(156), w, Stages.Length * Th.D(34));
    logBox.Multiline = true;
    logBox.ReadOnly = true;
    logBox.ScrollBars = ScrollBars.Vertical;
    logBox.BackColor = Th.Surface;
    logBox.ForeColor = Th.Dim;
    logBox.BorderStyle = BorderStyle.None;
    logBox.Font = new Font("Consolas", 9f);
    logBox.Bounds = new Rectangle(x, Th.D(154), w, p.Height - Th.D(166));
    logBox.Visible = false;
    p.Controls.AddRange(new Control[] { installTitle, percentLabel, installStatus, bar, elapsedLabel, detailsLink,
                                        stageList, logBox });
  }

  void BuildDone(Panel p) {
    int x = Th.D(40), w = PageWidth, badge = Th.D(56);
    p.Controls.Add(new Badge { Bounds = new Rectangle(x, Th.D(28), badge, badge) });
    p.Controls.Add(Th.Label("SSX is ready", Th.Semi(17f), Th.Text, x + badge + Th.D(18), Th.D(26), w, Th.D(40)));
    doneSub = Th.Label("", Th.F(10f, FontStyle.Regular), Th.Dim, x + badge + Th.D(20), Th.D(64),
                       w - badge - Th.D(20), Th.D(24));
    doneSub.AutoEllipsis = true;
    p.Controls.Add(doneSub);

    var tips = new Card {
      Title = "Good to know",
      Body = "F1 (or Back + Start) opens the settings: frame rate, resolution, FSR, controls.\r\n" +
             "Keep your ISO (or extracted disc) where it is: the game reads its data from it.\r\n" +
             "Everything stays in this folder: saves, settings, DLC and music (in source\\dist\\SSX\\userdata). " +
             "DLC you added is installed when the game starts.\r\n" +
             "Check for updates gets the latest ssx-recomp.exe and rebuilds SSX with it. Building needs no " +
             "download of the port: this program carries its source.",
      Tone = Th.Accent };
    tips.Bounds = new Rectangle(x, Th.D(112), w, tips.FitHeight(w));
    p.Controls.Add(tips);

    int y = tips.Bottom + Th.D(18);
    shortcutCheck.Bounds = new Rectangle(x, y, w, Th.D(30));
    p.Controls.Add(shortcutCheck);

    var play = new FlatButton("Play SSX", true) { Bounds = new Rectangle(x, y + Th.D(46), Th.D(160), Th.D(44)) };
    play.Font = Th.Semi(11f);
    play.Click += delegate {
      MakeShortcut();
      try {
        Play();
        Close();
      } catch (Exception ex) {
        MessageBox.Show(this, "SSX couldn't be started: " + ex.Message, "SSX", MessageBoxButtons.OK, MessageBoxIcon.Warning);
      }
    };
    var update = new FlatButton("Check for updates", false) {
      Bounds = new Rectangle(x + Th.D(172), y + Th.D(46), Th.D(180), Th.D(44)) };
    update.Click += delegate { MakeShortcut(); CheckForUpdates(update); };
    var dlc = new FlatButton("Add DLC / rebuild", false) {
      Bounds = new Rectangle(x + Th.D(364), y + Th.D(46), Th.D(170), Th.D(44)) };
    dlc.Click += delegate { MakeShortcut(); ShowPage(1); };
    var open = new LinkLabel { Text = "Open the SSX folder", Font = Th.F(9.5f, FontStyle.Regular), BackColor = Th.Bg,
                               LinkColor = Th.Accent, ActiveLinkColor = Th.AccentHover,
                               LinkBehavior = LinkBehavior.HoverUnderline, AutoSize = true,
                               Location = new Point(x, y + Th.D(104)) };
    open.LinkClicked += delegate {
      Process.Start(new ProcessStartInfo("explorer.exe", "\"" + InstallFolder() + "\""));
    };
    p.Controls.Add(play);
    p.Controls.Add(update);
    p.Controls.Add(dlc);
    p.Controls.Add(open);
  }

  // ---- Updates ----------------------------------------------------------------------------

  // The only time the port goes online: asks GitHub for the latest release and, when it's newer
  // and the player agrees, swaps this exe for that release's ssx-recomp.exe, which then rebuilds.
  void CheckForUpdates(FlatButton button) {
    button.Enabled = false;
    doneSub.Text = "Checking for updates...";
    RunInBackground(delegate {
      string url;
      string tag = Install.LatestRelease(out url);
      Ui(delegate { button.Enabled = true; OnLatestRelease(tag, url, button); });
    }, delegate(Exception ex) {
      button.Enabled = true;
      doneSub.Text = "Couldn't check for updates: " + ex.Message;
    });
  }

  void OnLatestRelease(string tag, string url, FlatButton button) {
    if (!Install.IsNewer(tag)) {
      doneSub.Text = "SSX is up to date (version " + Install.AppVersion + ").";
      return;
    }
    if (url == null) {
      doneSub.Text = "SSX " + tag + " is out, but its release has no ssx-recomp.exe to update to.";
      if (MessageBox.Show(this, "SSX " + tag + " is out, but its release has no ssx-recomp.exe to update to.\n\n" +
                          "Open the release page?", "SSX", MessageBoxButtons.YesNo, MessageBoxIcon.Information) ==
          DialogResult.Yes) Process.Start(RepoUrl + "/releases/latest");
      return;
    }
    if (MessageBox.Show(this, "SSX " + tag + " is available (you have " + Install.AppVersion + ").\n\n" +
                        "Update now? This downloads the new ssx-recomp.exe and rebuilds SSX with it, which takes " +
                        "a few minutes. Your saves and settings stay.", "SSX", MessageBoxButtons.YesNo,
                        MessageBoxIcon.Question) != DialogResult.Yes) {
      doneSub.Text = "SSX " + tag + " is available.";
      return;
    }
    button.Enabled = false;
    doneSub.Text = "Downloading SSX " + tag + "...";
    RunInBackground(delegate {
      Install.Replace(Install.DownloadUpdate(url, tag), Install.Self);
      Ui(delegate {
        Process.Start(new ProcessStartInfo(Install.Self, "--rebuild") { WorkingDirectory = folder });
        Close();
      });
    }, delegate(Exception ex) {
      button.Enabled = true;
      doneSub.Text = "The update failed: " + ex.Message;
    });
  }

  // Runs work on a background thread; a failure goes to onError on the UI thread.
  void RunInBackground(Action work, Action<Exception> onError) {
    var thread = new Thread(delegate() {
      try { work(); } catch (Exception ex) { Ui(delegate { onError(ex); }); }
    });
    thread.IsBackground = true;
    thread.Start();
  }

  void Ui(Action action) { if (!IsDisposed) BeginInvoke(action); }

  void BuildFooter(int y, int h) {
    var foot = new Panel { Bounds = new Rectangle(0, y, ClientSize.Width, h), BackColor = Th.Rail };
    foot.Paint += delegate(object s, PaintEventArgs e) {
      using (var pen = new Pen(Th.Border)) e.Graphics.DrawLine(pen, 0, 0, foot.Width, 0);
    };
    var link = new LinkLabel { Text = "github.com/AmarilloNL/ssx-recomp", Font = Th.F(9.5f, FontStyle.Regular),
                               BackColor = Th.Rail, LinkColor = Th.Faint, ActiveLinkColor = Th.Accent,
                               LinkBehavior = LinkBehavior.HoverUnderline, AutoSize = true,
                               Location = new Point(Th.D(28), (h - Th.D(18)) / 2) };
    link.LinkClicked += delegate { Process.Start(RepoUrl); };
    int bw = Th.D(124), bh = Th.D(40), by = (h - bh) / 2;
    nextButton.Bounds = new Rectangle(ClientSize.Width - Th.D(28) - bw, by, bw, bh);
    backButton.Bounds = new Rectangle(nextButton.Left - Th.D(12) - Th.D(104), by, Th.D(104), bh);
    nextButton.Click += delegate { Next(); };
    backButton.Click += delegate { Back(); };
    foot.Controls.AddRange(new Control[] { link, backButton, nextButton });
    Controls.Add(foot);
    AcceptButton = nextButton;
  }

  // ---- Navigation -------------------------------------------------------------------------

  void ShowPage(int p) {
    page = p;
    if (p == 1 && dlcFiles.Count == 0 && titleUpdateFile == null) ShowAddedHint();
    for (int i = 0; i < pages.Length; i++) pages[i].Visible = i == p;
    rail.Current = p == 3 ? 4 : p;
    rail.Invalidate();
    UpdateFooter();
    if (p == 1 && isoField.Box.Text.Length == 0) isoField.Box.Focus(); else nextButton.Focus();
  }

  void UpdateFooter() {
    backButton.Visible = page == 1 || (page == 2 && failed);
    nextButton.Enabled = true;
    nextButton.Primary = true;
    switch (page) {
      case 0: nextButton.Text = "Next"; break;
      case 1:
        nextButton.Text = !Install.Built(folder) ? "Install" : DlcOnly() ? "Add DLC" : "Rebuild";
        nextButton.Enabled = Valid();
        break;
      case 2:
        nextButton.Text = running ? "Cancel" : "Retry";
        nextButton.Primary = !running;
        break;
      case 3: nextButton.Text = "Close"; nextButton.Primary = false; break;
    }
    nextButton.Invalidate();
  }

  bool Valid() {
    return DlcOnly() || Install.IsGameCopy(isoField.Box.Text.Trim());
  }

  // Only DLC picked for a game that is built and up to date: the DLC is copied for the game to
  // install on its next start, no rebuild. (The title update changes the game's code: rebuild.)
  bool DlcOnly() {
    return dlcFiles.Count > 0 && titleUpdateFile == null && Install.Built(folder) && !Install.NeedsRebuild(folder);
  }

  void AddDlcOnly() {
    var problems = new List<string>();
    CopyDlc(dlcFiles.ToArray(), Install.UserData(folder), delegate(string line) {
      if (line.StartsWith("DLC: could not")) problems.Add(line);
    });
    doneSub.Text = problems.Count > 0 ? problems[0] : "DLC added: the game installs it when it starts.";
    dlcFiles.Clear();
    ShowPage(3);
  }

  const string DlcHelp = "Copied off your own Xbox 360 with a tool like FATXplorer or Velocity. In " +
                         "Content\\0000000000000000\\4541096D, the DLC (long-named files) is in 00000002 and the title " +
                         "update in 000B0000 (if there are two, the larger one). Add them once: later updates keep them.";

  // With nothing picked: what this SSX folder already has (issue #18: no need to add it again), or the help text.
  void ShowAddedHint() {
    List<string> added = Added(folder);
    if (added.Count == 0) SetHint(dlcHint, DlcHelp, Th.Faint);
    else SetHint(dlcHint, "Already added: " + string.Join(", ", added.ToArray()) +
                          ". They're kept for every rebuild: add files only for something new.", Th.Good);
  }

  // The title update and DLC this SSX folder keeps for every rebuild: the title update in
  // source\game-dump (build-ssx.ps1), the DLC the game installed (the .header files the SDK's
  // ContentManager writes: XCONTENT_DATA, display name in UTF-16 big-endian at offset 8) or will
  // install on its next start (files waiting in userdata\dlc, src/ssx_dlc.cpp).
  static List<string> Added(string folder) {
    var added = new List<string>();
    string dump = Path.Combine(folder, "source\\game-dump");
    if (File.Exists(Path.Combine(dump, "files\\default.xexp")) && File.Exists(Path.Combine(dump, "update\\patch0.big"))) {
      added.Add("Title update");
    }
    var dlc = new SortedDictionary<string, string>(StringComparer.OrdinalIgnoreCase);  // file name -> display name
    string userData = Path.Combine(Install.GameDir(folder), "userdata");
    try {
      string headers = Path.Combine(userData, "0000000000000000\\4541096D\\Headers\\00000002");
      if (Directory.Exists(headers)) {
        foreach (string file in Directory.GetFiles(headers, "*.header")) {
          byte[] h = File.ReadAllBytes(file);
          if (h.Length < 8 + 256) continue;
          string name = Encoding.BigEndianUnicode.GetString(h, 8, 256);
          int end = name.IndexOf('\0');
          if (end >= 0) name = name.Substring(0, end);
          if (name.Length > 0) dlc[Path.GetFileNameWithoutExtension(file)] = name;
        }
      }
      string waiting = Path.Combine(userData, "dlc");
      if (Directory.Exists(waiting)) {
        foreach (string file in Directory.GetFiles(waiting)) {
          if (dlc.ContainsKey(Path.GetFileName(file))) continue;
          bool isUpdate;
          string problem, name = ReadPackageName(file, out isUpdate, out problem);
          if (name != null && !isUpdate) dlc[Path.GetFileName(file)] = name;
        }
      }
    } catch (Exception) { }  // only a hint: an unreadable folder shows what was found so far
    foreach (string name in dlc.Values) added.Add(name.Replace("SSX™ ", ""));
    return added;
  }

  void Next() {
    switch (page) {
      case 0: ShowPage(1); break;
      case 1:
        if (!Valid()) break;
        if (DlcOnly()) AddDlcOnly(); else StartBuild();
        break;
      case 2:
        if (running) {
          if (MessageBox.Show(this, "Stop the installation?", "SSX PC Port Setup", MessageBoxButtons.YesNo,
                              MessageBoxIcon.Question) == DialogResult.Yes) StopBuild();
        } else {
          StartBuild();
        }
        break;
      case 3: MakeShortcut(); Close(); break;
    }
  }

  void Back() {
    if (page == 1) ShowPage(Install.Built(folder) ? 3 : 0);
    else if (page == 2 && !running) ShowPage(1);
  }

  void UpdateHints() {
    string iso = isoField.Box.Text.Trim();
    if (iso.Length == 0) {
      SetHint(isoHint, "SSX (USA), title ID 4541096D: the disc image, or default.xex in the extracted disc's folder.",
              Th.Faint);
    } else if (Install.IsLoneXex(iso)) {
      SetHint(isoHint, "Only default.xex is in that folder: pick default.xex in your whole extracted disc " +
                       "(data0.big and the other files next to it), or the disc image.", Th.Warn);
    } else if (File.Exists(iso) && Path.GetFileName(iso).Equals("default.xex", StringComparison.OrdinalIgnoreCase)) {
      SetHint(isoHint, "Extracted disc found. It is verified before the build starts; keep the folder where it is.", Th.Good);
    } else if (File.Exists(iso)) {
      SetHint(isoHint, string.Format("Disc image found ({0:0.0} GB). It is verified before the build starts; keep it where it is.",
                                     new FileInfo(iso).Length / 1e9), Th.Good);
    } else {
      SetHint(isoHint, "File not found.", Th.Warn);
    }

    try {
      var drive = new DriveInfo(Path.GetPathRoot(folder));
      long gb = drive.AvailableFreeSpace >> 30;
      if (!Install.Built(folder) && Install.Others(folder) > 0) {
        SetHint(folderHint, "This folder has other files. Put ssx-recomp.exe in a folder of its own first, e.g. C:\\Games\\SSX.",
                Th.Warn);
      } else {
        SetHint(folderHint, string.Format("{0} free on {1}  (needs about 10 GB)", FormatGb(gb), drive.Name.TrimEnd('\\')),
                gb >= 10 ? Th.Faint : Th.Warn);
      }
    } catch (Exception) {
      SetHint(folderHint, "Needs about 10 GB of free space.", Th.Faint);
    }
  }

  static string FormatGb(long gb) { return gb >= 1024 ? string.Format("{0:0.0} TB", gb / 1024.0) : gb + " GB"; }

  static void SetHint(Label l, string text, Color c) { l.Text = text; l.ForeColor = c; }

  void BrowseIso(object sender, EventArgs e) {
    using (var dialog = new OpenFileDialog { Title = "Select your own SSX (Xbox 360): the disc image, or default.xex in the extracted disc",
                                             Filter = "SSX disc image or extracted default.xex (*.iso, default.xex)|*.iso;default.xex|" +
                                                      "All files (*.*)|*.*" }) {
      if (dialog.ShowDialog(this) == DialogResult.OK) isoField.Box.Text = dialog.FileName;
    }
  }

  void BrowseDlc(object sender, EventArgs e) {
    using (var dialog = new OpenFileDialog { Title = "Select SSX DLC and title update packages from your own Xbox 360",
                                             Filter = "Xbox 360 content packages (*.*)|*.*", Multiselect = true }) {
      if (dialog.ShowDialog(this) != DialogResult.OK) return;
      // Picks add up: on the console the DLC and the title update are in different folders.
      var files = new List<string>(dlcFiles);
      if (titleUpdateFile != null) files.Add(titleUpdateFile);
      foreach (string file in dialog.FileNames) {
        if (!files.Exists(f => string.Equals(Path.GetFileName(f), Path.GetFileName(file),
                                             StringComparison.OrdinalIgnoreCase))) files.Add(file);
      }
      dlcFiles.Clear();
      titleUpdateFile = null;
      var names = new List<string>();
      string rejected = null, olderUpdate = null;
      foreach (string file in files) {
        bool isUpdate;
        string problem, name = ReadPackageName(file, out isUpdate, out problem);
        if (name == null) {
          if (rejected == null) rejected = Path.GetFileName(file) + " is " + problem;
        } else if (isUpdate) {
          // ponytail: SSX has two title updates and only their size tells them apart here (0.0.3.2
          // ~9 MB, 0.0.1.2 ~1 MB, contained in 0.0.3.2); the builder checks the files' SHA-256.
          if (new FileInfo(file).Length > 4 << 20) titleUpdateFile = file;
          else olderUpdate = file;
        } else {
          dlcFiles.Add(file);
          names.Add(name.Replace("SSX™ ", ""));
        }
      }
      if (titleUpdateFile != null) names.Insert(0, "Title update");
      else if (olderUpdate != null && rejected == null) {
        rejected = Path.GetFileName(olderUpdate) + " is an older title update: add the larger one (about 9 MB)";
      }
      if (rejected != null) {
        SetHint(dlcHint, (names.Count > 0 ? string.Join(", ", names.ToArray()) + " added. Skipped: " : "Skipped: ") +
                         rejected + ".", Th.Warn);
      } else {
        SetHint(dlcHint, string.Join(", ", names.ToArray()) +
                         (titleUpdateFile != null ? ": the game is built with the title update" +
                                                    (dlcFiles.Count > 0 ? ", the DLC goes to the game's saves folder." : ".")
                                                  : ": copied to the game's saves folder when you install.") +
                         " Files in another folder? Click Add files... again.",
                Th.Good);
      }
      UpdateFooter();
    }
  }

  // The name of an SSX DLC or title update package (an Xbox 360 STFS package for title 4541096D
  // with marketplace or title update content), or null and the reason it isn't one.
  static string ReadPackageName(string path, out bool isUpdate, out string problem) {
    isUpdate = false;
    problem = "not an Xbox 360 content package";
    var h = new byte[0x491];  // up to the end of the first display name
    try {
      using (var f = File.OpenRead(path)) {
        int n = 0, r;
        while (n < h.Length && (r = f.Read(h, n, h.Length - n)) > 0) n += r;
        if (n < h.Length) return null;
      }
    } catch (Exception ex) {
      problem = "not readable (" + ex.Message + ")";
      return null;
    }
    string magic = Encoding.ASCII.GetString(h, 0, 4);
    if (magic != "LIVE" && magic != "PIRS" && magic != "CON ") return null;
    uint type = BE32(h, 0x344), title = BE32(h, 0x360);
    if (title != 0x4541096D) { problem = "not for SSX"; return null; }
    isUpdate = type == 0x000B0000;
    if (type != 0x00000002 && !isUpdate) { problem = "not DLC or a title update"; return null; }
    string name = Encoding.BigEndianUnicode.GetString(h, 0x411, 0x80).Split('\0')[0].Trim();
    return name.Length > 0 ? name : Path.GetFileName(path);
  }

  static uint BE32(byte[] b, int o) { return (uint)(b[o] << 24 | b[o + 1] << 16 | b[o + 2] << 8 | b[o + 3]); }

  // ---- Build ------------------------------------------------------------------------------

  void StartBuild() {
    string iso = isoField.Box.Text.Trim();
    string[] dlc = dlcFiles.ToArray();
    string titleUpdate = titleUpdateFile;
    lock (sync) { stage = 0; stageFrac = 0; tool = null; pendingLog.Length = 0; }
    running = true;
    failed = cancelled = false;
    logBox.Clear();
    bar.Failed = stageList.Failed = false;
    installTitle.Text = "Installing SSX";
    installTitle.ForeColor = Th.Text;
    percentLabel.ForeColor = Th.Accent;
    installStatus.ForeColor = Th.Dim;
    ShowLog(false);
    clock = Stopwatch.StartNew();
    ShowPage(2);
    timer.Start();
    var worker = new Thread(delegate() {
      try {
        CopyDlc(dlc, Install.UserData(folder), OnLine);
      } catch (Exception ex) {
        OnLine("Couldn't set up " + Install.GameDir(folder) + ": " + ex.Message);
      }
      RunBuild(iso, folder, titleUpdate);
    });
    worker.IsBackground = true;
    worker.Start();
  }

  // Copies the chosen DLC packages to <userData>\dlc, where the game installs them on its next start.
  // A failed copy doesn't stop the install.
  internal static void CopyDlc(string[] files, string userData, Action<string> onLine) {
    if (files.Length == 0) return;
    string dir = Path.Combine(userData, "dlc");
    foreach (string file in files) {
      try {
        Directory.CreateDirectory(dir);
        string dest = Path.Combine(dir, Path.GetFileName(file));
        if (!string.Equals(Path.GetFullPath(file), Path.GetFullPath(dest), StringComparison.OrdinalIgnoreCase) &&
            (!File.Exists(dest) || new FileInfo(dest).Length != new FileInfo(file).Length)) {
          File.Copy(file, dest, true);
        }
        onLine("DLC: " + Path.GetFileName(file) + " is in " + dir);
      } catch (Exception ex) {
        onLine("DLC: could not copy " + file + ": " + ex.Message);
      }
    }
  }

  // Unpacks the source this exe carries into <folder>\source and runs its builder
  // (scripts\build-ssx.ps1), each output line to onLine; started gets the builder's process (to stop
  // it). Returns the builder's exit code. The port itself is never downloaded here; the builder
  // only downloads build tools and packages it doesn't have yet.
  internal static int Build(string iso, string folder, string titleUpdate, Action<string> onLine, Action<Process> started) {
    string source = Path.Combine(folder, "source");
    onLine("==> Unpacking the SSX PC port source (no game files)");
    Install.UnpackSource(folder, onLine);

    // Windows PowerShell by its full path: on some PCs its folder isn't on the PATH (issue #15).
    string powershell = Path.Combine(Environment.SystemDirectory, "WindowsPowerShell\\v1.0\\powershell.exe");
    if (!File.Exists(powershell)) {
      throw new FileNotFoundException("Windows PowerShell, which builds the game, wasn't found at " + powershell +
                                      ". It is part of Windows; a repair of Windows (sfc /scannow) restores it.");
    }
    var info = new ProcessStartInfo(powershell,
        "-NoProfile -ExecutionPolicy RemoteSigned -File \"" + Path.Combine(source, "scripts\\build-ssx.ps1") +
        "\" -Iso \"" + iso + "\"" + (titleUpdate != null ? " -TitleUpdate \"" + titleUpdate + "\"" : "") +
        " -NoPause");
    info.UseShellExecute = false;
    // Started from PowerShell 7 (pwsh), its module path would load PowerShell 7's modules into
    // Windows PowerShell, whose own commands (Get-FileHash...) then fail: let it use its own.
    info.EnvironmentVariables.Remove("PSModulePath");
    info.CreateNoWindow = true;
    info.RedirectStandardOutput = true;
    info.RedirectStandardError = true;
    // PowerShell writes redirected output in the console (OEM) code page; non-English
    // Windows messages are garbled when read as the ANSI default.
    var oem = Encoding.GetEncoding(System.Globalization.CultureInfo.CurrentCulture.TextInfo.OEMCodePage);
    info.StandardOutputEncoding = oem;
    info.StandardErrorEncoding = oem;
    info.WorkingDirectory = source;
    var process = new Process { StartInfo = info };
    process.OutputDataReceived += delegate(object s, DataReceivedEventArgs a) { onLine(a.Data); };
    process.ErrorDataReceived += delegate(object s, DataReceivedEventArgs a) { onLine(a.Data); };
    process.Start();
    if (started != null) started(process);
    process.BeginOutputReadLine();
    process.BeginErrorReadLine();
    process.WaitForExit();
    return process.ExitCode;
  }

  void RunBuild(string iso, string folder, string titleUpdate) {
    try {
      int code = Build(iso, folder, titleUpdate, OnLine, delegate(Process p) { buildProcess = p; });
      bool ok = code == 0 && File.Exists(GameExe());
      if (ok) Install.MarkBuilt(folder);
      Finish(ok, ok ? "" : cancelled ? "Installation cancelled."
                         : "Something went wrong. The full log is saved as build-ssx.log in the source folder.");
    } catch (Exception ex) {
      OnLine(ex.ToString());
      Finish(false, "Something went wrong: " + ex.Message);
    }
  }

  // Runs on the worker threads: queue the line for the log and track progress.
  void OnLine(string line) {
    if (line == null) return;
    lock (sync) {
      pendingLog.Append(line).Append("\r\n");
      if (line.StartsWith("==> ")) {
        int s = StageOf(line.Substring(4));
        if (s > stage) { stage = s; stageFrac = 0; }
        return;
      }
      // "  cmake: found", "  clang: C:\...\clang.exe": past that tool.
      if (stage == 2) tool = ToolOf(line) ?? (Regex.IsMatch(line, @"^\s+(.+: found|clang: .+)$") ? null : tool);
      Match m = NinjaStep.Match(line);
      if (m.Success && (stage == 4 || stage == 5)) {
        float total = float.Parse(m.Groups[2].Value);
        if (total >= 20) stageFrac = Math.Max(stageFrac, float.Parse(m.Groups[1].Value) / total);
      }
    }
  }

  internal static int StageOf(string text) {
    if (text.StartsWith("Unpacking the SSX")) return 0;
    if (text.StartsWith("Select your SSX")) return 1;
    if (text.StartsWith("Checking build tools")) return 2;
    if (text.StartsWith("Downloading the ReXGlue")) return 3;
    if (text.StartsWith("Recompiling the Xbox 360")) return 4;
    if (text.StartsWith("Compiling the PC version")) return 5;
    return -1;
  }

  void OnTick() {
    string text;
    int s;
    float frac;
    string installing;
    lock (sync) {
      text = pendingLog.ToString();
      pendingLog.Length = 0;
      s = stage;
      frac = stageFrac;
      installing = tool;
    }
    if (text.Length > 0) {
      if (logBox.TextLength > 600000) logBox.Text = logBox.Text.Substring(300000);
      logBox.AppendText(text);
    }
    if (running) {
      float value = Math.Min(.99f, StageStart[s] + (StageStart[s + 1] - StageStart[s]) * frac);
      bar.Value = Math.Max(bar.Value, value);
      percentLabel.Text = (int)(bar.Value * 100) + "%";
      stageList.Current = s;
      installStatus.Text = s == 2 && installing != null ? "Installing " + installing + ": Windows may ask for permission." :
          Stages[s] + "..." + (s == 2 ? "  Windows may ask for administrator permission." :
                               s == 5 ? "  This is the longest step." : "");
    }
    TimeSpan t = clock.Elapsed;
    elapsedLabel.Text = "Elapsed " + (int)t.TotalMinutes + ":" + t.Seconds.ToString("00");
    bar.Phase++;
    stageList.Pulse++;
    bar.Invalidate();
    stageList.Invalidate();
    if (!running && text.Length == 0) timer.Stop();
  }

  void Finish(bool ok, string message) {
    if (InvokeRequired) { BeginInvoke(new Action<bool, string>(Finish), ok, message); return; }
    running = false;
    clock.Stop();
    if (ok) {
      bar.Value = 1;
      percentLabel.Text = "100%";
      stageList.Current = Stages.Length;
      TimeSpan t = clock.Elapsed;
      doneSub.Text = "Built in " + Math.Max(1, (int)Math.Round(t.TotalMinutes)) + " min.  In " + folder;
      shortcutCheck.Checked = true;
      ShowPage(3);
    } else {
      failed = true;
      bar.Failed = stageList.Failed = true;
      installTitle.Text = cancelled ? "Installation cancelled" : "Installation failed";
      installTitle.ForeColor = Th.Bad;
      percentLabel.ForeColor = Th.Bad;
      installStatus.Text = message;
      installStatus.ForeColor = Th.Bad;
      if (!cancelled) ShowLog(true);
      UpdateFooter();
    }
    OnTick();
  }

  void StopBuild() {
    cancelled = true;
    try {
      if (buildProcess != null && !buildProcess.HasExited)  // also stops cmake/ninja/clang children
        Process.Start(new ProcessStartInfo("taskkill", "/T /F /PID " + buildProcess.Id) {
          CreateNoWindow = true, UseShellExecute = false }).WaitForExit();
    } catch (Exception) { }
  }

  void ShowLog(bool show) {
    logBox.Visible = show;
    stageList.Visible = !show;
    detailsLink.Text = show ? "Hide details" : "Show details";
    if (show) { logBox.SelectionStart = logBox.TextLength; logBox.ScrollToCaret(); }
  }

  string InstallFolder() { return folder; }
  string GameExe() { return Install.GameExe(InstallFolder()); }

  void Play() { Install.StartGame(InstallFolder(), new string[0]); }

  void MakeShortcut() {
    if (!shortcutCheck.Checked || shortcutMade) return;
    shortcutMade = true;
    try {
      // The shortcut starts this ssx-recomp.exe, which opens on the Play page.
      string exe = Install.Self;
      string lnk = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "SSX.lnk");
      var link = (IShellLinkW)new ShellLink();
      link.SetPath(exe);
      link.SetWorkingDirectory(Path.GetDirectoryName(exe));
      link.SetDescription("SSX (2012) PC port");
      link.SetIconLocation(exe, 0);
      ((System.Runtime.InteropServices.ComTypes.IPersistFile)link).Save(lnk, true);
    } catch (Exception) { }
  }

  protected override void OnFormClosing(FormClosingEventArgs e) {
    if (running) {
      if (MessageBox.Show(this, "SSX is still being installed. Stop and close?", "SSX PC Port Setup",
                          MessageBoxButtons.YesNo, MessageBoxIcon.Warning) == DialogResult.No) {
        e.Cancel = true;
        return;
      }
      StopBuild();
    }
    base.OnFormClosing(e);
  }
}
