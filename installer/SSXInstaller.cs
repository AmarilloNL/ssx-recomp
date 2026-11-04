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

[assembly: AssemblyTitle("SSX PC Port Setup")]
[assembly: AssemblyProduct("SSX PC Port Setup")]
[assembly: AssemblyFileVersion("1.0.0.0")]

static class Program {
  [DllImport("user32.dll")] static extern bool SetProcessDPIAware();

  [STAThread]
  static void Main() {
    try { SetProcessDPIAware(); } catch (Exception) { }
    Application.EnableVisualStyles();
    Application.SetCompatibleTextRenderingDefault(false);
    Application.Run(new InstallerForm());
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
  const string SourceZipUrl = "https://github.com/AmarilloNL/ssx-recomp/archive/refs/heads/main.zip";
  const string RepoUrl = "https://github.com/AmarilloNL/ssx-recomp";

  [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

  static readonly string[] Stages = {
    "Download the port's source code", "Check your ISO", "Install the build tools",
    "Download the recompiler SDK", "Recompile the Xbox 360 code", "Build the PC version" };
  // Where each stage starts on the overall bar (the last entry is the end).
  static readonly float[] StageStart = { 0f, .02f, .04f, .10f, .18f, .36f, 1f };
  static readonly Regex NinjaStep = new Regex(@"^\[(\d+)/(\d+)\]");

  readonly StepRail rail = new StepRail();
  readonly Panel[] pages = new Panel[4];
  readonly FlatButton backButton = new FlatButton("Back", false);
  readonly FlatButton nextButton = new FlatButton("Next", true);

  readonly Field isoField = new Field();
  readonly Field folderField = new Field();
  Label isoHint, folderHint;
  readonly Check ownCheck = new Check("I own SSX for Xbox 360 and made this ISO from my own disc.");

  Label installTitle, installStatus, percentLabel, elapsedLabel;
  LinkLabel detailsLink;
  readonly Bar bar = new Bar();
  readonly StageList stageList = new StageList();
  readonly TextBox logBox = new TextBox();

  Label doneSub;
  readonly Check shortcutCheck = new Check("Create a desktop shortcut");

  readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
  readonly object sync = new object();
  readonly StringBuilder pendingLog = new StringBuilder();
  int stage;
  float stageFrac;
  Stopwatch clock = new Stopwatch();
  Process buildProcess;
  bool running, failed, cancelled, shortcutMade;
  int page;

  public InstallerForm() {
    Text = "SSX PC Port Setup";
    AutoScaleMode = AutoScaleMode.None;
    ClientSize = new Size(Th.D(900), Th.D(600));
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

    timer.Interval = 80;
    timer.Tick += delegate { OnTick(); };
    ShowPage(0);
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
                      "Keyboard and controller support, and DLC from your own console" },
      Bounds = new Rectangle(x, Th.D(122), w, Th.D(120)) });
    var card = new Card {
      Title = "You need your own copy of the game",
      Body = "This installer contains no game code, graphics, music or other EA content, and downloads none. " +
             "You need SSX (USA) for Xbox 360, dumped to an ISO from your own disc.",
      Tone = Th.Warn };
    card.Bounds = new Rectangle(x, Th.D(254), w, card.FitHeight(w));
    p.Controls.Add(card);
  }

  void BuildGame(Panel p) {
    int x = Th.D(40), w = PageWidth;
    p.Controls.Add(Th.Label("Your game", Th.Semi(17f), Th.Text, x, Th.D(26), w, Th.D(40)));
    p.Controls.Add(Th.Label("Pick the ISO you made from your own SSX disc, and where SSX should be installed.",
                            Th.F(10.5f, FontStyle.Regular), Th.Dim, x, Th.D(70), w, Th.D(24)));

    isoHint = AddPathRow(p, "SSX disc image (.iso)", isoField, Th.D(108), BrowseIso);
    folderHint = AddPathRow(p, "Install location", folderField, Th.D(204), BrowseFolder);
    folderField.Box.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "SSX-PC");

    ownCheck.Bounds = new Rectangle(x, Th.D(304), w, Th.D(40));
    ownCheck.CheckedChanged += delegate { UpdateFooter(); };
    p.Controls.Add(ownCheck);
    UpdateHints();
  }

  Label AddPathRow(Panel p, string caption, Field field, int y, EventHandler browse) {
    int x = Th.D(40), w = PageWidth, browseW = Th.D(112), fieldH = Th.D(40);
    p.Controls.Add(Th.Label(caption, Th.Semi(10f), Th.Text, x, y, w, Th.D(24)));
    field.Bounds = new Rectangle(x, y + Th.D(26), w - browseW - Th.D(10), fieldH);
    field.Box.TextChanged += delegate { UpdateHints(); UpdateFooter(); };
    p.Controls.Add(field);
    var browseButton = new FlatButton("Browse...", false) {
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
             "Saves are kept in Documents\\ssx. DLC from your own console goes in Documents\\ssx\\dlc.\r\n" +
             "Run this setup again any time to update SSX to the latest version.",
      Tone = Th.Accent };
    tips.Bounds = new Rectangle(x, Th.D(112), w, tips.FitHeight(w));
    p.Controls.Add(tips);

    int y = tips.Bottom + Th.D(18);
    shortcutCheck.Checked = true;
    shortcutCheck.Bounds = new Rectangle(x, y, w, Th.D(30));
    p.Controls.Add(shortcutCheck);

    var play = new FlatButton("Play SSX", true) { Bounds = new Rectangle(x, y + Th.D(46), Th.D(160), Th.D(44)) };
    play.Font = Th.Semi(11f);
    play.Click += delegate { MakeShortcut(); Play(); };
    var open = new FlatButton("Open folder", false) {
      Bounds = new Rectangle(x + Th.D(172), y + Th.D(46), Th.D(140), Th.D(44)) };
    open.Click += delegate {
      Process.Start(new ProcessStartInfo("explorer.exe", "\"" + Path.GetDirectoryName(GameExe()) + "\""));
    };
    p.Controls.Add(play);
    p.Controls.Add(open);
  }

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
      case 1: nextButton.Text = "Install"; nextButton.Enabled = Valid(); break;
      case 2:
        nextButton.Text = running ? "Cancel" : "Retry";
        nextButton.Primary = !running;
        break;
      case 3: nextButton.Text = "Finish"; break;
    }
    nextButton.Invalidate();
  }

  bool Valid() {
    return ownCheck.Checked && File.Exists(isoField.Box.Text.Trim()) && folderField.Box.Text.Trim().Length > 0;
  }

  void Next() {
    switch (page) {
      case 0: ShowPage(1); break;
      case 1: if (Valid()) StartBuild(); break;
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
    if (page == 1) ShowPage(0);
    else if (page == 2 && !running) ShowPage(1);
  }

  void UpdateHints() {
    string iso = isoField.Box.Text.Trim();
    if (iso.Length == 0) {
      SetHint(isoHint, "SSX (USA), title ID 4541096D. Other versions aren't supported yet.", Th.Faint);
    } else if (File.Exists(iso)) {
      SetHint(isoHint, string.Format("Disc image found ({0:0.0} GB). It is verified before the build starts.",
                                     new FileInfo(iso).Length / 1e9), Th.Good);
    } else {
      SetHint(isoHint, "File not found.", Th.Warn);
    }

    string folder = folderField.Box.Text.Trim();
    try {
      var drive = new DriveInfo(Path.GetPathRoot(Path.GetFullPath(folder)));
      long gb = drive.AvailableFreeSpace >> 30;
      SetHint(folderHint, string.Format("{0} free on {1}  (needs about 10 GB)", FormatGb(gb), drive.Name.TrimEnd('\\')),
              gb >= 10 ? Th.Faint : Th.Warn);
    } catch (Exception) {
      SetHint(folderHint, "Needs about 10 GB of free space.", Th.Faint);
    }
  }

  static string FormatGb(long gb) { return gb >= 1024 ? string.Format("{0:0.0} TB", gb / 1024.0) : gb + " GB"; }

  static void SetHint(Label l, string text, Color c) { l.Text = text; l.ForeColor = c; }

  void BrowseIso(object sender, EventArgs e) {
    using (var dialog = new OpenFileDialog { Title = "Select your own SSX (Xbox 360) ISO",
                                             Filter = "Xbox 360 disc image (*.iso)|*.iso|All files (*.*)|*.*" }) {
      if (dialog.ShowDialog(this) == DialogResult.OK) isoField.Box.Text = dialog.FileName;
    }
  }

  void BrowseFolder(object sender, EventArgs e) {
    using (var dialog = new FolderBrowserDialog { Description = "Where should SSX be installed?" }) {
      if (dialog.ShowDialog(this) == DialogResult.OK) folderField.Box.Text = Path.Combine(dialog.SelectedPath, "SSX-PC");
    }
  }

  // ---- Build ------------------------------------------------------------------------------

  void StartBuild() {
    string iso = isoField.Box.Text.Trim(), folder = folderField.Box.Text.Trim();
    lock (sync) { stage = 0; stageFrac = 0; pendingLog.Length = 0; }
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
    var worker = new Thread(delegate() { RunBuild(iso, folder); });
    worker.IsBackground = true;
    worker.Start();
  }

  void RunBuild(string iso, string folder) {
    try {
      string source = Path.Combine(folder, "source");
      OnLine("==> Downloading the SSX PC port source (no game files)");
      Directory.CreateDirectory(folder);
      string zip = Path.Combine(folder, "source.zip");
      ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
      using (var web = new WebClient()) web.DownloadFile(SourceZipUrl, zip);
      ExtractOver(zip, source);
      File.Delete(zip);

      var info = new ProcessStartInfo("powershell.exe",
          "-NoProfile -ExecutionPolicy Bypass -File \"" + Path.Combine(source, "scripts\\build-ssx.ps1") +
          "\" -Iso \"" + iso + "\" -NoPause");
      info.UseShellExecute = false;
      info.CreateNoWindow = true;
      info.RedirectStandardOutput = true;
      info.RedirectStandardError = true;
      // PowerShell writes redirected output in the console (OEM) code page; non-English
      // Windows messages are garbled when read as the ANSI default.
      var oem = Encoding.GetEncoding(System.Globalization.CultureInfo.CurrentCulture.TextInfo.OEMCodePage);
      info.StandardOutputEncoding = oem;
      info.StandardErrorEncoding = oem;
      info.WorkingDirectory = source;
      buildProcess = new Process { StartInfo = info };
      buildProcess.OutputDataReceived += delegate(object s, DataReceivedEventArgs a) { OnLine(a.Data); };
      buildProcess.ErrorDataReceived += delegate(object s, DataReceivedEventArgs a) { OnLine(a.Data); };
      buildProcess.Start();
      buildProcess.BeginOutputReadLine();
      buildProcess.BeginErrorReadLine();
      buildProcess.WaitForExit();
      bool ok = buildProcess.ExitCode == 0 && File.Exists(GameExe());
      Finish(ok, ok ? "" : cancelled ? "Installation cancelled."
                         : "Something went wrong. The full log is saved as build-ssx.log in the install folder.");
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
      Match m = NinjaStep.Match(line);
      if (m.Success && (stage == 4 || stage == 5)) {
        float total = float.Parse(m.Groups[2].Value);
        if (total >= 20) stageFrac = Math.Max(stageFrac, float.Parse(m.Groups[1].Value) / total);
      }
    }
  }

  static int StageOf(string text) {
    if (text.StartsWith("Downloading the SSX")) return 0;
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
    lock (sync) {
      text = pendingLog.ToString();
      pendingLog.Length = 0;
      s = stage;
      frac = stageFrac;
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
      installStatus.Text = Stages[s] + "..." +
          (s == 2 ? "  Windows may ask for administrator permission." :
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
      doneSub.Text = "Built in " + Math.Max(1, (int)Math.Round(t.TotalMinutes)) + " min.  Installed to " +
                     Path.GetDirectoryName(GameExe());
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

  string GameExe() { return Path.Combine(folderField.Box.Text.Trim(), "source\\dist\\SSX\\ssx.exe"); }

  void Play() {
    string exe = GameExe();
    Process.Start(new ProcessStartInfo(exe) { WorkingDirectory = Path.GetDirectoryName(exe) });
  }

  void MakeShortcut() {
    if (!shortcutCheck.Checked || shortcutMade) return;
    shortcutMade = true;
    try {
      string exe = GameExe(), dir = Path.GetDirectoryName(exe);
      string lnk = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "SSX.lnk");
      Type shellType = Type.GetTypeFromProgID("WScript.Shell");
      object shell = Activator.CreateInstance(shellType);
      object sc = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { lnk });
      Type t = sc.GetType();
      t.InvokeMember("TargetPath", BindingFlags.SetProperty, null, sc, new object[] { exe });
      t.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, sc, new object[] { dir });
      t.InvokeMember("Description", BindingFlags.SetProperty, null, sc, new object[] { "SSX (2012) PC port" });
      string ico = Path.Combine(dir, "ssx.ico");
      if (File.Exists(ico)) t.InvokeMember("IconLocation", BindingFlags.SetProperty, null, sc, new object[] { ico });
      t.InvokeMember("Save", BindingFlags.InvokeMethod, null, sc, null);
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
