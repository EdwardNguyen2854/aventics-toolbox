// Compile the existing Electron toolbox through a narrow named-pipe shim.
// This keeps TOOLKIT/controller behavior unchanged while replacing only the
// blocking pipe calls that could stall Creo's UI thread.
#include "app/ElectronPipeShim.h"
#include "ElectronToolbox.cpp"
