.text
# Tail forwarders preserve the four Windows x64 register arguments and leave
# stack arguments untouched, including for the undocumented ByHandle export.
.macro FORWARD name, index
.globl \name
.def \name; .scl 2; .type 32; .endef
\name:
    subq $72, %rsp
    movq %rcx, 32(%rsp)
    movq %rdx, 40(%rsp)
    movq %r8, 48(%rsp)
    movq %r9, 56(%rsp)
    movl $\index, %ecx
    call ResolveVersionExport
    movq 32(%rsp), %rcx
    movq 40(%rsp), %rdx
    movq 48(%rsp), %r8
    movq 56(%rsp), %r9
    addq $72, %rsp
    jmp *%rax
.endm
FORWARD GetFileVersionInfoA, 0
FORWARD GetFileVersionInfoByHandle, 1
FORWARD GetFileVersionInfoExA, 2
FORWARD GetFileVersionInfoExW, 3
FORWARD GetFileVersionInfoSizeA, 4
FORWARD GetFileVersionInfoSizeExA, 5
FORWARD GetFileVersionInfoSizeExW, 6
FORWARD GetFileVersionInfoSizeW, 7
FORWARD GetFileVersionInfoW, 8
FORWARD VerFindFileA, 9
FORWARD VerFindFileW, 10
FORWARD VerInstallFileA, 11
FORWARD VerInstallFileW, 12
FORWARD VerLanguageNameA, 13
FORWARD VerLanguageNameW, 14
FORWARD VerQueryValueA, 15
FORWARD VerQueryValueW, 16
