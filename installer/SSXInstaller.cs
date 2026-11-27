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
[assembly: AssemblyVersion("2.0.1.0")]
[assembly: AssemblyFileVersion("2.0.1.0")]
[assembly: AssemblyInformationalVersion("2.0.1")]

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
  // arguments, or --play, start the game straight away with them (minus --play), and this exe
  // stays until the game exits and returns its exit code: RecompLauncher (start.program) and
  // shortcuts start the game through it, so they see the game's whole run and its exit code.
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
        return Install.StartGame(folder, Array.FindAll(args, a => a != "--play"), true);
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
  // tools, 40 a download, 50 the build, 60 Smart App Control blocked a build tool.
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
      Console.WriteLine(code == InstallerForm.SmartAppControlExit
                            ? InstallerForm.SmartAppControlMessage
                            : progress != null && progress.Error != null
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

  // wait: stays until the game exits and returns its exit code (0 without waiting).
  public static int StartGame(string folder, string[] args, bool wait = false) {
    UserData(folder);  // also an install built before the game was always portable
    string exe = GameExe(folder);
    var arguments = new StringBuilder();
    foreach (string arg in args) arguments.Append(arguments.Length > 0 ? " " : "").Append(Quote(arg));
    using (Process game = Process.Start(new ProcessStartInfo(exe, arguments.ToString()) {
             WorkingDirectory = Path.GetDirectoryName(exe), UseShellExecute = false })) {
      if (!wait) return 0;
      game.WaitForExit();
      return game.ExitCode;
    }
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
  // Deep navy, with a blue for actions and a mint green for good news.
  public static readonly Color Bg = Color.FromArgb(13, 20, 44);
  public static readonly Color Rail = Color.FromArgb(9, 14, 34);
  public static readonly Color Surface = Color.FromArgb(19, 28, 60);
  public static readonly Color Surface2 = Color.FromArgb(27, 38, 78);
  public static readonly Color Surface3 = Color.FromArgb(36, 50, 98);
  public static readonly Color Border = Color.FromArgb(37, 50, 94);
  public static readonly Color Accent = Color.FromArgb(52, 120, 246);
  public static readonly Color AccentHover = Color.FromArgb(84, 145, 255);
  public static readonly Color AccentDown = Color.FromArgb(36, 96, 214);
  public static readonly Color Sky = Color.FromArgb(118, 172, 255);
  public static readonly Color Ink = Color.White;
  public static readonly Color Text = Color.FromArgb(240, 244, 255);
  public static readonly Color Dim = Color.FromArgb(170, 181, 210);
  public static readonly Color Faint = Color.FromArgb(108, 120, 156);
  public static readonly Color Warn = Color.FromArgb(255, 183, 77);
  public static readonly Color Good = Color.FromArgb(46, 212, 167);
  public static readonly Color Bad = Color.FromArgb(255, 110, 110);

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

  // A rounded panel: fill, border, and a faint light along its top edge.
  public static void Panel(Graphics g, RectangleF r, float radius, Color fill, Color border) {
    using (var path = Round(r, radius)) {
      using (var b = new SolidBrush(fill)) g.FillPath(b, path);
      using (var pen = new Pen(border, Scale)) g.DrawPath(pen, path);
    }
  }

  public static void Tick(Graphics g, Color c, float cx, float cy, float size, float width) {
    using (var pen = new Pen(c, width) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
      g.DrawLines(pen, new[] { new PointF(cx - size * 0.45f, cy + size * 0.02f),
                               new PointF(cx - size * 0.12f, cy + size * 0.33f),
                               new PointF(cx + size * 0.48f, cy - size * 0.32f) });
  }

  public static Font F(float pt, FontStyle style) { return new Font("Segoe UI", pt, style); }
  public static Font Semi(float pt) { return new Font("Segoe UI Semibold", pt); }
  public static Font Bold(float pt) { return new Font("Segoe UI", pt, FontStyle.Bold); }

  public static Label Label(string text, Font font, Color color, int x, int y, int w, int h) {
    return new Label { Text = text, Font = font, ForeColor = color, BackColor = Bg,
                       Location = new Point(x, y), Size = new Size(w, h), UseMnemonic = false };
  }

  // Labels don't blend into what's behind them: give those in a container its colour.
  public static void Adopt(Control parent) {
    foreach (Control c in parent.Controls) if (c is Label) c.BackColor = parent.BackColor;
  }

  // Small capitals, the labels above values.
  public static void Caps(Graphics g, string text, Font font, Color c, int x, int y) {
    TextRenderer.DrawText(g, text.ToUpperInvariant(), font, new Point(x, y), c, TextFormatFlags.NoPadding);
  }

  [DllImport("user32.dll")] static extern bool ReleaseCapture();
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam);
  [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
  static extern int SHDefExtractIcon(string file, int index, uint flags, out IntPtr large, out IntPtr small, uint size);
  [DllImport("user32.dll")] static extern bool DestroyIcon(IntPtr icon);

  // The window has no title bar: dragging these areas moves it.
  public static void DragWindow(Control c) {
    Form f = c.FindForm();
    if (f == null) return;
    ReleaseCapture();
    SendMessage(f.Handle, 0xA1, (IntPtr)2, IntPtr.Zero);  // WM_NCLBUTTONDOWN, HTCAPTION
  }

  // This exe's icon (ssx.ico, embedded at compile time) at the given size.
  static readonly Dictionary<int, Bitmap> icons = new Dictionary<int, Bitmap>();
  public static Bitmap AppIcon(int size) {
    Bitmap b;
    if (icons.TryGetValue(size, out b)) return b;
    IntPtr large, small;
    try {
      if (SHDefExtractIcon(Application.ExecutablePath, 0, 0, out large, out small, (uint)size) == 0 && large != IntPtr.Zero) {
        using (var icon = Icon.FromHandle(large)) b = icon.ToBitmap();
        DestroyIcon(large);
        if (small != IntPtr.Zero) DestroyIcon(small);
      }
    } catch (Exception) { }
    icons[size] = b;
    return b;
  }
}

// Line icons, drawn in a square.
static class Glyph {
  public static void Draw(Graphics g, string name, RectangleF r, Color c) {
    if (name == null) return;
    float s = Math.Min(r.Width, r.Height), x = r.X + (r.Width - s) / 2, y = r.Y + (r.Height - s) / 2;
    Func<float, float, PointF> P = (u, v) => new PointF(x + u * s, y + v * s);
    var smooth = g.SmoothingMode;
    g.SmoothingMode = SmoothingMode.AntiAlias;
    using (var pen = new Pen(c, Math.Max(1.2f, s * 0.085f)) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
    using (var brush = new SolidBrush(c)) {
      switch (name) {
        case "home":
          g.DrawLines(pen, new[] { P(.12f, .48f), P(.5f, .14f), P(.88f, .48f) });
          g.DrawLines(pen, new[] { P(.22f, .4f), P(.22f, .86f), P(.78f, .86f), P(.78f, .4f) });
          g.DrawLines(pen, new[] { P(.42f, .86f), P(.42f, .62f), P(.58f, .62f), P(.58f, .86f) });
          break;
        case "disc":
          g.DrawEllipse(pen, x + s * .12f, y + s * .12f, s * .76f, s * .76f);
          g.DrawEllipse(pen, x + s * .4f, y + s * .4f, s * .2f, s * .2f);
          g.DrawArc(pen, x + s * .24f, y + s * .24f, s * .52f, s * .52f, 200, 70);
          break;
        case "download":
          g.DrawLine(pen, P(.5f, .12f), P(.5f, .62f));
          g.DrawLines(pen, new[] { P(.3f, .44f), P(.5f, .64f), P(.7f, .44f) });
          g.DrawLines(pen, new[] { P(.14f, .66f), P(.14f, .86f), P(.86f, .86f), P(.86f, .66f) });
          break;
        case "refresh":
          g.DrawArc(pen, x + s * .16f, y + s * .16f, s * .68f, s * .68f, -60, 290);
          g.FillPolygon(brush, new[] { P(.86f, .1f), P(.88f, .4f), P(.6f, .32f) });
          break;
        case "folder":
          g.DrawLines(pen, new[] { P(.1f, .8f), P(.1f, .22f), P(.4f, .22f), P(.48f, .32f), P(.9f, .32f), P(.9f, .8f), P(.1f, .8f) });
          g.DrawLine(pen, P(.1f, .44f), P(.9f, .44f));
          break;
        case "dlc":
          g.DrawRectangle(pen, x + s * .14f, y + s * .14f, s * .72f, s * .72f);
          g.DrawLine(pen, P(.5f, .32f), P(.5f, .68f));
          g.DrawLine(pen, P(.32f, .5f), P(.68f, .5f));
          break;
        case "drive":
          using (var path = Th.Round(new RectangleF(x + s * .1f, y + s * .3f, s * .8f, s * .4f), s * .08f)) g.DrawPath(pen, path);
          g.FillEllipse(brush, x + s * .66f, y + s * .46f, s * .09f, s * .09f);
          break;
        case "check":
          g.DrawEllipse(pen, x + s * .1f, y + s * .1f, s * .8f, s * .8f);
          Th.Tick(g, c, x + s * .5f, y + s * .5f, s * .42f, pen.Width);
          break;
        case "tag":
          g.DrawPolygon(pen, new[] { P(.14f, .14f), P(.5f, .14f), P(.88f, .52f), P(.52f, .88f), P(.14f, .5f) });
          g.FillEllipse(brush, x + s * .27f, y + s * .27f, s * .11f, s * .11f);
          break;
        case "play":
          g.FillPolygon(brush, new[] { P(.22f, .12f), P(.86f, .5f), P(.22f, .88f) });
          break;
        case "desktop":
          using (var path = Th.Round(new RectangleF(x + s * .1f, y + s * .16f, s * .8f, s * .52f), s * .06f)) g.DrawPath(pen, path);
          g.DrawLine(pen, P(.5f, .68f), P(.5f, .84f));
          g.DrawLine(pen, P(.32f, .86f), P(.68f, .86f));
          break;
        case "help":
          g.DrawEllipse(pen, x + s * .1f, y + s * .1f, s * .8f, s * .8f);
          g.DrawArc(pen, x + s * .36f, y + s * .26f, s * .28f, s * .26f, 180, 230);
          g.DrawLine(pen, P(.5f, .52f), P(.5f, .6f));
          g.FillEllipse(brush, x + s * .455f, y + s * .68f, s * .09f, s * .09f);
          break;
        case "snow":
          for (int i = 0; i < 3; i++) {
            double a = i * Math.PI / 3;
            float dx = (float)Math.Cos(a) * s * .38f, dy = (float)Math.Sin(a) * s * .38f;
            g.DrawLine(pen, x + s * .5f - dx, y + s * .5f - dy, x + s * .5f + dx, y + s * .5f + dy);
          }
          break;
        case "peak":
          g.DrawLines(pen, new[] { P(.08f, .82f), P(.38f, .26f), P(.56f, .56f), P(.68f, .4f), P(.92f, .82f), P(.08f, .82f) });
          break;
      }
    }
    g.SmoothingMode = smooth;
  }
}

class Painted : Control {
  public Painted() {
    SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
             ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
  }
  protected Color Behind { get { return Parent != null ? Parent.BackColor : Th.Bg; } }
}

// Night peaks with snow caps, stars and a board trail (original art), filling r.
static class Art {
  public static void Mountains(Graphics g, RectangleF r) {
    float w = r.Width, h = r.Height;
    Func<float, float, PointF> P = (x, y) => new PointF(r.X + x * w, r.Y + y * h);
    using (var glow = new GraphicsPath()) {
      glow.AddEllipse(r.X + w * 0.25f, r.Y - h * 0.55f, w * 0.9f, h * 1.6f);
      using (var b = new PathGradientBrush(glow)) {
        b.CenterColor = Color.FromArgb(70, Th.Accent);
        b.SurroundColors = new[] { Color.FromArgb(0, Th.Accent) };
        g.FillPath(b, glow);
      }
    }
    var rnd = new Random(11);
    for (int i = 0; i < 60; i++) {
      float sx = r.X + w * (float)rnd.NextDouble(), sy = r.Y + h * 0.6f * (float)rnd.NextDouble();
      float sz = Th.Scale * (rnd.Next(3) == 0 ? 2f : 1.2f);
      using (var b = new SolidBrush(Color.FromArgb(rnd.Next(50, 190), 220, 232, 255))) g.FillEllipse(b, sx, sy, sz, sz);
    }
    var back = new[] { P(-.02f, 1.02f), P(.2f, .46f), P(.32f, .6f), P(.5f, .2f), P(.66f, .46f),
                       P(.8f, .3f), P(1.02f, .58f), P(1.02f, 1.02f) };
    using (var b = new LinearGradientBrush(new RectangleF(r.X, r.Y + h * .2f, w, h * .82f),
                                           Color.FromArgb(46, 74, 150), Color.FromArgb(16, 24, 56), 90f))
      g.FillPolygon(b, back);
    var front = new[] { P(.1f, 1.02f), P(.36f, .54f), P(.46f, .66f), P(.68f, .28f), P(.88f, .64f),
                        P(1.02f, .5f), P(1.02f, 1.02f) };
    using (var b = new LinearGradientBrush(new RectangleF(r.X, r.Y + h * .28f, w, h * .75f),
                                           Color.FromArgb(120, 168, 255), Color.FromArgb(22, 36, 84), 90f))
      g.FillPolygon(b, front);
    using (var snow = new SolidBrush(Color.FromArgb(240, 246, 255)))
    using (var dimSnow = new SolidBrush(Color.FromArgb(160, 214, 228, 255))) {
      Cap(g, dimSnow, P(.5f, .2f), P(.32f, .6f), P(.66f, .46f), .3f);
      Cap(g, dimSnow, P(.2f, .46f), P(-.02f, 1.02f), P(.32f, .6f), .2f);
      Cap(g, snow, P(.68f, .28f), P(.46f, .66f), P(.88f, .64f), .34f);
      Cap(g, snow, P(.36f, .54f), P(.1f, 1.02f), P(.46f, .66f), .24f);
    }
    using (var pen = new Pen(Color.FromArgb(220, 246, 250, 255), Th.D(3)) { StartCap = LineCap.Round, EndCap = LineCap.Round })
      g.DrawBezier(pen, P(.3f, 1.0f), P(.5f, .74f), P(.64f, 1.04f), P(.96f, .76f));
  }

  static PointF Lerp(PointF a, PointF b, float t) { return new PointF(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t); }

  static void Cap(Graphics g, Brush b, PointF peak, PointF left, PointF right, float f) {
    PointF l = Lerp(peak, left, f), r = Lerp(peak, right, f);
    g.FillPolygon(b, new[] { peak, r, Lerp(Lerp(l, r, .72f), peak, .12f), Lerp(Lerp(l, r, .5f), peak, -.08f),
                             Lerp(Lerp(l, r, .3f), peak, .28f), l });
  }
}

// The left column: logo, the pages, and the status card.
class Sidebar : Painted {
  public string[] Items = { "Overview", "Game files", "Installation" };
  public string[] Glyphs = { "home", "disc", "download" };
  public bool[] Allowed = { true, true, false };
  public int Current;
  public string Status = "", Version = "";
  public Color StatusColor = Th.Good;
  public Action<int> Picked;
  public Action OpenRepo;
  int hover = -1;
  bool linkHover;

  Rectangle ItemRect(int i) { return new Rectangle(Th.D(14), Th.D(150) + i * Th.D(52), Width - Th.D(28), Th.D(44)); }
  Rectangle StatusRect { get { return new Rectangle(Th.D(14), Height - Th.D(158), Width - Th.D(28), Th.D(86)); } }
  Rectangle LinkRect { get { return new Rectangle(Th.D(16), Height - Th.D(42), Width - Th.D(28), Th.D(20)); } }

  int ItemAt(Point p) {
    for (int i = 0; i < Items.Length; i++) if (Allowed[i] && ItemRect(i).Contains(p)) return i;
    return -1;
  }

  protected override void OnMouseMove(MouseEventArgs e) {
    int h = ItemAt(e.Location);
    bool l = LinkRect.Contains(e.Location);
    Cursor = h >= 0 || l ? Cursors.Hand : Cursors.Default;
    if (h != hover || l != linkHover) { hover = h; linkHover = l; Invalidate(); }
    base.OnMouseMove(e);
  }
  protected override void OnMouseLeave(EventArgs e) { hover = -1; linkHover = false; Invalidate(); base.OnMouseLeave(e); }
  protected override void OnMouseDown(MouseEventArgs e) {
    base.OnMouseDown(e);
    if (e.Button != MouseButtons.Left) return;
    int i = ItemAt(e.Location);
    if (i >= 0) { if (Picked != null) Picked(i); return; }
    if (LinkRect.Contains(e.Location)) { if (OpenRepo != null) OpenRepo(); return; }
    if (e.Y < Th.D(130)) Th.DragWindow(this);
  }

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    using (var b = new LinearGradientBrush(ClientRectangle, Color.FromArgb(12, 19, 44), Th.Rail, 90f)) g.FillRectangle(b, ClientRectangle);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    using (var pen = new Pen(Color.FromArgb(28, 39, 76))) g.DrawLine(pen, Width - 1, 0, Width - 1, Height);

    // Logo: the icon over a soft glow, then the name.
    int icon = Th.D(48);
    using (var glow = new GraphicsPath()) {
      glow.AddEllipse(Width / 2f - icon, Th.D(22) - icon / 3f, icon * 2f, icon * 1.6f);
      using (var b = new PathGradientBrush(glow)) {
        b.CenterColor = Color.FromArgb(60, Th.Accent);
        b.SurroundColors = new[] { Color.FromArgb(0, Th.Accent) };
        g.FillPath(b, glow);
      }
    }
    Bitmap bmp = Th.AppIcon(64);
    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
    if (bmp != null) g.DrawImage(bmp, (Width - icon) / 2, Th.D(20), icon, icon);
    using (var title = new Font("Segoe UI Black", 15f, FontStyle.Italic))
      TextRenderer.DrawText(g, "SSX", title, new Rectangle(0, Th.D(72), Width, Th.D(30)), Th.Text,
                            TextFormatFlags.HorizontalCenter | TextFormatFlags.NoPadding);
    using (var caps = Th.Semi(7.5f))
      TextRenderer.DrawText(g, "P C   P O R T", caps, new Rectangle(0, Th.D(104), Width, Th.D(16)), Th.Sky,
                            TextFormatFlags.HorizontalCenter | TextFormatFlags.NoPadding);

    for (int i = 0; i < Items.Length; i++) {
      Rectangle r = ItemRect(i);
      bool cur = i == Current, hot = i == hover && !cur;
      if (cur) {
        for (int k = 3; k >= 1; k--) {
          var halo = RectangleF.Inflate(r, k * Th.Scale * 1.5f, k * Th.Scale * 1.5f);
          using (var path = Th.Round(halo, Th.D(10) + k * 1.5f * Th.Scale))
          using (var b = new SolidBrush(Color.FromArgb(14, Th.Accent))) g.FillPath(b, path);
        }
        using (var path = Th.Round(r, Th.D(10)))
        using (var b = new LinearGradientBrush(r, Th.Accent, Color.FromArgb(36, 84, 200), 0f))
        using (var pen = new Pen(Color.FromArgb(150, 140, 190, 255), Th.Scale)) {
          g.FillPath(b, path);
          g.DrawPath(pen, path);
        }
      } else if (hot) {
        using (var path = Th.Round(r, Th.D(10)))
        using (var b = new SolidBrush(Th.Surface)) g.FillPath(b, path);
      }
      Color c = cur ? Color.White : !Allowed[i] ? Color.FromArgb(70, 82, 118) : hot ? Th.Text : Th.Dim;
      Glyph.Draw(g, Glyphs[i], new RectangleF(r.X + Th.D(14), r.Y + Th.D(12), Th.D(20), Th.D(20)), c);
      using (var f = cur ? Th.Semi(10.5f) : Th.F(10.5f, FontStyle.Regular))
        TextRenderer.DrawText(g, Items[i], f, new Rectangle(r.X + Th.D(46), r.Y, r.Width - Th.D(46), r.Height), c,
                              TextFormatFlags.VerticalCenter);
    }

    // Status card, with a faint wave in the status colour.
    Rectangle s = StatusRect;
    using (var path = Th.Round(s, Th.D(12))) {
      using (var b = new SolidBrush(Th.Surface)) g.FillPath(b, path);
      g.SetClip(path);
      using (var wave = new GraphicsPath()) {
        wave.AddBezier(s.X, s.Bottom - Th.D(18), s.X + s.Width * .35f, s.Bottom - Th.D(34),
                       s.X + s.Width * .6f, s.Bottom - Th.D(4), s.Right, s.Bottom - Th.D(26));
        wave.AddLine(s.Right, s.Bottom - Th.D(26), s.Right, s.Bottom);
        wave.AddLine(s.Right, s.Bottom, s.X, s.Bottom);
        wave.CloseFigure();
        using (var b = new SolidBrush(Color.FromArgb(26, StatusColor))) g.FillPath(b, wave);
      }
      g.ResetClip();
      using (var pen = new Pen(Th.Border, Th.Scale)) g.DrawPath(pen, path);
    }
    float dot = Th.D(7);
    using (var b = new SolidBrush(StatusColor)) g.FillEllipse(b, s.X + Th.D(16), s.Y + Th.D(18), dot, dot);
    using (var caps = Th.Semi(7.5f)) Th.Caps(g, "Status", caps, Th.Faint, s.X + Th.D(30), s.Y + Th.D(14));
    using (var f = Th.Semi(11.5f))
      TextRenderer.DrawText(g, Status, f, new Rectangle(s.X + Th.D(14), s.Y + Th.D(36), s.Width - Th.D(24), Th.D(28)),
                            StatusColor, TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);

    using (var f = Th.F(8.5f, FontStyle.Regular)) {
      TextRenderer.DrawText(g, Version, f, new Point(Th.D(16), Height - Th.D(64)), Th.Faint, TextFormatFlags.NoPadding);
      TextRenderer.DrawText(g, "View on GitHub", f, LinkRect, linkHover ? Th.Sky : Th.Faint,
                            TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis);
    }
  }
}

// The top of the page: a small line, the page's title and, on the overview, a pill.
class Header : Painted {
  public string Kicker = "Your game", Title = "SSX", Pill;
  public Color PillDot = Th.Good;

  protected override void OnMouseDown(MouseEventArgs e) {
    base.OnMouseDown(e);
    if (e.Button == MouseButtons.Left) Th.DragWindow(this);
  }

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    int x = Th.D(26);
    using (var caps = Th.Semi(7.5f)) Th.Caps(g, Kicker, caps, Th.Faint, x, Th.D(16));
    using (var title = Th.Bold(17f)) {
      TextRenderer.DrawText(g, Title, title, new Point(x - Th.D(2), Th.D(30)), Th.Text, TextFormatFlags.NoPadding);
      if (Pill == null) return;
      int tw = TextRenderer.MeasureText(g, Title, title, Size.Empty, TextFormatFlags.NoPadding).Width;
      using (var caps = Th.Semi(7.5f)) {
        string text = Pill.ToUpperInvariant();
        int pw = TextRenderer.MeasureText(g, text, caps, Size.Empty, TextFormatFlags.NoPadding).Width;
        var r = new RectangleF(x + tw + Th.D(12), Th.D(35), pw + Th.D(36), Th.D(24));
        Th.Panel(g, r, r.Height / 2, Th.Surface, Th.Border);
        TextRenderer.DrawText(g, text, caps, new Point((int)r.X + Th.D(12), (int)r.Y + Th.D(6)), Th.Text, TextFormatFlags.NoPadding);
        float d = Th.D(8);
        using (var glow = new SolidBrush(Color.FromArgb(60, PillDot))) g.FillEllipse(glow, r.Right - Th.D(20) - 2, r.Y + r.Height / 2 - d / 2 - 2, d + 4, d + 4);
        using (var b = new SolidBrush(PillDot)) g.FillEllipse(b, r.Right - Th.D(20), r.Y + r.Height / 2 - d / 2, d, d);
      }
    }
  }
}

// Minimize and close, top right (the window has no title bar).
class WinButton : Painted {
  public bool IsClose;
  bool hover;
  public WinButton() { Cursor = Cursors.Hand; }
  protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
  protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
  protected override void OnClick(EventArgs e) {
    base.OnClick(e);
    Form f = FindForm();
    if (f == null) return;
    if (IsClose) f.Close(); else f.WindowState = FormWindowState.Minimized;
  }
  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(hover ? (IsClose ? Color.FromArgb(232, 17, 35) : Th.Surface2) : Behind);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    float cx = Width / 2f, cy = Height / 2f, k = Th.D(5);
    using (var pen = new Pen(hover ? Color.White : Th.Dim, Th.Scale * 1.1f)) {
      if (IsClose) { g.DrawLine(pen, cx - k, cy - k, cx + k, cy + k); g.DrawLine(pen, cx - k, cy + k, cx + k, cy - k); }
      else g.DrawLine(pen, cx - k, cy, cx + k, cy);
    }
  }
}

class Chip {
  public string Glyph, Label, Value;
  public Color Color;
  public Chip(string glyph, string label, string value, Color color) { Glyph = glyph; Label = label; Value = value; Color = color; }
}

// The big card at the top of the overview: artwork on the right, the game's name and chips on the left.
class HeroCard : Painted {
  public string Kicker = "", Title = "SSX", Sub = "", Body = "";
  public Chip[] Chips = new Chip[0];

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Behind);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    var rect = new RectangleF(0, 0, Width - 1, Height - 1);
    Color left = Color.FromArgb(17, 27, 62);
    using (var path = Th.Round(rect, Th.D(14))) {
      using (var b = new LinearGradientBrush(rect, Color.FromArgb(20, 34, 78), Color.FromArgb(12, 19, 46), 35f)) g.FillPath(b, path);
      g.SetClip(path);
      Art.Mountains(g, new RectangleF(Width * .38f, 0, Width * .62f, Height));
      var fade = new RectangleF(Width * .36f, 0, Width * .3f, Height);
      using (var b = new LinearGradientBrush(fade, left, Color.FromArgb(0, left), 0f)) g.FillRectangle(b, fade.X + 1, 0, fade.Width - 2, Height);
      using (var b = new SolidBrush(left)) g.FillRectangle(b, 0, 0, fade.X + 2, Height);
      g.ResetClip();
      using (var pen = new Pen(Th.Border, Th.Scale)) g.DrawPath(pen, path);
    }
    int x = Th.D(26);
    using (var caps = Th.Semi(7.5f)) Th.Caps(g, Kicker, caps, Th.Sky, x, Th.D(22));
    using (var title = new Font("Segoe UI Black", 30f, FontStyle.Italic))
      TextRenderer.DrawText(g, Title, title, new Point(x - Th.D(3), Th.D(36)), Th.Text, TextFormatFlags.NoPadding);
    using (var f = Th.Semi(11.5f))
      TextRenderer.DrawText(g, Sub, f, new Point(x, Th.D(88)), Th.Text, TextFormatFlags.NoPadding);
    using (var f = Th.F(9f, FontStyle.Regular))
      TextRenderer.DrawText(g, Body, f, new Rectangle(x, Th.D(112), (int)(Width * .56f), Th.D(20)), Th.Dim,
                            TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);

    int cx = x, cy = Height - Th.D(64), ch = Th.D(44);
    using (var caps = Th.Semi(7f))
    using (var value = Th.Semi(10f)) {
      foreach (Chip c in Chips) {
        int tw = Math.Max(TextRenderer.MeasureText(g, c.Label.ToUpperInvariant(), caps, Size.Empty, TextFormatFlags.NoPadding).Width,
                          TextRenderer.MeasureText(g, c.Value, value, Size.Empty, TextFormatFlags.NoPadding).Width);
        var r = new RectangleF(cx, cy, Th.D(56) + tw, ch);
        Th.Panel(g, r, Th.D(9), Color.FromArgb(230, 16, 25, 56), Th.Border);
        var box = new RectangleF(r.X + Th.D(8), r.Y + Th.D(7), Th.D(30), Th.D(30));
        Th.Panel(g, box, Th.D(7), Th.Surface2, Th.Border);
        Glyph.Draw(g, c.Glyph, RectangleF.Inflate(box, -Th.D(7), -Th.D(7)), c.Color == Th.Text ? Th.Sky : c.Color);
        Th.Caps(g, c.Label, caps, Th.Faint, (int)r.X + Th.D(46), (int)r.Y + Th.D(7));
        TextRenderer.DrawText(g, c.Value, value, new Point((int)r.X + Th.D(46), (int)r.Y + Th.D(21)), c.Color, TextFormatFlags.NoPadding);
        cx = (int)r.Right + Th.D(10);
      }
    }
  }
}

// A small card with an icon, a label, a value and a line under it.
class Tile : Painted {
  public string Glyph = "", Label = "", Value = "", Sub = "";
  public Color ValueColor = Th.Text;

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Behind);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    var rect = new RectangleF(0, 0, Width - 1, Height - 1);
    using (var path = Th.Round(rect, Th.D(12))) {
      using (var b = new SolidBrush(Th.Surface)) g.FillPath(b, path);
      g.SetClip(path);
      using (var wave = new GraphicsPath()) {
        wave.AddBezier(Width * .45f, Height, Width * .65f, Height * .55f, Width * .8f, Height * .95f, Width, Height * .5f);
        wave.AddLine(Width, Height * .5f, Width, Height);
        wave.CloseFigure();
        using (var b = new SolidBrush(Color.FromArgb(10, 160, 190, 255))) g.FillPath(b, wave);
      }
      g.ResetClip();
      using (var pen = new Pen(Th.Border, Th.Scale)) g.DrawPath(pen, path);
    }
    var box = new RectangleF(Th.D(14), (Height - Th.D(36)) / 2f, Th.D(36), Th.D(36));
    Th.Panel(g, box, Th.D(8), Th.Surface2, Th.Border);
    global::Glyph.Draw(g, Glyph, RectangleF.Inflate(box, -Th.D(9), -Th.D(9)), Th.Sky);
    int x = Th.D(62), w = Width - x - Th.D(10);
    using (var caps = Th.Semi(7f)) Th.Caps(g, Label, caps, Th.Faint, x, Th.D(14));
    using (var f = Th.Semi(11f))
      TextRenderer.DrawText(g, Value, f, new Rectangle(x, Th.D(28), w, Th.D(22)), ValueColor,
                            TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);
    using (var f = Th.F(8f, FontStyle.Regular))
      TextRenderer.DrawText(g, Sub, f, new Rectangle(x, Th.D(51), w, Th.D(16)), Th.Faint,
                            TextFormatFlags.EndEllipsis | TextFormatFlags.NoPadding);
  }
}

// A rounded card that holds controls: a title with an icon, and optionally a list or text.
class CardPanel : Panel {
  public string Title = "", Glyph, Body;
  public string[] Items;
  public Color Tone = Th.Sky;
  readonly Font bodyFont = Th.F(9.5f, FontStyle.Regular);

  public CardPanel() {
    SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
             ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
    BackColor = Th.Surface;
  }

  public int Top0 { get { return Th.D(58); } }

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Parent != null ? Parent.BackColor : Th.Bg);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    Th.Panel(g, new RectangleF(0, 0, Width - 1, Height - 1), Th.D(12), Th.Surface, Th.Border);
    int x = Th.D(20);
    if (Glyph != null) {
      var circle = new RectangleF(Th.D(18), Th.D(16), Th.D(30), Th.D(30));
      using (var b = new SolidBrush(Th.Surface2)) g.FillEllipse(b, circle);
      using (var pen = new Pen(Th.Border, Th.Scale)) g.DrawEllipse(pen, circle);
      this.DrawGlyph(g, RectangleF.Inflate(circle, -Th.D(7), -Th.D(7)));
      x = Th.D(58);
    }
    using (var f = Th.Bold(9.5f)) Th.Caps(g, Title, f, Th.Text, x, Th.D(22));
    int inner = Width - Th.D(40), y = Top0;
    if (Body != null)
      TextRenderer.DrawText(g, Body, bodyFont, new Rectangle(Th.D(20), y, inner, Height - y), Th.Dim, TextFormatFlags.WordBreak);
    if (Items == null) return;
    foreach (string item in Items) {
      int h = TextRenderer.MeasureText(item, bodyFont, new Size(inner - Th.D(28), 1000), TextFormatFlags.WordBreak).Height;
      float r = Th.D(9), cy = y + bodyFont.Height / 2f;
      using (var b = new SolidBrush(Color.FromArgb(40, Tone))) g.FillEllipse(b, Th.D(20), cy - r, 2 * r, 2 * r);
      Th.Tick(g, Tone, Th.D(20) + r, cy, r * 0.95f, Th.Scale * 1.7f);
      TextRenderer.DrawText(g, item, bodyFont, new Rectangle(Th.D(48), y, inner - Th.D(28), h), Th.Text, TextFormatFlags.WordBreak);
      y += h + Th.D(10);
    }
  }

  void DrawGlyph(Graphics g, RectangleF r) { global::Glyph.Draw(g, Glyph, r, Tone); }
}

// A square button with an icon, a name and a state line (the overview's shortcuts).
class ShortcutTile : Painted {
  public string Glyph = "", Label = "", State = "";
  public Color StateColor = Th.Faint;
  bool hover;
  public ShortcutTile() { Cursor = Cursors.Hand; }
  protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
  protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Behind);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    Th.Panel(g, new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f), Th.D(10),
             hover ? Th.Surface3 : Th.Surface2, hover ? Th.Accent : Th.Border);
    using (var f = Th.Semi(9f)) {
      var flags = TextFormatFlags.HorizontalCenter | TextFormatFlags.WordBreak | TextFormatFlags.NoPadding;
      int lh = TextRenderer.MeasureText(g, Label, f, new Size(Width - Th.D(12), 1000), flags).Height;
      int top = (Height - Th.D(22) - (Th.D(34) + lh)) / 2;
      global::Glyph.Draw(g, Glyph, new RectangleF((Width - Th.D(26)) / 2f, top, Th.D(26), Th.D(26)), Th.Sky);
      TextRenderer.DrawText(g, Label, f, new Rectangle(Th.D(6), top + Th.D(34), Width - Th.D(12), lh), Th.Text, flags);
    }
    if (State.Length > 0)
      using (var caps = Th.Semi(7f))
        TextRenderer.DrawText(g, State.ToUpperInvariant(), caps, new Rectangle(0, Height - Th.D(22), Width, Th.D(16)), StateColor,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.NoPadding);
  }
}

enum ButtonKind { Primary, Secondary, Pill, Play }

class FlatButton : Button {
  public ButtonKind Kind;
  public string Glyph;
  bool hover, down;

  public FlatButton(string text, bool primary) {
    Text = text;
    Kind = primary ? ButtonKind.Primary : ButtonKind.Secondary;
    SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
    Cursor = Cursors.Hand;
    Font = Th.Semi(10f);
  }

  public bool Primary {
    get { return Kind == ButtonKind.Primary || Kind == ButtonKind.Play; }
    set { if (value != Primary) Kind = value ? ButtonKind.Primary : ButtonKind.Secondary; }
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
    var rect = new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f);
    float radius = Kind == ButtonKind.Pill ? rect.Height / 2 : Kind == ButtonKind.Play ? Th.D(12) : Th.D(9);
    Color text = Th.Text;
    using (var path = Th.Round(rect, radius)) {
      if (!Enabled) {
        using (var b = new SolidBrush(Th.Surface)) g.FillPath(b, path);
        using (var pen = new Pen(Th.Surface2, Th.Scale)) g.DrawPath(pen, path);
        text = Color.FromArgb(86, 98, 136);
      } else if (Primary) {
        Color top = down ? Th.AccentDown : hover ? Th.AccentHover : Color.FromArgb(70, 136, 255);
        Color bottom = down ? Color.FromArgb(28, 78, 190) : hover ? Th.Accent : Color.FromArgb(40, 100, 230);
        if (Kind == ButtonKind.Play)
          for (int k = 3; k >= 1; k--) {
            var halo = RectangleF.Inflate(rect, k * Th.Scale * 1.5f, k * Th.Scale * 1.5f);
            using (var hp = Th.Round(halo, radius + k * 1.5f * Th.Scale))
            using (var b = new SolidBrush(Color.FromArgb(hover ? 26 : 16, Th.Accent))) g.FillPath(b, hp);
          }
        using (var b = new LinearGradientBrush(rect, top, bottom, 90f)) g.FillPath(b, path);
        using (var pen = new Pen(Color.FromArgb(140, 160, 200, 255), Th.Scale)) g.DrawPath(pen, path);
        text = Color.White;
      } else {
        using (var b = new SolidBrush(down ? Th.Surface : hover ? Th.Surface3 : Th.Surface2)) g.FillPath(b, path);
        using (var pen = new Pen(hover ? Th.Accent : Th.Border, Th.Scale)) g.DrawPath(pen, path);
      }
    }
    if (Focused && ShowFocusCues) {
      var ring = RectangleF.Inflate(rect, -Th.D(3), -Th.D(3));
      using (var path = Th.Round(ring, Math.Max(1, radius - Th.D(3))))
      using (var pen = new Pen(Color.FromArgb(150, Primary ? Color.White : Th.Accent), Th.Scale)) g.DrawPath(pen, path);
    }
    Font font = Kind == ButtonKind.Play ? null : Font;
    using (var play = Kind == ButtonKind.Play ? Th.Bold(13f) : null) {
      if (font == null) font = play;
      string label = Kind == ButtonKind.Play ? Text.ToUpperInvariant() : Text;
      int tw = TextRenderer.MeasureText(g, label, font, Size.Empty, TextFormatFlags.NoPadding).Width;
      int gs = Glyph == null ? 0 : Kind == ButtonKind.Play ? Th.D(16) : Th.D(15), gap = Glyph == null ? 0 : Th.D(9);
      int x = (Width - tw - gs - gap) / 2;
      if (Glyph != null) global::Glyph.Draw(g, Glyph, new RectangleF(x, (Height - gs) / 2f, gs, gs), text);
      TextRenderer.DrawText(g, label, font, new Rectangle(x + gs + gap, 0, tw + Th.D(4), Height), text,
                            TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.NoPadding);
    }
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
      using (var b = new SolidBrush(Checked ? Th.Accent : Th.Bg)) g.FillPath(b, path);
      using (var pen = new Pen(Checked || hover || Focused ? Th.AccentHover : Th.Border, Th.Scale * 1.4f)) g.DrawPath(pen, path);
    }
    if (Checked) Th.Tick(g, Color.White, box.X + s / 2, box.Y + s / 2, s * 0.62f, Th.Scale * 2.2f);
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
    BackColor = Th.Bg;
    Box.BorderStyle = BorderStyle.None;
    Box.BackColor = Th.Bg;
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
    Th.Panel(g, new RectangleF(0.5f, 0.5f, Width - 1.5f, Height - 1.5f), Th.D(9), Th.Bg, Box.Focused ? Th.Accent : Th.Border);
  }
}

class StageList : Painted {
  public string[] Items = new string[0];
  public int Current;
  public bool Failed;
  public int Pulse;

  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(Behind);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    for (int i = 0; i < Items.Length; i++) {
      int y = i * Th.D(34);
      float r = Th.D(9), cx = r + 1, cy = y + Th.D(17);
      var circle = new RectangleF(cx - r, cy - r, 2 * r, 2 * r);
      bool done = i < Current, cur = i == Current;
      Color textColor = Th.Faint;
      if (done) {
        using (var b = new SolidBrush(Th.Good)) g.FillEllipse(b, circle);
        Th.Tick(g, Th.Rail, cx, cy, r, Th.Scale * 1.9f);
        textColor = Th.Dim;
      } else if (cur && Failed) {
        using (var b = new SolidBrush(Th.Bad)) g.FillEllipse(b, circle);
        using (var pen = new Pen(Th.Rail, Th.Scale * 1.9f)) {
          float k = r * 0.38f;
          g.DrawLine(pen, cx - k, cy - k, cx + k, cy + k);
          g.DrawLine(pen, cx - k, cy + k, cx + k, cy - k);
        }
        textColor = Th.Bad;
      } else if (cur) {
        using (var pen = new Pen(Th.AccentHover, Th.Scale * 1.8f)) g.DrawEllipse(pen, circle);
        int a = 110 + (int)(110 * Math.Sin(Pulse * 0.25));
        float k = r * 0.48f;
        using (var b = new SolidBrush(Color.FromArgb(a, Th.AccentHover))) g.FillEllipse(b, cx - k, cy - k, 2 * k, 2 * k);
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
    g.Clear(Behind);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    float h = Height - 1;
    using (var track = Th.Round(new RectangleF(0, 0, Width - 1, h), h / 2))
    using (var b = new SolidBrush(Th.Bg)) g.FillPath(b, track);
    float fw = Math.Max(h, (Width - 1) * Math.Min(1f, Value));
    if (Value <= 0) return;
    var fill = new RectangleF(0, 0, fw, h);
    using (var path = Th.Round(fill, h / 2)) {
      if (Failed) {
        using (var b = new SolidBrush(Th.Bad)) g.FillPath(b, path);
        return;
      }
      using (var b = new LinearGradientBrush(new RectangleF(0, 0, fw + 1, h), Th.AccentDown, Color.FromArgb(110, 175, 255), 0f))
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

// The bar along the bottom: the game's icon, name and status line, and the page's buttons.
class BottomBar : Panel {
  public BottomBar() {
    SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
             ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
    BackColor = Color.FromArgb(11, 17, 40);
  }
  protected override void OnPaint(PaintEventArgs e) {
    var g = e.Graphics;
    g.Clear(BackColor);
    g.SmoothingMode = SmoothingMode.AntiAlias;
    using (var pen = new Pen(Color.FromArgb(28, 39, 76))) g.DrawLine(pen, 0, 0, Width, 0);
    int icon = Th.D(44);
    Bitmap bmp = Th.AppIcon(64);
    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
    if (bmp != null) g.DrawImage(bmp, Th.D(24), (Height - icon) / 2, icon, icon);
    using (var f = Th.Bold(11.5f))
      TextRenderer.DrawText(g, "SSX", f, new Point(Th.D(80), Height / 2 - Th.D(20)), Th.Text, TextFormatFlags.NoPadding);
  }
}

class InstallerForm : Form {
  const string RepoUrl = "https://github.com/AmarilloNL/ssx-recomp";

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

  readonly Sidebar sidebar = new Sidebar();
  readonly Header header = new Header();
  readonly Panel[] pages = new Panel[4];
  readonly FlatButton backButton = new FlatButton("Back", false);
  readonly FlatButton nextButton = new FlatButton("Next", true);
  readonly FlatButton updateButton = new FlatButton("Check for updates", false) { Kind = ButtonKind.Pill, Glyph = "refresh" };

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

  // The overview (game built): the big card, the four tiles and the desktop shortcut's tile.
  readonly HeroCard playHero = new HeroCard();
  readonly Tile gameTile = new Tile { Glyph = "disc", Label = "Game" };
  readonly Tile dlcTile = new Tile { Glyph = "dlc", Label = "DLC" };
  readonly Tile storageTile = new Tile { Glyph = "drive", Label = "Storage" };
  readonly Tile folderTile = new Tile { Glyph = "folder", Label = "SSX folder" };
  readonly ShortcutTile shortcutTile = new ShortcutTile { Glyph = "desktop", Label = "Desktop shortcut" };

  // The status line in the bottom bar.
  Label doneSub;
  // Whether to make the desktop shortcut (the overview's tile; made on Play or when the window closes).
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
  int uacCheck;
  bool uacOpen;
  Stopwatch clock = new Stopwatch();
  Process buildProcess;
  bool running, failed, cancelled, shortcutMade, buildStarted;
  // No game in the folder when this build started: a first install.
  bool firstBuild;
  // A middle dot between the parts of a subtitle: version or build time, then the folder.
  static readonly string Dot = "  " + (char)0xB7 + "  ";
  int page;

  // folder: the SSX folder. With the game built there the window opens on the overview, or with
  // setup on the Game files page (rebuild: and starts building at once); otherwise on Welcome.
  public InstallerForm(string folder, bool setup, bool rebuild) {
    this.folder = folder;
    Text = "SSX PC Port";
    AutoScaleMode = AutoScaleMode.None;
    ClientSize = new Size(Th.D(1100), Th.D(700));
    FormBorderStyle = FormBorderStyle.None;
    MaximizeBox = false;
    StartPosition = FormStartPosition.CenterScreen;
    BackColor = Th.Bg;
    ForeColor = Th.Text;
    Font = Th.F(10f, FontStyle.Regular);
    Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);  // ssx.ico, embedded at compile time

    int sideW = Th.D(220), headH = Th.D(76), footH = Th.D(80);
    int contentW = ClientSize.Width - sideW, contentH = ClientSize.Height - headH - footH;
    sidebar.Bounds = new Rectangle(0, 0, sideW, ClientSize.Height);
    sidebar.Version = "SSX PC Port " + Install.AppVersion;
    sidebar.Picked = delegate(int i) {
      if (i == 0) ShowPage(Install.Built(folder) ? 3 : 0);
      else if (i == 1) { MakeShortcut(); ShowPage(1); }
      else ShowPage(2);
    };
    sidebar.OpenRepo = delegate { Process.Start(RepoUrl); };
    Controls.Add(sidebar);

    header.Bounds = new Rectangle(sideW, 0, contentW, headH);
    var close = new WinButton { IsClose = true, Bounds = new Rectangle(contentW - Th.D(46), 0, Th.D(46), Th.D(32)) };
    var minimize = new WinButton { Bounds = new Rectangle(contentW - Th.D(92), 0, Th.D(46), Th.D(32)) };
    updateButton.Bounds = new Rectangle(contentW - Th.D(92) - Th.D(14) - Th.D(178), Th.D(22), Th.D(178), Th.D(38));
    updateButton.Font = Th.Semi(9.5f);
    updateButton.Click += delegate { MakeShortcut(); CheckForUpdates(updateButton); };
    header.Controls.AddRange(new Control[] { close, minimize, updateButton });
    Controls.Add(header);

    for (int i = 0; i < pages.Length; i++) {
      pages[i] = new Panel { Bounds = new Rectangle(sideW, headH, contentW, contentH), BackColor = Th.Bg, Visible = false };
      Controls.Add(pages[i]);
    }
    BuildWelcome(pages[0]);
    BuildGame(pages[1]);
    BuildInstall(pages[2]);
    BuildDone(pages[3]);
    BuildBottom(new Rectangle(sideW, ClientSize.Height - footH, contentW, footH));
    string iso = Install.IsoOf(folder);
    if (iso != null) isoField.Box.Text = iso;

    timer.Interval = 80;
    timer.Tick += delegate { OnTick(); };
    if (!Install.Built(folder)) {
      doneSub.Text = "Not installed yet" + Dot + folder;
      ShowPage(0);
    } else if (setup) {
      ShowPlayStatus();
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
        ? "Version " + Install.AppVersion + " isn't built yet: click Install / Manage, then Rebuild."
        : "Version " + Install.AppVersion + Dot + folder;
  }

  [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
  // Dark scroll bars for the log (Windows 10 1809 and later; older ones keep the light ones).
  [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)] static extern int SetWindowTheme(IntPtr hwnd, string app, string idList);

  // No title bar, but still a window Windows can minimize from the taskbar, with a shadow.
  protected override CreateParams CreateParams {
    get {
      CreateParams cp = base.CreateParams;
      cp.Style |= 0x00020000 | 0x00080000;  // WS_MINIMIZEBOX, WS_SYSMENU
      cp.ClassStyle |= 0x00020000;          // CS_DROPSHADOW
      return cp;
    }
  }

  protected override void OnHandleCreated(EventArgs e) {
    base.OnHandleCreated(e);
    try {
      int on = 1;
      DwmSetWindowAttribute(Handle, 20, ref on, 4);  // dark (for the taskbar preview and system menus)
      int round = 2;
      DwmSetWindowAttribute(Handle, 33, ref round, 4);  // rounded corners (Windows 11)
    } catch (Exception) { }
  }

  int PageX { get { return Th.D(24); } }
  int PageWidth { get { return pages[0].Width - 2 * PageX; } }

  void BuildWelcome(Panel p) {
    int x = PageX, w = PageWidth, gap = Th.D(14);
    var hero = new HeroCard {
      Kicker = "The native PC version", Title = "SSX", Sub = "Recompiled on your PC, from your own disc.",
      Body = "The Xbox 360 code is rebuilt to run directly on Windows: no emulator.",
      Chips = new[] { new Chip("download", "Installation", "Not installed yet", Th.Warn),
                      new Chip("disc", "You need", "Your own SSX disc image", Th.Text) },
      Bounds = new Rectangle(x, Th.D(4), w, Th.D(210)) };
    p.Controls.Add(hero);

    int y = hero.Bottom + gap, half = (w - gap) / 2, h = p.Height - y - Th.D(12);
    var features = new CardPanel {
      Title = "What you get", Glyph = "snow",
      Items = new[] { "Up to 300 fps, or your monitor's refresh rate",
                      "Drawn at your screen's resolution, up to 4K",
                      "Settings in game: F1, or Back + Start on a controller",
                      "Keyboard and controller, your DLC and your own music" },
      Bounds = new Rectangle(x, y, half, h) };
    p.Controls.Add(features);

    var own = new CardPanel {
      Title = "Your own copy of the game", Glyph = "disc", Tone = Th.Warn,
      Body = "This installer contains no game code, graphics, music or other EA content, and downloads none. " +
             "You need SSX (USA) for Xbox 360, dumped to an ISO from your own disc.",
      Bounds = new Rectangle(x + half + gap, y, w - half - gap, h) };
    // The setup installs Microsoft's build tools when they're missing: say whose license that accepts.
    const string tools = "A PC without Microsoft's Visual Studio Build Tools gets them installed: by installing " +
                         "SSX you accept the Visual Studio license terms.";
    var license = new LinkLabel { Text = tools, Font = Th.F(8.5f, FontStyle.Regular), BackColor = Th.Surface,
                                  ForeColor = Th.Faint, LinkColor = Th.Sky, ActiveLinkColor = Th.AccentHover,
                                  Bounds = new Rectangle(Th.D(20), h - Th.D(56), own.Width - Th.D(40), Th.D(40)) };
    license.LinkArea = new LinkArea(tools.IndexOf("Visual Studio license terms"), "Visual Studio license terms".Length);
    license.LinkClicked += delegate { Process.Start("https://visualstudio.microsoft.com/license-terms/"); };
    own.Controls.Add(license);
    p.Controls.Add(own);
  }

  void BuildGame(Panel p) {
    int x = PageX, w = PageWidth;
    var iso = new CardPanel { Title = "Your copy of SSX", Glyph = "disc", Bounds = new Rectangle(x, Th.D(4), w, Th.D(150)) };
    isoHint = AddPathRow(iso, "The disc image (.iso) you made from your own disc, or default.xex in the extracted disc.",
                         isoField, "Browse...", BrowseIso);
    p.Controls.Add(iso);

    // The folder is where ssx-recomp.exe is: to use another, move ssx-recomp.exe there.
    var where = new CardPanel { Title = "SSX folder", Glyph = "folder", Bounds = new Rectangle(x, iso.Bottom + Th.D(14), w, Th.D(150)) };
    folderHint = AddPathRow(where, "SSX is built in the folder this program is in, and keeps its saves there.", folderField, "Open",
                            delegate { Process.Start(new ProcessStartInfo("explorer.exe", "\"" + folder + "\"")); });
    folderField.Box.Text = folder;
    folderField.Box.ReadOnly = true;
    folderField.Cursor = folderField.Box.Cursor = Cursors.Default;
    p.Controls.Add(where);

    var dlc = new CardPanel { Title = "DLC and title update", Glyph = "dlc",
                              Bounds = new Rectangle(x, where.Bottom + Th.D(14), w, Th.D(150)) };
    int inner = w - Th.D(40), addW = Th.D(124);
    var optional = Th.Label("Optional. Copied off your own Xbox 360.", Th.F(9.5f, FontStyle.Regular), Th.Dim,
                            Th.D(20), Th.D(58), inner - addW - Th.D(10), Th.D(22));
    dlc.Controls.Add(optional);
    var help = new LinkLabel { Text = "Where do I find these?", Font = Th.F(9.5f, FontStyle.Regular), BackColor = Th.Surface,
                               LinkColor = Th.Sky, ActiveLinkColor = Th.AccentHover, AutoSize = true,
                               LinkBehavior = LinkBehavior.HoverUnderline,
                               Location = new Point(Th.D(20) + TextRenderer.MeasureText(optional.Text, optional.Font).Width, Th.D(58)) };
    help.LinkClicked += delegate { Process.Start(RepoUrl + "#adding-dlc"); };
    dlc.Controls.Add(help);
    help.BringToFront();  // over the line before it, which is wider than its text
    var addDlc = new FlatButton("Add files...", false) { Glyph = "dlc", Bounds = new Rectangle(Th.D(20) + inner - addW, Th.D(14), addW, Th.D(36)) };
    addDlc.Click += BrowseDlc;
    dlc.Controls.Add(addDlc);
    // Three lines: the help text, or up to six package names and what happens with them.
    dlcHint = Th.Label("", Th.F(9.5f, FontStyle.Regular), Th.Faint, Th.D(20), Th.D(86), inner, Th.D(54));
    dlcHint.AutoEllipsis = true;
    dlc.Controls.Add(dlcHint);
    SetHint(dlcHint, DlcHelp, Th.Faint);
    Th.Adopt(dlc);
    p.Controls.Add(dlc);

    UpdateHints();
  }

  // A card's line of text, its field with a button, and a hint under them.
  Label AddPathRow(CardPanel card, string caption, Field field, string button, EventHandler browse) {
    int x = Th.D(20), w = card.Width - 2 * x, browseW = Th.D(112), fieldH = Th.D(40);
    card.Controls.Add(Th.Label(caption, Th.F(9.5f, FontStyle.Regular), Th.Dim, x, Th.D(52), w, Th.D(22)));
    field.Bounds = new Rectangle(x, Th.D(78), w - browseW - Th.D(10), fieldH);
    field.Box.TextChanged += delegate { UpdateHints(); UpdateFooter(); };
    card.Controls.Add(field);
    var browseButton = new FlatButton(button, false) { Bounds = new Rectangle(x + w - browseW, Th.D(78), browseW, fieldH) };
    browseButton.Click += browse;
    card.Controls.Add(browseButton);
    var hint = Th.Label("", Th.F(9f, FontStyle.Regular), Th.Faint, x + Th.D(2), Th.D(124), w, Th.D(20));
    hint.AutoEllipsis = true;
    card.Controls.Add(hint);
    Th.Adopt(card);
    return hint;
  }

  void BuildInstall(Panel p) {
    int x = PageX, w = PageWidth;
    var card = new CardPanel { Bounds = new Rectangle(x, Th.D(4), w, p.Height - Th.D(16)) };
    int cx = Th.D(26), cw = w - 2 * cx;
    installTitle = Th.Label("Installing SSX", Th.Semi(16f), Th.Text, cx, Th.D(20), cw - Th.D(130), Th.D(38));
    percentLabel = Th.Label("0%", Th.Semi(16f), Th.Sky, cx + cw - Th.D(130), Th.D(20), Th.D(130), Th.D(38));
    percentLabel.TextAlign = ContentAlignment.TopRight;
    installStatus = Th.Label("", Th.F(10f, FontStyle.Regular), Th.Dim, cx, Th.D(62), cw, Th.D(24));
    installStatus.AutoEllipsis = true;
    bar.Bounds = new Rectangle(cx, Th.D(96), cw, Th.D(10));
    elapsedLabel = Th.Label("", Th.F(9f, FontStyle.Regular), Th.Faint, cx, Th.D(116), cw / 2, Th.D(22));
    detailsLink = new LinkLabel { Text = "Show details", Font = Th.F(9f, FontStyle.Regular), BackColor = Th.Surface,
                                  LinkColor = Th.Sky, ActiveLinkColor = Th.AccentHover,
                                  LinkBehavior = LinkBehavior.HoverUnderline, TextAlign = ContentAlignment.TopRight,
                                  Bounds = new Rectangle(cx + cw / 2, Th.D(116), cw / 2, Th.D(22)) };
    detailsLink.LinkClicked += delegate { ShowLog(!logBox.Visible); };

    stageList.Items = Stages;
    stageList.Bounds = new Rectangle(cx, Th.D(152), cw, Stages.Length * Th.D(34));
    logBox.Multiline = true;
    logBox.ReadOnly = true;
    logBox.ScrollBars = ScrollBars.Vertical;
    logBox.BackColor = Th.Bg;
    logBox.ForeColor = Th.Dim;
    logBox.BorderStyle = BorderStyle.None;
    logBox.Font = new Font("Consolas", 9f);
    logBox.HandleCreated += delegate { try { SetWindowTheme(logBox.Handle, "DarkMode_Explorer", null); } catch (Exception) { } };
    logBox.Bounds = new Rectangle(cx, Th.D(150), cw, card.Height - Th.D(172));
    logBox.Visible = false;
    card.Controls.AddRange(new Control[] { installTitle, percentLabel, installStatus, bar, elapsedLabel, detailsLink,
                                           stageList, logBox });
    Th.Adopt(card);
    p.Controls.Add(card);
  }

  void BuildDone(Panel p) {
    int x = PageX, w = PageWidth, gap = Th.D(12);
    playHero.Kicker = "The native PC version";
    playHero.Sub = "Recompiled on your PC, from your own disc.";
    playHero.Body = "In game, F1 (or Back + Start) opens the settings and cheats.";
    playHero.Bounds = new Rectangle(x, Th.D(4), w, Th.D(206));
    p.Controls.Add(playHero);

    int ty = playHero.Bottom + gap, tw = (w - 3 * gap) / 4, th = Th.D(74);
    Tile[] tiles = { gameTile, dlcTile, storageTile, folderTile };
    for (int i = 0; i < tiles.Length; i++) {
      tiles[i].Bounds = new Rectangle(x + i * (tw + gap), ty, i == 3 ? w - 3 * (tw + gap) : tw, th);
      p.Controls.Add(tiles[i]);
    }

    int cy = ty + th + gap, ch = p.Height - cy - Th.D(10), sw = Th.D(372);
    var tips = new CardPanel {
      Title = "Good to know", Glyph = "help",
      Items = new[] { "Keep your ISO (or extracted disc) where it is: the game reads its data from it.",
                      "Saves, settings, DLC and music all stay in this folder.",
                      "Check for updates gets the newest version and rebuilds SSX with it." },
      Bounds = new Rectangle(x, cy, w - sw - gap, ch) };
    p.Controls.Add(tips);

    var shortcuts = new CardPanel { Title = "Shortcuts", Glyph = "snow", Bounds = new Rectangle(x + w - sw, cy, sw, ch) };
    int bx = Th.D(18), bw = (sw - 2 * bx - 2 * Th.D(10)) / 3, by = Th.D(58), bh = ch - by - Th.D(16);
    var open = new ShortcutTile { Glyph = "folder", Label = "Open the SSX folder", Bounds = new Rectangle(bx, by, bw, bh) };
    open.Click += delegate { Process.Start(new ProcessStartInfo("explorer.exe", "\"" + InstallFolder() + "\"")); };
    shortcutTile.Bounds = new Rectangle(bx + bw + Th.D(10), by, bw, bh);
    shortcutTile.Click += delegate { shortcutCheck.Checked = !shortcutCheck.Checked; ShowShortcutState(); };
    var guide = new ShortcutTile { Glyph = "help", Label = "Guide and help", State = "GitHub", Bounds = new Rectangle(bx + 2 * (bw + Th.D(10)), by, bw, bh) };
    guide.Click += delegate { Process.Start(RepoUrl); };
    shortcuts.Controls.AddRange(new Control[] { open, shortcutTile, guide });
    p.Controls.Add(shortcuts);
    ShowShortcutState();
  }

  void ShowShortcutState() {
    shortcutTile.State = shortcutCheck.Checked ? "On" : "Off";
    shortcutTile.StateColor = shortcutCheck.Checked ? Th.Good : Th.Faint;
    shortcutTile.Invalidate();
  }

  // The overview's chips and tiles: what this SSX folder has.
  void RefreshOverview() {
    bool rebuild = Install.NeedsRebuild(folder);
    playHero.Chips = new[] {
      new Chip("tag", "Port version", Install.AppVersion, Th.Text),
      new Chip("check", "Installation", rebuild ? "Rebuild needed" : "Installed and ready", rebuild ? Th.Warn : Th.Good) };
    List<string> added = Added(folder);
    bool update = added.Remove("Title update");
    gameTile.Value = update ? "Title update" : "Disc version";
    gameTile.Sub = update ? "Rider levels up to 15" : "SSX (USA), 4541096D";
    dlcTile.Value = added.Count == 0 ? "None added" : added.Count + (added.Count == 1 ? " pack" : " packs");
    dlcTile.Sub = added.Count == 0 ? "Add them under Install / Manage" : string.Join(", ", added.ToArray());
    try {
      var drive = new DriveInfo(Path.GetPathRoot(folder));
      storageTile.Value = FormatGb(drive.AvailableFreeSpace >> 30) + " free";
      storageTile.Sub = "On " + drive.Name.TrimEnd('\\');
    } catch (Exception) {
      storageTile.Value = "Unknown";
      storageTile.Sub = "";
    }
    string name = Path.GetFileName(folder.TrimEnd('\\'));
    folderTile.Value = name.Length > 0 ? name : folder;
    folderTile.Sub = folder;
    foreach (Control c in new Control[] { playHero, gameTile, dlcTile, storageTile, folderTile }) c.Invalidate();
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

  void BuildBottom(Rectangle bounds) {
    var foot = new BottomBar { Bounds = bounds };
    int h = bounds.Height;
    doneSub = Th.Label("", Th.F(9f, FontStyle.Regular), Th.Dim, Th.D(80), h / 2 + Th.D(2), bounds.Width - Th.D(80) - Th.D(420), Th.D(20));
    doneSub.AutoEllipsis = true;
    int bw = Th.D(196), bh = Th.D(52);
    nextButton.Bounds = new Rectangle(bounds.Width - Th.D(24) - bw, (h - bh) / 2, bw, bh);
    backButton.Bounds = new Rectangle(nextButton.Left - Th.D(14) - Th.D(170), (h - Th.D(44)) / 2, Th.D(170), Th.D(44));
    nextButton.Click += delegate { Next(); };
    backButton.Click += delegate { Back(); };
    foot.Controls.AddRange(new Control[] { doneSub, backButton, nextButton });
    Th.Adopt(foot);
    Controls.Add(foot);
    AcceptButton = nextButton;
  }

  // ---- Navigation -------------------------------------------------------------------------

  void ShowPage(int p) {
    page = p;
    if (p == 1 && dlcFiles.Count == 0 && titleUpdateFile == null) ShowAddedHint();
    if (p == 3) RefreshOverview();
    for (int i = 0; i < pages.Length; i++) pages[i].Visible = i == p;
    sidebar.Current = p == 3 ? 0 : p;
    header.Kicker = p == 1 ? "Setup" : p == 2 ? "Setup" : "Your game";
    header.Title = p == 1 ? "Game files" : p == 2 ? "Installation" : "SSX";
    header.Pill = p == 0 || p == 3 ? "Native Windows build" : null;
    header.PillDot = Install.Built(folder) ? Th.Good : Th.Warn;
    header.Invalidate();
    updateButton.Visible = p == 3;
    UpdateFooter();
    if (p == 1 && isoField.Box.Text.Length == 0) isoField.Box.Focus(); else nextButton.Focus();
  }

  void UpdateFooter() {
    backButton.Visible = page == 1 || page == 3 || (page == 2 && failed);
    backButton.Glyph = page == 3 ? "download" : null;
    backButton.Text = page == 3 ? "Install / Manage" : "Back";
    nextButton.Enabled = true;
    nextButton.Kind = ButtonKind.Primary;
    nextButton.Glyph = null;
    nextButton.Font = Th.Semi(10.5f);
    switch (page) {
      case 0: nextButton.Text = "Get started"; break;
      case 1:
        nextButton.Text = !Install.Built(folder) ? "Install" : DlcOnly() ? "Add DLC" : "Rebuild";
        nextButton.Enabled = Valid();
        break;
      case 2:
        nextButton.Text = running ? "Cancel" : "Retry";
        nextButton.Primary = !running;
        break;
      case 3: nextButton.Text = "Play"; nextButton.Kind = ButtonKind.Play; nextButton.Glyph = "play"; break;
    }
    nextButton.Invalidate();
    backButton.Invalidate();
    sidebar.Allowed = new[] { !running, !running, running || failed || buildStarted };
    UpdateStatus();
  }

  // The sidebar's status card.
  void UpdateStatus() {
    string s;
    Color c;
    if (running) { s = "Building  " + (int)(bar.Value * 100) + "%"; c = Th.Sky; }
    else if (failed) { s = cancelled ? "Build cancelled" : "Build failed"; c = Th.Bad; }
    else if (!Install.Built(folder)) { s = "Not installed"; c = Th.Warn; }
    else if (Install.NeedsRebuild(folder)) { s = "Rebuild needed"; c = Th.Warn; }
    else { s = "Ready to play"; c = Th.Good; }
    if (s == sidebar.Status && c == sidebar.StatusColor) return;
    sidebar.Status = s;
    sidebar.StatusColor = c;
    sidebar.Invalidate();
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

  // Where they are on the console and which tool copies them off: the README (Where do I find these?).
  const string DlcHelp = "Add them once: later updates keep them.";

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
      case 3: PlayNow(); break;
    }
  }

  void PlayNow() {
    MakeShortcut();
    try {
      Play();
      Close();
    } catch (Exception ex) {
      MessageBox.Show(this, "SSX couldn't be started: " + ex.Message, "SSX", MessageBoxButtons.OK, MessageBoxIcon.Warning);
    }
  }

  void Back() {
    if (page == 1) ShowPage(Install.Built(folder) ? 3 : 0);
    else if (page == 2 && !running) ShowPage(1);
    else if (page == 3) { MakeShortcut(); ShowPage(1); }
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
        SetHint(folderHint, string.Format("{0} free on {1} (needs about 10 GB)", FormatGb(gb), drive.Name.TrimEnd('\\')),
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
    firstBuild = !Install.Built(folder);
    running = true;
    buildStarted = true;
    failed = cancelled = false;
    logBox.Clear();
    bar.Failed = stageList.Failed = false;
    installTitle.Text = "Installing SSX";
    doneSub.Text = "Building version " + Install.AppVersion + Dot + folder;
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
    bool blocked = false;
    DataReceivedEventHandler read = delegate(object s, DataReceivedEventArgs a) {
      if (a.Data != null && IsSmartAppControlBlock(a.Data)) blocked = true;
      onLine(a.Data);
    };
    process.OutputDataReceived += read;
    process.ErrorDataReceived += read;
    process.Start();
    if (started != null) started(process);
    process.BeginOutputReadLine();
    process.BeginErrorReadLine();
    process.WaitForExit();
    if (process.ExitCode != 0 && blocked) {
      onLine("");
      onLine("Windows Smart App Control stopped one of the build tools (error 0xc0e90002). It blocks programs");
      onLine("that aren't code-signed, and the game is built on this PC. To build SSX, turn it off: Windows Security >");
      onLine("App & browser control > Smart App Control settings > Off. Then try again. Defender stays on.");
      return SmartAppControlExit;
    }
    return process.ExitCode;
  }

  // Build's exit code when Smart App Control blocked a build tool.
  internal const int SmartAppControlExit = 60;
  internal const string SmartAppControlMessage =
      "Windows Smart App Control blocked the build. Turn it off in Windows Security, then try again.";

  // STATUS_SYSTEM_INTEGRITY_POLICY_VIOLATION, as ninja, cmd or PowerShell print it.
  internal static bool IsSmartAppControlBlock(string line) {
    return line.IndexOf("0xc0e90002", StringComparison.OrdinalIgnoreCase) >= 0 ||
           line.Contains("3236495362") || line.Contains("-1058471934");
  }

  void RunBuild(string iso, string folder, string titleUpdate) {
    try {
      int code = Build(iso, folder, titleUpdate, OnLine, delegate(Process p) { buildProcess = p; });
      bool ok = code == 0 && File.Exists(GameExe());
      if (ok) Install.MarkBuilt(folder);
      Finish(ok, ok ? "" : cancelled ? "Installation cancelled."
                         : code == SmartAppControlExit ? SmartAppControlMessage
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

  // Whether Windows' permission prompt (UAC) is open: consent.exe shows it (checked on Windows 11 26200),
  // and its name is visible to programs that aren't elevated. Best effort: Windows doesn't promise that
  // name, and without it the step just shows its usual text.
  internal static bool UacPromptOpen() {
    Process[] found = Process.GetProcessesByName("consent");
    foreach (Process p in found) p.Dispose();
    return found.Length > 0;
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
      UpdateStatus();
      // Windows' permission prompt (UAC) for a build tool's installer can open minimized in the
      // taskbar, and the step waits for it: say so while it's open (checked about once a second).
      if (s == 2 && ++uacCheck % 12 == 0) uacOpen = UacPromptOpen();
      installStatus.Text = s == 2 && uacOpen ? "Windows is asking for administrator permission: allow it (look in the taskbar)." :
          s == 2 && installing != null ? "Installing " + installing + ": Windows may ask for permission." :
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
      doneSub.Text = "Built in " + Math.Max(1, (int)Math.Round(t.TotalMinutes)) + " min" + Dot + folder;
      // Offered on the first install only: after an update or rebuild the box stays as it was, so
      // Check for updates or Close doesn't point the desktop shortcut at this folder unasked.
      if (firstBuild) shortcutCheck.Checked = true;
      ShowShortcutState();
      ShowPage(3);
    } else {
      failed = true;
      bar.Failed = stageList.Failed = true;
      installTitle.Text = cancelled ? "Installation cancelled" : "Installation failed";
      doneSub.Text = installTitle.Text + Dot + folder;
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
    } else if (Install.Built(folder)) {
      MakeShortcut();  // the overview's Desktop shortcut tile, when it's on
    }
    base.OnFormClosing(e);
  }
}
