#pragma once

#include "color_fill_vs_glsl.h"
#include "color_fill_vs_mtl.h"
#include "color_fill_vs_spv.h"
#include "color_fill_vs_essl.h"

#include "color_fill_fs_glsl.h"
#include "color_fill_fs_mtl.h"
#include "color_fill_fs_spv.h"
#include "color_fill_fs_essl.h"

#if BX_PLATFORM_WINDOWS
#include "color_fill_vs_dx9.h"
#include "color_fill_vs_dx11.h"
#include "color_fill_fs_dx9.h"
#include "color_fill_fs_dx11.h"
#endif

