#include "app.h"
#include "util.h"

#include <SDL.h>

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  InstallCrashHandlers();

  App app;
  if (!app.Init()) {
    SDL_Log("Failed to initialize funsim");
    return 1;
  }

  app.Run();
  return 0;
}
