// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Threading;
using Microsoft.PowerToys.Settings.UI.Library;
using Microsoft.PowerToys.Settings.UI.Library.Interfaces;
using Microsoft.PowerToys.Settings.UI.Library.ViewModels.Commands;
using PowerToys.Interop;

namespace Microsoft.PowerToys.Settings.UI.ViewModels
{
    public partial class DEPiPViewModel : PageViewModelBase
    {
        protected override string ModuleName => "DEPiP";

        private GeneralSettings GeneralSettingsConfig { get; }

        private Func<string, int> SendConfigMSG { get; }

        private bool _isEnabled;

        public ButtonClickCommand LaunchEventHandler => new ButtonClickCommand(Launch);

        public DEPiPViewModel(ISettingsRepository<GeneralSettings> settingsRepository, Func<string, int> ipcMSGCallBackFunc)
        {
            ArgumentNullException.ThrowIfNull(settingsRepository);

            GeneralSettingsConfig = settingsRepository.SettingsConfig;
            SendConfigMSG = ipcMSGCallBackFunc ?? (_ => 0);
            InitializeEnabledValue();
        }

        public bool IsEnabled
        {
            get => _isEnabled;
            set
            {
                if (value != _isEnabled)
                {
                    _isEnabled = value;
                    GeneralSettingsConfig.Enabled.DEPiP = value;
                    SendConfigMSG(new OutGoingGeneralSettings(GeneralSettingsConfig).ToString());
                    OnPropertyChanged(nameof(IsEnabled));
                }
            }
        }

        public void RefreshEnabledState()
        {
            InitializeEnabledValue();
            OnPropertyChanged(nameof(IsEnabled));
        }

        public void Launch()
        {
            using var eventHandle = new EventWaitHandle(false, EventResetMode.AutoReset, Constants.ShowDEPiPSharedEvent());
            eventHandle.Set();
        }

        private void InitializeEnabledValue()
        {
            _isEnabled = GeneralSettingsConfig.Enabled.DEPiP;
        }
    }
}
