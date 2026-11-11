#version 440

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec3 v_normal;  // view space
layout(location = 1) out vec3 v_pos;     // view space

layout(std140, binding = 0) uniform buf {
  mat4 mvp;     // clip-space correction * projection * view
  mat4 view;    // world -> view (rigid, so it also transforms normals)
  vec4 color;   // entity colour
  vec4 params;  // x: 1 = shaded, 0 = flat; y: 1 = perspective
};

void main() {
  v_normal = mat3(view) * normal;
  v_pos = (view * vec4(position, 1.0)).xyz;
  gl_Position = mvp * vec4(position, 1.0);
}
