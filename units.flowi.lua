require "tundra.syntax.glob"
require "tundra.path"
require "tundra.util"

local FLOWI_DIR = "core/src/"

-----------------------------------------------------------------------------------------------------------------------

StaticLibrary {
    Name = "flowi",

    Sources = {
        FLOWI_DIR .. "flowi.c",
    },
}

