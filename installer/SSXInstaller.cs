// SSX (2012) PC port - installer.
//
// Downloads this project's source (no game code or data), then runs
// scripts\build-ssx.ps1 against the player's own SSX ISO, which installs the
// build tools, recompiles the game on this PC and produces the playable folder.
//
// Built with the C# compiler that ships with Windows (.NET Framework 4.x, C# 5):
//   installer\build-installer.ps1

using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Threading;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("SSX PC Port Installer")]
[assembly: System.Reflection.AssemblyProduct("SSX PC Port Installer")]

static class Program {
  [STAThread]
  static void Main() {
    Application.EnableVisualStyles();
    Application.SetCompatibleTextRenderingDefault(false);
    Application.Run(new InstallerForm());
  }
}

class InstallerForm : Form {
  const string SourceZipUrl = "https://github.com/AmarilloNL/ssx-recomp/archive/refs/heads/main.zip";

  static readonly Color Bg = Color.FromArgb(9, 15, 23);
  static readonly Color Panel = Color.FromArgb(18, 28, 40);
  static readonly Color Accent = Color.FromArgb(92, 209, 245);
  static readonly Color Warn = Color.FromArgb(255, 199, 77);
  static readonly Color TextColor = Color.FromArgb(230, 242, 250);
  static readonly Color Dim = Color.FromArgb(140, 165, 184);

  readonly TextBox isoBox = new TextBox();
  readonly TextBox folderBox = new TextBox();
  readonly CheckBox ownBox = new CheckBox();
  readonly Button buildButton = new Button();
  readonly Button playButton = new Button();
  readonly ProgressBar progress = new ProgressBar();
  readonly Label status = new Label();
  readonly TextBox logBox = new TextBox();
  Process buildProcess;

  public InstallerForm() {
    Text = "SSX PC Port Installer";
    ClientSize = new Size(820, 700);
    FormBorderStyle = FormBorderStyle.FixedSingle;
    MaximizeBox = false;
    StartPosition = FormStartPosition.CenterScreen;
    BackColor = Bg;
    ForeColor = TextColor;
    Font = new Font("Segoe UI", 10f);

    var title = new Label { Text = "SSX  PC PORT", Font = new Font("Segoe UI", 22f, FontStyle.Bold),
                            ForeColor = Accent, AutoSize = true, Location = new Point(24, 18) };
    var subtitle = new Label { Text = "Builds a native PC version of SSX (2012) from your own Xbox 360 disc.",
                               ForeColor = Dim, AutoSize = true, Location = new Point(28, 64) };

    var notice = new Label {
      Text = "YOU NEED YOUR OWN COPY OF THE GAME\r\n\r\n" +
             "This installer contains no game code, graphics, music or other EA content, and downloads none. " +
             "You must own SSX (2012) for Xbox 360 and dump it to an ISO yourself. " +
             "Supported: SSX (USA), title ID 4541096D. Do not ask for ISO files; they will not be provided.",
      ForeColor = Warn, BackColor = Panel, Location = new Point(24, 98), Size = new Size(772, 130),
      Padding = new Padding(14, 12, 14, 12), Font = new Font("Segoe UI", 10f) };

    AddRow("Your SSX ISO", isoBox, 248, BrowseIso);
    AddRow("Install folder", folderBox, 312, BrowseFolder);
    folderBox.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "SSX-PC");

    ownBox.Text = "I own SSX for Xbox 360 and made this ISO from my own disc.";
    ownBox.ForeColor = TextColor;
    ownBox.AutoSize = true;
    ownBox.Location = new Point(26, 370);
    ownBox.CheckedChanged += delegate { UpdateButtons(); };

    StyleButton(buildButton, "Build SSX", true);
    buildButton.Location = new Point(24, 406);
    buildButton.Size = new Size(180, 42);
    buildButton.Click += delegate { StartBuild(); };

    StyleButton(playButton, "Play", true);
    playButton.Location = new Point(216, 406);
    playButton.Size = new Size(120, 42);
    playButton.Visible = false;
    playButton.Click += delegate { Play(); };

    progress.Location = new Point(352, 418);
    progress.Size = new Size(444, 18);
    progress.Style = ProgressBarStyle.Marquee;
    progress.Visible = false;

    status.Location = new Point(24, 458);
    status.Size = new Size(772, 24);
    status.ForeColor = Dim;
    status.Text = "Needs about 30 GB free, an internet connection and 30-60 minutes.";

    logBox.Multiline = true;
    logBox.ReadOnly = true;
    logBox.ScrollBars = ScrollBars.Vertical;
    logBox.BackColor = Panel;
    logBox.ForeColor = Dim;
    logBox.BorderStyle = BorderStyle.None;
    logBox.Font = new Font("Consolas", 9f);
    logBox.Location = new Point(24, 488);
    logBox.Size = new Size(772, 192);

    Controls.AddRange(new Control[] { title, subtitle, notice, ownBox, buildButton, playButton, progress, status, logBox });
    UpdateButtons();
  }

  void AddRow(string label, TextBox box, int y, EventHandler browse) {
    Controls.Add(new Label { Text = label, ForeColor = TextColor, AutoSize = true, Location = new Point(24, y) });
    box.Location = new Point(24, y + 24);
    box.Size = new Size(640, 28);
    box.BackColor = Panel;
    box.ForeColor = TextColor;
    box.BorderStyle = BorderStyle.FixedSingle;
    box.TextChanged += delegate { UpdateButtons(); };
    var button = new Button();
    StyleButton(button, "Browse...", false);
    button.Location = new Point(676, y + 22);
    button.Size = new Size(120, 32);
    button.Click += browse;
    Controls.Add(box);
    Controls.Add(button);
  }

  static void StyleButton(Button b, string text, bool primary) {
    b.Text = text;
    b.FlatStyle = FlatStyle.Flat;
    b.FlatAppearance.BorderColor = Accent;
    b.BackColor = primary ? Color.FromArgb(30, 86, 112) : Panel;
    b.ForeColor = TextColor;
    b.Cursor = Cursors.Hand;
  }

  void UpdateButtons() {
    bool running = buildProcess != null && !buildProcess.HasExited;
    buildButton.Enabled = !running && ownBox.Checked && File.Exists(isoBox.Text) && folderBox.Text.Length > 0;
  }

  void BrowseIso(object sender, EventArgs e) {
    using (var dialog = new OpenFileDialog { Title = "Select your own SSX (Xbox 360) ISO",
                                             Filter = "Xbox 360 disc image (*.iso)|*.iso" }) {
      if (dialog.ShowDialog(this) == DialogResult.OK) isoBox.Text = dialog.FileName;
    }
  }

  void BrowseFolder(object sender, EventArgs e) {
    using (var dialog = new FolderBrowserDialog { Description = "Where should SSX be built and installed?" }) {
      if (dialog.ShowDialog(this) == DialogResult.OK) folderBox.Text = Path.Combine(dialog.SelectedPath, "SSX-PC");
    }
  }

  void Log(string line) {
    if (line == null) return;
    if (InvokeRequired) { BeginInvoke(new Action<string>(Log), line); return; }
    logBox.AppendText(line + "\r\n");
    if (line.StartsWith("==> ")) status.Text = line.Substring(4);
  }

  void StartBuild() {
    string iso = isoBox.Text, folder = folderBox.Text;
    buildButton.Enabled = false;
    playButton.Visible = false;
    progress.Visible = true;
    logBox.Clear();
    status.ForeColor = Dim;
    var worker = new Thread(delegate() { RunBuild(iso, folder); });
    worker.IsBackground = true;
    worker.Start();
  }

  void RunBuild(string iso, string folder) {
    try {
      string source = Path.Combine(folder, "source");
      Log("==> Downloading the SSX PC port source (no game files)");
      Directory.CreateDirectory(folder);
      string zip = Path.Combine(folder, "source.zip");
      ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
      using (var web = new WebClient()) web.DownloadFile(SourceZipUrl, zip);
      ExtractOver(zip, source);
      File.Delete(zip);

      Log("==> Starting the build");
      var info = new ProcessStartInfo("powershell.exe",
          "-NoProfile -ExecutionPolicy Bypass -File \"" + Path.Combine(source, "scripts\\build-ssx.ps1") +
          "\" -Iso \"" + iso + "\" -NoPause");
      info.UseShellExecute = false;
      info.CreateNoWindow = true;
      info.RedirectStandardOutput = true;
      info.RedirectStandardError = true;
      info.WorkingDirectory = source;
      buildProcess = new Process { StartInfo = info };
      buildProcess.OutputDataReceived += delegate(object s, DataReceivedEventArgs a) { Log(a.Data); };
      buildProcess.ErrorDataReceived += delegate(object s, DataReceivedEventArgs a) { Log(a.Data); };
      buildProcess.Start();
      buildProcess.BeginOutputReadLine();
      buildProcess.BeginErrorReadLine();
      buildProcess.WaitForExit();
      bool ok = buildProcess.ExitCode == 0 && File.Exists(GameExe(folder));
      Finish(ok, ok ? "Done! Click Play, or start ssx.exe in " + Path.Combine(source, "dist\\SSX")
                    : "The build failed. See the log above (also saved as build-ssx.log).");
    } catch (Exception ex) {
      Log(ex.ToString());
      Finish(false, "Something went wrong: " + ex.Message);
    }
  }

  // Extracts the GitHub archive (single top-level folder) over `target`, keeping
  // earlier build output (tools, out, dist) so a re-run only rebuilds what changed.
  static void ExtractOver(string zip, string target) {
    using (var archive = ZipFile.OpenRead(zip)) {
      foreach (var entry in archive.Entries) {
        int slash = entry.FullName.IndexOf('/');
        string rel = slash >= 0 ? entry.FullName.Substring(slash + 1) : entry.FullName;
        if (rel.Length == 0) continue;
        string path = Path.Combine(target, rel.Replace('/', '\\'));
        if (rel.EndsWith("/")) { Directory.CreateDirectory(path); continue; }
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        entry.ExtractToFile(path, true);
      }
    }
  }

  static string GameExe(string folder) { return Path.Combine(folder, "source\\dist\\SSX\\ssx.exe"); }

  void Finish(bool ok, string message) {
    if (InvokeRequired) { BeginInvoke(new Action<bool, string>(Finish), ok, message); return; }
    progress.Visible = false;
    status.Text = message;
    status.ForeColor = ok ? Accent : Warn;
    playButton.Visible = ok;
    UpdateButtons();
  }

  void Play() {
    string exe = GameExe(folderBox.Text);
    Process.Start(new ProcessStartInfo(exe) { WorkingDirectory = Path.GetDirectoryName(exe) });
  }

  protected override void OnFormClosing(FormClosingEventArgs e) {
    if (buildProcess != null && !buildProcess.HasExited &&
        MessageBox.Show(this, "The build is still running. Stop it and close?", "SSX PC Port Installer",
                        MessageBoxButtons.YesNo, MessageBoxIcon.Warning) == DialogResult.No) {
      e.Cancel = true;
      return;
    }
    if (buildProcess != null && !buildProcess.HasExited) {
      try { buildProcess.Kill(); } catch (Exception) { }
    }
    base.OnFormClosing(e);
  }
}
