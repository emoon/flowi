fn main() {
    let buffer = flowi::DynamicImageBuffer::new(1, 1);
    let opts = flowi::ImageOpts {
        color: flowi::Color { r: 255, g: 255, b: 255, a: 255 },
        fill_container: false,
    };
    flowi::DynamicImage::new(&buffer, opts);
}
