// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System.Threading;

using Microsoft.PowerToys.Settings.UI.Library;
using Microsoft.PowerToys.Settings.UI.UnitTests.BackwardsCompatibility;
using Microsoft.PowerToys.Settings.UI.UnitTests.Mocks;
using Microsoft.PowerToys.Settings.UI.ViewModels;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using PowerToys.Interop;

namespace ViewModelTests;

[TestClass]
public class DEPiP
{
    [TestMethod]
    public void EnablingModuleSendsUpdatedGeneralSettings()
    {
        var repository = new BackCompatTestProperties.MockSettingsRepository<GeneralSettings>(
            ISettingsUtilsMocks.GetStubSettingsUtils<GeneralSettings>().Object);
        string sentMessage = null;
        var viewModel = new DEPiPViewModel(repository, message =>
        {
            sentMessage = message;
            return 0;
        });

        viewModel.IsEnabled = true;

        Assert.IsTrue(repository.SettingsConfig.Enabled.DEPiP);
        Assert.IsFalse(string.IsNullOrWhiteSpace(sentMessage));
        StringAssert.Contains(sentMessage, "\"DEPiP\":true");
    }

    [TestMethod]
    public void LaunchSignalsSharedEvent()
    {
        var repository = new BackCompatTestProperties.MockSettingsRepository<GeneralSettings>(
            ISettingsUtilsMocks.GetStubSettingsUtils<GeneralSettings>().Object);
        var viewModel = new DEPiPViewModel(repository, _ => 0);
        using var eventHandle = new EventWaitHandle(false, EventResetMode.AutoReset, Constants.ShowDEPiPSharedEvent());

        viewModel.Launch();

        Assert.IsTrue(eventHandle.WaitOne(0));
    }
}
