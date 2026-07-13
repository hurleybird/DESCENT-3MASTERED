#version 460

layout(location = 0) out vec4 out_color;

void main()
{
    // Pipeline colorWriteMask selects alpha only; RGB is attachment LOAD.
    out_color = vec4(0.0);
}
