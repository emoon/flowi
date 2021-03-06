local native = require('tundra.native')

-----------------------------------------------------------------------------------------------------------------------

local mac_opts = {
    "-Wall", "-I.",
    "-DFLOWI_MAC",
    { "-DFLOWI_DEBUG", "-O0", "-g"; Config = "*-*-debug" },
    { "-DFLOWI_DEBUG", "-O0", "-fsanitize=address", "-fno-omit-frame-pointer", "-g"; Config = "*-*-debug-asan" },
    { "-DFLOWI_RELEASE", "-O3", "-g"; Config = "*-*-release" },
}

-----------------------------------------------------------------------------------------------------------------------

local macosx = {
    Env = {
		BGFX_SHADERC = "$(OBJECTDIR)$(SEP)bgfx_shaderc$(PROGSUFFIX)",

        CCOPTS =  {
            mac_opts,
        },

        CXXOPTS = {
            mac_opts,
            "-std=c++14",
        },

        SHLIBOPTS = {
			"-lstdc++",
			{ "-fsanitize=address"; Config = "*-*-debug-asan" },
		},

        PROGCOM = {
			"-lstdc++",
			{ "-fsanitize=address"; Config = "*-*-debug-asan" },
		},
    },

    Frameworks = {
        { "Cocoa" },
        { "Metal" },
        { "QuartzCore" },
    },
}

-----------------------------------------------------------------------------------------------------------------------

local gcc_opts = {
    "-I.",
    "-Wno-array-bounds",
    "-Wno-attributes",
    "-Wno-unused-value",
    "-DOBJECT_DIR=\\\"$(OBJECTDIR)\\\"",
    "-I$(OBJECTDIR)",
    "-Wall",
    "-fPIC",
    "-msse2",   -- TODO: Separate gcc options for x64/arm somehow?
    { "-DFLOWI_TEST", "-O2", "-g"; Config = "*-*-test" },
    { "-DFLOWI_DEBUG", "-O0", "-g"; Config = "*-*-debug" },
    { "-DFLOWI_RELEASE", "-O3", Config = "*-*-release" },
}

local gcc_env = {
    Env = {
		FLOWI_VERSION = native.getenv("FLOWI_VERSION", "Development Version"),
		BGFX_SHADERC = "$(OBJECTDIR)$(SEP)bgfx_shaderc$(PROGSUFFIX)",

        RUST_CARGO_OPTS = {
            { "test"; Config = "*-*-*-test" },
        },

        CCOPTS = {
			"-Werror=incompatible-pointer-types",
            gcc_opts,
        },

        CXXOPTS = {
            gcc_opts,
            "-DFLOWI_VERSION='\"$(FLOWI_VERSION)\"'",
        },
    },

    ReplaceEnv = {
        LD = "c++",
    },
}

-----------------------------------------------------------------------------------------------------------------------

local win64_opts = {
    "/EHsc", "/FS", "/MD", "/W3", "/I.", "/DUNICODE", "/D_UNICODE", "/DWIN32", "/D_CRT_SECURE_NO_WARNINGS",
    "\"/DOBJECT_DIR=$(OBJECTDIR:#)\"",
    { "/DFLOWI_DEBUG","/Od"; Config = "*-*-debug" },
    { "/DFLOWI_RELEASE", "/O2"; Config = "*-*-release" },
}

local win64 = {
    Env = {
		FLOWI_VERSION = native.getenv("FLOWI_VERSION", "Development Version"),
		BGFX_SHADERC = "$(OBJECTDIR)$(SEP)bgfx_shaderc$(PROGSUFFIX)",

        GENERATE_PDB = "1",
        CCOPTS = {
            win64_opts,
        },

        CXXOPTS = {
            win64_opts,
        },

        OBJCCOM = "meh",
    },
}

-----------------------------------------------------------------------------------------------------------------------

Build {
    Passes = {
        BuildTools = { Name = "BuildTools", BuildOrder = 1 },
        GenerateSources = { Name = "GenerateSources", BuildOrder = 2 },
    },

    Units = {
        "units.lua",
        "units.flowi.lua",
        "units.bgfx.lua",
    },

    Configs = {
        Config { Name = "macos-clang", DefaultOnHost = "macosx", Inherit = macosx, Tools = { "clang-osx" } },
        Config { Name = "win64-msvc", DefaultOnHost = { "windows" }, Inherit = win64, Tools = { "msvc-vs2019" } },
        Config { Name = "linux-gcc", DefaultOnHost = { "linux" }, Inherit = gcc_env, Tools = { "gcc" } },
        Config { Name = "linux-clang", DefaultOnHost = { "linux" }, Inherit = gcc_env, Tools = { "clang" } },
    },

    IdeGenerationHints = {
        Msvc = {
            -- Remap config names to MSVC platform names (affects things like header scanning & debugging)
            PlatformMappings = {
                ['win64-msvc'] = 'x64',
            },

            -- Remap variant names to MSVC friendly names
            VariantMappings = {
                ['release']    = 'Release',
                ['debug']      = 'Debug',
            },
        },

        MsvcSolutions = {
            ['Flowi.sln'] = { }
        },

    },

    -- Variants = { "debug", "test", "release" },
    Variants = { "debug", "release" },
    SubVariants = { "default", "test" },
}

-- vim: ts=4:sw=4:sts=4

