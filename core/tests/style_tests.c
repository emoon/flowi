#include "utest.h"
#include "../src/flowi.h"
#include "../src/internal.h"
#include "../src/style.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(Style, create) {
	// This to be called before using any other functions
	FlGlobalState* global = fl_create(NULL);
	FlContext* ctx = fl_context_create(global);

	FlStyle* style = fl_style_create(ctx, "test");
	FlStyle* default_style = fl_style_get_default(ctx);

	ASSERT_TRUE(memcmp(style, default_style, sizeof(FlStyle)) == 0);
}

