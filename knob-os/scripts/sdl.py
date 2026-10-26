# Simulator build: adds SDL2 (MinGW build in %USERPROFILE%\.tools\SDL2) and copies SDL2.dll next to the program,
# so it runs straight from the build folder. Done here because platformio.ini mangles Windows paths from sysenv.
import os
import shutil

Import("env")  # noqa: F821 (provided by PlatformIO)

SDL_ROOT = os.path.join(os.environ["USERPROFILE"], ".tools", "SDL2", "SDL2-2.30.9", "x86_64-w64-mingw32")

env.Append(  # noqa: F821
    CPPPATH=[os.path.join(SDL_ROOT, "include")],
    LIBPATH=[os.path.join(SDL_ROOT, "lib")],
    LIBS=["SDL2"],
)


def copy_sdl(source, target, env):
    shutil.copy2(os.path.join(SDL_ROOT, "bin", "SDL2.dll"), env.subst("$BUILD_DIR"))


env.AddPostAction("$PROGPATH", copy_sdl)  # noqa: F821
