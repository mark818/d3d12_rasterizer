#include "Rasterizer.h"

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE hPrev, CHAR* cmd, int showCmd) {
  try {
    Rasterizer app(hinst);
    app.Initialize();
    app.Run();
    return 0;
  } catch (DxException &e) {
    MessageBoxW(nullptr, e.ToString().c_str(), L"HR FAILED", MB_OK);
  }
  return 0;
}