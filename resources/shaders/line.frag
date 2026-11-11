#version 440

layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 frag_color;

void main() {
  // premultiplied alpha (blend: ONE, ONE_MINUS_SRC_ALPHA)
  frag_color = vec4(v_color.rgb * v_color.a, v_color.a);
}
