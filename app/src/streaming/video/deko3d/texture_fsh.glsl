#version 460

layout (location = 0) in vec2 vTextureCoord;
layout (location = 0) out vec4 outColor;

layout (binding = 0) uniform sampler2D plane0;
layout (binding = 1) uniform sampler2D plane1;

layout (std140, binding = 0) uniform Transformation
{
    mat3 yuvmat;
    vec3 offset;
    vec4 uv_data;
} u;

void main()
{
    // Sample YUV from the two planes
    float Y = texture2D(plane0, vTextureCoord).r;
    float U = texture2D(plane1, vTextureCoord).r;
    float V = texture2D(plane1, vTextureCoord).g;
    
    // Create YCbCr vector and apply offset
    vec3 YCbCr = vec3(Y, U, V) - u.offset;
    
    // Use proper color space matrix from CPU for BT.601/BT.709/BT.2020 support
    vec3 rgb = u.yuvmat * YCbCr;
    
    // Clamp to valid range and output
    outColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
