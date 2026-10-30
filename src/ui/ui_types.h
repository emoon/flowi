#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// flowi UI types - internal umbrella
//
// The public UI types are authored in api_gen .def files and emitted into include/flowi/ui/; this internal
// header re-exports them under one include so flowi's UI code gets the whole set with a single `<ui/ui_types.h>`.
// It defines no types of its own - every Fl* type below is generated.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <core/string.h> // FlString (length-prefixed string), includer-provided to the generated headers
#include <stdbool.h>
#include <stdint.h>

// Generated public UI types (api_gen owns these; see the matching .def files).
#include <flowi/core/math_data.h>
#include <flowi/ui/design_tokens.h>
#include <flowi/ui/element_state.h>
#include <flowi/ui/font_id.h>
#include <flowi/ui/font_style.h>
#include <flowi/ui/image.h>
#include <flowi/ui/image_handle.h>
#include <flowi/ui/item_flags.h>
#include <flowi/ui/key.h>
#include <flowi/ui/key_modifier.h>
#include <flowi/ui/layout.h>
#include <flowi/ui/list_clipper.h>
#include <flowi/ui/menu.h>
#include <flowi/ui/popup.h>
#include <flowi/ui/scroll.h>
#include <flowi/ui/scrollbar_display.h>
#include <flowi/ui/signal.h>
#include <flowi/ui/text_config.h>
#include <flowi/ui/text_input.h>
#include <flowi/ui/widget_config.h>
