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
            Log("=== ScummVM launcher started ===");
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
            var ok = await Launcher.LaunchUriAsync(uri);
            Log("LaunchUriAsync ok=" + ok);

            if (ok)
            {
                await PollRetroArchLog();
                Application.Current.Exit();
            }
            else
            {
                Log("ERROR: failed to open scummvm-core (RetroArch not registered?).");
                SetStatus("RetroArch ScummVM not found.");
            }
        }

        private static async System.Threading.Tasks.Task PollRetroArchLog()
        {
            string raLog = Path.Combine(LocalPath, "retroarch.log");
            for (int i = 0; i < 20; i++)
            {
                await System.Threading.Tasks.Task.Delay(500);
                if (File.Exists(raLog))
                {
                    var fi = new FileInfo(raLog);
                    Log("retroarch.log detected (" + fi.Length + " bytes, mtime " + fi.LastWriteTime.ToString("HH:mm:ss") + ").");
                    return;
                }
            }
            Log("retroarch.log did not appear within 10s.");
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

                string sysDir = Path.Combine(LocalPath, "system");

                // Keys guaranteed in the config. "false" avoids the RGUI menu when
                // the core shuts down (load_dummy_on_core_shutdown=true loads the
                // dummy core and opens the menu) and disables the OSD toast.
                // video_shader_enable=false neutralizes any shader persisted from a
                // prior session; video_smooth=true keeps bilinear filtering for the
                // ScummVM core.
                var desired = new Dictionary<string, string>
                {
                    { "video_driver", "d3d11" },
                    { "load_dummy_on_core_shutdown", "false" },
                    { "video_font_enable", "false" },
                    { "video_shader_enable", "false" },
                    { "video_smooth", "true" }
                };

                if (File.Exists(cfg))
                {
                    // RA may persist "gl" in the config (save-on-suspend with the
                    // GL/HW core active). A stale gl driver after core unload =
                    // menu crash (null call). Force d3d11 for the menu, as on Xbox.
                    var lines = File.ReadAllLines(cfg).ToList();
                    bool changed = false;
                    foreach (var kv in desired)
                    {
                        bool found = false;
                        for (int i = 0; i < lines.Count; i++)
                        {
                            if (lines[i].TrimStart().StartsWith(kv.Key))
                            {
                                found = true;
                                if (!lines[i].Contains("\"" + kv.Value + "\""))
                                {
                                    lines[i] = kv.Key + " = \"" + kv.Value + "\"";
                                    changed = true;
                                    Log("retroarch.cfg: " + kv.Key + " forced to " + kv.Value + ".");
                                }
                                break;
                            }
                        }
                        if (!found)
                        {
                            lines.Add(kv.Key + " = \"" + kv.Value + "\"");
                            changed = true;
                            Log("retroarch.cfg: " + kv.Key + "=" + kv.Value + " added.");
                        }
                    }
                    if (changed)
                    {
                        File.WriteAllLines(cfg, lines);
                        Log("retroarch.cfg updated.");
                    }
                    else
                    {
                        Log("retroarch.cfg already exists and is consistent; kept.");
                    }
                    return;
                }

                string content =
                    "menu_driver = \"rgui\"\n" +
                    "log_to_file = \"true\"\n" +
                    "log_dir = \"" + LocalPath.Replace('\\', '/') + "\"\n" +
                    "system_directory = \"" + sysDir.Replace('\\', '/') + "\"\n" +
                    "video_driver = \"d3d11\"\n" +
                    "load_dummy_on_core_shutdown = \"false\"\n" +
                    "video_font_enable = \"false\"\n" +
                    "video_shader_enable = \"false\"\n" +
                    "video_smooth = \"true\"\n";
                File.WriteAllText(cfg, content);
                Log("retroarch.cfg pre-configured (rgui, log_dir, system_dir, video_driver=d3d11, load_dummy_on_core_shutdown=false, video_font_enable=false, video_shader_enable=false, video_smooth=true).");
                Log("Contents:");
                foreach (string line in File.ReadAllLines(cfg))
                    Log("  " + line);
            }
            catch (Exception ex)
            {
                Log("ERROR pre-configuring retroarch.cfg: " + ex.Message);
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
