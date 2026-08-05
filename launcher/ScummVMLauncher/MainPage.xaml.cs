using System;
using System.IO;
using System.IO.Compression;
using Windows.ApplicationModel;
using Windows.System;
using Windows.UI.Xaml;
using Windows.UI.Xaml.Controls;

namespace ScummVMLauncher
{
    public sealed partial class MainPage : Page
    {
        public MainPage()
        {
            InitializeComponent();
        }

        public async void Run()
        {
            try
            {
                StatusText.Text = "Preparando ScummVM...";
                await System.Threading.Tasks.Task.Run(() => Bootstrap());
            }
            catch (Exception ex)
            {
                StatusText.Text = "Falha na preparação: " + ex.Message;
                return;
            }

            StatusText.Text = "Iniciando ScummVM...";
            var uri = new Uri(
                "retroarch:?cmd=retroarch -L cores\\scummvm_libretro.dll" +
                "&launchOnExit=scummvm-launcher:?cmd=restart");
            var ok = await Launcher.LaunchUriAsync(uri);

            if (ok)
            {
                Application.Current.Exit();
            }
            else
            {
                StatusText.Text = "RetroArch ScummVM não encontrado.";
            }
        }

        private static void Bootstrap()
        {
            string sysDir = Path.Combine(
                Windows.Storage.ApplicationData.Current.LocalFolder.Path, "system");
            string flag = Path.Combine(sysDir, ".scummvm-ready");

            if (File.Exists(flag))
                return;

            Directory.CreateDirectory(sysDir);

            string zipPath = Path.Combine(Package.Current.InstalledLocation.Path, "system", "scummvm.zip");
            ZipFile.ExtractToDirectory(zipPath, sysDir);

            File.WriteAllText(
                Path.Combine(sysDir, "scummvm.ini"),
                "[scummvm]\ngui_theme=scummremastered\n");

            File.WriteAllText(flag, DateTime.UtcNow.ToString("o"));
        }
    }
}
