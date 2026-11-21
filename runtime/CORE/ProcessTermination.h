#pragma once
namespace w2v2 {
// Set by our user DllMain before CRT static destructors run. Only whole-process
// termination abandons COM objects: graphics-provider global state may be gone.
// Dynamic FreeLibrary, Reset, eviction and explicit clear retain normal cleanup.
inline bool processTerminating=false;
inline void ProcessDetach(bool terminating) noexcept {processTerminating=terminating;}
inline bool MayReleaseGraphics() noexcept {return !processTerminating;}
}
