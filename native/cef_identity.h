#pragma once

namespace cef_compat {

struct Identity {
    int major;
    int minor;
    int patch;
    int commit;
};

constexpr Identity kSupportedIdentity{146, 0, 10, 3504};

constexpr bool IsSupported(Identity identity) noexcept {
    return identity.major == kSupportedIdentity.major &&
           identity.minor == kSupportedIdentity.minor &&
           identity.patch == kSupportedIdentity.patch &&
           identity.commit == kSupportedIdentity.commit;
}

} // namespace cef_compat
