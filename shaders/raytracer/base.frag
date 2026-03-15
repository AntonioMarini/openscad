#version 450 core
in vec2 TexCoord;

uniform sampler2D screenTexture;

out vec4 FragColor;

uniform vec3 u_background;

void main() {
	vec4 col = texture(screenTexture, TexCoord);
	FragColor = vec4(mix(u_background, col.rgb, col.a), 1.0);
}