#pragma once
// The handful of OpenGL 1.5+ entry points we use, loaded at startup.
// (Everything else comes from opengl32's GL 1.1 exports; ImGui loads its own.)
#include <gl/GL.h>
#include <cstddef>

// Windows ships only the OpenGL 1.1 header, so a few later names the
// renderer uses are defined here with their standard values.
typedef std::ptrdiff_t GLsizeiptr;
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif

typedef void (APIENTRY* PFN_GenBuffers)(GLsizei, GLuint*);
typedef void (APIENTRY* PFN_BindBuffer)(GLenum, GLuint);
typedef void (APIENTRY* PFN_BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (APIENTRY* PFN_DeleteBuffers)(GLsizei, const GLuint*);

extern PFN_GenBuffers pglGenBuffers;
extern PFN_BindBuffer pglBindBuffer;
extern PFN_BufferData pglBufferData;
extern PFN_DeleteBuffers pglDeleteBuffers;

bool loadGlProcs();
