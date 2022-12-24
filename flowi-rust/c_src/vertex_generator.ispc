/*
struct Glyph {
    unsigned int16 x0, y0;
    unsigned int16 x1, y1;
    int16 x_offset, y_offset;
    uniform float advance_x;
};

struct VertPosUvColor {
    float x;
    float y;
    unsigned int16 u;
    unsigned int16 v;
    unsigned int32 color;
};

// programIndex gives the index of the SIMD-lane being used for running each program instance.  (In other words, it's a varying integer value that has value zero for the first program instance, and so forth.) 
// programCount builtin gives the total number of instances in the gang.  Together, these can be used to uniquely map executing program instances to input data. [4]

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void generate_mesh(uniform VertPosUvColor out[], uniform Glyph glyphs[], int count) {
    int chunks = count & ~(programCount-1);
    uniform unsigned int32 color = 1;

    float pos_x = 0.0f;
    float pos_y = 0.0f;

    for (uniform int i = 0; i < chunks; i += programCount) {
        Glyph g = glyphs[i];

        const unsigned int16 x0 = g.x0 + 1;
        const unsigned int16 y0 = g.y0 + 1;
        const unsigned int16 x1 = g.x1 + 1;
        const unsigned int16 y1 = g.y1 + 1;
        const int16 xoff = g.x_offset + 1;
        const int16 yoff = g.y_offset + 1;

        float rx = xoff + pos_x;
        float ry = yoff + pos_y;

        float nx0 = rx;
        float ny0 = ry;
        float nx1 = rx + (x1 - x0);
        float ny1 = ry + (y1 - y0);

        int ci = i + programIndex;

        out[ci + 0].x = nx0;
        out[ci + 0].y = ny0;
        out[ci + 0].u = x0;
        out[ci + 0].v = y0;
        out[ci + 0].color = color;

        out[ci + 1].x = nx1;
        out[ci + 1].y = ny0;
        out[ci + 1].u = x1;
        out[ci + 1].v = y0;
        out[ci + 1].color = color;

        out[ci + 2].x = nx1;
        out[ci + 2].y = ny1;
        out[ci + 2].u = x1;
        out[ci + 2].v = y1;
        out[ci + 2].color = color;

        out[ci + 3].x = nx0;
        out[ci + 3].y = ny1;
        out[ci + 3].u = x0;
        out[ci + 3].v = y1;
        out[ci + 3].color = color;


        pos_x += g.advance_x;
    }
}

void dct_6(float values[], uniform int stride)
{
    uniform static const float scale[] = { 0.408248290, 0.577350269 };
    uniform static const float c[7] =
        { 1, 0.965925813, 0.866025388, 0.707106769, 0.500000000, 0.258819044, 0 };
    
    float data[6];
    for (uniform int i = 0; i < 3; i++)
    {
        float a = values[stride * i];
        float b = values[stride * (5 - i)];
        data[0 + i] = a + b;
        data[3 + i] = a - b;
    }

    for (uniform int i = 0; i < 6; i++)
    {
        float acc = 0;
        varying float* uniform input = &data[(i % 2) * 3];
        for (uniform int j = 0; j < 3; j++)
        {
            uniform int e = (2 * j + 1)*i;
            e = e % (4 * 6);
            uniform float w = 1;
            if (e>12) { e = 24 - e; }
            if (e>6) { w = -1;  e = 12 - e; }
            w *= c[e];
            acc += w * input[j];
        }

        values[stride * i] = acc * scale[i > 0];
    }
}

void dct_6(float values[], uniform int stride)
{
    uniform static const float scale[] = { 0.408248290, 0.577350269 };
    uniform static const float c[7] =
        { 1, 0.965925813, 0.866025388, 0.707106769, 0.500000000, 0.258819044, 0 };
    
    float data[6];
    for (uniform int i = 0; i < 3; i++)
    {
        float a = values[stride * i];
        float b = values[stride * (5 - i)];
        data[0 + i] = a + b;
        data[3 + i] = a - b;
    }

    for (uniform int i = 0; i < 6; i++)
    {
        float acc = 0;
        varying float* uniform input = &data[(i % 2) * 3];
        for (uniform int j = 0; j < 3; j++)
        {
            uniform int e = (2 * j + 1)*i;
            e = e % (4 * 6);
            uniform float w = 1;
            if (e>12) { e = 24 - e; }
            if (e>6) { w = -1;  e = 12 - e; }
            w *= c[e];
            acc += w * input[j];
        }

        values[stride * i] = acc * scale[i > 0];
    }
}
*/

export void write_index_buffer(uint16 index_buffer[], uniform uint16 start_index, uniform uint16 count) {
    varying uint16 step = 4;
    varying uint16 vertex_id = start_index;

    uniform int chunks = count & ~(programCount-1);

    for (uniform int i = 0; i < chunks; i += programCount) {
        index_buffer[(i * 6) + 0] = vertex_id + 0;
        index_buffer[(i * 6) + 1] = vertex_id + 1;
        index_buffer[(i * 6) + 2] = vertex_id + 2;

        index_buffer[(i * 6) + 3] = vertex_id + 0;
        index_buffer[(i * 6) + 4] = vertex_id + 2;
        index_buffer[(i * 6) + 5] = vertex_id + 3;
        
        vertex_id += step;
    }
}
