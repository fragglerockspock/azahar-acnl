import Foundation
import Metal

precondition(CommandLine.arguments.count == 3)
guard let device = MTLCreateSystemDefaultDevice(), let queue = device.makeCommandQueue() else {
    fatalError("Metal device required for Leaf alpha pixel test")
}
let options = MTLCompileOptions()
options.languageVersion = .version2_1
let vertexLibrary = try device.makeLibrary(source: String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8), options: options)
let fragmentLibrary = try device.makeLibrary(source: String(contentsOfFile: CommandLine.arguments[2], encoding: .utf8), options: options)
guard let vertexName = vertexLibrary.functionNames.first(where: { vertexLibrary.makeFunction(name: $0)?.functionType == .vertex }),
      let fragmentName = fragmentLibrary.functionNames.first(where: { fragmentLibrary.makeFunction(name: $0)?.functionType == .fragment }) else {
    fatalError("Leaf Metal entry points missing")
}
let descriptor = MTLRenderPipelineDescriptor()
descriptor.vertexFunction = vertexLibrary.makeFunction(name: vertexName)
descriptor.fragmentFunction = fragmentLibrary.makeFunction(name: fragmentName)
let vertex = MTLVertexDescriptor()
vertex.attributes[0].format = .float2
vertex.attributes[0].offset = 0
vertex.attributes[0].bufferIndex = 1
vertex.attributes[1].format = .float2
vertex.attributes[1].offset = 8
vertex.attributes[1].bufferIndex = 1
vertex.layouts[1].stride = 16
descriptor.vertexDescriptor = vertex
let color = descriptor.colorAttachments[0]!
color.pixelFormat = .bgra8Unorm
color.isBlendingEnabled = true
color.sourceRGBBlendFactor = .sourceAlpha
color.destinationRGBBlendFactor = .oneMinusSourceAlpha
color.sourceAlphaBlendFactor = .one
color.destinationAlphaBlendFactor = .oneMinusSourceAlpha
let pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
let sourceDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: 2, height: 2, mipmapped: false)
sourceDescriptor.storageMode = .shared
sourceDescriptor.usage = .shaderRead
let source = device.makeTexture(descriptor: sourceDescriptor)!
let texels: [UInt8] = [255,0,255,255, 0,255,0,255, 255,0,255,255, 0,255,0,255]
texels.withUnsafeBytes { source.replace(region: MTLRegionMake2D(0,0,2,2), mipmapLevel: 0, withBytes: $0.baseAddress!, bytesPerRow: 8) }
let samplerDescriptor = MTLSamplerDescriptor()
samplerDescriptor.minFilter = .nearest
samplerDescriptor.magFilter = .nearest
samplerDescriptor.sAddressMode = .clampToEdge
samplerDescriptor.tAddressMode = .clampToEdge
let sampler = device.makeSamplerState(descriptor: samplerDescriptor)!
let vertices: [Float] = [-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1]
let vertexBuffer = vertices.withUnsafeBytes { device.makeBuffer(bytes: $0.baseAddress!, length: $0.count, options: .storageModeShared)! }
func render(keyed: Bool, preservePink: Bool = false) -> [UInt8] {
    let targetDescriptor = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .bgra8Unorm, width: 8, height: 4, mipmapped: false)
    targetDescriptor.storageMode = .shared
    targetDescriptor.usage = .renderTarget
    let target = device.makeTexture(descriptor: targetDescriptor)!
    let pass = MTLRenderPassDescriptor()
    pass.colorAttachments[0].texture = target
    pass.colorAttachments[0].loadAction = .clear
    pass.colorAttachments[0].storeAction = .store
    pass.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 1, alpha: 1)
    var uniform = Data(count: 112)
    let matrix: [Float] = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    matrix.withUnsafeBytes { uniform.replaceSubrange(0..<64, with: $0) }
    let ids: [Int32] = [keyed ? 2 : 0, keyed ? (preservePink ? -2 : -1) : 0, 0, 0]
    ids.withUnsafeBytes { uniform.replaceSubrange(96..<112, with: $0) }
    let command = queue.makeCommandBuffer()!
    let encoder = command.makeRenderCommandEncoder(descriptor: pass)!
    encoder.setRenderPipelineState(pipeline)
    encoder.setVertexBuffer(vertexBuffer, offset: 0, index: 1)
    uniform.withUnsafeBytes {
        encoder.setVertexBytes($0.baseAddress!, length: $0.count, index: 0)
        encoder.setFragmentBytes($0.baseAddress!, length: $0.count, index: 0)
    }
    encoder.setFragmentTextures([source,source,source], range: 0..<3)
    encoder.setFragmentSamplerStates([sampler,sampler,sampler], range: 0..<3)
    encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
    encoder.endEncoding()
    command.commit()
    command.waitUntilCompleted()
    precondition(command.status == .completed, "Leaf Metal draw failed: \(String(describing: command.error))")
    var bytes = [UInt8](repeating: 0, count: 8*4*4)
    bytes.withUnsafeMutableBytes { target.getBytes($0.baseAddress!, bytesPerRow: 32, from: MTLRegionMake2D(0,0,8,4), mipmapLevel: 0) }
    return bytes
}
let normal = render(keyed: false)
let keyed = render(keyed: true)
precondition(normal[0] == 255 && normal[1] == 0 && normal[2] == 255, "Main-screen magenta must remain opaque")
precondition(keyed[0] == 255 && keyed[1] == 0 && keyed[2] == 0, "UI key must reveal blue scene")
let green = 7*4
precondition(keyed[green] == 0 && keyed[green+1] == 255 && keyed[green+2] == 0, "Opaque UI must remain visible")
let edge = 3*4
precondition(keyed[edge] > 0 && keyed[edge+1] > 0 && keyed[edge+2] == 0, "Filtered edge must blend without magenta")
let pinkKeyed = render(keyed: true, preservePink: true)
precondition(pinkKeyed[0] == 255 && pinkKeyed[1] == 0 && pinkKeyed[2] == 0, "Pink tab key must remain transparent")
precondition(pinkKeyed[green] == 0 && pinkKeyed[green+1] == 255 && pinkKeyed[green+2] == 0, "Pink mode must preserve non-key foreground")
let pinkTexels: [UInt8] = [242,77,166,255, 242,77,166,255, 242,77,166,255, 242,77,166,255]
pinkTexels.withUnsafeBytes { source.replace(region: MTLRegionMake2D(0,0,2,2), mipmapLevel: 0, withBytes: $0.baseAddress!, bytesPerRow: 8) }
let pink = render(keyed: true, preservePink: true)
precondition(abs(Int(pink[0])-166)<=1 && abs(Int(pink[1])-77)<=1 && abs(Int(pink[2])-242)<=1, "Pink foreground must remain opaque and retain RGB")
print("Leaf Metal pixel tests passed: main screen unchanged, UI cutout, opaque UI, filtered edge and pink foreground")
