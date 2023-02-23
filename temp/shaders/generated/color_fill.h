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

#include "vs_texture_glsl.h"
#include "vs_texture_mtl.h"
#include "vs_texture_spv.h"
#include "vs_texture_essl.h"
#include "fs_texture_glsl.h"
#include "fs_texture_mtl.h"
#include "fs_texture_spv.h"
#include "fs_texture_essl.h"

#include "vs_texture_r_glsl.h"
#include "vs_texture_r_mtl.h"
#include "vs_texture_r_spv.h"
#include "vs_texture_r_essl.h"
#include "fs_texture_r_glsl.h"
#include "fs_texture_r_mtl.h"
#include "fs_texture_r_spv.h"
#include "fs_texture_r_essl.h"

#if BX_PLATFORM_WINDOWS
#include "vs_texture_dx9.h"
#include "vs_texture_dx11.h"
#include "fs_texture_dx9.h"
#include "fs_texture_dx11.h"
#include "vs_texture_r_dx9.h"
#include "vs_texture_r_dx11.h"
#include "fs_texture_r_dx9.h"
#include "fs_texture_r_dx11.h"
#endif


