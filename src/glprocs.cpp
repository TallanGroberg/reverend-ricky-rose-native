#include <windows.h>
#include "glprocs.h"

PFN_GenBuffers pglGenBuffers = nullptr;
PFN_BindBuffer pglBindBuffer = nullptr;
PFN_BufferData pglBufferData = nullptr;
PFN_DeleteBuffers pglDeleteBuffers = nullptr;

bool loadGlProcs() {
  pglGenBuffers = (PFN_GenBuffers)wglGetProcAddress("glGenBuffers");
  pglBindBuffer = (PFN_BindBuffer)wglGetProcAddress("glBindBuffer");
  pglBufferData = (PFN_BufferData)wglGetProcAddress("glBufferData");
  pglDeleteBuffers = (PFN_DeleteBuffers)wglGetProcAddress("glDeleteBuffers");
  return pglGenBuffers && pglBindBuffer && pglBufferData && pglDeleteBuffers;
}
