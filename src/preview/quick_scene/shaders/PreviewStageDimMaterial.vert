#version 440

layout(location = 0) in vec2 aPosition;

layout(location = 0) out vec2 vPosition;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float outerDarkAlpha;
    float innerDarkAlpha;
    float smoothBrightness;
    vec4 stageRect;
    vec4 geometryParams;
};

void main()
{
    vPosition = aPosition;
    gl_Position = qt_Matrix * vec4(aPosition, 0.0, 1.0);
}
