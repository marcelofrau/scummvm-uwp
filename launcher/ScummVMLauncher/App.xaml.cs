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
