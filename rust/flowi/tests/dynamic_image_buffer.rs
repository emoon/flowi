//! A dynamic image is drawn from the caller's pixels after the frame callback returns, so the buffer is
//! owned by `DynamicImageBuffer` and stays put for as long as the value lives: the pixels written in one frame
//! are what the software renderer reads back, and a rewrite before the next frame shows up in that frame.

use flowi::{Application, Color, DynamicImage, DynamicImageBuffer, EmbeddedSettings, ImageOpts};

const WIDTH: u32 = 64;
const HEIGHT: u32 = 48;
const SIDE: u32 = 16;

/// The tint multiply rounds, so a channel can land a step off the buffer's value.
const TOLERANCE: u8 = 2;

const RED: [u8; 3] = [200, 40, 40];
const GREEN: [u8; 3] = [40, 200, 40];

fn fill(buffer: &mut DynamicImageBuffer, rgb: [u8; 3]) {
    for px in buffer.pixels_mut().as_chunks_mut::<4>().0.iter_mut() {
        *px = [rgb[0], rgb[1], rgb[2], 255];
    }
}

fn count_pixels(app: &Application, rgb: [u8; 3]) -> usize {
    let pixels = app.pixels().expect("software backend reads back");
    pixels
        .rows()
        .flat_map(|row| row.as_chunks::<3>().0)
        .filter(|px| {
            px.iter()
                .zip(rgb)
                .all(|(&got, want)| got.abs_diff(want) <= TOLERANCE)
        })
        .count()
}

fn draw(buffer: &DynamicImageBuffer) {
    let opts = ImageOpts {
        color: Color {
            r: 255,
            g: 255,
            b: 255,
            a: 255,
        },
        fill_container: false,
    };
    // SAFETY: buffer outlives the tick_embedded call this runs in and is not written during it.
    unsafe { DynamicImage::new(buffer, opts) };
}

#[test]
fn the_renderer_reads_the_pixels_the_buffer_owns() {
    let mut app =
        Application::embedded(EmbeddedSettings::new(WIDTH, HEIGHT)).expect("embedded flowi");
    let mut buffer = DynamicImageBuffer::new(SIDE, SIDE);
    assert_eq!(
        buffer.pixels().len(),
        (SIDE * SIDE * 4) as usize,
        "RGBA8, no padding"
    );

    fill(&mut buffer, RED);
    app.tick_embedded(1.0 / 60.0, || draw(&buffer));
    assert_eq!(
        count_pixels(&app, RED),
        (SIDE * SIDE) as usize,
        "the image covers exactly its own pixels, in the buffer's colour"
    );

    fill(&mut buffer, GREEN);
    app.tick_embedded(1.0 / 60.0, || draw(&buffer));
    assert_eq!(
        count_pixels(&app, GREEN),
        (SIDE * SIDE) as usize,
        "the rewrite is what renders"
    );
    assert_eq!(
        count_pixels(&app, RED),
        0,
        "nothing of the previous frame's pixels remains"
    );
}
