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
    float r, g, b, yt, ut, vt;
    
    yt = texture2D(plane0, vTextureCoord).r;
    ut = texture2D(plane1, vTextureCoord).r - 0.5;
    vt = texture2D(plane1, vTextureCoord).g - 0.5;

    r = yt + 1.13983*vt;
    g = yt - 0.39465*ut - 0.58060*vt;
    b = yt + 2.03211*ut;

    outColor = vec4(r, g, b, 1.0);
}
