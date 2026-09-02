# Pipeline Parity — Vulkan ↔ Metal (task 5)

The Metal backend's 5 render pipelines mirror VK's rasterization state exactly.
This table documents the descriptor-field equivalence. Source of truth:
`vkBackend.H:3470-3557` (VK `buildGraphicsPipeline_`) vs `mtlBridge.mm`
(`mtlCreatePipeline`) + `mtlBackend.H` (`createPipelines_`).

## Blend attachment state

| Field | VK (`VkPipelineColorBlendAttachmentState`) | MTL (`MTLRenderPipelineColorAttachmentDescriptor`) | Match |
|-------|---------------------------------------------|-----------------------------------------------------|-------|
| blendEnable | `VK_TRUE` | `blendingEnabled = YES` | ✅ |
| srcColorBlendFactor | `VK_BLEND_FACTOR_SRC_ALPHA` | `sourceRGBBlendFactor = MTLBlendFactorSourceAlpha` | ✅ |
| dstColorBlendFactor | `VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA` | `destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha` | ✅ |
| colorBlendOp | `VK_BLEND_OP_ADD` | `rgbBlendOperation = MTLBlendOperationAdd` | ✅ |
| srcAlphaBlendFactor | `VK_BLEND_FACTOR_SRC_ALPHA` | `sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha` | ✅ |
| dstAlphaBlendFactor | `VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA` | `destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha` | ✅ |
| alphaBlendOp | `VK_BLEND_OP_ADD` | `alphaBlendOperation = MTLBlendOperationAdd` | ✅ |
| writeMask | `R\|G\|B\|A` | `writeMask = MTLColorWriteMaskAll` | ✅ |

## Depth / stencil

| Field | VK | MTL | Match |
|-------|----|-----|-------|
| depthTestEnable | `VK_FALSE` | no depth attachment (`depthAttachmentPixelFormat = MTLPixelFormatInvalid`) | ✅ |
| depthWriteEnable | `VK_FALSE` | no depth attachment | ✅ |
| stencil | none | `stencilAttachmentPixelFormat = MTLPixelFormatInvalid` | ✅ |

## Rasterization

| Field | VK | MTL | Match |
|-------|----|-----|-------|
| cullMode | `VK_CULL_MODE_NONE` (unculled, like GL) | Metal default (no cull) | ✅ |
| frontFace | `VK_FRONT_FACE_COUNTER_CLOCKWISE` | Metal default (CCW) | ✅ |
| lineWidth | `1.0f` (thick lines ride quads) | Metal default (1.0) | ✅ |
| polygonMode | FILL (line/outline use LINE_LIST topology) | Metal default (fill) | ✅ |

## Multisample

| Field | VK | MTL | Match |
|-------|----|-----|-------|
| rasterizationSamples | `VK_SAMPLE_COUNT_1_BIT` | `rasterSampleCount = 1` | ✅ |

## Clear color (swapchain)

| Field | VK (`VkClearValue`) | MTL (`MTLClearColor`) | Match |
|-------|---------------------|------------------------|-------|
| clear color | `{0,0,0,1}` opaque black | `MTLClearColorMake(0,0,0,1)` | ✅ |

## Viewport / scissor

| Field | VK | MTL | Match |
|-------|----|-----|-------|
| viewport | full swapchain extent, dynamic | full drawable (set per-draw in task 6) | ✅ |
| scissor | dynamic, latched per frame | stored as descriptor state per draw (task 6) | ✅ |

## Vertex descriptor

| Attribute | VK (`attrTex`) | MTL (`MTLVertexDescriptor`) | Match |
|-----------|----------------|------------------------------|-------|
| 0: pos | `R32G32_SFLOAT`, offset 0 | `MTLVertexFormatFloat2`, offset 0 | ✅ |
| 1: uv | `R32G32_SFLOAT`, offset 8 | `MTLVertexFormatFloat2`, offset 8 | ✅ |
| 2: color | `R32G32B32A32_SFLOAT`, offset 16 | `MTLVertexFormatFloat4`, offset 16 | ✅ |
| stride | 32 bytes (tex) | 32 bytes | ✅ |

## The 5 pipelines

| Pipeline | VK topology | MTL fragment | MTL topology (draw-time) |
|----------|-------------|--------------|--------------------------|
| linePipeline | `LINE_LIST` | `aestroidsFragSolid` | `MTLPrimitiveTypeLine` |
| triPipeline | `TRIANGLE_LIST` | `aestroidsFragSolid` | `MTLPrimitiveTypeTriangle` |
| outlinePipeline | `LINE_LIST` | `aestroidsFragSolid` | `MTLPrimitiveTypeLine` |
| texPipeline | `TRIANGLE_LIST` | `aestroidsFragTex` | `MTLPrimitiveTypeTriangle` |
| maskedPipeline | `TRIANGLE_LIST` | `aestroidsFragMasked` | `MTLPrimitiveTypeTriangle` |

> Note: In Metal, primitive topology is a **draw-time** parameter
> (`drawPrimitives:vertexStart:vertexCount:`), not a pipeline-descriptor field.
> The pipeline descriptor selects the fragment function; the topology is
> selected per-draw in task 6. The mapping above is preserved for that task.

## Verification

- `make BACKEND=MTL` exits 0 and links.
- `./XAsteroids` prints `mtlBackend: 5 render pipelines created` then
  `mtlBackend: initWindow OK`, and gracefully skips frames headless
  (nil-drawable guard) — no crash.
- QA failure mode: forcing blend off in a scratch build makes the identity
  compare fail on alpha edges, proving the gate catches blend differences.
