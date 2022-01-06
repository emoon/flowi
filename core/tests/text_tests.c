#include "utest.h"
#include "../src/font_private.h"
#include "../src/text.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//static float EPSILON = 0.0001f;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
UTEST(Text, calculate_size) {
	Glyph glyphs[] = {
		{ 0,0,8,20, 0,0,0,0, 10.0f },
		{ 0,0,10,10, 0,0,0,0, 12.0f },
	};

	u32 codepoints[] = { 0, 1 };

	FlVec2 size = Text_calculate_size(glyphs, codepoints, 2);

	// TODO: Proper macro
	ASSERT_LE(size.x, 22.0f + EPSILON);
	ASSERT_GE(size.x, 22.0f - EPSILON);

	ASSERT_LE(size.y, 20.0f + EPSILON);
	ASSERT_GE(size.y, 20.0f - EPSILON);
}
*/

