// Cross-process probe for the IOSurface surface-texture variant, for tests only. gpu_smoke_test.c is plain C and
// never names a Metal or IOSurface type, so this translation unit is its Objective-C arm. It exposes one C-callable
// function that stands in for an out-of-process host: it opens a flowi-produced IOSurface into a second MTLTexture
// on a freshly created MTLDevice, independent of the backend's device, and blits the pixels back to the CPU. Reading
// the expected pixels through that device shows, within one process, that the surface is valid across contexts.
// Compiled into gpu_smoke_test on Apple platforms only.

#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#import <Foundation/Foundation.h>

#include <core/types.h> // u8, u32
#include <string.h>     // memcpy

// Open `iosurface` (an IOSurfaceRef as void*) into a second MTLTexture on a fresh system device, read its RGBA8 pixels
// back, and copy them tightly into out_rgba (width*height*4 bytes, row 0 first). Returns true on success. Mirrors the
// backend's private-target readback (256-aligned blit bytes-per-row, then strip the row padding) so the bytes compare
// directly against a readback of the same surface through the primary accessor.
bool gpu_test_iosurface_readback(void* iosurface, int width, int height, u8* out_rgba) {
    @autoreleasepool {
        IOSurfaceRef surf = (IOSurfaceRef)iosurface;
        if (surf == nullptr || width <= 0 || height <= 0 || out_rgba == nullptr) {
            return false;
        }

        // A fresh device, so the readback proves the IOSurface is valid outside the context that produced it. On a
        // single-GPU Mac it is the same physical device behind an independent handle, and the cross-context path
        // (open IOSurface -> texture -> blit) is exercised all the same. The build is MRC (no ARC), so every
        // "new"/"Create" object below is +1-owned and released before each return, as in the backend's gpu_metal.m
        // and gpu_debug_readback_metal.m. [nil release] is a no-op, so the failure paths are safe.
        id<MTLDevice> device = MTLCreateSystemDefaultDevice(); // +1 owned
        if (device == nil) {
            return false;
        }

        MTLTextureDescriptor* desc = // autoreleased, drained by the pool
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:(NSUInteger)width
                                                              height:(NSUInteger)height mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = device.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
        id<MTLTexture> tex = [device newTextureWithDescriptor:desc iosurface:surf plane:0]; // +1 owned
        if (tex == nil) {
            [device release];
            return false;
        }

        NSUInteger w = (NSUInteger)width;
        NSUInteger h = (NSUInteger)height;
        NSUInteger tight_bpr = w * 4u;
        NSUInteger aligned_bpr = (tight_bpr + 255u) & ~(NSUInteger)255u; // 256-aligned blit destination (macOS)

        id<MTLCommandQueue> queue = [device newCommandQueue];                                              // +1 owned
        id<MTLBuffer> staging = [device newBufferWithLength:aligned_bpr * h options:MTLResourceStorageModeShared]; // +1
        if (queue == nil || staging == nil) {
            [staging release];
            [queue release];
            [tex release];
            [device release];
            return false;
        }

        id<MTLCommandBuffer> cmd = [queue commandBuffer]; // autoreleased convenience objects, drained by the pool
        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit copyFromTexture:tex
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(w, h, 1)
                        toBuffer:staging
               destinationOffset:0
          destinationBytesPerRow:aligned_bpr
        destinationBytesPerImage:aligned_bpr * h];
        [blit endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];

        const u8* src = (const u8*)[staging contents];
        for (NSUInteger y = 0; y < h; y++) {
            memcpy(out_rgba + y * tight_bpr, src + y * aligned_bpr, tight_bpr);
        }

        [staging release];
        [queue release];
        [tex release];
        [device release];
        return true;
    }
}

// CoreFoundation retain-count probes for the IOSurface leak test (gpu_smoke_test.c). IOSurfaceRef is a CF type;
// keeping these here lets the plain-C test assert the backend's CFRelease balance without pulling any CF/Metal type
// into gpu_smoke_test.c. gpu_test_cf_retain holds an independent reference across a surface destroy, and the retain
// count afterwards shows whether the backend dropped exactly its own references.
void gpu_test_cf_retain(void* obj) {
    if (obj != nullptr) {
        CFRetain((CFTypeRef)obj);
    }
}

long gpu_test_cf_retain_count(void* obj) {
    return obj != nullptr ? (long)CFGetRetainCount((CFTypeRef)obj) : 0;
}

void gpu_test_cf_release(void* obj) {
    if (obj != nullptr) {
        CFRelease((CFTypeRef)obj);
    }
}
