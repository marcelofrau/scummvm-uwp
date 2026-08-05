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
