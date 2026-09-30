#include "../native/cef_identity.h"

#include <cassert>

int main() {
    assert(cef_compat::IsSupported({146, 0, 10, 3504}));
    assert(!cef_compat::IsSupported({145, 0, 10, 3504}));
    assert(!cef_compat::IsSupported({146, 1, 10, 3504}));
    assert(!cef_compat::IsSupported({146, 0, 11, 3504}));
    assert(!cef_compat::IsSupported({146, 0, 10, 3503}));
}
