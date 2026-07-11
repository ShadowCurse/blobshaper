#include <stdint.h>
#include <stdio.h>

#include "raylib.h"
#include "raymath.h"
#include "box3d/box3d.h"

#define u8  uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t
#define f32 float

void camera_init_default(Camera* camera) {
  camera->position = (Vector3){-0.0f, 8.0f, 5.0f};
  camera->target   = (Vector3){0.0f, 0.0f,  0.0f};
  camera->up       = (Vector3){0.0f, 1.0f,  0.0f};
  camera->fovy     = 90.0f;
  camera->projection = CAMERA_PERSPECTIVE;
}

void draw_level() {
  // DrawGrid(40, 1.0f);
  DrawPlane((Vector3){0.0f, 0.1f,   0.0f}, (Vector2){40.0f, 40.0f}, DARKGRAY);
  DrawCube((Vector3){ 0.0f, 0.5f, -10.0f}, 20.f, 1.0f, 1.0, BROWN);
  DrawCube((Vector3){10.0f, 0.5f,   0.0f}, 1.0f, 1.0f, 20.0, BROWN);
}

b3Vec3 vec3_rl_to_b3v(Vector3 v) {
  b3Vec3 result;
  result.x = v.x;
  result.y = v.y;
  result.z = v.z;
  return result;
}

Vector3 vec3_b3v_to_rl(b3Vec3 v) {
  Vector3 result;
  result.x = v.x;
  result.y = v.y;
  result.z = v.z;
  return result;
}

Vector3 vec3_b3p_to_rl(b3Pos v) {
  Vector3 result;
  result.x = v.x;
  result.y = v.y;
  result.z = v.z;
  return result;
}

f32 rad_to_degree(f32 rad) {
  return rad / PI * 360.0;
}

#define MAX_PLANES 16
b3CollisionPlane collision_planes[MAX_PLANES];
int collision_plane_count = 0;

bool plane_result_fn(b3ShapeId shape, const b3PlaneResult* results, int plane_count, void* context) {
  for (int i = 0; i < plane_count && collision_plane_count < MAX_PLANES; ++i) {
    collision_planes[collision_plane_count] = (b3CollisionPlane){results[i].plane, FLT_MAX, 0.0f, true};
    collision_plane_count += 1;
  }
  return true;
}

int main(void) {
  InitWindow(1280, 720, "test");
  InitAudioDevice();

  b3WorldDef world_def = b3DefaultWorldDef();
  world_def.gravity = (b3Vec3){0.0f, -10.0f, 0.0f};
  b3WorldId world_id = b3CreateWorld(&world_def);

  Camera free_camera;
  Camera game_camera;
  camera_init_default(&free_camera);
  camera_init_default(&game_camera);

  // plyer
  f32   player_speed    = 8.0f;
  f32   player_friction = 1.0f;
  Mesh  player_mesh  = GenMeshSphere(0.5, 32, 32);
  Model player_model = LoadModelFromMesh(player_mesh);

  b3BodyId player_body_id;
  {
    b3BodyDef player_body_def = b3DefaultBodyDef();
    player_body_def.type     = b3_kinematicBody;//b3_dynamicBody
    player_body_def.position = (b3Vec3){0.0f, 3.0f, 0.0f};
    player_body_def.rotation = b3MakeQuatFromAxisAngle((b3Vec3){1.0f, 0.0, 0.0}, 1.1);
    player_body_id = b3CreateBody(world_id, &player_body_def);

    b3BoxHull player_box =  b3MakeCubeHull(0.5f);// b3MakeBoxHull(0.5f, 0.5f, 0.5f);
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.density = 1.0f;
    shape_def.baseMaterial.friction = 0.3f;
    b3CreateHullShape(player_body_id, &shape_def, &player_box.base);
  }

  // level
  b3BodyId floor_body_id;
  b3BodyId wall_1_body_id;
  b3BodyId wall_2_body_id;
  {
    b3BodyDef body_def = b3DefaultBodyDef();
    body_def.type     = b3_staticBody;
    body_def.position = (b3Vec3){0.0f, 0.0f, 0.0f};
    floor_body_id = b3CreateBody(world_id, &body_def);

    b3BoxHull box  = b3MakeBoxHull(20.0f, 0.5f, 20.0f);
    b3ShapeDef shape_def = b3DefaultShapeDef();
    b3CreateHullShape(floor_body_id, &shape_def, &box.base);
  }
  {
    b3BodyDef body_def = b3DefaultBodyDef();
    body_def.type     = b3_staticBody;
    body_def.position = (b3Vec3){0.0f, 1.0f, -20.0f};
    wall_1_body_id = b3CreateBody(world_id, &body_def);

    b3BoxHull box  = b3MakeBoxHull(20.0f, 0.5f, 0.5f);
    b3ShapeDef shape_def = b3DefaultShapeDef();
    b3CreateHullShape(wall_1_body_id , &shape_def, &box.base);
  }
  {
    b3BodyDef body_def = b3DefaultBodyDef();
    body_def.type     = b3_staticBody;
    body_def.position = (b3Vec3){20.0f, 1.0f, 0.0f};
    wall_2_body_id = b3CreateBody(world_id, &body_def);

    b3BoxHull box  = b3MakeBoxHull(0.5f, 0.5f, 20.0f);
    b3ShapeDef shape_def = b3DefaultShapeDef();
    b3CreateHullShape(wall_2_body_id, &shape_def, &box.base);
  }

  CameraMode camera_mode = CAMERA_CUSTOM;

  while (!WindowShouldClose()) {
    f32 dt = GetFrameTime();

    b3World_Step(world_id, dt, 4);

    if (IsKeyPressed(KEY_F1)) {
      camera_mode = CAMERA_FREE;
    }
    if (IsKeyPressed(KEY_F2)) {
      camera_mode = CAMERA_CUSTOM;
    }

    if (camera_mode == CAMERA_CUSTOM) {
      b3Vec3 player_acceleration = {0};
      if (IsKeyDown(KEY_A)) {
        player_acceleration.x -= 1.0f;
      }
      if (IsKeyDown(KEY_D)) {
        player_acceleration.x += 1.0f;
      }
      if (IsKeyDown(KEY_W)) {
        player_acceleration.z += 1.0f;
      }
      if (IsKeyDown(KEY_S)) {
        player_acceleration.z -= 1.0f;
      }
      if (IsKeyDown(KEY_SPACE)) {
        player_acceleration.y += 10.0f;
      }

      player_acceleration = b3MulSV(player_speed, player_acceleration);
      b3Vec3 player_velocity = b3Body_GetLinearVelocity(player_body_id);
      player_acceleration = b3Sub(player_acceleration, b3MulSV(player_friction, player_velocity));
      player_velocity = b3Add(player_velocity, b3MulSV(dt, player_acceleration));
      player_velocity = b3Add(player_velocity, (b3Vec3){0.0, -10.0f * dt, 0.0});

      // physics/collision
      b3Vec3 player_position = b3Body_GetPosition(player_body_id);
      b3Vec3 player_translation = b3MulSV(dt, player_velocity);

      b3Capsule capsule = (b3Capsule){{0}, {0}, 0.5f};
      b3QueryFilter filter = b3DefaultQueryFilter();
      collision_plane_count = 0;

      float fraction = b3World_CastMover(world_id,
                                         player_position,
                                         &capsule,
                                         player_translation,
                                         filter,
                                         NULL,
                                         NULL);
      b3Vec3 safe_delta = b3MulSV(fraction, player_translation);
      capsule.center1 = b3Add(capsule.center1, safe_delta);
      capsule.center2 = b3Add(capsule.center2, safe_delta);

      b3World_CollideMover(world_id, player_position, &capsule, filter, plane_result_fn, NULL);
      b3PlaneSolverResult result = b3SolvePlanes(b3Vec3_zero, collision_planes, collision_plane_count);
      player_velocity = b3ClipVector(player_velocity, collision_planes, collision_plane_count);

      b3Body_SetLinearVelocity(player_body_id, player_velocity);
    }

    {
      Vector3 player_position = vec3_b3p_to_rl(b3Body_GetPosition(player_body_id));
      game_camera.position = Vector3Add(player_position, (Vector3){-3.0f, 8.0f, 3.0f});
      game_camera.target = player_position;
      UpdateCamera(&free_camera, camera_mode);
    }

    BeginDrawing();
    ClearBackground(BLACK);

      if (camera_mode == CAMERA_CUSTOM) {
        BeginMode3D(game_camera);
      } else {
        BeginMode3D(free_camera);
      }

        DrawCube((Vector3){1.0f, 0.5f, 0.0f}, 2.0f, 0.1f, 0.1f, RED);
        DrawCube((Vector3){0.0f, 1.5f, 0.0f}, 0.1f, 2.0f, 0.1f, GREEN);
        DrawCube((Vector3){0.0f, 0.5f, 1.0f}, 0.1f, 0.1f, 2.0f, BLUE);

        // player
        Vector3 player_position = vec3_b3p_to_rl(b3Body_GetPosition(player_body_id));
        b3Quat rotation = b3Body_GetRotation(player_body_id);
        f32 angle;
        Vector3 axis = vec3_b3v_to_rl(b3GetAxisAngle(&angle, rotation));
        DrawModelEx(player_model, player_position, axis, rad_to_degree(angle), (Vector3){1.0f, 1.0f, 1.0f}, GOLD);

        Vector3 floor_position = vec3_b3p_to_rl(b3Body_GetPosition(floor_body_id));
        DrawCube(floor_position, 40.0f, 1.0f, 40.0f, GRAY);

        Vector3 wall_1_position = vec3_b3p_to_rl(b3Body_GetPosition(wall_1_body_id));
        DrawCube(wall_1_position, 40.0f, 1.0f, 1.0f, BROWN);

        Vector3 wall_2_position = vec3_b3p_to_rl(b3Body_GetPosition(wall_2_body_id));
        DrawCube(wall_2_position, 1.0f, 1.0f, 40.0f, BROWN);

        // draw_level();
      EndMode3D();

    DrawText(TextFormat("player_pos: x: %f y: %f z: %f", player_position.x, player_position.y, player_position.z), 10, 10, 32, RED);
    DrawText(TextFormat("player_rotaiton: axis: x: %f y: %f z: %f angle: %f", axis.x, axis.y, axis.z, angle), 10, 42, 32, RED);
    DrawText(TextFormat("controller: %s", IsGamepadAvailable(0) ? "on" : "off"), 10, 72, 32, RED);

    EndDrawing();
  }
  CloseAudioDevice();
  CloseWindow();

  b3DestroyWorld(world_id);
  return 0;
}
