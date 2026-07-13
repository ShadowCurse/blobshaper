#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "box3d/box3d.h"

#define u8  uint8_t
#define u16 uint16_t
#define u32 uint32_t
#define u64 uint64_t
#define i8  int8_t
#define i16 int16_t
#define i32 int32_t
#define i64 int64_t
#define f32 float

void camera_init_default(Camera* camera) {
  camera->position = (Vector3){0.0f, 8.0f, 5.0f};
  camera->target   = (Vector3){0.0f, 0.0f, 0.0f};
  camera->up       = (Vector3){0.0f, 1.0f, 0.0f};
  camera->fovy     = 90.0f;
  camera->projection = CAMERA_PERSPECTIVE;
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

#define DEPTH_TEXTURE_RESOLUTION 1024
RenderTexture2D create_depth_texture(int width, int height) {
    RenderTexture2D target = {0};

    target.id = rlLoadFramebuffer(); // Load an empty framebuffer
    target.texture.width = width;
    target.texture.height = height;

    if (target.id > 0) {
        rlEnableFramebuffer(target.id);

        // Create depth texture
        // NOTE: No need a color texture attachment for the shadowmap
        target.depth.id = rlLoadTextureDepth(width, height, false);
        target.depth.width = width;
        target.depth.height = height;
        target.depth.format = 19; // DEPTH_COMPONENT_24BIT?
        target.depth.mipmaps = 1;

        // Attach depth texture to FBO
        rlFramebufferAttach(target.id, target.depth.id, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_TEXTURE2D, 0);

        // Check if fbo is complete with attachments (valid)
        if (rlFramebufferComplete(target.id)) {
          TRACELOG(LOG_INFO, "FBO: [ID %i] Framebuffer object created successfully", target.id);
        }

        rlDisableFramebuffer();
    } else {
      TRACELOG(LOG_WARNING, "FBO: Framebuffer object can not be created");
    }

    return target;
}

void body_id_array_add(b3BodyId* array, i32 capacity, i32* count, b3BodyId item) {
  if (capacity == *count) {
    return;
  }
  array[*count] = item;
  *count += 1;
}

void body_id_array_remove(b3BodyId* array, i32* count, b3BodyId item) {
  for (i32 i = 0; i < *count; i += 1) {
    if (B3_ID_EQUALS(array[i], item)) {
      array[i] = array[*count - 1];
      *count -= 1;
      break;
    }
  }
}

typedef enum {
  GAME_MODE_GAME,
  GAME_MODE_EDITOR,
} GameMode;
GameMode game_mode   = GAME_MODE_GAME;
u8       game_paused = false;


typedef enum {
  PHYSICS_CATEGORY_PLAYER = 1 << 0,
  PHYSICS_CATEGORY_LEVEL  = 1 << 1,
  PHYSICS_CATEGORY_PEBBLE = 1 << 2,
} PhysicsCategory;

b3WorldId world_id;
Camera    free_camera;
Camera    game_camera;
Camera    light_camera;

f32       player_speed    = 8.0f;
f32       player_friction = 1.0f;
Model     player_model;
b3BodyId  player_body_id;
b3ShapeId player_sensor_id;
b3Vec3    player_aim;

#define MAX_GRAVITY_BODIES 16
b3BodyId player_gravity_bodies[MAX_GRAVITY_BODIES];
i32      player_gravity_body_count = 0;
f32      player_gravity_orbit_distance = 1.1f;

typedef struct {
  b3BodyId  body_id;
  b3ShapeId shape_id;
  Color     color;
} Pebble;
#define MAX_PEBBLES 16
Pebble pebbles[MAX_PEBBLES];
i32    pebble_count = 0;

Mesh  cube_mesh;
Model cube_model;

b3BodyId floor_body_id;

typedef struct {
  b3BodyId  body_id;
  b3ShapeId shape_id;
  Vector3   scale;
  Color     color;
} Wall;
#define MAX_WALLS 16
Wall walls[MAX_WALLS];
i32  wall_count = 0;

void pebble_spawn(b3Vec3 position, Color color) {
  assert(pebble_count < MAX_PEBBLES);

  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.type     = b3_dynamicBody;
  body_def.position = (b3Vec3){2.0f, 5.0f, 0.0f};
  b3BodyId body_id = b3CreateBody(world_id, &body_def);

  b3ShapeDef shape_def = b3DefaultShapeDef();
  shape_def.filter.categoryBits = PHYSICS_CATEGORY_PEBBLE;
  shape_def.filter.maskBits     = PHYSICS_CATEGORY_LEVEL | PHYSICS_CATEGORY_PEBBLE;
  // TODO figure out why this feels so heavy
  shape_def.density = 0.1f;
  shape_def.enableSensorEvents = true;
  b3Sphere sphere = {b3Vec3_zero, 0.25f};
  b3ShapeId shape_id = b3CreateSphereShape(body_id, &shape_def, &sphere);

  Pebble pebble = {body_id, shape_id, color};
  pebbles[pebble_count] = pebble;
  pebble_count += 1;
}

void pebble_draw(Pebble* pebble) {
  Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(pebble->body_id));
  b3Quat rotation = b3Body_GetRotation(pebble->body_id);
  f32 angle;
  Vector3 axis = vec3_b3v_to_rl(b3GetAxisAngle(&angle, rotation));
  DrawModelEx(player_model, position, axis, rad_to_degree(angle), (Vector3){0.5f, 0.5f, 0.5f}, pebble->color);

  Vector3 velocity = vec3_b3p_to_rl(b3Body_GetLinearVelocity(pebble->body_id));
  DrawLine3D(position, Vector3Add(position, velocity), LIME);
}

void wall_spawn(b3Vec3 position, Vector3 scale, Color color) {
  assert(wall_count < MAX_WALLS);

  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.type      = b3_staticBody;
  body_def.position  = position;
  b3BodyId body_id   = b3CreateBody(world_id, &body_def);

  b3BoxHull box        = b3MakeBoxHull(scale.x / 2.0f, scale.y / 2.0f, scale.z / 2.0f);
  b3ShapeDef shape_def = b3DefaultShapeDef();
  shape_def.filter.categoryBits = PHYSICS_CATEGORY_LEVEL;
  shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER | PHYSICS_CATEGORY_PEBBLE;
  b3ShapeId shape_id   = b3CreateHullShape(body_id , &shape_def, &box.base);

  Wall wall = {body_id, shape_id, scale, color};
  walls[wall_count] = wall;
  wall_count += 1;
}

void wall_draw(Wall* wall) {
  Vector3 rotation_axis = (Vector3){0.0f, 1.0f, 0.0f};
  Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(wall->body_id));
  Vector3 scale = wall->scale;
  DrawModelEx(cube_model, position, rotation_axis , 0.0f, scale, wall->color);
}

void draw_scene() {
  DrawCube((Vector3){1.0f, 0.5f, 0.0f}, 2.0f, 0.1f, 0.1f, RED);
  DrawCube((Vector3){0.0f, 1.5f, 0.0f}, 0.1f, 2.0f, 0.1f, GREEN);
  DrawCube((Vector3){0.0f, 0.5f, 1.0f}, 0.1f, 0.1f, 2.0f, BLUE);

  // player
  {
    Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(player_body_id));
    b3Quat rotation = b3Body_GetRotation(player_body_id);
    f32 angle;
    Vector3 axis = vec3_b3v_to_rl(b3GetAxisAngle(&angle, rotation));
    DrawModelEx(player_model, position, axis, rad_to_degree(angle), (Vector3){1.0f, 1.0f, 1.0f}, GOLD);
    DrawModelWiresEx(player_model, position, axis, rad_to_degree(angle), (Vector3){4.0f, 4.0f, 4.0f}, MAGENTA);
  }

  for (i32 i = 0; i < pebble_count; i += 1) {
    pebble_draw(pebbles + i);
  }

  // level
  Vector3 floor_rotation_axis = (Vector3){0.0f, 1.0f, 0.0f};
  Vector3 floor_position = vec3_b3p_to_rl(b3Body_GetPosition(floor_body_id));
  Vector3 floor_scale = (Vector3){40.0f, 1.0f, 40.0f};
  DrawModelEx(cube_model, floor_position, floor_rotation_axis , 0.0f, floor_scale, GRAY);

  for (i32 i = 0; i < wall_count; i += 1) {
    wall_draw(walls + i);
  }

  DrawSphere(vec3_b3v_to_rl(player_aim), 0.2, BLUE);
}

int main(void) {
  InitWindow(1280, 720, "test");
  InitAudioDevice();
  SetTargetFPS(60);

  camera_init_default(&free_camera);
  camera_init_default(&game_camera);
  light_camera.position = (Vector3){-3.0f, 8.0f, 3.0f};
  light_camera.target   = (Vector3){0.0f, 0.0f,  0.0f};
  light_camera.up       = (Vector3){0.0f, 1.0f,  0.0f};
  light_camera.fovy     = 20.0f;
  light_camera.projection = CAMERA_ORTHOGRAPHIC;

  Shader mesh_shader = LoadShader(
      "shaders/mesh_vert.glsl",
      "shaders/mesh_frag.glsl"
  );
  i32 ligth_pos_loc = GetShaderLocation(mesh_shader, "light_pos");
  SetShaderValue(mesh_shader, ligth_pos_loc, &light_camera.position, SHADER_UNIFORM_VEC3);
  i32 shadow_map_loc = GetShaderLocation(mesh_shader, "shadow_map");
  i32 light_vp_loc   = GetShaderLocation(mesh_shader, "light_vp");

  Shader depth_shader = LoadShader(
      "shaders/depth_vert.glsl",
      "shaders/depth_frag.glsl"
  );
  RenderTexture2D depth_texture = create_depth_texture(DEPTH_TEXTURE_RESOLUTION, DEPTH_TEXTURE_RESOLUTION);

  b3WorldDef world_def = b3DefaultWorldDef();
  world_def.gravity = (b3Vec3){0.0f, -10.0f, 0.0f};
  world_id = b3CreateWorld(&world_def);

  // player
  Mesh  player_mesh = GenMeshSphere(0.5f, 32, 32);
  player_model = LoadModelFromMesh(player_mesh);
  player_model.materials[0].shader = mesh_shader;
  {
    b3BodyDef player_body_def = b3DefaultBodyDef();
    player_body_def.type     = b3_kinematicBody;
    player_body_def.position = (b3Vec3){0.0f, 3.0f, 0.0f};
    player_body_id = b3CreateBody(world_id, &player_body_def);

    {
      b3ShapeDef shape_def = b3DefaultShapeDef();
      shape_def.density = 1.0f;
      shape_def.baseMaterial.friction = 0.3f;
      b3Sphere sphere = {b3Vec3_zero, 0.5f};
      b3CreateSphereShape(player_body_id, &shape_def, &sphere);
    }

    {
      b3ShapeDef shape_def = b3DefaultShapeDef();
      shape_def.density = 0.0f;
      shape_def.isSensor = true;
      shape_def.enableSensorEvents = true;
      b3Sphere sphere = {b3Vec3_zero, 2.0f};
      player_sensor_id = b3CreateSphereShape(player_body_id, &shape_def, &sphere);
    }
  }

  // pebble
  pebble_spawn((b3Vec3){2.0f, 5.0f, 0.0f}, MAROON);
  pebble_spawn((b3Vec3){2.0f, 5.0f, 0.0f}, PURPLE);
  pebble_spawn((b3Vec3){2.0f, 5.0f, 0.0f}, DARKBROWN);

  // level
  cube_mesh  = GenMeshCube(1.0f, 1.0f, 1.0f);
  cube_model = LoadModelFromMesh(cube_mesh);
  cube_model.materials[0].shader = mesh_shader;
  {
    b3BodyDef body_def = b3DefaultBodyDef();
    body_def.type     = b3_staticBody;
    body_def.position = (b3Vec3){0.0f, 0.0f, 0.0f};
    floor_body_id = b3CreateBody(world_id, &body_def);

    b3BoxHull box  = b3MakeBoxHull(20.0f, 0.5f, 20.0f);
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.filter.categoryBits = PHYSICS_CATEGORY_LEVEL;
    shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER | PHYSICS_CATEGORY_PEBBLE;
    b3CreateHullShape(floor_body_id, &shape_def, &box.base);
  }
  wall_spawn((b3Vec3){0.0f,  5.0f, -20.0f}, (Vector3){40.0f, 10.0f,  1.0f}, BROWN);
  wall_spawn((b3Vec3){20.0f, 1.0f,   0.0f}, (Vector3){1.0f,   1.0f, 40.0f}, BROWN);
  wall_spawn((b3Vec3){0.0f,  1.0f,  20.0f}, (Vector3){40.0f,  1.0f,  1.0f}, BROWN);

  while (!WindowShouldClose()) {
    f32 dt = GetFrameTime();

    if (!game_paused) b3World_Step(world_id, dt, 4);

    if (IsKeyPressed(KEY_F1)) {
      game_mode = GAME_MODE_EDITOR;
    }
    if (IsKeyPressed(KEY_F2)) {
      game_mode = GAME_MODE_GAME;
      EnableCursor();
    }
    if (IsKeyPressed(KEY_F5)) {
      game_paused = !game_paused;
    }

    if (!game_paused) {
      b3SensorEvents events = b3World_GetSensorEvents(world_id);
      for (i32 i = 0; i < events.beginCount; i += 1) {
        b3SensorBeginTouchEvent* event = events.beginEvents + i;
        if (B3_ID_EQUALS(event->sensorShapeId, player_sensor_id)) {
          b3BodyId body_id = b3Shape_GetBody(event->visitorShapeId);
          body_id_array_add(player_gravity_bodies, MAX_GRAVITY_BODIES, &player_gravity_body_count, body_id);
        }
      }
      for (i32 i = 0; i < events.endCount; i += 1) {
        b3SensorEndTouchEvent* event = events.endEvents + i;
        if (B3_ID_EQUALS(event->sensorShapeId, player_sensor_id)) {
          b3BodyId body_id = b3Shape_GetBody(event->visitorShapeId);
          body_id_array_remove(player_gravity_bodies, &player_gravity_body_count, body_id);
        }
      }

      Vector3 camera_forward = Vector3Normalize(Vector3Subtract(game_camera.target,
                                                                game_camera.position));
      Vector3 player_right_rl   = Vector3CrossProduct(camera_forward, (Vector3){0.0f, 1.0f, 0.0f});
      Vector3 player_forward_rl = Vector3CrossProduct((Vector3){0.0f, 1.0f, 0.0f}, player_right_rl);
      b3Vec3  player_right      = vec3_rl_to_b3v(player_right_rl);
      b3Vec3  player_forward    = vec3_rl_to_b3v(player_forward_rl);

      b3Vec3 player_acceleration = {0};
      if (game_mode == GAME_MODE_GAME) {
        if (IsKeyDown(KEY_A)) {
          player_acceleration = b3Sub(player_acceleration, player_right);
        }
        if (IsKeyDown(KEY_D)) {
          player_acceleration = b3Add(player_acceleration, player_right);
        }
        if (IsKeyDown(KEY_W)) {
          player_acceleration = b3Add(player_acceleration, player_forward);
        }
        if (IsKeyDown(KEY_S)) {
          player_acceleration = b3Sub(player_acceleration, player_forward);
        }
        if (IsKeyDown(KEY_SPACE) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) {
          player_acceleration.y += 10.0f;
        }
        if (IsKeyPressed(KEY_Q)) {
          b3Shape_EnableSensorEvents(player_sensor_id, !b3Shape_AreSensorEventsEnabled(player_sensor_id));
        }

        f32 gamepad_x = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        player_acceleration = b3Add(player_acceleration, b3MulSV(gamepad_x, player_right));
        f32 gamepad_y = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        player_acceleration = b3Add(player_acceleration, b3MulSV(-gamepad_y, player_forward));
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
      filter.categoryBits = PHYSICS_CATEGORY_PLAYER;
      filter.maskBits     = PHYSICS_CATEGORY_LEVEL;
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

      for (i32 i = 0; i < player_gravity_body_count; i += 1) {
        // TODO maybe improve somehow
        b3BodyId body_id = player_gravity_bodies[i];
        b3Vec3 body_position = b3Body_GetPosition(body_id);
        b3Vec3 to_body       = b3Sub(body_position, player_position);
        f32 distance         = b3Length(to_body);
        to_body              = b3Normalize(to_body);
        b3Vec3 right         = b3Cross(to_body, (b3Vec3){0.0f, 1.0f, 0.0});
        b3Vec3 to_player     = b3Neg(to_body);

        f32 delta = distance - player_gravity_orbit_distance;
        delta = delta * delta;
        delta = delta * delta;
        delta = delta * delta;
        delta = delta * delta;

        b3Vec3 v = b3Add(b3MulSV(delta, to_player), b3MulSV(5.0f, right));
        b3Body_SetLinearVelocity(body_id, v);
      }
    }

    if (game_mode == GAME_MODE_GAME) {
      Vector3 player_position = vec3_b3p_to_rl(b3Body_GetPosition(player_body_id));
      game_camera.position = Vector3Add(player_position, (Vector3){-3.0f, 8.0f, 3.0f});
      game_camera.target = player_position;

      Ray to_mouse_ray = GetScreenToWorldRay(GetMousePosition(), game_camera);
      RayCollision collision = GetRayCollisionMesh(to_mouse_ray, cube_mesh, MatrixScale(40.0f, 1.0f, 40.0f));
      f32 closest = 100000000000.0f;
      if (collision.hit) {
        player_aim = vec3_rl_to_b3v(collision.point);
        closest = collision.distance;
      }
      for (i32 i = 0; i < wall_count; i += 1) {
        Wall* wall = walls + i;
        b3Vec3 position = b3Body_GetPosition(wall->body_id);
        Matrix mt = MatrixTranslate(position.x, position.y, position.z);
        Matrix ms = MatrixScale(wall->scale.x, wall->scale.y, wall->scale.z);
        Matrix m  = MatrixMultiply(ms, mt);
        collision = GetRayCollisionMesh(to_mouse_ray, cube_mesh, m);
        if (collision.hit) {
          if (collision.distance < closest) {
            closest = collision.distance;
            player_aim = vec3_rl_to_b3v(collision.point);
          }
        }
      }
    }
    if (game_mode == GAME_MODE_EDITOR) {
      if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        UpdateCamera(&free_camera, CAMERA_FREE);
        DisableCursor();
      }
      if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        EnableCursor();
      }
    }

    Camera camera;
    switch (game_mode) {
      case GAME_MODE_GAME:   camera = game_camera; break;
      case GAME_MODE_EDITOR: camera = free_camera; break;
    }

    Matrix light_view = {0};
    Matrix light_proj = {0};
    Matrix light_view_proj = {0};

    BeginTextureMode(depth_texture);
      BeginShaderMode(depth_shader);
        ClearBackground(WHITE);
        BeginMode3D(light_camera);
          light_view = rlGetMatrixModelview();
          light_proj = rlGetMatrixProjection();
          draw_scene();
        EndMode3D();
      EndShaderMode();
    EndTextureMode();

    light_view_proj = MatrixMultiply(light_view, light_proj);
    SetShaderValueMatrix(mesh_shader, light_vp_loc, light_view_proj);

    BeginDrawing();
      ClearBackground(BLACK);

      i32 texture_slot = 10;
      rlActiveTextureSlot(texture_slot);
      rlEnableTexture(depth_texture.depth.id);
      rlSetUniform(shadow_map_loc, &texture_slot, SHADER_UNIFORM_INT, 1);

      BeginMode3D(camera);
        draw_scene();
      EndMode3D();

      f32 gamepad_x = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
      f32 gamepad_y = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);

      i32 text_offset = 10;
      DrawText(TextFormat("controller: %s", IsGamepadAvailable(0) ? "on" : "off"), 10, text_offset, 32, RED);
      text_offset += 32;
      DrawText(TextFormat("controller: x: %f, y: %f", gamepad_x, gamepad_y), 10, text_offset, 32, RED);
      text_offset += 32;
      DrawText(TextFormat("paused: %s", game_paused ? "yes" : "no"), 10, text_offset, 32, RED);
      text_offset += 32;
      DrawText(
        TextFormat(
          "player sensor enabled: %s",
          b3Shape_AreSensorEventsEnabled(player_sensor_id) ? "yes" : "no"),
        10, text_offset, 32, RED);
      text_offset += 32;

    EndDrawing();
  }
  CloseAudioDevice();
  CloseWindow();

  b3DestroyWorld(world_id);
  return 0;
}
