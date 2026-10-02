#pragma once

namespace hooks {

struct InstallCounts {
    unsigned found = 0;
    unsigned created = 0;
    unsigned enabled = 0;

    void Found() noexcept { ++found; }
    void Created(bool success) noexcept { if (success) ++created; }
    void Enabled(bool success) noexcept { if (success) ++enabled; }
    bool Usable() const noexcept { return enabled != 0; }
};

} // namespace hooks
