using System;
using Windows.ApplicationModel.Activation;
using Windows.UI.Xaml;

namespace ScummVMLauncher
{
    sealed partial class App : Application
    {
        private MainPage _page;

        public App()
        {
            InitializeComponent();
            UnhandledException += OnUnhandledException;
            System.Threading.Tasks.TaskScheduler.UnobservedTaskException += OnUnobservedTask;
        }

        private static string CrashLogPath()
        {
            try { return System.IO.Path.Combine(Windows.Storage.ApplicationData.Current.LocalFolder.Path, "launcher.log"); }
            catch { return null; }
        }

        private static void CrashLog(string msg)
        {
            string path = CrashLogPath();
            if (path == null) return;
            try { System.IO.File.AppendAllText(path, DateTime.Now.ToString("HH:mm:ss.fff ") + "[crash] " + msg + Environment.NewLine); } catch { }
        }

        private void OnUnhandledException(object sender, Windows.UI.Xaml.UnhandledExceptionEventArgs e)
        {
            CrashLog("UnhandledException: " + e.Exception);
        }

        private void OnUnobservedTask(object sender, System.Threading.Tasks.UnobservedTaskExceptionEventArgs e)
        {
            CrashLog("UnobservedTaskException: " + e.Exception);
            e.SetObserved();
        }

        protected override void OnLaunched(LaunchActivatedEventArgs e)
        {
            ActivateRoot();
        }

        protected override void OnActivated(IActivatedEventArgs args)
        {
            if (args is ProtocolActivatedEventArgs protocol && protocol.Uri != null)
            {
                string query = protocol.Uri.Query ?? string.Empty;
                if (query.IndexOf("cmd=exit", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    Application.Current.Exit();
                    return;
                }
            }
            ActivateRoot();
        }

        private void ActivateRoot()
        {
            if (_page == null)
                _page = new MainPage();
            Window.Current.Content = _page;
            Window.Current.Activate();
            _page.Run();
        }
    }
}
