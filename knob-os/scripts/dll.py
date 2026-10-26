# Preview build: links Knob OS as knob_preview.dll for Knob Hub, with the C++ runtime inside so it needs no MinGW DLLs.
Import("env")  # noqa: F821 (provided by PlatformIO)

env.Replace(PROGNAME="knob_preview", PROGSUFFIX=".dll")  # noqa: F821
env.Append(LINKFLAGS=["-shared", "-static", "-static-libgcc", "-static-libstdc++"])  # noqa: F821
