#include "../native/hook_installation.h"

#include <cassert>

int main() {
    hooks::InstallCounts none;
    none.Found();
    none.Created(false);
    assert(none.found == 1);
    assert(none.created == 0);
    assert(none.enabled == 0);
    assert(!none.Usable());

    hooks::InstallCounts partial;
    partial.Found();
    partial.Created(true);
    partial.Enabled(true);
    partial.Found();
    partial.Created(false);
    partial.Found();
    partial.Created(true);
    partial.Enabled(false);
    assert(partial.found == 3);
    assert(partial.created == 2);
    assert(partial.enabled == 1);
    assert(partial.Usable());
}
