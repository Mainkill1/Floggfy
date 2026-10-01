Public menu ABI declarations were audited against these exact CEF revisions:

- https://github.com/chromiumembedded/cef/blob/8219561/include/cef_menu_model.h
- https://github.com/chromiumembedded/cef/blob/8219561/include/cef_menu_model_delegate.h
- https://github.com/chromiumembedded/cef/blob/beff58dbc4d0fd12b3eafea8f5314ce22e649078/include/cef_menu_model.h
- https://github.com/chromiumembedded/cef/blob/beff58dbc4d0fd12b3eafea8f5314ce22e649078/include/cef_menu_model_delegate.h

The 8219561 and beff58d revisions have the same relevant method counts and order:
menu model 56, menu delegate 7, client 19, display handler 13, load handler 4,
browser 21 and frame 26. The CEF translator maps bool to int and reference
strings to cef_string_utf16_t pointers. Returned structure sizes and the exact
CEF major, minor, patch and commit number are checked before use. Original
delegate callbacks and references are forwarded through a proxy, without
changing the original callback object.

Transferred callback arguments and factory ownership follow CEF's translator
rules: the incoming delegate's reference is consumed, the replacement is passed
with one transferred reference, and callback model arguments are consumed by
the forwarded callback or released locally when handled. Reference:

- https://github.com/chromiumembedded/cef/blob/8219561/libcef_dll/ctocpp/ctocpp_ref_counted.h
- https://github.com/chromiumembedded/cef/blob/8219561/libcef_dll/cpptoc/cpptoc_ref_counted.h

See LICENSE.txt for the CEF license. No CEF binary is redistributed.

Optional metadata bridge: public cef_client, cef_browser, cef_frame,
cef_display_handler and cef_load_handler declarations at the same revision.
Their callback reference transfers use the same translator ownership rules.
Browser/load/display structure sizes are checked before interception.

The metadata poll task uses the public cef_task_t/cef_post_task ABI from
https://github.com/chromiumembedded/cef/blob/8219561/include/cef_task.h .
The UI queue has at most one outstanding task, with transferred CEF ownership.
