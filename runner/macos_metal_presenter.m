/* Adapted from DKC1Recomp's runner/macos_metal_presenter.m and
 * runner/macos_graphics.metal (MIT License, Copyright (c) 2026 DKC1Recomp
 * contributors): the CAMetalLayer view, the CAMetalDisplayLink thread, the
 * input-texture rotation and the Metal translation of the reconstruction
 * upscaler. See THIRD_PARTY_NOTICES.md. */
#import "macos_metal_presenter.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop_present_sdl.h"
#include "desktop_viewport.h"
#include "dkc3_video.h"

/* The upscalers as Metal shading language. dkc3_flat covers the nearest and
 * bilinear samplers (u[4] selects); dkc3_reconstruct is the reconstruction
 * upscaler, the same arithmetic as the GLSL program in desktop_present_sdl.c
 * (which DKC1Recomp had already carried to Metal in macos_graphics.metal).
 * u: source width, source height, output width, output height,
 *    mode or sampler, strength, softness, shading. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
static const char kDkc3MetalShaderSource[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VertexOutput { float4 position [[position]]; float2 uv; };\n"
    "constexpr sampler pointSampler(coord::normalized, address::clamp_to_edge, filter::nearest);\n"
    "constexpr sampler linearSampler(coord::normalized, address::clamp_to_edge, filter::linear);\n"
    "vertex VertexOutput dkc3_vertex(uint id [[vertex_id]]) {\n"
    "  const float2 positions[] = {float2(-1, 1), float2(-1, -1), float2(1, 1), float2(1, -1)};\n"
    "  const float2 coords[] = {float2(0, 0), float2(0, 1), float2(1, 0), float2(1, 1)};\n"
    "  VertexOutput out; out.position = float4(positions[id], 0, 1); out.uv = coords[id]; return out;\n"
    "}\n"
    "fragment float4 dkc3_flat(VertexOutput in [[stage_in]], texture2d<float> source [[texture(0)]],\n"
    "                          constant float *u [[buffer(0)]]) {\n"
    "  if (int(u[4]) == 1) return float4(source.sample(linearSampler, in.uv).rgb, 1);\n"
    "  return float4(source.sample(pointSampler, in.uv).rgb, 1);\n"
    "}\n"
    "float3 tx(float2 t, texture2d<float> source, float2 source_size) {\n"
    "  return source.sample(pointSampler, (t + 0.5) / source_size).rgb;\n"
    "}\n"
    "float df(float3 a, float3 b) {\n"
    "  float3 d = abs(a - b);\n"
    "  return dot(d, float3(0.299, 0.587, 0.114)) * 2.0 + abs((a.r - b.r) - (a.b - b.b)) * 0.5;\n"
    "}\n"
    "bool eq(float3 a, float3 b) { return df(a, b) < 0.004; }\n"
    "float3 reconstruct_decode(float3 c, float3 n, float3 s, float3 w, float3 e,\n"
    "                          float3 nw, float3 ne, float3 sw, float3 se, int mode) {\n"
    "  if (mode < 1) return c;\n"
    "  if (eq(c, nw) && eq(c, ne) && eq(c, sw) && eq(c, se) &&\n"
    "      eq(n, s) && eq(n, e) && eq(n, w) && !eq(c, n))\n"
    "    return mix(c, n, 0.5);\n"
    "  if (eq(c, n) && eq(c, s) && eq(e, ne) && eq(e, se) &&\n"
    "      eq(w, nw) && eq(w, sw) && eq(e, w) && !eq(c, e))\n"
    "    return mix(c, e, 0.5);\n"
    "  if (eq(c, e) && eq(c, w) && eq(n, ne) && eq(n, nw) &&\n"
    "      eq(s, se) && eq(s, sw) && eq(n, s) && !eq(c, n))\n"
    "    return mix(c, n, 0.5);\n"
    "  return c;\n"
    "}\n"
    "fragment float4 dkc3_reconstruct(VertexOutput in [[stage_in]], texture2d<float> source [[texture(0)]],\n"
    "                                 constant float *u [[buffer(0)]]) {\n"
    "  float2 uv = in.uv, source_size = float2(u[0], u[1]), output_size = float2(u[2], u[3]);\n"
    "  int mode = int(u[4]);\n"
    "  float strength = u[5], softness = u[6], shading = u[7];\n"
    "  float2 pos = uv * source_size;\n"
    "  float2 t = floor(pos);\n"
    "  float2 fp = pos - t;\n"
    "  float2 dir = float2(fp.x < 0.5 ? -1.0 : 1.0, fp.y < 0.5 ? -1.0 : 1.0);\n"
    "  float2 f = abs(fp - 0.5) + 0.5;\n"
    "  float2 scale = max(output_size / source_size, float2(1.0));\n"
    "  float band = 1.0 + 2.0 * softness;\n"
    "  float aa = min(scale.x, scale.y) / band;\n"
    "  float2 dx = float2(dir.x, 0.0);\n"
    "  float2 dy = float2(0.0, dir.y);\n"
    "  float3 A1 = tx(t - dx - dy - dy, source, source_size), B1 = tx(t - dy - dy, source, source_size), C1 = tx(t + dx - dy - dy, source, source_size);\n"
    "  float3 A0 = tx(t - dx - dx - dy, source, source_size), A = tx(t - dx - dy, source, source_size), B = tx(t - dy, source, source_size), C = tx(t + dx - dy, source, source_size), C4 = tx(t + dx + dx - dy, source, source_size);\n"
    "  float3 D0 = tx(t - dx - dx, source, source_size), D = tx(t - dx, source, source_size), E = tx(t, source, source_size), F = tx(t + dx, source, source_size), F4 = tx(t + dx + dx, source, source_size);\n"
    "  float3 G0 = tx(t - dx - dx + dy, source, source_size), G = tx(t - dx + dy, source, source_size), H = tx(t + dy, source, source_size), I = tx(t + dx + dy, source, source_size), I4 = tx(t + dx + dx + dy, source, source_size);\n"
    "  float3 G5 = tx(t - dx + dy + dy, source, source_size), H5 = tx(t + dy + dy, source, source_size), I5 = tx(t + dx + dy + dy, source, source_size);\n"
    "  float3 e = reconstruct_decode(E, B, H, D, F, A, C, G, I, mode);\n"
    "  float3 fc = reconstruct_decode(F, C, I, E, F4, B, C4, H, I4, mode);\n"
    "  float3 hc = reconstruct_decode(H, E, H5, G, I, D, F, G5, I5, mode);\n"
    "  float3 ic = reconstruct_decode(I, F, I5, H, I4, E, F4, H5, I5, mode);\n"
    "  float2 adj = 0.5 * clamp((f - (1.0 - 0.5 * band / scale)) * scale / band, 0.0, 1.0);\n"
    "  float3 base = mix(mix(e, fc, adj.x), mix(hc, ic, adj.x), adj.y);\n"
    "  if (shading > 0.0) {\n"
    "    float2 g = f - 0.5;\n"
    "    float3 bil = mix(mix(e, fc, g.x), mix(hc, ic, g.x), g.y);\n"
    "    float sim = max(max(df(e, fc), df(e, hc)), df(e, ic));\n"
    "    float w = shading * (1.0 - smoothstep(0.03, 0.14, sim));\n"
    "    base = mix(base, bil, w);\n"
    "  }\n"
    "  if (mode < 2) { return float4(base, 1.0); }\n"
    "  float wd1 = df(E, C) + df(E, G) + df(I, H5) + df(I, F4) + 4.0 * df(H, F);\n"
    "  float wd2 = df(H, D) + df(H, I5) + df(F, I4) + df(F, B) + 4.0 * df(E, I);\n"
    "  bool edr = wd1 < wd2 && !eq(E, H) && !eq(E, F) && !(eq(E, I) && eq(H, F));\n"
    "  if (!edr) { return float4(base, 1.0); }\n"
    "  float3 nc = (df(E, F) <= df(E, H)) ? fc : hc;\n"
    "  float cov = clamp((f.x + f.y - 1.5) * aa + 0.5, 0.0, 1.0);\n"
    "  if (mode >= 3) {\n"
    "    bool left = 2.0 * df(F, G) <= df(H, C) && !eq(E, G) && !eq(D, G);\n"
    "    bool up = df(F, G) >= 2.0 * df(H, C) && !eq(E, C) && !eq(B, C);\n"
    "    if (left) cov = max(cov, clamp((2.0 * f.x + f.y - 2.0) * aa * 0.75 + 0.5, 0.0, 1.0));\n"
    "    if (up) cov = max(cov, clamp((f.x + 2.0 * f.y - 2.0) * aa * 0.75 + 0.5, 0.0, 1.0));\n"
    "    if (mode >= 4) {\n"
    "      bool left3 = left && 4.0 * df(F, G) <= df(H, C) && !eq(E, G0) && !eq(D0, G0);\n"
    "      bool up3 = up && df(F, G) >= 4.0 * df(H, C) && !eq(E, C1) && !eq(B1, C1);\n"
    "      if (left3) cov = max(cov, clamp((3.0 * f.x + f.y - 2.5) * aa * 0.6 + 0.5, 0.0, 1.0));\n"
    "      if (up3) cov = max(cov, clamp((f.x + 3.0 * f.y - 2.5) * aa * 0.6 + 0.5, 0.0, 1.0));\n"
    "    }\n"
    "  }\n"
    "  return float4(mix(base, nc, cov * strength), 1.0);\n"
    "}\n";
#pragma clang diagnostic pop

enum {
  kDkc3MetalFrameSlots = 3,
  kDkc3MetalInputTextures = 3,
  kDkc3MetalFrameBytes = kDkc3VideoMaximumWidth * kDkc3VideoHeight * 4,
};

typedef struct Dkc3MetalFrame {
  uint8_t pixels[kDkc3MetalFrameBytes];
  int width;
  int height;
  uint64_t sequence;
  Dkc3MacMetalFrameSettings settings;
} Dkc3MetalFrame;

static double HostSeconds(void) {
  static mach_timebase_info_data_t timebase;
  if (!timebase.denom) (void)mach_timebase_info(&timebase);
  return (double)mach_absolute_time() * (double)timebase.numer /
         (double)timebase.denom / 1000000000.0;
}

@interface Dkc3MetalView : NSView
- (void)updateMetalDrawableSize;
@end

@implementation Dkc3MetalView
- (CALayer *)makeBackingLayer {
  return [CAMetalLayer layer];
}
- (void)updateMetalDrawableSize {
  CAMetalLayer *metalLayer = (CAMetalLayer *)self.layer;
  if (![metalLayer isKindOfClass:[CAMetalLayer class]]) return;
  NSRect backing = [self convertRectToBacking:self.bounds];
  metalLayer.drawableSize =
      CGSizeMake(MAX(1.0, NSWidth(backing)), MAX(1.0, NSHeight(backing)));
  const CGFloat scale = self.window.backingScaleFactor;
  metalLayer.contentsScale = scale > 0.0 ? scale : 1.0;
}
- (void)layout {
  [super layout];
  [self updateMetalDrawableSize];
}
- (void)viewDidChangeBackingProperties {
  [super viewDidChangeBackingProperties];
  [self updateMetalDrawableSize];
}
@end

@interface Dkc3MetalPresenter : NSObject <CAMetalDisplayLinkDelegate> {
@public
  Dkc3MetalView *view;
  CAMetalLayer *metalLayer;
  id<MTLDevice> device;
  id<MTLCommandQueue> commandQueue;
  id<MTLRenderPipelineState> flatPipeline;
  id<MTLRenderPipelineState> reconstructPipeline;
  id<MTLTexture> inputs[kDkc3MetalInputTextures];
  BOOL inputBusy[kDkc3MetalInputTextures];
  NSLock *inputLock;
  CAMetalDisplayLink *displayLink;
  NSThread *displayThread;
  NSCondition *condition;
  BOOL started;
  BOOL stopRequested;
  BOOL stopped;
  _Atomic bool visible;
  /* Frame mailbox: the newest complete frame waits in `pending`; `current`
   * is the frame being shown. Producers write only slots that are neither. */
  Dkc3MetalFrame frames[kDkc3MetalFrameSlots];
  NSLock *frameLock;
  int current;
  int pending;
  uint64_t nextSequence;
  /* Input texture holding the current frame, reused across repeats. */
  int uploadedSlot;
  uint64_t uploadedSequence;
  /* Ticks, published with the display link's contract. */
  pthread_mutex_t tickMutex;
  pthread_cond_t tickChanged;
  Dkc3MacDisplayTick latestTick;
  /* One-shot capture. */
  NSLock *captureLock;
  uint8_t *captureRgb;
  int captureWidth;
  int captureHeight;
  _Atomic bool captureDone;
  BOOL captureReadable;
  /* Counters. */
  _Atomic uint64_t callbacks;
  _Atomic uint64_t presented;
  _Atomic uint64_t repeated;
  _Atomic uint64_t dropped;
}
- (BOOL)buildPipelines:(NSError **)outError;
- (void)runDisplayThread;
@end

static Dkc3MetalPresenter *s_presenter;
static _Atomic bool s_active;

@implementation Dkc3MetalPresenter

- (BOOL)buildPipelines:(NSError **)outError {
  NSString *source = [NSString stringWithUTF8String:kDkc3MetalShaderSource];
  MTLCompileOptions *options = [[[MTLCompileOptions alloc] init] autorelease];
  /* Same arithmetic as the OpenGL program: no fast-math contraction. */
  if (@available(macOS 15.0, *)) {
    options.mathMode = MTLMathModeSafe;
  } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    options.fastMathEnabled = NO;
#pragma clang diagnostic pop
  }
  id<MTLLibrary> library = [device newLibraryWithSource:source
                                                options:options
                                                  error:outError];
  if (!library) return NO;
  id<MTLFunction> vertex = [library newFunctionWithName:@"dkc3_vertex"];
  id<MTLFunction> flat = [library newFunctionWithName:@"dkc3_flat"];
  id<MTLFunction> reconstruct =
      [library newFunctionWithName:@"dkc3_reconstruct"];
  BOOL ok = vertex && flat && reconstruct;
  if (ok) {
    MTLRenderPipelineDescriptor *descriptor =
        [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = flat;
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    flatPipeline = [device newRenderPipelineStateWithDescriptor:descriptor
                                                          error:outError];
    descriptor.fragmentFunction = reconstruct;
    reconstructPipeline =
        [device newRenderPipelineStateWithDescriptor:descriptor
                                               error:outError];
    [descriptor release];
    ok = flatPipeline && reconstructPipeline;
  }
  [vertex release];
  [flat release];
  [reconstruct release];
  [library release];
  return ok;
}

- (void)runDisplayThread {
  @autoreleasepool {
    [NSThread currentThread].qualityOfService =
        NSQualityOfServiceUserInteractive;
    NSRunLoop *runLoop = [NSRunLoop currentRunLoop];
    [displayLink addToRunLoop:runLoop forMode:NSRunLoopCommonModes];
    [condition lock];
    started = YES;
    [condition signal];
    [condition unlock];
    for (;;) {
      [condition lock];
      BOOL shouldStop = stopRequested;
      [condition unlock];
      if (shouldStop) break;
      @autoreleasepool {
        [runLoop runMode:NSDefaultRunLoopMode
              beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.050]];
      }
    }
    [displayLink invalidate];
    [condition lock];
    stopped = YES;
    [condition broadcast];
    [condition unlock];
  }
}

- (void)publishTickWithTarget:(CFTimeInterval)target
                 presentation:(CFTimeInterval)presentation {
  const double now = HostSeconds();
  pthread_mutex_lock(&tickMutex);
  /* The pacer locks its refresh divisor on the tick interval. Callback
   * delivery jitters by a fraction of a millisecond, so measure the interval
   * between the link's refresh targets, which are exact. */
  latestTick.interval =
      latestTick.sequence && latestTick.target > 0.0 ? target - latestTick.target
                                                     : 0.0;
  latestTick.sequence++;
  latestTick.timestamp = now;
  latestTick.target = target;
  latestTick.duration = presentation > target ? presentation - target : 0.0;
  pthread_cond_broadcast(&tickChanged);
  pthread_mutex_unlock(&tickMutex);
}

- (int)acquireInputTextureWidth:(int)width height:(int)height {
  int slot = -1;
  [inputLock lock];
  for (int i = 0; i < kDkc3MetalInputTextures; i++) {
    if (!inputBusy[i]) {
      slot = i;
      break;
    }
  }
  [inputLock unlock];
  if (slot < 0) return -1;
  id<MTLTexture> texture = inputs[slot];
  if (!texture || texture.width != (NSUInteger)width ||
      texture.height != (NSUInteger)height) {
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:(NSUInteger)width
                                    height:(NSUInteger)height
                                 mipmapped:NO];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;
    [inputs[slot] release];
    inputs[slot] = [device newTextureWithDescriptor:descriptor];
    if (!inputs[slot]) return -1;
  }
  return slot;
}

- (void)metalDisplayLink:(CAMetalDisplayLink *)link
             needsUpdate:(CAMetalDisplayLinkUpdate *)update
    API_AVAILABLE(macos(14.0)) {
  (void)link;
  @autoreleasepool {
    atomic_fetch_add(&callbacks, 1);
    [self publishTickWithTarget:update.targetTimestamp
                   presentation:update.targetPresentationTimestamp];
    if (!atomic_load(&visible)) return;

    /* Take the newest complete frame, if one arrived since the last
     * callback; otherwise repeat the current one. */
    [frameLock lock];
    if (pending >= 0) {
      current = pending;
      pending = -1;
    }
    const int shown = current;
    [frameLock unlock];
    if (shown < 0) return;
    Dkc3MetalFrame *frame = &frames[shown];

    int slot = uploadedSlot;
    if (uploadedSequence != frame->sequence || slot < 0 || !inputs[slot]) {
      slot = [self acquireInputTextureWidth:frame->width
                                     height:frame->height];
      if (slot < 0) {
        /* Every input texture is still in flight; keep the previous one. */
        slot = uploadedSlot;
        if (slot < 0 || !inputs[slot]) return;
        atomic_fetch_add(&repeated, 1);
      } else {
        [inputs[slot] replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)frame->width,
                                                    (NSUInteger)frame->height)
                        mipmapLevel:0
                          withBytes:frame->pixels
                        bytesPerRow:(NSUInteger)frame->width * 4];
        uploadedSlot = slot;
        uploadedSequence = frame->sequence;
        atomic_fetch_add(&presented, 1);
      }
    } else {
      atomic_fetch_add(&repeated, 1);
    }

    id<CAMetalDrawable> drawable = update.drawable;
    if (!drawable) return;
    id<MTLTexture> target = drawable.texture;
    Dkc3DesktopViewport viewport;
    if (!Dkc3DesktopComputeViewport((int)target.width, (int)target.height,
                                    frame->width, frame->height, &viewport))
      return;
    const Dkc3MacMetalFrameSettings settings = frame->settings;
    const bool reconstruct = settings.upscaler == kDkc3UpscalerReconstruct;
    float uniforms[8] = {
      (float)frame->width, (float)frame->height,
      (float)viewport.width, (float)viewport.height,
      reconstruct ? (float)settings.reconstruct_mode
                  : (settings.linear_filter ? 1.0f : 0.0f),
      settings.reconstruct_strength, settings.reconstruct_softness,
      settings.reconstruct_shading,
    };

    id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
    MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    id<MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:reconstruct ? reconstructPipeline
                                                : flatPipeline];
    [encoder setViewport:(MTLViewport){(double)viewport.x, (double)viewport.y,
                                       (double)viewport.width,
                                       (double)viewport.height, 0.0, 1.0}];
    [encoder setFragmentTexture:inputs[slot] atIndex:0];
    [encoder setFragmentBytes:uniforms length:sizeof uniforms atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                vertexStart:0
                vertexCount:4];
    [encoder endEncoding];

    /* Screenshot: copy the finished drawable into a shared texture and read
     * it back once the GPU has completed. */
    uint8_t *captureTarget = NULL;
    int captureW = 0, captureH = 0;
    [captureLock lock];
    if (captureRgb && captureReadable && captureWidth == (int)target.width &&
        captureHeight == (int)target.height) {
      captureTarget = captureRgb;
      captureW = captureWidth;
      captureH = captureHeight;
      captureRgb = NULL;
    }
    [captureLock unlock];
    id<MTLTexture> captureTexture = nil;
    if (captureTarget) {
      MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                       width:(NSUInteger)captureW
                                      height:(NSUInteger)captureH
                                   mipmapped:NO];
      descriptor.storageMode = MTLStorageModeShared;
      descriptor.usage = MTLTextureUsageShaderRead;
      captureTexture = [device newTextureWithDescriptor:descriptor];
      if (captureTexture) {
        id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
        [blit copyFromTexture:target
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake((NSUInteger)captureW,
                                          (NSUInteger)captureH, 1)
                    toTexture:captureTexture
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
      }
    }

    [inputLock lock];
    inputBusy[slot] = YES;
    [inputLock unlock];
    Dkc3MetalPresenter *presenter = self;
    const int usedSlot = slot;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
      if (completed.status == MTLCommandBufferStatusError) {
        const char *reason = completed.error.localizedDescription.UTF8String;
        fprintf(stderr, "[metal-presenter] command buffer failed: %s\n",
                reason ? reason : "unknown");
      }
      [presenter->inputLock lock];
      presenter->inputBusy[usedSlot] = NO;
      [presenter->inputLock unlock];
      if (captureTexture && captureTarget) {
        const size_t stride = (size_t)captureW * 4u;
        uint8_t *bgra = (uint8_t *)malloc(stride * (size_t)captureH);
        if (bgra) {
          [captureTexture getBytes:bgra
                       bytesPerRow:stride
                        fromRegion:MTLRegionMake2D(0, 0, (NSUInteger)captureW,
                                                   (NSUInteger)captureH)
                       mipmapLevel:0];
          for (int y = 0; y < captureH; y++) {
            const uint8_t *row = bgra + (size_t)y * stride;
            uint8_t *out = captureTarget + (size_t)y * (size_t)captureW * 3u;
            for (int x = 0; x < captureW; x++) {
              out[x * 3 + 0] = row[x * 4 + 2];
              out[x * 3 + 1] = row[x * 4 + 1];
              out[x * 3 + 2] = row[x * 4 + 0];
            }
          }
          free(bgra);
        }
        [captureTexture release];
        atomic_store(&presenter->captureDone, true);
      }
    }];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
  }
}

- (void)dealloc {
  for (int i = 0; i < kDkc3MetalInputTextures; i++) [inputs[i] release];
  [flatPipeline release];
  [reconstructPipeline release];
  [commandQueue release];
  [device release];
  [displayLink release];
  [displayThread release];
  [condition release];
  [frameLock release];
  [inputLock release];
  [captureLock release];
  [view release];
  pthread_mutex_destroy(&tickMutex);
  pthread_cond_destroy(&tickChanged);
  [super dealloc];
}

@end

static void SetError(char *error, size_t capacity, const char *message) {
  if (error && capacity) (void)snprintf(error, capacity, "%s", message);
}

bool Dkc3MacMetalPresenterStart(void *native_window, double preferred_hz,
                                char *error, size_t error_capacity) {
  @autoreleasepool {
    if (s_presenter) return true;
    if (!native_window) {
      SetError(error, error_capacity, "no native window for the Metal presenter");
      return false;
    }
    if (@available(macOS 14.0, *)) {
      NSWindow *window = (NSWindow *)native_window;
      NSView *contentView = window.contentView;
      if (!contentView) {
        SetError(error, error_capacity, "the window has no content view");
        return false;
      }
      Dkc3MetalPresenter *presenter = [[Dkc3MetalPresenter alloc] init];
      presenter->condition = [[NSCondition alloc] init];
      presenter->frameLock = [[NSLock alloc] init];
      presenter->inputLock = [[NSLock alloc] init];
      presenter->captureLock = [[NSLock alloc] init];
      {
        const char *shot = getenv("DKC3_DESKTOP_SCREENSHOT");
        presenter->captureReadable = shot && *shot;
      }
      pthread_mutex_init(&presenter->tickMutex, NULL);
      pthread_cond_init(&presenter->tickChanged, NULL);
      presenter->current = -1;
      presenter->pending = -1;
      presenter->uploadedSlot = -1;
      atomic_store(&presenter->visible, true);
      presenter->device = MTLCreateSystemDefaultDevice();
      presenter->commandQueue = [presenter->device newCommandQueue];
      if (!presenter->device || !presenter->commandQueue) {
        SetError(error, error_capacity, "no Metal device");
        [presenter release];
        return false;
      }
      NSError *pipelineError = nil;
      if (![presenter buildPipelines:&pipelineError]) {
        const char *reason = pipelineError.localizedDescription.UTF8String;
        if (error && error_capacity)
          (void)snprintf(error, error_capacity, "Metal pipeline: %s",
                         reason ? reason : "unknown");
        [presenter release];
        return false;
      }
      presenter->view =
          [[Dkc3MetalView alloc] initWithFrame:contentView.bounds];
      presenter->view.autoresizingMask =
          NSViewWidthSizable | NSViewHeightSizable;
      presenter->view.wantsLayer = YES;
      presenter->metalLayer = (CAMetalLayer *)presenter->view.layer;
      presenter->metalLayer.device = presenter->device;
      presenter->metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
      presenter->metalLayer.framebufferOnly = !presenter->captureReadable;
      presenter->metalLayer.opaque = YES;
      presenter->metalLayer.backgroundColor = NSColor.blackColor.CGColor;
      presenter->metalLayer.maximumDrawableCount = 3;
      /* The display link is the only cadence authority. */
      presenter->metalLayer.displaySyncEnabled = NO;
      presenter->metalLayer.presentsWithTransaction = NO;
      [contentView addSubview:presenter->view
                   positioned:NSWindowAbove
                   relativeTo:nil];
      [presenter->view updateMetalDrawableSize];

      presenter->displayLink = [[CAMetalDisplayLink alloc]
          initWithMetalLayer:presenter->metalLayer];
      presenter->displayLink.delegate = presenter;
      presenter->displayLink.preferredFrameLatency = 1.0f;
      if (preferred_hz > 0.0) {
        const float rate = (float)preferred_hz;
        presenter->displayLink.preferredFrameRateRange =
            CAFrameRateRangeMake(rate, rate, rate);
      }
      presenter->displayLink.paused = NO;
      presenter->displayThread =
          [[NSThread alloc] initWithTarget:presenter
                                  selector:@selector(runDisplayThread)
                                    object:nil];
      presenter->displayThread.name = @"DKC3 Metal presenter";
      s_presenter = presenter;
      [presenter->displayThread start];
      [presenter->condition lock];
      NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:1.0];
      while (!presenter->started && !presenter->stopped) {
        if (![presenter->condition waitUntilDate:deadline]) break;
      }
      const BOOL running = presenter->started && !presenter->stopped;
      [presenter->condition unlock];
      if (running) {
        atomic_store(&s_active, true);
        return true;
      }
      SetError(error, error_capacity, "the Metal display link did not start");
      Dkc3MacMetalPresenterStop();
      return false;
    }
    SetError(error, error_capacity, "the Metal presenter needs macOS 14");
    return false;
  }
}

bool Dkc3MacMetalPresenterActive(void) {
  return atomic_load(&s_active);
}

void Dkc3MacMetalPresenterQueueFrame(
    const uint8_t *bgra, int width, int height,
    const Dkc3MacMetalFrameSettings *settings) {
  Dkc3MetalPresenter *presenter = s_presenter;
  if (!presenter || !bgra || !settings || width <= 0 || height <= 0 ||
      width > kDkc3VideoMaximumWidth || height > kDkc3VideoHeight)
    return;
  /* Choose a slot that is neither shown nor waiting; with three slots one
   * always exists. */
  [presenter->frameLock lock];
  int slot = 0;
  while (slot == presenter->current || slot == presenter->pending) slot++;
  [presenter->frameLock unlock];
  Dkc3MetalFrame *frame = &presenter->frames[slot];
  memcpy(frame->pixels, bgra, (size_t)width * (size_t)height * 4u);
  frame->width = width;
  frame->height = height;
  frame->settings = *settings;
  [presenter->frameLock lock];
  frame->sequence = ++presenter->nextSequence;
  if (presenter->pending >= 0) atomic_fetch_add(&presenter->dropped, 1);
  presenter->pending = slot;
  [presenter->frameLock unlock];
}

void Dkc3MacMetalPresenterSetVisible(bool visible) {
  Dkc3MetalPresenter *presenter = s_presenter;
  if (!presenter) return;
  if (atomic_load(&presenter->visible) == visible) return;
  atomic_store(&presenter->visible, visible);
  presenter->view.hidden = !visible;
}

bool Dkc3MacMetalPresenterWaitTick(uint64_t sequence, double timeout_seconds,
                                   Dkc3MacDisplayTick *tick) {
  Dkc3MetalPresenter *presenter = s_presenter;
  if (!presenter || !atomic_load(&s_active)) {
    if (tick) memset(tick, 0, sizeof *tick);
    return false;
  }
  struct timespec relative;
  if (timeout_seconds < 0.0) timeout_seconds = 0.0;
  relative.tv_sec = (time_t)timeout_seconds;
  relative.tv_nsec =
      (long)((timeout_seconds - (double)relative.tv_sec) * 1000000000.0);
  pthread_mutex_lock(&presenter->tickMutex);
  bool arrived = presenter->latestTick.sequence >= sequence;
  if (!arrived && timeout_seconds > 0.0) {
    (void)pthread_cond_timedwait_relative_np(&presenter->tickChanged,
                                             &presenter->tickMutex, &relative);
    arrived = presenter->latestTick.sequence >= sequence;
  }
  if (tick) *tick = presenter->latestTick;
  pthread_mutex_unlock(&presenter->tickMutex);
  return arrived;
}

bool Dkc3MacMetalPresenterLatestTick(Dkc3MacDisplayTick *tick) {
  Dkc3MetalPresenter *presenter = s_presenter;
  if (!presenter) {
    if (tick) memset(tick, 0, sizeof *tick);
    return false;
  }
  pthread_mutex_lock(&presenter->tickMutex);
  if (tick) *tick = presenter->latestTick;
  pthread_mutex_unlock(&presenter->tickMutex);
  return atomic_load(&s_active);
}

void Dkc3MacMetalPresenterArmCapture(uint8_t *rgb, int width, int height) {
  Dkc3MetalPresenter *presenter = s_presenter;
  if (!presenter) return;
  [presenter->captureLock lock];
  presenter->captureRgb = rgb;
  presenter->captureWidth = width;
  presenter->captureHeight = height;
  atomic_store(&presenter->captureDone, false);
  [presenter->captureLock unlock];
}

bool Dkc3MacMetalPresenterCaptureDone(void) {
  Dkc3MetalPresenter *presenter = s_presenter;
  return presenter && atomic_load(&presenter->captureDone);
}

void Dkc3MacMetalPresenterStats(uint64_t *callbacks, uint64_t *presented,
                                uint64_t *repeated, uint64_t *dropped) {
  Dkc3MetalPresenter *presenter = s_presenter;
  if (callbacks) *callbacks = presenter ? atomic_load(&presenter->callbacks) : 0;
  if (presented) *presented = presenter ? atomic_load(&presenter->presented) : 0;
  if (repeated) *repeated = presenter ? atomic_load(&presenter->repeated) : 0;
  if (dropped) *dropped = presenter ? atomic_load(&presenter->dropped) : 0;
}

void Dkc3MacMetalPresenterStop(void) {
  @autoreleasepool {
    Dkc3MetalPresenter *presenter = s_presenter;
    if (!presenter) return;
    atomic_store(&s_active, false);
    s_presenter = nil;
    [presenter->condition lock];
    presenter->stopRequested = YES;
    NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:1.0];
    while (!presenter->stopped) {
      if (![presenter->condition waitUntilDate:deadline]) break;
    }
    [presenter->condition unlock];
    pthread_mutex_lock(&presenter->tickMutex);
    pthread_cond_broadcast(&presenter->tickChanged);
    pthread_mutex_unlock(&presenter->tickMutex);
    [presenter->view removeFromSuperview];
    /* Completed-handler blocks hold their own reference to the presenter,
     * so command buffers still in flight drain before it is freed. */
    [presenter release];
  }
}
