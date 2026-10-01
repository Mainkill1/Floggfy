#pragma once

namespace cef_compat {

struct Identity {
    int major;
    int minor;
    int patch;
    int commit;
};

constexpr Identity kSupportedIdentities[] = {
    {146, 0, 10, 3504},
    {151, 3, 18, 3578},
};

constexpr bool IsSupported(Identity identity) noexcept {
    for (const auto& supported : kSupportedIdentities) {
        if (identity.major == supported.major &&
            identity.minor == supported.minor &&
            identity.patch == supported.patch &&
            identity.commit == supported.commit)
            return true;
    }
    return false;
}

} // namespace cef_compat
