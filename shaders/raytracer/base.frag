#version 450 core
in vec2 TexCoord;

uniform sampler2D screenTexture;

out vec4 FragColor;

void main() {
	vec4 col = vec4(texture(screenTexture, TexCoord).rgb, 1.0);
	FragColor = col;
}