using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using Windows.ApplicationModel;
using Windows.Storage;
using Windows.System;
using Windows.UI.Core;
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
            TryUseELogs();
            RotateLog(LogPath, 3);
            Log("=== ScummVM launcher started ===");
            Log("launcher.log path: " + LogPath);
            var pv = Package.Current.Id.Version;
            Log("Package: " + Package.Current.Id.Name + " v" + pv.Major + "." + pv.Minor + "." + pv.Build + "." + pv.Revision);
            LogInstalledPayload();
            LogRetroArchProcesses("startup");
            LogBootstrapDiagnostics();
            CreditStoryboard.Begin();
            SetStateIndicator("starting", "#455A64", "state: starting");

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

            Log("Drive check: E: exists=" + FromAppFile.DriveExists('E') + " allDrives=[" + FromAppFile.DriveList() + "]");
            bool raReal = await IsRealRetroArchInstalled();
            if (raReal)
            {
                SetStatus("RetroArch found...");
                Log("Real RetroArch installed; trying it first.");

                bool eDrive = FromAppFile.DriveExists('E');
                Log("Drive E: present: " + eDrive);

                if (eDrive)
                {
                    string ver = pv.Major + "." + pv.Minor + "." + pv.Build + "." + pv.Revision;
                    Log("Staging to E:\\ started (app version " + ver + ").");
                    SetStateIndicator("staging", "#F9A825", "state: staging to E:\\ (yellow)");
                    SetProgressPanel(true);
                    bool staged = false;
                    var stageStart = DateTime.UtcNow;
                    try
                    {
                        staged = await System.Threading.Tasks.Task.Run(() => StageToE(ver));
                    }
                    catch (Exception ex)
                    {
                        Log("StageToE THREW: " + ex);
                        staged = false;
                    }
                    Log("Staging result: staged=" + staged + " elapsed=" + (DateTime.UtcNow - stageStart).TotalSeconds + "s");
                    SetProgressPanel(false);

                    if (staged)
                    {
                        bool? lastCore = CoreLastRunResolved();
                        Log("Last real-RA core resolution: " + (lastCore.HasValue ? lastCore.ToString() : "unknown"));
                        if (lastCore == false)
                        {
                            Log("Real RetroArch lacks the scummvm core (from last run's log); falling back to bundled.");
                            SetStateIndicator("fallback", "#1B5E20", "state: fallback bundled (dark green)");
                        }
                        else
                        {
                            Log("E:\\ staging ok; launching real RetroArch with its own core.");
                            SetStateIndicator("real", "#7B1FA2", "state: real RetroArch (purple)");
                            string realLog = @"E:\scummvm\logs\retroarch-real.log".Replace('\\', '/');
                            var realUri = new Uri(
                                "retroarch:?cmd=retroarch -v --log-file=" + realLog +
                                " -L scummvm_libretro.dll" +
                                " -c E:\\scummvm\\retroarch.cfg" +
                                "&launchOnExit=scummvm-launcher:?cmd=exit");
                            Log("OUR LocalState: " + LocalPath);
                            Log("OUR package family: " + Package.Current.Id.FamilyName);
                            Log("Staged system dir E:\\scummvm\\system exists: " + FromAppFile.Exists(@"E:\scummvm\system"));
                            Log("URI (real RA): " + realUri);

                            bool realOk = await LaunchWithRetry(realUri, 4, useBundled: false);
                            Log("Real RetroArch launch result: realOk=" + realOk);
                            if (realOk)
                            {
                                Log("Started via real RetroArch; exiting launcher.");
                                Application.Current.Exit();
                                return;
                            }
                        }
                    }
                    else
                    {
                        Log("E:\\ staging failed; falling back to bundled shell.");
                        SetStateIndicator("fallback", "#1B5E20", "state: fallback bundled (dark green)");
                    }
                }
                else
                {
                    Log("Drive E: missing; falling back to bundled shell.");
                    SetStateIndicator("fallback", "#1B5E20", "state: fallback bundled (dark green)");
                }

                Log("Real RetroArch path failed; falling back to bundled shell.");
                SetStateIndicator("fallback", "#1B5E20", "state: fallback bundled (dark green)");
                SetStatus("Retrying with bundled engine...");
            }
            else
            {
                Log("Real RetroArch not installed; using bundled shell.");
                SetStateIndicator("fallback", "#1B5E20", "state: fallback bundled (dark green)");
            }

            SetStatus("Starting ScummVM...");
            SetStateIndicator("fallback", "#1B5E20", "state: fallback bundled (dark green)");
            Log("Launching scummvm-core: protocol...");
            string logFile = ResolveRetroLogPath().Replace('\\', '/');
            var uri = new Uri(
                "scummvm-core:?cmd=retroarch -v --log-file=" + logFile +
                " -L cores\\scummvm_libretro.dll" +
                "&launchOnExit=scummvm-launcher:?cmd=exit");
            Log("URI: " + uri);

            bool launchStarted = await LaunchWithRetry(uri, 4, useBundled: true);

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

        private async System.Threading.Tasks.Task<bool> LaunchWithRetry(Uri uri, int maxAttempts, bool useBundled)
        {
            for (int attempt = 1; attempt <= maxAttempts; attempt++)
            {
                Log("=== Activation attempt " + attempt + "/" + maxAttempts + " (" + (useBundled ? "bundled" : "real RetroArch") + ") ===");

                bool raAlive = HasRetroArchProcess();
                SetStatus("Attempt " + attempt + "/" + maxAttempts + " - checking RetroArch...");
                Log("RA alive before attempt: " + raAlive);

                if (raAlive)
                {
                    // If RA is already running, its cmd is ignored (m_initialized
                    // guard in uwp_main.cpp). Force it to exit so the next
                    // activation re-inits with our cmd.
                    Log("RA still running; sending forceExit.");
                    await ForceExitRetroArch(useBundled);
                    await WaitForRetroArchExit(TimeSpan.FromSeconds(8));
                }

                if (useBundled)
                    RotateLog(ResolveRetroLogPath(), 3);
                else
                    RotateLog(@"E:\scummvm\logs\retroarch-real.log", 3);

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
                    bool up = useBundled ? await PollRetroArchLog() : await PollRetroArchProcess();
                    if (up)
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

        private static async System.Threading.Tasks.Task ForceExitRetroArch(bool useBundled)
        {
            try
            {
                string scheme = useBundled ? "scummvm-core" : "retroarch";
                var forceUri = new Uri(scheme + ":?forceExit");
                Log("Sending forceExit (" + scheme + ":)...");
                bool ok = await Launcher.LaunchUriAsync(forceUri);
                Log("forceExit ok=" + ok);
            }
            catch (Exception ex)
            {
                Log("forceExit THREW: " + ex.Message);
            }
        }

        private static async System.Threading.Tasks.Task<bool> PollRetroArchProcess()
        {
            Log("Polling RetroArch process (real RA path)...");
            for (int i = 0; i < 20; i++)
            {
                await System.Threading.Tasks.Task.Delay(500);
                if (HasRetroArchProcess())
                {
                    Log("RetroArch process is up (after " + ((i + 1) * 500) + "ms).");
                    return true;
                }
            }
            Log("RetroArch process did not appear within 10s.");
            return false;
        }

        private static async System.Threading.Tasks.Task<bool> IsRealRetroArchInstalled()
        {
            // Protocol probe: the real RetroArch registers "retroarch:". No
            // packageQuery needed (FindPackages() is denied on Xbox dev mode).
            try
            {
                var probe = new Uri("retroarch:?cmd=probe");
                var status = await Launcher.QueryUriSupportAsync(probe, LaunchQuerySupportType.Uri);
                Log("retroarch: protocol support = " + status);
                if (status == LaunchQuerySupportStatus.Available)
                {
                    Log("Real RetroArch INSTALLED (retroarch: registered).");
                    return true;
                }
                Log("retroarch: protocol not available (" + status + ").");
            }
            catch (Exception ex)
            {
                Log("retroarch: protocol probe FAILED: " + ex.Message + " (0x" + ex.HResult.ToString("X8") + ")");
            }

            // Fallback: PackageManager enumeration (needs packageQuery; works on some platforms).
            try
            {
                var pm = new Windows.Management.Deployment.PackageManager();
                var packages = pm.FindPackages();
                int count = 0;
                foreach (var pkg in packages)
                {
                    count++;
                    string name = "";
                    try { name = pkg.Id.Name ?? ""; } catch { }
                    string family = "";
                    try { family = pkg.Id.FamilyName ?? ""; } catch { }
                    if (count <= 30)
                        Log("Package #" + count + ": name=" + name + " family=" + family);
                    if (name.IndexOf("RetroArch", StringComparison.OrdinalIgnoreCase) >= 0 ||
                        family.StartsWith("1e4cf179", StringComparison.OrdinalIgnoreCase))
                    {
                        Log("Real RetroArch found via PackageManager: name=" + name + " family=" + family);
                        return true;
                    }
                }
                Log("Package enumeration done: total=" + count + " (no RetroArch).");
            }
            catch (Exception ex)
            {
                Log("Package enumeration FAILED: " + ex.Message + " (0x" + ex.HResult.ToString("X8") + ")");
            }

            Log("Real RetroArch NOT installed.");
            return false;
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

        private static void LogBootstrapDiagnostics()
        {
            try
            {
                var ver = Package.Current.Id.Version;
                string root = Package.Current.InstalledLocation.Path;
                Log("--- bootstrap diagnostics ---");
                Log("App: " + Package.Current.Id.Name + " family=" + Package.Current.Id.FamilyName);
                Log("Version: " + ver.Major + "." + ver.Minor + "." + ver.Build + "." + ver.Revision);
                Log("Architecture: " + Package.Current.Id.Architecture);
                Log("InstalledLocation: " + root);
                Log("LocalState: " + LocalPath);
                Log("LogPath: " + LogPath);

                string exe = System.IO.Path.Combine(root, "RetroArch-msvcUWP.exe");
                if (File.Exists(exe))
                    Log("Bundled exe: " + new FileInfo(exe).Length + " bytes");
                else
                    Log("Bundled exe: MISSING");
                string core = System.IO.Path.Combine(root, "cores", "scummvm_libretro.dll");
                if (File.Exists(core))
                    Log("Bundled core: " + new FileInfo(core).Length + " bytes");
                else
                    Log("Bundled core: MISSING");
                string cfg = System.IO.Path.Combine(root, "retroarch.cfg");
                if (File.Exists(cfg))
                    Log("Bundled retroarch.cfg: " + new FileInfo(cfg).Length + " bytes");
                else
                    Log("Bundled retroarch.cfg: MISSING");
                string zip = System.IO.Path.Combine(root, "system", "scummvm.zip");
                if (File.Exists(zip))
                    Log("Bundled scummvm.zip: " + new FileInfo(zip).Length + " bytes");
                else
                    Log("Bundled scummvm.zip: MISSING");

                string sysFlag = System.IO.Path.Combine(LocalPath, "system", ".scummvm-ready");
                Log("LocalState: system/.scummvm-ready present=" + File.Exists(sysFlag));
                string localCfg = System.IO.Path.Combine(LocalPath, "retroarch.cfg");
                if (File.Exists(localCfg))
                    Log("LocalState: retroarch.cfg present (" + new FileInfo(localCfg).Length + " bytes)");
                else
                    Log("LocalState: retroarch.cfg absent (will be seeded)");
                string raLog = System.IO.Path.Combine(LocalPath, "retroarch.log");
                if (File.Exists(raLog))
                    Log("LocalState: retroarch.log present (" + new FileInfo(raLog).Length + " bytes)");
                else
                    Log("LocalState: retroarch.log absent");

                Log("OS: " + Windows.System.Profile.AnalyticsInfo.VersionInfo.DeviceFamily);
                Log("Drives: [" + FromAppFile.DriveList() + "]");
                Log("--- end bootstrap diagnostics ---");
            }
            catch (Exception ex)
            {
                Log("Bootstrap diagnostics FAILED: " + ex.Message);
            }
        }

        private void SetStateIndicator(string state, string colorHex, string tooltip)
        {
            Dispatcher.RunAsync(CoreDispatcherPriority.Normal, () =>
            {
                if (StateIndicator == null)
                    return;
                try
                {
                    string hex = colorHex.TrimStart('#');
                    StateIndicator.Background = new Windows.UI.Xaml.Media.SolidColorBrush(
                        Windows.UI.Color.FromArgb(
                            0xFF,
                            Convert.ToByte(hex.Substring(0, 2), 16),
                            Convert.ToByte(hex.Substring(2, 2), 16),
                            Convert.ToByte(hex.Substring(4, 2), 16)));
                }
                catch { }
                try
                {
                    Windows.UI.Xaml.Controls.ToolTipService.SetToolTip(StateIndicator, tooltip);
                }
                catch { }
            });
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

        private void SetProgress(double fraction, string message)
        {
            Dispatcher.RunAsync(CoreDispatcherPriority.Normal, () =>
            {
                ProgressBarControl.Value = Math.Max(0, Math.Min(100, fraction * 100));
                if (message != null)
                    ProgressText.Text = message;
            });
        }

        private void SetProgressPanel(bool visible)
        {
            Dispatcher.RunAsync(CoreDispatcherPriority.Normal, () =>
            {
                ProgressPanel.Visibility = visible ? Visibility.Visible : Visibility.Collapsed;
            });
        }

        private bool StageToE(string version)
        {
            if (!FromAppFile.DriveExists('E'))
            {
                Log("StageToE: E: drive not present; cannot stage.");
                return false;
            }

            string eSys = @"E:\scummvm\system";
            if (FromAppFile.IsReparsePoint(eSys))
            {
                Log("StageToE: E:\\scummvm\\system is a reparse point/junction; refusing to stage (safety).");
                return false;
            }
            string flag = eSys + @"\.scummvm-ready";
            Log("StageToE: eSys=" + eSys + " version=" + version);

            bool flagExists = false;
            string flagContent = null;
            try
            {
                flagExists = FromAppFile.Exists(flag);
                if (flagExists)
                    flagContent = FromAppFile.ReadAllText(flag);
            }
            catch (Exception ex)
            {
                Log("StageToE: flag read FAILED: " + ex.Message);
            }
            Log("StageToE: flag exists=" + flagExists + " content='" + (flagContent ?? "(null)") + "'");

            if (flagExists && flagContent == version)
            {
                Log("E:\\ staging ok (version matches); skip.");
                return true;
            }

            Log("E:\\ staging: version differs; re-staging.");
            ResetRealCoreMarker();
            SetProgress(0.0, "Preparing E:\\scummvm...");

            try
            {
                FromAppFile.CreateDirectory(eSys);
                Log("StageToE: CreateDirectory(E:\\scummvm\\system) ok.");
            }
            catch (Exception ex)
            {
                Log("CreateDirectory E:\\ failed: " + ex.Message);
                return false;
            }

            string zipPath = Path.Combine(Package.Current.InstalledLocation.Path, "system", "scummvm.zip");
            if (!File.Exists(zipPath))
            {
                Log("ERROR: scummvm.zip missing at " + zipPath);
                return false;
            }
            var zipInfo = new FileInfo(zipPath);
            Log("StageToE: scummvm.zip = " + zipInfo.Length + " bytes at " + zipPath);

            SetProgress(0.05, "Extracting ScummVM to E:\\...");
            int totalFiles = 0;
            int totalDirs = 0;
            long totalBytes = 0;
            try
            {
                using (var zip = ZipFile.OpenRead(zipPath))
                {
                    int total = zip.Entries.Count;
                    int i = 0;
                    Log("StageToE: zip has " + total + " entries");
                    foreach (var entry in zip.Entries)
                    {
                        i++;
                        string target = eSys + "\\" + entry.FullName;
                        if (entry.FullName.EndsWith("/", StringComparison.Ordinal))
                        {
                            FromAppFile.CreateDirectory(target);
                            totalDirs++;
                        }
                        else
                        {
                            string dir = Path.GetDirectoryName(target);
                            FromAppFile.CreateDirectory(dir);
                            using (var src = entry.Open())
                                FromAppFile.WriteFromStream(target, src);
                            totalFiles++;
                            totalBytes += entry.Length;
                        }
                        if (i % 20 == 0)
                            SetProgress(0.05 + 0.80 * i / (double)total, "Extracting... " + i + "/" + total);
                    }
                }
            }
            catch (Exception ex)
            {
                Log("E:\\ extraction FAILED: " + ex);
                return false;
            }
            Log("StageToE: extraction done, files=" + totalFiles + " dirs=" + totalDirs + " bytes=" + totalBytes);

            Log("StageToE: top-level contents of E:\\scummvm\\system:");
            try
            {
                var entries = FromAppFile.ListEntries(eSys);
                Log("StageToE: " + entries.Count + " top-level entries");
                foreach (string entry in entries)
                    Log("  " + entry);
            }
            catch (Exception ex)
            {
                Log("StageToE: listing E:\\scummvm\\system FAILED: " + ex.Message);
            }

            SetProgress(0.9, "Writing scummvm.ini...");
            try
            {
                string iniPath = eSys + @"\scummvm.ini";
                if (FromAppFile.Exists(iniPath))
                {
                    Log("StageToE: scummvm.ini already present; keeping user config.");
                }
                else
                {
                    FromAppFile.WriteAllText(iniPath,
                        "[scummvm]\ngui_theme=scummremastered\ngui_scale=150\n");
                    Log("StageToE: scummvm.ini written.");
                }
            }
            catch (Exception ex)
            {
                Log("scummvm.ini write failed: " + ex.Message);
                return false;
            }

            try
            {
                WriteCfgWithSystemDir(@"E:\scummvm\retroarch.cfg",
                    Path.Combine(Package.Current.InstalledLocation.Path, "retroarch.cfg"),
                    @"E:/scummvm/system");
                Log("StageToE: E:\\scummvm\\retroarch.cfg written.");
            }
            catch (Exception ex)
            {
                Log("retroarch.cfg staging failed: " + ex.Message);
                return false;
            }

            try
            {
                FromAppFile.CreateDirectory(@"E:\scummvm\logs");
                FromAppFile.Copy(@"E:\scummvm\retroarch.cfg", @"E:\scummvm\logs\retroarch.cfg", true);
                Log("StageToE: cfg snapshot -> E:\\scummvm\\logs\\retroarch.cfg.");
            }
            catch (Exception ex)
            {
                Log("cfg snapshot failed: " + ex.Message);
            }

            SetProgress(1.0, "Done.");
            try
            {
                FromAppFile.WriteAllText(flag, version);
                Log("StageToE: flag written (version " + version + ").");
            }
            catch (Exception ex)
            {
                Log("flag write failed: " + ex.Message);
                return false;
            }

            Log("E:\\ staging complete (version " + version + ").");
            return true;
        }

        private static void WriteCfgWithSystemDir(string destCfg, string bundledCfg, string systemDir)
        {
            string[] lines = File.ReadAllLines(bundledCfg);
            bool found = false;
            for (int i = 0; i < lines.Length; i++)
            {
                string t = lines[i].TrimStart();
                if (t.StartsWith("system_directory", StringComparison.OrdinalIgnoreCase) &&
                    !t.StartsWith("libretro_system_directory", StringComparison.OrdinalIgnoreCase))
                {
                    lines[i] = "system_directory = \"" + systemDir + "\"";
                    found = true;
                    Log("WriteCfgWithSystemDir: replaced system_directory (line " + (i + 1) + ")");
                    break;
                }
            }
            string joined = string.Join(Environment.NewLine, lines);
            if (!found)
            {
                joined += Environment.NewLine + "system_directory = \"" + systemDir + "\"";
                Log("WriteCfgWithSystemDir: appended system_directory (bundled had " + lines.Length + " lines)");
            }
            FromAppFile.WriteAllText(destCfg, joined);
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
            if (File.Exists(iniPath))
            {
                Log("scummvm.ini already present; keeping user config: " + iniPath);
            }
            else
            {
                File.WriteAllText(
                    iniPath,
                    "[scummvm]\ngui_theme=scummremastered\ngui_scale=150\n");
                Log("scummvm.ini written: " + iniPath);
            }
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
                    EnsureSystemDirInCfg(cfg, Path.Combine(LocalPath, "system").Replace('\\', '/'));
                    return;
                }
                Log("retroarch.cfg missing; seeding bundled config (" + srcInfo.Length + " bytes).");

                File.Copy(src, cfg, true);
                EnsureSystemDirInCfg(cfg, Path.Combine(LocalPath, "system").Replace('\\', '/'));

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

        private static void EnsureSystemDirInCfg(string cfgPath, string systemDir)
        {
            string[] lines = File.ReadAllLines(cfgPath);
            bool found = false;
            for (int i = 0; i < lines.Length; i++)
            {
                string t = lines[i].TrimStart();
                if (t.StartsWith("system_directory", StringComparison.OrdinalIgnoreCase) &&
                    !t.StartsWith("libretro_system_directory", StringComparison.OrdinalIgnoreCase))
                {
                    lines[i] = "system_directory = \"" + systemDir + "\"";
                    found = true;
                    break;
                }
            }
            string joined = string.Join(Environment.NewLine, lines);
            if (!found)
                joined += Environment.NewLine + "system_directory = \"" + systemDir + "\"";
            File.WriteAllText(cfgPath, joined);
        }

        private static void TryUseELogs()
        {
            if (!FromAppFile.DriveExists('E'))
                return;
            try
            {
                FromAppFile.CreateDirectory(@"E:\scummvm\logs");
                LogPath = @"E:\scummvm\logs\launcher.log";
            }
            catch
            {
                LogPath = Path.Combine(LocalPath, "launcher.log");
            }
        }

        private static string ResolveRetroLogPath()
        {
            if (FromAppFile.DriveExists('E'))
            {
                try
                {
                    FromAppFile.CreateDirectory(@"E:\scummvm\logs");
                    return @"E:\scummvm\logs\retroarch.log";
                }
                catch { }
            }
            return Path.Combine(LocalPath, "retroarch.log");
        }

        private static bool? CoreLastRunResolved()
        {
            try
            {
                string logPath = @"E:\scummvm\logs\retroarch-real.log";
                if (!FromAppFile.Exists(logPath))
                    return null;
                string content = FromAppFile.ReadAllText(logPath);
                if (content.Length > 200000)
                    content = content.Substring(content.Length - 200000);
                int okIdx = content.LastIndexOf("matches core file", StringComparison.Ordinal);
                int missingIdx = content.LastIndexOf("is not a file, core name or directory", StringComparison.Ordinal);
                if (okIdx < 0 && missingIdx < 0)
                    return null;
                return okIdx > missingIdx;
            }
            catch (Exception ex)
            {
                Log("CoreLastRunResolved FAILED: " + ex.Message);
                return null;
            }
        }

        private static void ResetRealCoreMarker()
        {
            try
            {
                string logPath = @"E:\scummvm\logs\retroarch-real.log";
                if (FromAppFile.Exists(logPath))
                    FromAppFile.Delete(logPath);
                Log("StageToE: real-RA core-check marker reset (version changed).");
            }
            catch (Exception ex)
            {
                Log("ResetRealCoreMarker FAILED: " + ex.Message);
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
            string line = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff ") + msg;
            OutputDebugStringA("[launcher] " + line + Environment.NewLine);
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
