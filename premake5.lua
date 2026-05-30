workspace "file_bundler"
    configurations { "Debug", "Release" }
    platforms { "x64" }
    location ("build/" .. _ACTION)
    startproject "file_bundler"

filter "platforms:x64"
    architecture "x86_64"

project "file_bundler"
    kind "WindowedApp"
    language "C"
    cdialect "C11"
    system "windows"
    characterset "Unicode"
    targetname "file_bundler"
    targetdir "."
    objdir ("build/obj/" .. _ACTION .. "/%{cfg.platform}/%{cfg.buildcfg}")

    files {
        "file_bundler.c",
        "application.rc",
        "application.ico",
    }

    links {
        "cabinet",
        "shell32",
        "comdlg32",
        "ole32",
        "gdi32",
        "user32",
    }

    filter "configurations:Debug"
        runtime "Debug"
        symbols "On"
        targetsuffix "_debug"

    filter "configurations:Release"
        runtime "Release"
        optimize "Size"
        symbols "Off"
        linktimeoptimization "On"

    filter { "configurations:Release", "toolset:gcc or clang" }
        -- Keep the MinGW/Clang release binary as close as possible to the
        -- existing Zig build by optimizing for size and stripping at link time.
        buildoptions { "-ffunction-sections", "-fdata-sections" }
        linkoptions { "-s", "-Wl,--gc-sections", "-Wl,--strip-all" }

    filter { "configurations:Release", "action:vs*" }
        -- MSVC-specific size wins: fold identical code/data and drop anything
        -- the linker can prove is unused.
        buildoptions { "/Gw", "/Gy" }
        linkoptions { "/OPT:REF", "/OPT:ICF" }

    filter "action:gmake"
        -- MinGW-style toolchains need -municode to route startup through wWinMain.
        buildoptions { "-municode" }
        linkoptions { "-municode" }

    filter {}
