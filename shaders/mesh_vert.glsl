#version 300 es
precision highp float;

// raylib inputs
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;

// out vec4 frag_color;
out vec3 frag_position;
out vec3 frag_normal;
out vec4 frag_light_space_position;

// raylib inputs
uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;

uniform mat4 light_vp;

void main() {
    frag_position = vec3(matModel * vec4(vertexPosition, 1.0));
    frag_normal   = normalize(transpose(inverse(mat3(matModel))) * vertexNormal);
    frag_light_space_position = light_vp * vec4(frag_position, 1.0);
    gl_Position = matProjection * matView * vec4(frag_position, 1.0);
}

