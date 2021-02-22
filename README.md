# Tadershoy

A quick and dirty program to display and hotload a fragment shader. (OpenGL/Xlib)

### Building

Compile the source (src/tadershoy.c) and link with X11 and GL

### Running

Run the program and provide a path to a shader file as an argument.
If the file does not exist, the program tries to create it.
The created file lists the relevant shader inputs and outputs.

`./tadershoy path/to/shader`

The program hotloads the shader file from the disk, enabling live-editing.
Additionally, the program will display an FPS counter, and possible GLSL compilation/linking errors as well.

### License

MIT
