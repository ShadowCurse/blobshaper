#version 300 es
precision highp float;

in vec4 frag_color;
in vec3 frag_position;
in vec3 frag_normal;
in vec4 frag_light_space_position;

out vec4 final_color;

// raylib inputs
uniform vec4  colDiffuse;

uniform vec3      light_pos;
uniform sampler2D shadow_map;

float direct_shadow(vec4 light_space_position, vec3 normal, vec3 to_light) {
  vec3 projection = light_space_position.xyz / light_space_position.w;
  if (1.0 < projection.z)
    return 0.0;

  vec3 uv = projection * 0.5 + 0.5;

  float curr_depth = uv.z;
  float bias = max(0.0002 * (1.0 - dot(normal, to_light)), 0.00002) + 0.00001;

  float shadow = 0.0;
  vec2 texel_size = vec2(1.0) / vec2(textureSize(shadow_map, 0));
  for(int x = -1; x <= 1; ++x) {
    for(int y = -1; y <= 1; ++y) {
      float pcf_depth = texture(shadow_map, uv.xy + vec2(x, y) * texel_size).r;
      shadow += pcf_depth < curr_depth - bias ? 1.0 : 0.0;
    }
  }
  shadow /= 9.0;
  return shadow;
}

void main() {
  vec3 to_light = normalize(light_pos - frag_position);

  final_color = colDiffuse;
  final_color.xyz *= ((max(dot(frag_normal, to_light), 0.0) * 0.5) + 0.5);

  float in_shadow = direct_shadow(frag_light_space_position, frag_normal, to_light);
  final_color = mix(final_color, vec4(0, 0, 0, 1), in_shadow);
  final_color += 0.3 * colDiffuse;
}

