#include "raylib.h"
#include "box3d/box3d.h"

int main(void) {
  b3WorldDef world_def = b3DefaultWorldDef();
  b3WorldId world = b3CreateWorld(&world_def);

  InitWindow(1280, 720, "test");
  InitAudioDevice();
  while (!WindowShouldClose()) {
    BeginDrawing();
    ClearBackground(GREEN);
    DrawText("test", 10, 20, 32, RED);
    EndDrawing();
  }
  CloseAudioDevice();
  CloseWindow();

  b3DestroyWorld(world);
  return 0;
}
