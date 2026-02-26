#version 450 core
in vec2 TexCoord;
uniform sampler2D depthTex;
void main() {
    gl_FragDepth = texture(depthTex, TexCoord).r;
}