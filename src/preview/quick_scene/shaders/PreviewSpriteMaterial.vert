#version 440

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in float aOpacity;
layout(location = 3) in float aEffect;

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out float vOpacity;
layout(location = 2) out float vWave;
layout(location = 3) out float vAbsWave;
layout(location = 4) out float vEffect;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float wave;
    float absWave;
    float effect;
};

void main()
{
    vTexCoord = aTexCoord;
    vOpacity = aOpacity * qt_Opacity;
    vWave = wave;
    vAbsWave = absWave;
    vEffect = aEffect + effect * 0.0;
    gl_Position = qt_Matrix * vec4(aPosition, 0.0, 1.0);
}
