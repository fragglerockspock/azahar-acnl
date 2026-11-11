import Foundation
import Metal

precondition(CommandLine.arguments.count == 3)
if let device = MTLCreateSystemDefaultDevice() {
    let options = MTLCompileOptions()
    options.languageVersion = .version2_1
    let vertexSource = try String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8)
    let fragmentSource = try String(contentsOfFile: CommandLine.arguments[2], encoding: .utf8)
    let vertexLibrary = try device.makeLibrary(source: vertexSource, options: options)
    let fragmentLibrary = try device.makeLibrary(source: fragmentSource, options: options)
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
    vertex.layouts[1].stepFunction = .perVertex
    descriptor.vertexDescriptor = vertex
    let color = descriptor.colorAttachments[0]!
    color.pixelFormat = .bgra8Unorm
    color.isBlendingEnabled = true
    color.sourceRGBBlendFactor = .sourceAlpha
    color.destinationRGBBlendFactor = .oneMinusSourceAlpha
    color.sourceAlphaBlendFactor = .one
    color.destinationAlphaBlendFactor = .oneMinusSourceAlpha
    _ = try device.makeRenderPipelineState(descriptor: descriptor)
    print("Leaf Metal library and render-pipeline smoke test passed")
} else {
    print("Metal device unavailable; runtime pipeline smoke test skipped")
}
