using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using Windows.ApplicationModel;
using Windows.Storage;
using Windows.System;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;
using Windows.UI.Xaml.Media.Animation;

namespace ScummVMLauncher
{
    public sealed partial class MainPage : Page
    {
        private static readonly object LogLock = new object();
        private static string LocalPath;
        private static string LogPath;

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
        private static extern void OutputDebugStringA(string lpOutputString);

        public MainPage()
        {
            InitializeComponent();
        }

        private void SplashImage_ImageOpened(object sender, Windows.UI.Xaml.RoutedEventArgs e)
        {
            Log("Splash image opened OK.");
        }

        private void SplashImage_ImageFailed(object sender, Windows.UI.Xaml.ExceptionRoutedEventArgs e)
        {
            Log("Splash image FAILED: " + e.ErrorMessage);
        }

        private static readonly string[] BootQuotes =
        {
            "You fight like a dairy farmer!",
            "How appropriate. You fight like a cow.",
            "I'm Guybrush Threepwood, mighty pirate!",
            "You're the biggest pirate I've ever seen.",
            "A pirate's life is the life for me!",
            "I am the magnificent pirate Guybrush Threepwood!",
            "I'm not stopping until my father's ghost is avenged!",
            "I was just sitting here, eating my lunch.",
            "Are you ready to go riding?",
            "I know it was you, Freddie. You broke my heart.",
            "Mister... do you know why you're here?",
            "That's it! I'm gonna have to kick some butt!",
            "I hope the tentacles don't eat my hamster.",
            "Time to make a decision.",
            "I'm a tentacle, baby.",
            "I'm going to change the world!",
            "I used to be a human until I got a bath.",
            "This is the end of the world!",
            "Guybrush, stop talking like an idiot.",
            "I'm Manny Calavera.",
            "Every year, thousands of souls are shipped through the land of the dead.",
            "You're in the land of the dead now, Manny.",
            "It's a long trip to the Ninth Underworld.",
            "Happy trails, Manuel.",
            "These belong in a museum!",
            "I should have known you were going to say that.",
            "It's not safe here. Let's go.",
            "The threads of destiny...",
            "I've got a voodoo head for you.",
            "Now is the time to become one with the cosmos."
        };

        private volatile bool _quotesActive;

        private async void ShowBootQuotes()
        {
            _quotesActive = true;
            await System.Threading.Tasks.Task.Delay(1200);
            if (!_quotesActive)
                return;

            var rnd = new Random();
            var pick = BootQuotes.OrderBy(_ => rnd.Next()).Take(rnd.Next(1, 3)).ToArray();

            foreach (var q in pick)
            {
                if (!_quotesActive)
                    return;
                SetStatus(q);
                await System.Threading.Tasks.Task.Delay(1900);
            }
        }

        private void SetStatus(string msg)
        {
            if (StatusText.Opacity > 0.05)
            {
                var fadeOut = new DoubleAnimation
                {
                    To = 0.0,
                    Duration = TimeSpan.FromMilliseconds(300)
                };
                Storyboard.SetTarget(fadeOut, StatusText);
                Storyboard.SetTargetProperty(fadeOut, "Opacity");
                var sb = new Storyboard();
                sb.Children.Add(fadeOut);
                fadeOut.Completed += (s, e) =>
                {
                    StatusText.Text = msg;
                    var fadeIn = new DoubleAnimation
                    {
                        From = 0.0,
                        To = 1.0,
                        Duration = TimeSpan.FromMilliseconds(300)
                    };
                    Storyboard.SetTarget(fadeIn, StatusText);
                    Storyboard.SetTargetProperty(fadeIn, "Opacity");
                    var sb2 = new Storyboard();
                    sb2.Children.Add(fadeIn);
                    sb2.Begin();
                };
                sb.Begin();
            }
            else
            {
                StatusText.Text = msg;
                var fadeIn = new DoubleAnimation
                {
                    From = 0.0,
                    To = 1.0,
                    Duration = TimeSpan.FromMilliseconds(300)
                };
                Storyboard.SetTarget(fadeIn, StatusText);
                Storyboard.SetTargetProperty(fadeIn, "Opacity");
                var sb2 = new Storyboard();
                sb2.Children.Add(fadeIn);
                sb2.Begin();
            }
        }

        public async void Run()
        {
            LocalPath = ApplicationData.Current.LocalFolder.Path;
            LogPath = Path.Combine(LocalPath, "launcher.log");
            RotateLog(LogPath, 3);
            Log("=== ScummVM launcher started ===");
            var pv = Package.Current.Id.Version;
            Log("Package: " + Package.Current.Id.Name + " v" + pv.Major + "." + pv.Minor + "." + pv.Build + "." + pv.Revision);
            LogInstalledPayload();
            LogRetroArchProcesses("startup");
            CreditStoryboard.Begin();

            try
            {
                SetStatus("Preparing ScummVM...");
                ShowBootQuotes();
                Log("Bootstrap started. LocalState=" + LocalPath);
                var started = DateTime.UtcNow;
                await System.Threading.Tasks.Task.Run(() => Bootstrap());
                var elapsed = DateTime.UtcNow - started;
                if (elapsed < TimeSpan.FromSeconds(5))
                    await System.Threading.Tasks.Task.Delay(TimeSpan.FromSeconds(5) - elapsed);
                Log("Bootstrap finished.");
            }
            catch (Exception ex)
            {
                _quotesActive = false;
                Log("Bootstrap FAILED: " + ex);
                SetStatus("Preparation failed: " + ex.Message);
                return;
            }

            _quotesActive = false;
            SeedRetroArchConfig();

            SetStatus("Starting ScummVM...");
            Log("Launching scummvm-core: protocol...");
            string logFile = Path.Combine(LocalPath, "retroarch.log").Replace('\\', '/');
            var uri = new Uri(
                "scummvm-core:?cmd=retroarch -v --log-file=" + logFile +
                " -L cores\\scummvm_libretro.dll" +
                "&launchOnExit=scummvm-launcher:?cmd=exit");
            Log("URI: " + uri);

            bool launchStarted = await LaunchWithRetry(uri, 4);

            if (launchStarted)
            {
                Log("Launcher exiting (Application.Current.Exit).");
                Application.Current.Exit();
            }
            else
            {
                Log("ERROR: failed to start ScummVM after retries.");
                SetStatus("Could not start ScummVM.");
            }
        }

        private async System.Threading.Tasks.Task<bool> LaunchWithRetry(Uri uri, int maxAttempts)
        {
            for (int attempt = 1; attempt <= maxAttempts; attempt++)
            {
                Log("=== Activation attempt " + attempt + "/" + maxAttempts + " ===");

                bool raAlive = HasRetroArchProcess();
                SetStatus("Attempt " + attempt + "/" + maxAttempts + " - checking RetroArch...");
                Log("RA alive before attempt: " + raAlive);

                if (raAlive)
                {
                    // If RA is already running, its cmd is ignored (m_initialized
                    // guard in uwp_main.cpp). Force it to exit so the next
                    // activation re-inits with our cmd.
                    Log("RA still running; sending forceExit.");
                    await ForceExitRetroArch();
                    await WaitForRetroArchExit(TimeSpan.FromSeconds(8));
                }

                RotateLog(Path.Combine(LocalPath, "retroarch.log"), 3);

                string support = await QueryProtocolSupport(uri);
                Log("Protocol support: " + support);

                LogRetroArchProcesses("before LaunchUriAsync");
                bool ok = false;
                string launchError = null;
                try
                {
                    ok = await Launcher.LaunchUriAsync(uri);
                }
                catch (Exception ex)
                {
                    launchError = ex.ToString();
                    Log("LaunchUriAsync THREW: " + ex);
                }
                Log("LaunchUriAsync ok=" + ok + (launchError != null ? " err=" + launchError : ""));
                LogRetroArchProcesses("after LaunchUriAsync");

                if (ok || launchError != null)
                {
                    bool grew = await PollRetroArchLog();
                    if (grew)
                    {
                        Log("RetroArch is up. Launch succeeded on attempt " + attempt + ".");
                        return true;
                    }
                }

                Log("Attempt " + attempt + " did not produce a running RetroArch; " +
                    (attempt < maxAttempts ? "retrying." : "giving up."));
                SetStatus("Retrying... (" + attempt + "/" + maxAttempts + ")");
                await System.Threading.Tasks.Task.Delay(1500);
            }
            return false;
        }

        private static bool HasRetroArchProcess()
        {
            try
            {
                foreach (var p in Windows.System.Diagnostics.ProcessDiagnosticInfo.GetForProcesses())
                {
                    string name = "";
                    try { name = p.ExecutableFileName ?? ""; } catch { }
                    if (name.IndexOf("RetroArch", StringComparison.OrdinalIgnoreCase) >= 0)
                        return true;
                }
            }
            catch (Exception ex)
            {
                Log("HasRetroArchProcess FAILED: " + ex.Message);
            }
            return false;
        }

        private static async System.Threading.Tasks.Task ForceExitRetroArch()
        {
            try
            {
                var forceUri = new Uri("scummvm-core:?forceExit");
                Log("Sending forceExit...");
                bool ok = await Launcher.LaunchUriAsync(forceUri);
                Log("forceExit ok=" + ok);
            }
            catch (Exception ex)
            {
                Log("forceExit THREW: " + ex.Message);
            }
        }

        private static async System.Threading.Tasks.Task WaitForRetroArchExit(TimeSpan timeout)
        {
            var deadline = DateTime.UtcNow + timeout;
            while (DateTime.UtcNow < deadline)
            {
                if (!HasRetroArchProcess())
                {
                    Log("RetroArch exited after forceExit.");
                    return;
                }
                await System.Threading.Tasks.Task.Delay(500);
            }
            Log("RetroArch did not exit within timeout; proceeding anyway.");
        }

        private static async System.Threading.Tasks.Task<string> QueryProtocolSupport(Uri uri)
        {
            try
            {
                var status = await Launcher.QueryUriSupportAsync(uri, LaunchQuerySupportType.Uri);
                return status.ToString();
            }
            catch (Exception ex)
            {
                return "query FAILED: " + ex.Message;
            }
        }

        private static async System.Threading.Tasks.Task<bool> PollRetroArchLog()
        {
            string raLog = Path.Combine(LocalPath, "retroarch.log");
            bool existed = File.Exists(raLog);
            long lastLen = existed ? new FileInfo(raLog).Length : 0;
            Log("Polling retroarch.log (exists=" + existed + ", len=" + lastLen + ")...");
            for (int i = 0; i < 20; i++)
            {
                await System.Threading.Tasks.Task.Delay(500);
                if (File.Exists(raLog))
                {
                    var fi = new FileInfo(raLog);
                    if (fi.Length > lastLen)
                    {
                        Log("retroarch.log grew (" + lastLen + " -> " + fi.Length + " bytes, mtime " + fi.LastWriteTime.ToString("HH:mm:ss") + ").");
                        DumpRetroArchTail();
                        return true;
                    }
                }
            }
            Log("retroarch.log did not appear/grow within 10s.");
            DumpRetroArchTail();
            return false;
        }

        private static void DumpRetroArchTail()
        {
            string raLog = Path.Combine(LocalPath, "retroarch.log");
            try
            {
                if (!File.Exists(raLog))
                {
                    Log("(no retroarch.log to dump)");
                    return;
                }
                string[] lines = File.ReadAllLines(raLog);
                Log("--- retroarch.log tail (" + lines.Length + " lines) ---");
                for (int i = Math.Max(0, lines.Length - 25); i < lines.Length; i++)
                    Log("RA| " + lines[i]);
                Log("--- end retroarch.log tail ---");
            }
            catch (Exception ex)
            {
                Log("tail dump failed: " + ex.Message);
            }
        }

        private static void LogInstalledPayload()
        {
            try
            {
                string root = Package.Current.InstalledLocation.Path;
                string exe = System.IO.Path.Combine(root, "RetroArch-msvcUWP.exe");
                if (File.Exists(exe))
                {
                    var fi = new FileInfo(exe);
                    Log("Payload: RetroArch-msvcUWP.exe = " + fi.Length + " bytes");
                }
                else
                    Log("Payload: RetroArch-msvcUWP.exe MISSING at " + exe);
                string core = System.IO.Path.Combine(root, "cores", "scummvm_libretro.dll");
                if (File.Exists(core))
                {
                    var fi = new FileInfo(core);
                    using (var sha = System.Security.Cryptography.SHA1.Create())
                    using (var fs = File.OpenRead(core))
                    {
                        byte[] h = sha.ComputeHash(fs);
                        Log("Payload: scummvm_libretro.dll = " + fi.Length + " bytes sha1=" + BitConverter.ToString(h).Replace("-", "").Substring(0, 16));
                    }
                }
                else
                    Log("Payload: scummvm_libretro.dll MISSING at " + core);
            }
            catch (Exception ex)
            {
                Log("Payload fingerprint FAILED: " + ex.Message);
            }
        }

        private static void LogRetroArchProcesses(string phase)
        {
            try
            {
                int n = 0;
                foreach (var p in Windows.System.Diagnostics.ProcessDiagnosticInfo.GetForProcesses())
                {
                    string name = "";
                    try { name = p.ExecutableFileName ?? ""; } catch { }
                    if (name.IndexOf("RetroArch", StringComparison.OrdinalIgnoreCase) < 0 && p.ProcessId != 0)
                        continue;
                    string ws = "";
                    try { ws = " wsMB=" + Math.Round(p.MemoryUsage.GetReport().WorkingSetSizeInBytes / 1048576.0, 1); } catch { }
                    string st = "";
                    try { st = " start=" + p.ProcessStartTime.LocalDateTime.ToString("HH:mm:ss"); } catch { }
                    Log("RA processes (" + phase + "): pid=" + p.ProcessId + ws + st + " (" + name + ")");
                    n++;
                }
                if (n == 0)
                    Log("RA processes (" + phase + "): none");
            }
            catch (Exception ex)
            {
                Log("RA process check failed (" + phase + "): " + ex.Message);
            }
        }

        private static void Bootstrap()
        {
            string sysDir = Path.Combine(LocalPath, "system");
            string flag = Path.Combine(sysDir, ".scummvm-ready");

            if (File.Exists(flag))
            {
                Log("Flag .scummvm-ready present; skipping extraction.");
                return;
            }

            Directory.CreateDirectory(sysDir);

            string zipPath = Path.Combine(Package.Current.InstalledLocation.Path, "system", "scummvm.zip");
            Log("Extracting " + zipPath + " -> " + sysDir);
            if (!File.Exists(zipPath))
            {
                Log("ERROR: scummvm.zip does not exist at " + zipPath);
                throw new FileNotFoundException("scummvm.zip", zipPath);
            }
            ZipFile.ExtractToDirectory(zipPath, sysDir);
            Log("Extraction done. Contents of " + sysDir + ":");
            foreach (string entry in Directory.GetFileSystemEntries(sysDir))
            {
                bool isDir = Directory.Exists(entry);
                string size = "";
                if (!isDir)
                {
                    var fi = new FileInfo(entry);
                    size = " (" + fi.Length + " bytes)";
                }
                Log("  " + (isDir ? "[dir] " : "[file]") + Path.GetFileName(entry) + size);
            }

            string themeDir = Path.Combine(sysDir, "scummvm", "theme");
            Log("Contents of " + themeDir + ":");
            if (Directory.Exists(themeDir))
            {
                foreach (string entry in Directory.GetFileSystemEntries(themeDir))
                {
                    var fi = new FileInfo(entry);
                    Log("  " + Path.GetFileName(entry) + " (" + fi.Length + " bytes)");
                }
            }
            else
            {
                Log("  [ERROR] theme directory does NOT exist!");
            }

            string theme = Path.Combine(themeDir, "scummremastered.zip");
            Log("Theme scummremastered present: " + File.Exists(theme));
            string themeClassic = Path.Combine(themeDir, "scummclassic.zip");
            string themeModern = Path.Combine(themeDir, "scummmodern.zip");
            Log("Additional themes: classic=" + File.Exists(themeClassic) + " modern=" + File.Exists(themeModern));

            string iniPath = Path.Combine(sysDir, "scummvm.ini");
            File.WriteAllText(
                iniPath,
                "[scummvm]\ngui_theme=scummremastered\ngui_scale=150\n");
            Log("scummvm.ini written: " + iniPath);
            Log("Contents read back:");
            foreach (string line in File.ReadAllLines(iniPath))
                Log("  " + line);

            File.WriteAllText(flag, DateTime.UtcNow.ToString("o"));
        }

        private static void SeedRetroArchConfig()
        {
            try
            {
                string cfg = Path.Combine(LocalPath, "retroarch.cfg");
                string src = Path.Combine(Package.Current.InstalledLocation.Path, "retroarch.cfg");

                if (!File.Exists(src))
                {
                    Log("ERROR: bundled retroarch.cfg MISSING at " + src);
                    return;
                }

                var srcInfo = new FileInfo(src);
                if (File.Exists(cfg))
                {
                    var oldInfo = new FileInfo(cfg);
                    Log("retroarch.cfg already present (" + oldInfo.Length + " bytes); not re-seeding.");
                    return;
                }
                Log("retroarch.cfg missing; seeding bundled config (" + srcInfo.Length + " bytes).");

                File.Copy(src, cfg, true);

                using (var sha = System.Security.Cryptography.SHA1.Create())
                using (var fs = File.OpenRead(cfg))
                {
                    byte[] h = sha.ComputeHash(fs);
                    Log("retroarch.cfg seeded: " + srcInfo.Length + " bytes sha1=" + BitConverter.ToString(h).Replace("-", "").Substring(0, 16));
                }
            }
            catch (Exception ex)
            {
                Log("ERROR pre-configuring retroarch.cfg: " + ex.Message);
            }
        }

        private static void RotateLog(string path, int keep)
        {
            try
            {
                if (!File.Exists(path))
                    return;
                for (int i = keep - 1; i >= 1; i--)
                {
                    string from = path + "." + i;
                    string to = path + "." + (i + 1);
                    if (File.Exists(to))
                        File.Delete(to);
                    if (File.Exists(from))
                        File.Move(from, to);
                }
                File.Move(path, path + ".1");
                Log("Rotated " + path + " -> .1 (kept " + keep + " generations).");
            }
            catch (Exception ex)
            {
                Log("RotateLog failed for " + path + ": " + ex.Message);
            }
        }

        private static void Log(string msg)
        {
            string line = DateTime.Now.ToString("HH:mm:ss.fff ") + msg;
            OutputDebugStringA("[launcher] " + line);
            try
            {
                lock (LogLock)
                {
                    File.AppendAllText(LogPath, line + Environment.NewLine);
                }
            }
            catch { }
        }
    }
}
