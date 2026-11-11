#version 440

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_pos;

layout(location = 0) out vec4 frag_color;

layout(std140, binding = 0) uniform buf {
  mat4 mvp;
  mat4 view;
  vec4 color;
  vec4 params;
};

void main() {
  if (params.x < 0.5) {  // "Surfaces": flat colour
    frag_color = vec4(color.rgb, 1.0);
    return;
  }
  // "Shaded": Blinn-Phong with two headlights (view space), back faces darker.
  vec3 v = params.y > 0.5 ? normalize(-v_pos) : vec3(0.0, 0.0, 1.0);
  vec3 n = length(v_normal) > 1e-6 ? normalize(v_normal) : v;
  float side = 1.0;
  if (dot(n, v) < 0.0) {
    n = -n;
    side = 0.55;
  }
  vec3 key = normalize(vec3(0.35, 0.45, 1.0));
  vec3 fill = normalize(vec3(-0.6, -0.3, 0.5));
  float diffuse = 0.22 + 0.62 * max(dot(n, key), 0.0) + 0.22 * max(dot(n, fill), 0.0);
  float spec = 0.28 * pow(max(dot(n, normalize(key + v)), 0.0), 48.0);
  frag_color = vec4((color.rgb * diffuse + vec3(spec)) * side, 1.0);
}
