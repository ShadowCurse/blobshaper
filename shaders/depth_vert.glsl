#version 300 es
precision highp float;

in vec3 vertexPosition;

uniform mat4 mvp;

void main() {
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
