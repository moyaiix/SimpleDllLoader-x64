# SimpleDllLoader-x64

A small and simple x64 Windows PE/DLL loader.

The code manually maps a DLL by
- copying PE headers and sections
- applying base relocations
- writing the mapped image into a target process
- running a small remote loader stub
- resolving imports
- and calling the DLL entry point

This is mostly based on the information from Microsoft:
https://learn.microsoft.com/en-us/windows/win32/debug/pe-format

That document explains the PE structures, but it does not explain every
detail used here. For e.g., the remote stub loader currently assumes that the
LoadLibraryA pointer from this PE loader process are also
valid in the target process.

Normally, ASLR should prevent this, at least that's what I thought. It doesnt tho.

See: https://cloud.google.com/blog/topics/threat-intelligence/six-facts-about-address-space-layout-randomization-on-windows

TLS callback and a bit of error handling is planned soonTM (never).
