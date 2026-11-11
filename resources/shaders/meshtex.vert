#version 440

// Faces with a result texture (fields / images on faces, docs/plugins.md P2).
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_pos;
layout(location = 2) out vec2 v_uv;

layout(std140, binding = 0) uniform buf {
  mat4 mvp;
  mat4 view;
  vec4 color;   // unused (texture colour)
  vec4 params;  // x: 1 = shaded, y: 1 = perspective
};

void main() {
  v_normal = mat3(view) * normal;
  v_pos = (view * vec4(position, 1.0)).xyz;
  v_uv = uv;
  gl_Position = mvp * vec4(position, 1.0);
}
