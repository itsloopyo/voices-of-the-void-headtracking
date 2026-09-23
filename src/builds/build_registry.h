// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <windows.h>
#include "build_profile.h"

// Profile registry and selection. SelectProfile() fingerprints the host EXE
// (PE TimeDateStamp + SizeOfImage + CheckSum) and installs the matching profile
// as active, or stays dormant if no profile claims this build.

namespace votv_ht
{
    namespace builds
    {
        enum class MatchResult
        {
            Matched,      // Active profile set; mod can run.
            ReadFailed,   // Could not read the PE header.
            HostUnknown,  // No profile claims this build (newer or older than known).
            HostDiffers,  // A known stamp with a different size or checksum.
        };

        MatchResult SelectProfile(HMODULE host);
        const BuildProfile& ActiveProfile();
    }

    // Accessor for the active profile's offset table. Must run after
    // SelectProfile() returns Matched.
    inline const OffsetTable& Offsets()
    {
        return builds::ActiveProfile().Offsets;
    }
}
