#version 440

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_pos;
layout(location = 2) in vec2 v_uv;

layout(location = 0) out vec4 frag_color;

layout(std140, binding = 0) uniform buf {
  mat4 mvp;
  mat4 view;
  vec4 color;
  vec4 params;
};
layout(binding = 1) uniform sampler2D field;

void main() {
  vec4 t = texture(field, v_uv);
  if (t.a < 0.5) discard;  // no data (NaN)
  if (params.x < 0.5) {    // surfaces / wireframe: the field as it is
    frag_color = vec4(t.rgb, 1.0);
    return;
  }
  // shaded: the same two headlights as mesh.frag, softened so that the colours stay readable
  vec3 v = params.y > 0.5 ? normalize(-v_pos) : vec3(0.0, 0.0, 1.0);
  vec3 n = length(v_normal) > 1e-6 ? normalize(v_normal) : v;
  if (dot(n, v) < 0.0) n = -n;
  vec3 key = normalize(vec3(0.35, 0.45, 1.0));
  float light = 0.55 + 0.45 * max(dot(n, key), 0.0);
  frag_color = vec4(t.rgb * light, 1.0);
}
