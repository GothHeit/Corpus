# Compiling

## Icon

The `.exe` uses its own icon (`corpus.ico`), embedded via a resource script (`resource.rc`).
Before linking, it needs to be compiled with `windres`:

```sh
windres resource.rc -O coff -o resource.res
```

Basic command:

```sh
g++ test.cpp window.cpp search_bar.cpp ui_font.cpp ../src/library.cpp ../src/file.cpp ../src/tag.cpp ../src/saving.cpp ../src/tag_query.cpp resource.res -o visual -ldwmapi -lgdi32 -lgdiplus -luser32
```

## Missing DLL when running on another machine

If the `.exe` complains about a missing DLL on another machine (e.g. `libgcc_s_seh-1.dll not found`,
`libstdc++-6.dll not found`), it's because by default `g++` links the MinGW runtime (`libgcc`, `libstdc++`)
as a DLL, requiring MinGW to be installed (or the DLLs present) on the machine that **runs** the
program — not just the one that compiled it. To avoid this, the runtime can be embedded inside the `.exe` itself:

```sh
g++ test.cpp window.cpp search_bar.cpp ui_font.cpp ../src/library.cpp ../src/file.cpp ../src/tag.cpp ../src/saving.cpp ../src/tag_query.cpp resource.res -o visual -ldwmapi -lgdi32 -lgdiplus -luser32 -static-libgcc -static-libstdc++ -static
```

| Flag | Effect |
|---|---|
| `-static-libgcc` | embeds the gcc runtime (`libgcc`) in the `.exe` instead of depending on `libgcc_s_seh-1.dll` |
| `-static-libstdc++` | embeds the std lib (`libstdc++`) in the `.exe` instead of depending on `libstdc++-6.dll` |
| `-static` | same for the remaining libs g++/MinGW would otherwise link dynamically (e.g. `libwinpthread`) |

The `.exe` gets bigger (the whole runtime goes with it), but runs standalone on any Windows without
needing to install anything. Without these flags, it works the same on a machine that already has MinGW installed.
