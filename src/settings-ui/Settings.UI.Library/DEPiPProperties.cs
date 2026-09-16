// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System.Text.Json.Serialization;

namespace Microsoft.PowerToys.Settings.UI.Library
{
    public class DEPiPProperties
    {
        public const int DefaultInactiveTransparency = 14;
        public const bool DefaultLockAspectRatio = false;

        public DEPiPProperties()
        {
            InactiveTransparency = new IntProperty(DefaultInactiveTransparency);
            LockAspectRatio = new BoolProperty(DefaultLockAspectRatio);
        }

        [JsonPropertyName("inactiveTransparency")]
        public IntProperty InactiveTransparency { get; set; }

        [JsonPropertyName("lockAspectRatio")]
        public BoolProperty LockAspectRatio { get; set; }
    }
}
