#pragma once
// The handful of OpenGL 1.5+ entry points we use, loaded at startup.
// (Everything else comes from opengl32's GL 1.1 exports; ImGui loads its own.)
#include <gl/GL.h>

typedef void (APIENTRY* PFN_GenBuffers)(GLsizei, GLuint*);
typedef void (APIENTRY* PFN_BindBuffer)(GLenum, GLuint);
typedef void (APIENTRY* PFN_BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (APIENTRY* PFN_DeleteBuffers)(GLsizei, const GLuint*);

extern PFN_GenBuffers pglGenBuffers;
extern PFN_BindBuffer pglBindBuffer;
extern PFN_BufferData pglBufferData;
extern PFN_DeleteBuffers pglDeleteBuffers;

bool loadGlProcs();
