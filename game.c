#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/mman.h>

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"

#include "rlImGui.h"
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

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

#define ARRAY_LEN(array) sizeof(array) / sizeof(array[0])

#define FixedArrayImpl(T, N) typedef struct { \
  T items[N];                                 \
  u32  count;                                 \
} FixedArray_##T;

#define FixedArray(T) FixedArray_##T
#define fixed_array_item_index(array, item) ((u64)item - (u64)(array)->items) / sizeof((array)->items[0])
#define fixed_array_last_item(array) &(array)->items[(array)->count - 1]

#define fixed_array_add(array, item)         \
  i32 array_len = ARRAY_LEN((array)->items); \
  assert((array)->count < array_len);        \
  (array)->items[(array)->count] = item;     \
  (array)->count += 1;

#define fixed_array_remove(array, item)                            \
  i32 array_len = ARRAY_LEN((array)->items);                       \
  u64 item_index = fixed_array_item_index((array), item);          \
  assert(item_index < array_len);                                  \
  (array)->items[item_index] = (array)->items[(array)->count - 1]; \
  (array)->count -= 1;

#define fixed_array_remove_index(array, item_index )               \
  i32 array_len = ARRAY_LEN((array)->items);                       \
  assert(item_index < array_len);                                  \
  (array)->items[item_index] = (array)->items[(array)->count - 1]; \
  (array)->count -= 1;

#define fixed_array_remove_body_id(array, body_id)            \
  for (i32 i = 0; i < (array)->count; i += 1) {               \
    if (B3_ID_EQUALS((array)->items[i], body_id)) {           \
      (array)->items[i] = (array)->items[(array)->count - 1]; \
      (array)->count -= 1;                                    \
      break;                                                  \
    }                                                         \
  }

#define fixed_array_clear(array) (array)->count = 0;

#define MAX_PLANES 16
FixedArrayImpl(b3CollisionPlane, MAX_PLANES);

bool player_collider_result_fn(b3ShapeId shape, const b3PlaneResult* results, int plane_count, void* context) {
  FixedArray(b3CollisionPlane)* array = (FixedArray(b3CollisionPlane)*)context;
  for (int i = 0; i < plane_count && array->count < MAX_PLANES; ++i) {
    b3CollisionPlane plane = {results[i].plane, FLT_MAX, 0.0f, true};
    fixed_array_add(array, plane);
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

typedef enum {
  GAME_MODE_GAME,
  GAME_MODE_EDITOR,
} GameMode;
GameMode game_mode          = GAME_MODE_GAME;
bool     game_show_debug_ui = true;
ImGuiIO* imgui_io;
u8       game_paused        = false;
u32      game_slow_mode     = 1;
Camera   free_camera;
b3Vec3   player_resolved_positon;

bool imgui_wants_to_handle_events() {
  return imgui_io->WantCaptureMouse    ||
         imgui_io->WantCaptureKeyboard ||
         imgui_io->WantTextInput;
}

#define IMGUI_DRAG_FLOAT(var, min, max) \
  igDragFloat(#var, &var, 1.0f, min, max, NULL, 0);

#define IMGUI_DRAG_FLOAT_001(var, min, max) \
  igDragFloat(#var, &var, 0.001f, min, max, NULL, 0);

#define IMGUI_EDIT_COLOR(c)             \
  f32 color[4] = {(f32)(c).r / 255.0f,  \
                  (f32)(c).g / 255.0f,  \
                  (f32)(c).b / 255.0f,  \
                  (f32)(c).a / 255.0f}; \
  igColorEdit4("color", color, 0);          \
  (c).r = color[0] * 255.0f;            \
  (c).g = color[1] * 255.0f;            \
  (c).b = color[2] * 255.0f;            \
  (c).a = color[3] * 255.0f;


bool input_slam_key_pressed;
bool input_gravity_key_pressed;
bool input_gravity_shoot_key_pressed;
bool input_dash_key_pressed;
bool input_forward;
bool input_back;
bool input_left;
bool input_right;
f32  input_gamepad_axis_x;
f32  input_gamepad_axis_y;

typedef enum : u8 {
  OBJECT_TYPE_NONE,
  OBJECT_TYPE_PLAYER,
  OBJECT_TYPE_PLAYER_SPAWN_POINT,
  OBJECT_TYPE_WALL,
  OBJECT_TYPE_PEBBLE,
  OBJECT_TYPE_ENEMY,
  OBJECT_TYPE_TURRET,
  OBJECT_TYPE_BUTTON,
  OBJECT_TYPE_KEY,
  OBJECT_TYPE_GATE,
} ObjectType;

typedef struct {
  b3BodyId   body_id;
  b3ShapeId  shape_id;
  b3ShapeId  sensor_shape_id;
  b3BodyId   ground_id;
  b3JointId  joint_id;
  Vector3    rotation;
  Vector3    scale;
  Color      color;
  i32        hp;
  i32        damage;
  f32        attack_range;
  f32        attack_speed;
  f32        attack_timer;
  ObjectType type;
  u32        id;
  u32        activates_id;
  bool       disabled;
} Object;

Object* selected_object;

#define MAX_OBJECTS 64
FixedArrayImpl(Object, MAX_OBJECTS);
FixedArray(Object) objects;

void object_destroy(Object* o) {
  b3DestroyBody(o->body_id);
  fixed_array_remove(&objects, o);
  if (selected_object == o) {
    selected_object = NULL;
  }
}

void object_take_damage(Object* o, i32 damage) {
  printf("object id: %d takes %d damage\n", o->id, damage);
  o->hp -= damage;
  if (o->hp <= 0) {
    object_destroy(o);
  }
}

typedef enum : u32 {
  PHYSICS_CATEGORY_PLAYER               = 1 << 0,
  PHYSICS_CATEGORY_PLAYER_SPAWN_POINT   = 1 << 1,
  PHYSICS_CATEGORY_PLAYER_GRAVITY_FIELD = 1 << 2,
  PHYSICS_CATEGORY_PLAYER_SLAM_FIELD    = 1 << 3,
  PHYSICS_CATEGORY_LEVEL                = 1 << 4,
  PHYSICS_CATEGORY_PEBBLE               = 1 << 5,
  PHYSICS_CATEGORY_ENEMY                = 1 << 6,
  PHYSICS_CATEGORY_TURRET               = 1 << 7,
  PHYSICS_CATEGORY_BUTTON               = 1 << 8,
  PHYSICS_CATEGORY_KEY                  = 1 << 9,
  PHYSICS_CATEGORY_GATE                 = 1 << 10,
  PHYSICS_CATEGORY_GATE_SENSOR          = 1 << 11,
} PhysicsCategory;

b3WorldId world_id;
Camera    game_camera;
Camera    light_camera;

Shader mesh_shader;
Shader depth_shader;

Sound sound_dash;
Sound sound_jump;
Sound sound_pebble_impact;
Sound sound_pebble_throw;
Sound sound_slam;

f32       player_speed    = 250.0f;
f32       player_friction = 20.0f;
f32       player_gravity_shoot_strength = 100.0f;

f32       player_dash_legth = 5.0f;
f32       player_dash_time  = 0.1f;
bool      player_in_dash_mode = false;
f32       player_dash_dt      = 0.0f;
i32       player_dash_damage  = 10;
b3Vec3    player_dash_target;

f32       player_slam_up_time   = 0.2f;
f32       player_slam_hold_time = 0.3f;
f32       player_slam_down_time = 0.1f;
f32       player_slam_height    = 2.0f;
i32       player_slam_damage    = 5;
f32       player_slam_radius    = 4.0f;

bool      player_in_slam_mode   = false;
f32       player_slam_accumulate_time = 0.0f;
bool      player_slam_finished  = false;

b3BodyId  player_body_id;
b3ShapeId player_sensor_id;
b3ShapeId player_gravity_sensor_id;
b3Vec3    player_aim;

#define MAX_COLLECTED_KEYS 4
FixedArrayImpl(Color, MAX_COLLECTED_KEYS);
FixedArray(Color) player_collected_keys;

#define MAX_GRAVITY_BODIES 16
FixedArrayImpl(b3BodyId, MAX_GRAVITY_BODIES);
FixedArray(b3BodyId) player_gravity_bodies;
f32                  player_gravity_orbit_distance = 1.1f;

Mesh  player_mesh;
Model player_model;

Mesh  player_spawn_point_mesh;
Model player_spawn_point_model;

Mesh  pebble_mesh;
Model pebble_model;

Mesh  enemy_mesh;
Model enemy_model;

Mesh  turret_body_mesh;
Model turret_body_model;
Mesh  turret_gun_mesh;
Material turret_material;

Mesh  button_mesh;
Model button_model;

Mesh  key_mesh;
Model key_model;

Mesh  gate_mesh;
Model gate_model;

Mesh     cube_mesh;
Model    cube_model;
Material wall_material;
b3BodyId floor_body_id;

void camera_follow_player(Camera* camera, Vector3 player_position) {
  camera->position = Vector3Add(player_position, (Vector3){-3.0f, 8.0f, 3.0f});
  camera->target = player_position;
}

Vector3 camera_world_ray_cast(Camera camera) {
  Vector3 result = {0};
  Ray to_mouse_ray = GetScreenToWorldRay(GetMousePosition(), camera);
  RayCollision collision = GetRayCollisionMesh(to_mouse_ray, cube_mesh, MatrixScale(40.0f, 1.0f, 40.0f));
  f32 closest = 100000000000.0f;
  if (collision.hit) {
    result = collision.point;
    closest = collision.distance;
  }
  for (i32 i = 0; i < objects.count; i += 1) {
    Object* o = objects.items + i;
    if (o->type != OBJECT_TYPE_WALL) continue;
    b3Vec3 position = b3Body_GetPosition(o->body_id);
    Vector3 scale = o->scale;

    Matrix mx = MatrixRotateX(o->rotation.x);
    Matrix my = MatrixRotateY(o->rotation.y);
    Matrix mz = MatrixRotateZ(o->rotation.z);
    Matrix mt = MatrixTranslate(position.x, position.y, position.z);
    Matrix ms = MatrixScale(scale.x, scale.y, scale.z);

    Matrix m = ms;
    m = MatrixMultiply(m, MatrixMultiply(mx, MatrixMultiply(my, mz)));
    m = MatrixMultiply(m, mt);

    collision = GetRayCollisionMesh(to_mouse_ray, cube_mesh, m);
    if (collision.hit) {
      if (collision.distance < closest) {
        closest = collision.distance;
        result = collision.point;
      }
    }
  }
  return result;
}

Object* camera_ray_cast_object(Camera camera) {
  Object* result = NULL;
  Ray to_mouse_ray = GetScreenToWorldRay(GetMousePosition(), camera);
  f32 closest = 100000000000.0f;

  // {
  //   b3Vec3 position = b3Body_GetPosition(player_body_id);
  //   Matrix mt = MatrixTranslate(position.x, position.y, position.z);
  //   RayCollision collision = GetRayCollisionMesh(to_mouse_ray, player_mesh, mt);
  //   if (collision.hit) {
  //     if (collision.distance < closest) {
  //       closest = collision.distance;
  //       result = NULL;
  //     }
  //   }
  // }

  for (i32 i = 0; i < objects.count; i += 1) {
    Object* o = objects.items + i;
    b3Vec3 position = b3Body_GetPosition(o->body_id);
    Matrix mt = MatrixTranslate(position.x, position.y, position.z);
    Matrix ms = MatrixScale(o->scale.x, o->scale.y, o->scale.z);
    Matrix m  = MatrixMultiply(ms, mt);
    RayCollision collision = GetRayCollisionMesh(to_mouse_ray, cube_mesh, m);
    if (collision.hit) {
      if (collision.distance < closest) {
        closest = collision.distance;
        result = o;
      }
    }
  }

  return result;
}

void player_spawn() {
  {
    b3BodyDef player_body_def = b3DefaultBodyDef();
    player_body_def.type      = b3_kinematicBody;
    player_body_def.position  = (b3Vec3){0.0f, 3.0f, 0.0f};
    player_body_id = b3CreateBody(world_id, &player_body_def);

    {
      b3ShapeDef shape_def = b3DefaultShapeDef();
      shape_def.enableSensorEvents  = true;
      shape_def.filter.categoryBits = PHYSICS_CATEGORY_PLAYER;
      shape_def.filter.maskBits     = PHYSICS_CATEGORY_ENEMY  |
                                      PHYSICS_CATEGORY_BUTTON |
                                      PHYSICS_CATEGORY_KEY    |
                                      PHYSICS_CATEGORY_GATE;
      b3Sphere sphere = {b3Vec3_zero, 0.55f};
      b3CreateSphereShape(player_body_id, &shape_def, &sphere);
    }

    // main player sensor
    {
      b3ShapeDef shape_def = b3DefaultShapeDef();
      shape_def.isSensor            = true;
      shape_def.enableSensorEvents  = true;
      shape_def.filter.categoryBits = PHYSICS_CATEGORY_PLAYER;
      shape_def.filter.maskBits     = PHYSICS_CATEGORY_ENEMY  |
                                      PHYSICS_CATEGORY_BUTTON |
                                      PHYSICS_CATEGORY_KEY    |
                                      PHYSICS_CATEGORY_GATE_SENSOR;
      b3Sphere sphere = {b3Vec3_zero, 0.55f};
      player_sensor_id = b3CreateSphereShape(player_body_id, &shape_def, &sphere);
    }

    // gravity sensor
    {
      b3ShapeDef shape_def = b3DefaultShapeDef();
      shape_def.isSensor            = true;
      shape_def.enableSensorEvents  = true;
      shape_def.filter.categoryBits = PHYSICS_CATEGORY_PLAYER_GRAVITY_FIELD;
      shape_def.filter.maskBits     = PHYSICS_CATEGORY_PEBBLE;
      b3Sphere sphere = {b3Vec3_zero, 2.0f};
      player_gravity_sensor_id = b3CreateSphereShape(player_body_id, &shape_def, &sphere);
    }
  }
}

void player_move(Camera* camera, f32 dt) {
  Vector3 camera_forward = Vector3Normalize(Vector3Subtract(camera->target,
                                                            camera->position));
  Vector3 player_right_rl   = Vector3CrossProduct(camera_forward, (Vector3){0.0f, 1.0f, 0.0f});
  Vector3 player_forward_rl = Vector3CrossProduct((Vector3){0.0f, 1.0f, 0.0f}, player_right_rl);
  b3Vec3  player_right      = vec3_rl_to_b3v(player_right_rl);
  b3Vec3  player_forward    = vec3_rl_to_b3v(player_forward_rl);

  b3Vec3 acceleration = {0};
  if (input_left) {
    acceleration = b3Sub(acceleration, player_right);
  }
  if (input_right) {
    acceleration = b3Add(acceleration, player_right);
  }
  if (input_forward) {
    acceleration = b3Add(acceleration, player_forward);
  }
  if (input_back) {
    acceleration = b3Sub(acceleration, player_forward);
  }
  acceleration = b3Add(acceleration, b3MulSV(input_gamepad_axis_x, player_right));
  acceleration = b3Add(acceleration, b3MulSV(-input_gamepad_axis_y, player_forward));

  b3Vec3 player_position = b3Body_GetPosition(player_body_id);
  b3Vec3 player_velocity = {0};
  if (player_in_dash_mode) {
    player_dash_dt += dt;
    if (player_dash_time < player_dash_dt) {
      player_in_dash_mode = false;
      player_dash_dt = 0.0f;
      player_velocity = (b3Vec3){0};
    } else {
      f32 v = player_dash_legth / player_dash_time;
      player_velocity = b3MulSV(v, b3Normalize(b3Sub(player_dash_target, player_position)));
    }
  } else {
    player_velocity = b3Body_GetLinearVelocity(player_body_id);

    if (player_in_slam_mode) {
      player_slam_accumulate_time += dt;
      if (player_slam_up_time + player_slam_hold_time + player_slam_down_time < player_slam_accumulate_time) {
        // slam is done
        player_in_slam_mode = false;
        player_slam_accumulate_time = 0.0f;
        player_slam_finished = true;
      } else if (player_slam_up_time + player_slam_hold_time < player_slam_accumulate_time) {
        f32 slamming_velocity = player_slam_height / player_slam_down_time;
        player_velocity.y = -slamming_velocity;
      } else if (player_slam_up_time < player_slam_accumulate_time) {
        player_velocity.y = 0.0f;
      } else {
        f32 rising_velocity = player_slam_height / player_slam_up_time;
        player_velocity.y = rising_velocity;
      }
    } else {
      acceleration = b3Normalize(acceleration);

      f32 speed = player_speed;
      if (player_in_slam_mode) {
        speed /= 3.0f;
      }
      acceleration = b3MulSV(speed, acceleration);
      acceleration.x -= player_friction * player_velocity.x;
      acceleration.z -= player_friction * player_velocity.z;
      player_velocity = b3Add(player_velocity, b3MulSV(dt, acceleration));
      // gravity
      player_velocity.y -= 10.0f * dt;
    }
  }

  b3Vec3 player_translation = b3MulSV(dt, player_velocity);

  b3Capsule capsule = (b3Capsule){{0}, {0}, 0.5f};
  b3QueryFilter filter = b3DefaultQueryFilter();
  filter.categoryBits = PHYSICS_CATEGORY_PLAYER;
  filter.maskBits     = PHYSICS_CATEGORY_LEVEL |
                        PHYSICS_CATEGORY_ENEMY |
                        PHYSICS_CATEGORY_GATE;
  if (player_in_dash_mode) {
    filter.maskBits &= ~PHYSICS_CATEGORY_ENEMY;
  }
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

  FixedArray(b3CollisionPlane) collision_planes = {0};
  b3World_CollideMover(world_id,
                       player_position,
                       &capsule,
                       filter,
                       player_collider_result_fn,
                       &collision_planes);

  // TODO figure out how to avoid getting player into the collidiers
  b3PlaneSolverResult result = b3SolvePlanes(b3Vec3_zero, collision_planes.items, collision_planes.count);
  player_resolved_positon = b3Add(player_position, result.delta);

  player_velocity = b3ClipVector(player_velocity, collision_planes.items, collision_planes.count);
  b3Body_SetLinearVelocity(player_body_id, player_velocity);
}

void player_move_to_spawn_point(Object* spawn_point) {
  b3Vec3 position = b3Body_GetPosition(spawn_point->body_id);
  position.y = 1.0f;
  b3Body_SetTransform(player_body_id, position, b3Quat_identity);
}

void player_update_gravity_objects_velocities(b3Vec3 player_position) {
  for (i32 i = 0; i < player_gravity_bodies.count; i += 1) {
    // TODO maybe improve somehow
    b3BodyId body_id = player_gravity_bodies.items[i];
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

Object* pebble_spawn(b3Vec3 position, Color color) {
  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.type     = b3_dynamicBody;
  body_def.isBullet = true;
  body_def.position = position;
  b3BodyId body_id = b3CreateBody(world_id, &body_def);

  b3ShapeId shape_id;
  {
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.filter.categoryBits = PHYSICS_CATEGORY_PEBBLE;
    shape_def.filter.maskBits     = PHYSICS_CATEGORY_LEVEL                |
                                    PHYSICS_CATEGORY_PLAYER_GRAVITY_FIELD |
                                    PHYSICS_CATEGORY_PEBBLE               |
                                    PHYSICS_CATEGORY_ENEMY;
    // TODO figure out why this feels so heavy
    shape_def.density = 0.1f;
    shape_def.enableSensorEvents = true;
    b3Sphere sphere = {b3Vec3_zero, 0.25f};
    shape_id = b3CreateSphereShape(body_id, &shape_def, &sphere);
  }

  b3ShapeId sensor_shape_id;
  {
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.isSensor = true;
    shape_def.enableSensorEvents  = true;
    shape_def.filter.categoryBits = PHYSICS_CATEGORY_PEBBLE;
    shape_def.filter.maskBits     = PHYSICS_CATEGORY_LEVEL | PHYSICS_CATEGORY_ENEMY;
    b3Sphere sphere = {b3Vec3_zero, 0.25f};
    sensor_shape_id = b3CreateSphereShape(body_id, &shape_def, &sphere);
  }

  Object object = {
    .type            = OBJECT_TYPE_PEBBLE,
    .id              = *(u32*)&body_id,
    .body_id         = body_id,
    .shape_id        = shape_id,
    .sensor_shape_id = sensor_shape_id,
    .scale           = Vector3One(),
    .color           = color,
    .hp              = 5,
    .damage          = 1,
  };
  fixed_array_add(&objects, object);
  b3Body_SetUserData(body_id, fixed_array_last_item(&objects));
  return fixed_array_last_item(&objects);
}

Object* wall_spawn(b3Vec3 position, Vector3 scale, Color color) {
  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.type      = b3_staticBody;
  body_def.position  = position;
  b3BodyId body_id   = b3CreateBody(world_id, &body_def);

  b3BoxHull box        = b3MakeBoxHull(scale.x / 2.0f, scale.y / 2.0f, scale.z / 2.0f);
  b3ShapeDef shape_def = b3DefaultShapeDef();
  shape_def.enableSensorEvents  = true;
  shape_def.filter.categoryBits = PHYSICS_CATEGORY_LEVEL;
  shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER | PHYSICS_CATEGORY_PEBBLE;
  b3ShapeId shape_id   = b3CreateHullShape(body_id , &shape_def, &box.base);

  Object object = {
    .type            = OBJECT_TYPE_WALL,
    .id              = *(u32*)&body_id,
    .body_id         = body_id,
    .shape_id        = shape_id,
    .sensor_shape_id = 0,
    .scale           = scale,
    .color           = color,
    .hp              = 0,
    .damage          = 0,
    .disabled        = true,
  };
  fixed_array_add(&objects, object);
  b3Body_SetUserData(body_id, fixed_array_last_item(&objects));
  return fixed_array_last_item(&objects);
}

Object* enemy_spawn(b3Vec3 position, Vector3 scale, Color color) {
  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.type      = b3_kinematicBody;
  body_def.position  = position;
  b3BodyId body_id   = b3CreateBody(world_id, &body_def);

  b3BoxHull box        = b3MakeBoxHull(scale.x / 2.0f, scale.y / 2.0f, scale.z / 2.0f);
  b3ShapeDef shape_def = b3DefaultShapeDef();
  shape_def.enableSensorEvents = true;
  shape_def.filter.categoryBits = PHYSICS_CATEGORY_ENEMY;
  shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER            |
                                  PHYSICS_CATEGORY_PLAYER_SLAM_FIELD |
                                  PHYSICS_CATEGORY_LEVEL             |
                                  PHYSICS_CATEGORY_PEBBLE;
  b3ShapeId shape_id = b3CreateHullShape(body_id, &shape_def, &box.base);

  Object object = {
    .type            = OBJECT_TYPE_ENEMY,
    .id              = *(u32*)&body_id,
    .body_id         = body_id,
    .shape_id        = shape_id,
    .sensor_shape_id = 0,
    .scale           = scale,
    .color           = color,
    .hp              = 20,
    .damage          = 5,
  };
  fixed_array_add(&objects, object);
  b3Body_SetUserData(body_id, fixed_array_last_item(&objects));
  return fixed_array_last_item(&objects);
}

Object* player_spawn_point_spawn(b3Vec3 position, Color color) {
  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.type      = b3_staticBody;
  body_def.position  = position;
  b3BodyId body_id   = b3CreateBody(world_id, &body_def);

  // b3BoxHull box        = b3MakeBoxHull(0.5f, 0.1f, 0.5f);
  // b3ShapeDef shape_def = b3DefaultShapeDef();
  // shape_def.isSensor   = true;
  // shape_def.enableSensorEvents  = true;
  // shape_def.filter.categoryBits = PHYSICS_CATEGORY_PLAYER_SPAWN_POINT;
  // shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER;
  // b3ShapeId sensor_shape_id = b3CreateHullShape(body_id, &shape_def, &box.base);

  Object object = {
    .type            = OBJECT_TYPE_PLAYER_SPAWN_POINT,
    .id              = *(u32*)&body_id,
    .body_id         = body_id,
    .shape_id        = 0,
    .sensor_shape_id = 0,//sensor_shape_id,
    .scale           = Vector3One(),
    .color           = color,
    .hp              = 0,
    .damage          = 0,
  };
  fixed_array_add(&objects, object);
  b3Body_SetUserData(body_id, fixed_array_last_item(&objects));
  return fixed_array_last_item(&objects);
}

Object* button_spawn(b3Vec3 position, Color color) {
  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.type      = b3_staticBody;
  body_def.position  = position;
  b3BodyId body_id   = b3CreateBody(world_id, &body_def);

  b3BoxHull box        = b3MakeBoxHull(0.5f, 0.1f, 0.5f);
  b3ShapeDef shape_def = b3DefaultShapeDef();
  shape_def.isSensor            = true;
  shape_def.enableSensorEvents  = true;
  shape_def.filter.categoryBits = PHYSICS_CATEGORY_BUTTON;
  shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER;
  b3ShapeId sensor_shape_id = b3CreateHullShape(body_id, &shape_def, &box.base);

  Object object = {
    .type            = OBJECT_TYPE_BUTTON,
    .id              = *(u32*)&body_id,
    .body_id         = body_id,
    .shape_id        = 0,
    .sensor_shape_id = sensor_shape_id,
    .scale           = Vector3One(),
    .color           = color,
    .hp              = 0,
    .damage          = 0,
  };
  fixed_array_add(&objects, object);
  b3Body_SetUserData(body_id, fixed_array_last_item(&objects));
  return fixed_array_last_item(&objects);
}

Object* key_spawn(b3Vec3 position, Color color) {
  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.type      = b3_staticBody;
  body_def.position  = position;
  b3BodyId body_id   = b3CreateBody(world_id, &body_def);

  b3ShapeDef shape_def = b3DefaultShapeDef();
  shape_def.isSensor            = true;
  shape_def.enableSensorEvents  = true;
  shape_def.filter.categoryBits = PHYSICS_CATEGORY_KEY;
  shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER;
  b3Sphere sphere = {b3Vec3_zero, 0.5f};
  b3ShapeId sensor_shape_id = b3CreateSphereShape(body_id, &shape_def, &sphere);

  Object object = {
    .type            = OBJECT_TYPE_KEY,
    .id              = *(u32*)&body_id,
    .body_id         = body_id,
    .shape_id        = 0,
    .sensor_shape_id = sensor_shape_id,
    .scale           = Vector3One(),
    .color           = color,
    .hp              = 0,
    .damage          = 0,
  };
  fixed_array_add(&objects, object);
  b3Body_SetUserData(body_id, fixed_array_last_item(&objects));
  return fixed_array_last_item(&objects);
}

Object* gate_spawn(b3Vec3 position, b3Quat rotation, Color color) {
  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.rotation  = rotation;

  b3BodyId ground_id = b3CreateBody(world_id, &body_def);

  body_def.position  = position;
  body_def.rotation  = rotation;
  body_def.type      = b3_dynamicBody;
  b3BodyId body_id   = b3CreateBody(world_id, &body_def);

  b3ShapeId shape_id;
  {
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.filter.categoryBits = PHYSICS_CATEGORY_GATE;
    shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER;

    b3BoxHull box = b3MakeBoxHull(0.75f, 1.0f, 0.125f);
    shape_id      = b3CreateHullShape(body_id, &shape_def, &box.base);
  }

  b3ShapeId sensor_shape_id;
  {
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.isSensor            = true;
    shape_def.enableSensorEvents  = true;
    shape_def.filter.categoryBits = PHYSICS_CATEGORY_GATE_SENSOR;
    shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER;
    b3BoxHull box   = b3MakeBoxHull(0.75f, 1.0f, 0.5f);
    sensor_shape_id = b3CreateHullShape(body_id, &shape_def, &box.base);
  }

  b3Quat axis_quat = b3ComputeQuatBetweenUnitVectors( b3Vec3_axisZ, b3Vec3_axisY );
  b3Vec3 offset = { -0.75f, 0.0f, 0.0f };

  b3RevoluteJointDef joint_def = b3DefaultRevoluteJointDef();
  joint_def.base.bodyIdA = ground_id;
  joint_def.base.bodyIdB = body_id;
  joint_def.base.localFrameA.p = b3ToVec3(b3Add(body_def.position, offset));
  joint_def.base.localFrameA.q = axis_quat;
  joint_def.base.localFrameB.p = offset;
  joint_def.base.localFrameB.q = axis_quat;

  joint_def.enableLimit = true;
  joint_def.lowerAngle = 0.0f; // on activation becomes -PI / 2.0f;
  joint_def.upperAngle = 0.0f; // on activation becomes  PI / 2.0f;
  joint_def.enableSpring = true;
  joint_def.hertz = 1.0f;
  joint_def.dampingRatio = 0.5f;
  joint_def.enableMotor = false;
  joint_def.maxMotorTorque = 100.0f;
  joint_def.base.drawScale = 2.0f;

  b3JointId joint_id = b3CreateRevoluteJoint(world_id, &joint_def);

  Object object = {
    .type            = OBJECT_TYPE_GATE,
    .id              = *(u32*)&body_id,
    .body_id         = body_id,
    .shape_id        = shape_id,
    .sensor_shape_id = sensor_shape_id,
    .ground_id       = ground_id,
    .joint_id        = joint_id,
    .scale           = Vector3One(),
    .color           = color,
    .hp              = 0,
    .damage          = 0,
  };
  fixed_array_add(&objects, object);
  b3Body_SetUserData(body_id, fixed_array_last_item(&objects));
  return fixed_array_last_item(&objects);
}

Object* turret_spawn(b3Vec3 position, Color color) {
  b3BodyDef body_def = b3DefaultBodyDef();
  body_def.type      = b3_staticBody;
  body_def.position  = position;
  b3BodyId body_id   = b3CreateBody(world_id, &body_def);

  b3ShapeDef shape_def = b3DefaultShapeDef();
  shape_def.enableSensorEvents = true;
  shape_def.filter.categoryBits = PHYSICS_CATEGORY_TURRET;
  shape_def.filter.maskBits     = PHYSICS_CATEGORY_PEBBLE;
  b3Sphere sphere = {b3Vec3_zero, 0.5f};
  b3ShapeId shape_id = b3CreateSphereShape(body_id, &shape_def, &sphere);

  Object object = {
    .type            = OBJECT_TYPE_TURRET,
    .id              = *(u32*)&body_id,
    .body_id         = body_id,
    .shape_id        = shape_id,
    .sensor_shape_id = 0,
    .scale           = Vector3One(),
    .color           = color,
    .hp              = 50,
    .damage          = 10,
    .attack_range    = 10.0f,
    .attack_speed    = 1.0f,
    .attack_timer    = 0.0f,
  };
  fixed_array_add(&objects, object);
  b3Body_SetUserData(body_id, fixed_array_last_item(&objects));
  return fixed_array_last_item(&objects);
}

void turrets_aim_and_shoot(f32 dt) {
  Vector3 player_position = vec3_b3p_to_rl(b3Body_GetPosition(player_body_id));
  for (i32 i = 0; i < objects.count; i += 1) {
    Object* o = objects.items + i;
    if (o->type == OBJECT_TYPE_TURRET && !o->disabled) {
      Vector3 body_position = vec3_b3p_to_rl(b3Body_GetPosition(o->body_id));
      Vector3 gun_position = body_position;
      gun_position.y += 0.2;
      Vector3 to_player = Vector3Subtract(player_position, gun_position);
      f32 distance = Vector3Length(to_player);
      if (distance < o->attack_range) {
        o->attack_timer += dt;
        if (o->attack_speed < o->attack_timer) {
          o->attack_timer = 0;
          to_player = Vector3Normalize(to_player);
          Vector3 bullet_spawn_position = Vector3Add(gun_position, Vector3Scale(to_player, 1.5f));
          Object* bullet = pebble_spawn(vec3_rl_to_b3v(bullet_spawn_position), WHITE);
          b3Body_SetLinearVelocity(bullet->body_id, vec3_rl_to_b3v(Vector3Scale(to_player, 20.0f)));
        }
      }
    }
  }
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
    DrawModelWiresEx(player_model, position, axis, rad_to_degree(angle), (Vector3){1.0f, 1.0f, 1.0f}, GOLD);
    DrawModelWiresEx(player_model, position, axis, rad_to_degree(angle), (Vector3){4.0f, 4.0f, 4.0f}, MAGENTA);

    DrawLine3D(position, vec3_b3v_to_rl(player_dash_target), RED);

    DrawModelWiresEx(player_model,
                     vec3_b3p_to_rl(player_resolved_positon),
                     axis,
                     rad_to_degree(angle),
                     (Vector3){1.0f, 1.0f, 1.0f},
                     DARKGREEN);

    DrawCylinderWires(position, player_slam_radius, player_slam_radius, 1.0f, 32, ORANGE);

    Vector3 keys_offset = position;
    keys_offset.y += 1.0f;
    Vector3 camera_forward = Vector3Normalize(Vector3Subtract(game_camera.target, game_camera.position));
    Vector3 camera_right = Vector3CrossProduct(camera_forward, (Vector3){0.0f, 1.0f, 0.0});
    keys_offset = Vector3Subtract(keys_offset,
                                  Vector3Scale(camera_right, 0.5f * (player_collected_keys.count - 1)));
    for (i32 i = 0; i < player_collected_keys.count; i += 1) {
      Vector3 key_position = Vector3Add(keys_offset, Vector3Scale(camera_right, (f32)i));
      Color color = *(player_collected_keys.items + i);
      DrawModel(key_model, key_position, 1.0f, color);
    }
  }

  // floor
  Vector3 floor_rotation_axis = (Vector3){0.0f, 1.0f, 0.0f};
  Vector3 floor_position = vec3_b3p_to_rl(b3Body_GetPosition(floor_body_id));
  Vector3 floor_scale = (Vector3){40.0f, 1.0f, 40.0f};
  DrawModelEx(cube_model, floor_position, floor_rotation_axis , 0.0f, floor_scale, GRAY);

  for (i32 i = 0; i < objects.count; i += 1) {
    Object* o = objects.items + i;
    switch (o->type) {
      case OBJECT_TYPE_NONE:   break;
      case OBJECT_TYPE_PLAYER: break;
      case OBJECT_TYPE_PLAYER_SPAWN_POINT: {
        Vector3 rotation_axis = (Vector3){0.0f, 1.0f, 0.0f};
        Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(o->body_id));
        Vector3 scale = o->scale;
        DrawModelEx(player_spawn_point_model, position, rotation_axis , 0.0f, scale, o->color);
      } break;
      case OBJECT_TYPE_WALL: {
        Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(o->body_id));
        Vector3 scale = o->scale;

        Matrix mx = MatrixRotateX(o->rotation.x);
        Matrix my = MatrixRotateY(o->rotation.y);
        Matrix mz = MatrixRotateZ(o->rotation.z);
        Matrix mt = MatrixTranslate(position.x, position.y, position.z);
        Matrix ms = MatrixScale(scale.x, scale.y, scale.z);

        Matrix m = ms;
        m = MatrixMultiply(m, MatrixMultiply(mx, MatrixMultiply(my, mz)));
        m = MatrixMultiply(m, mt);

        DrawMesh(cube_mesh, wall_material, m);
      } break;
      case OBJECT_TYPE_PEBBLE: {
        Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(o->body_id));
        b3Quat rotation = b3Body_GetRotation(o->body_id);
        f32 angle;
        Vector3 axis = vec3_b3v_to_rl(b3GetAxisAngle(&angle, rotation));

        Color color = o->color;
        if (!b3Body_IsAwake(o->body_id)) {
          color.a /= 2;
        }

        DrawModelEx(pebble_model, position, axis, rad_to_degree(angle), (Vector3){0.5f, 0.5f, 0.5f}, color);

        Vector3 velocity = vec3_b3p_to_rl(b3Body_GetLinearVelocity(o->body_id));
        DrawLine3D(position, Vector3Add(position, velocity), LIME);
      } break;
      case OBJECT_TYPE_ENEMY: {
        Vector3 rotation_axis = (Vector3){0.0f, 1.0f, 0.0f};
        Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(o->body_id));
        Vector3 scale = o->scale;
        DrawModelEx(enemy_model, position, rotation_axis , 0.0f, scale, o->color);
      } break;
      case OBJECT_TYPE_TURRET: {
        Vector3 rotation_axis = (Vector3){0.0f, 1.0f, 0.0f};
        Vector3 body_position = vec3_b3p_to_rl(b3Body_GetPosition(o->body_id));
        Vector3 scale = o->scale;

        Color color = o->color;
        if (o->disabled) {
          color.r = 255;
        }
        DrawModelEx(turret_body_model, body_position, rotation_axis , 0.0f, scale, color);

        Vector3 gun_position = body_position;
        gun_position.y += 0.2;

        Matrix m;
        Vector3 forward;
        Vector3 right;
        Vector3 up;
        if (!o->disabled) {
          Vector3 player_position = vec3_b3p_to_rl(b3Body_GetPosition(player_body_id));

          forward = Vector3Normalize(Vector3Subtract(player_position, gun_position));
          right   = Vector3CrossProduct(forward, (Vector3){0.0f, 1.0f, 0.0f});
          up      = Vector3CrossProduct(right, forward);
        } else {
          forward = (Vector3){1.0f, 0.0f, 0.0f};
          right   = (Vector3){0.0f, 0.0f, 1.0f};
          up      = (Vector3){0.0f, 1.0f, 0.0f};
        }
        m = (Matrix){up.x, forward.x, right.x, gun_position.x,
                     up.y, forward.y, right.y, gun_position.y,
                     up.z, forward.z, right.z, gun_position.z,
                     0.0f,      0.0f,    0.0f,           1.0f};

        DrawMesh(turret_gun_mesh, turret_material, m);
      } break;
      case OBJECT_TYPE_BUTTON: {
        Vector3 rotation_axis = (Vector3){0.0f, 1.0f, 0.0f};
        Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(o->body_id));
        Vector3 scale = o->scale;
        DrawModelEx(player_spawn_point_model, position, rotation_axis , 0.0f, scale, o->color);
      } break;
      case OBJECT_TYPE_KEY: {
        Vector3 rotation_axis = (Vector3){0.0f, 1.0f, 0.0f};
        Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(o->body_id));
        Vector3 scale = o->scale;
        DrawModelEx(key_model, position, rotation_axis , 0.0f, scale, o->color);
      } break;
      case OBJECT_TYPE_GATE: {
        Vector3 position = vec3_b3p_to_rl(b3Body_GetPosition(o->body_id));
        Vector3 scale = o->scale;

        b3Quat rotation = b3Body_GetRotation(o->body_id);
        Matrix mr = QuaternionToMatrix(*(Quaternion*)&rotation);

        Matrix mt = MatrixTranslate(position.x, position.y, position.z);
        Matrix ms = MatrixScale(scale.x, scale.y, scale.z);

        Matrix m = ms;
        m = MatrixMultiply(m, mr);
        m = MatrixMultiply(m, mt);

        DrawMesh(gate_mesh, wall_material, m);
      } break;
    }
  }

  DrawSphere(vec3_b3v_to_rl(player_aim), 0.2, BLUE);
}

float slam_shape_cast_fn(
  b3ShapeId shape_id,
  b3Pos point,
  b3Vec3 normal,
  float fraction,
  uint64_t userMaterialId,
  int triangleIndex,
  int childIndex,
  void* context
) {
  b3BodyId body_id = b3Shape_GetBody(shape_id);
  Object* enemy = (Object*)b3Body_GetUserData(body_id);
  if (enemy->type == OBJECT_TYPE_ENEMY) {
    object_take_damage(enemy, player_slam_damage);
  }
  return 1.0f;
}

void game_process_sensor_events() {
  b3SensorEvents events = b3World_GetSensorEvents(world_id);
  for (i32 i = 0; i < events.beginCount; i += 1) {
    b3SensorBeginTouchEvent* event = events.beginEvents + i;

    if (B3_ID_EQUALS(event->sensorShapeId, player_sensor_id)) {
      b3BodyId body_id = b3Shape_GetBody(event->visitorShapeId);
      Object* o = (Object*)b3Body_GetUserData(body_id);
      if (player_in_dash_mode && o->type == OBJECT_TYPE_ENEMY) {
        object_take_damage(o, player_dash_damage);
      } else if (o->type == OBJECT_TYPE_KEY) {
        fixed_array_add(&player_collected_keys, o->color);
        object_destroy(o);
      }
    }

    if (B3_ID_EQUALS(event->sensorShapeId, player_gravity_sensor_id)) {
      b3BodyId body_id = b3Shape_GetBody(event->visitorShapeId);
      fixed_array_add(&player_gravity_bodies, body_id);
    }

    for (i32 i = 0; i < objects.count; i += 1) {
      Object* o = objects.items + i;
      if (o->type == OBJECT_TYPE_PEBBLE) {
        if (B3_ID_EQUALS(event->sensorShapeId, o->sensor_shape_id)) {
          b3BodyId body_id = b3Shape_GetBody(event->visitorShapeId);
          Object* other = (Object*)b3Body_GetUserData(body_id);
          if (other->type == OBJECT_TYPE_ENEMY) {
            object_take_damage(other, o->damage);
            object_take_damage(o, 1);
          } else if (other->type == OBJECT_TYPE_WALL && other->disabled == false) {
            object_take_damage(other, o->damage);
            object_take_damage(o, 1);
          }
          PlaySound(sound_pebble_impact);
        }
      } else if (o->type == OBJECT_TYPE_BUTTON) {
        if (B3_ID_EQUALS(event->sensorShapeId, o->sensor_shape_id)) {
          for (i32 j = 0; j < objects.count; j += 1) {
            Object* o2 = objects.items + j;
            if (o2->id == o->activates_id) {
              o2->disabled = false;
            }
          }
        }
      } else if (o->type == OBJECT_TYPE_GATE) {
        if (B3_ID_EQUALS(event->sensorShapeId, o->sensor_shape_id)) {
          for (i32 j = 0; j < player_collected_keys.count; j += 1) {
            Color c = *(player_collected_keys.items + j);
            if (ColorIsEqual(c, o->color)) {
              PlaySound(sound_jump);
              b3RevoluteJoint_SetLimits(o->joint_id, -PI / 2.0f, PI / 2.0f);
              fixed_array_remove_index(&player_collected_keys, j);
              break;
            }
          }
        }
      }
    }
  }
  for (i32 i = 0; i < events.endCount; i += 1) {
    b3SensorEndTouchEvent* event = events.endEvents + i;
    if (B3_ID_EQUALS(event->sensorShapeId, player_gravity_sensor_id)) {
      if (b3Shape_IsValid(event->visitorShapeId)) {
        b3BodyId body_id = b3Shape_GetBody(event->visitorShapeId);
        fixed_array_remove_body_id(&player_gravity_bodies, body_id);
      }
    }
  }
}

char level_name[32] = {0};
void level_save(void) {
  i32 fd = open(level_name, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
  if (fd <= 0) {
    perror("save level");
    return;
  }
  char buff[128];

  for (i32 i = 0; i < objects.count; i += 1) {
    Object* o = objects.items + i;
    char* type_str = "???";
    switch (o->type) {
      case OBJECT_TYPE_NONE:                type_str = "OBJECT_TYPE_NONE";               break;
      case OBJECT_TYPE_PLAYER:              type_str = "OBJECT_TYPE_PLAYER";             break;
      case OBJECT_TYPE_PLAYER_SPAWN_POINT:  type_str = "OBJECT_TYPE_PLAYER_SPAWN_POINT"; break;
      case OBJECT_TYPE_WALL:                type_str = "OBJECT_TYPE_WALL";               break;
      case OBJECT_TYPE_PEBBLE:              type_str = "OBJECT_TYPE_PEBBLE";             break;
      case OBJECT_TYPE_ENEMY:               type_str = "OBJECT_TYPE_ENEMY";              break;
      case OBJECT_TYPE_TURRET:              type_str = "OBJECT_TYPE_TURRET";             break;
      case OBJECT_TYPE_BUTTON:              type_str = "OBJECT_TYPE_BUTTON";             break;
      case OBJECT_TYPE_KEY:                 type_str = "OBJECT_TYPE_KEY";                break;
      case OBJECT_TYPE_GATE:                type_str = "OBJECT_TYPE_GATE";               break;
    }

    i32 n = snprintf(buff, 128, "type %s\n", type_str);
    write(fd, buff, n);

    b3Vec3 position = b3Body_GetPosition(o->body_id);
    n = snprintf(buff, 128, "positon %f %f %f\n", position.x, position.y, position.z);
    write(fd, buff, n);

    b3Quat rotation = b3Body_GetRotation(o->body_id);
    n = snprintf(buff, 128, "rotation %f %f %f %f\n", rotation.v.x, rotation.v.y, rotation.v.z, rotation.s);
    write(fd, buff, n);

    n = snprintf(buff, 128, "scale %f %f %f\n", o->scale.x, o->scale.y, o->scale.z);
    write(fd, buff, n);

    n = snprintf(buff, 128, "color %d %d %d\n", o->color.r, o->color.g, o->color.b);
    write(fd, buff, n);

    n = snprintf(buff, 128, "hp %d\n", o->hp);
    write(fd, buff, n);

    n = snprintf(buff, 128, "damage %d\n", o->hp);
    write(fd, buff, n);

    n = snprintf(buff, 128, "attack_range %f\n", o->attack_range);
    write(fd, buff, n);

    n = snprintf(buff, 128, "attack_speed %f\n", o->attack_speed);
    write(fd, buff, n);

    n = snprintf(buff, 128, "id %u\n", o->id);
    write(fd, buff, n);

    n = snprintf(buff, 128, "activates_id %u\n", o->activates_id);
    write(fd, buff, n);

    n = snprintf(buff, 128, "disabled %b\n", o->disabled);
    write(fd, buff, n);

    write(fd, "\n", 1);
  }

  close(fd);
}

void skip_past_new_line(char** mem) {
  while (**mem != '\n') *mem += 1;
  *mem += 1;
}
void level_load(void) {
  i32 fd = open(level_name, O_RDONLY, 0);
  if (fd < 0) {
    perror("load level");
    return;
  }

  struct stat file_stat;
  if (fstat(fd, &file_stat) == -1) {
    perror("load level fstat");
    close(fd);
    return;
  }

  char* mem = (char*)mmap(NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (mem == MAP_FAILED) {
    perror("load level mmap");
    close(fd);
    return;
  }

  for (i32 i = 0; i < objects.count; i += 1) {
    Object* o = objects.items + i;
    b3DestroyBody(o->body_id);
  }
  fixed_array_clear(&objects);
  fixed_array_clear(&player_gravity_bodies);
  fixed_array_clear(&player_collected_keys);

  char* end = mem + file_stat.st_size;
  while (mem < end) {
    char type_str[128];
    sscanf(mem, "type %s\n", type_str);
    skip_past_new_line(&mem);

    b3Vec3 position;
    sscanf(mem, "positon %f %f %f\n", &position.x, &position.y, &position.z);
    skip_past_new_line(&mem);

    b3Quat rotation;
    sscanf(mem, "rotation %f %f %f %f\n", &rotation.v.x,
                                          &rotation.v.y,
                                          &rotation.v.z,
                                          &rotation.s);
    skip_past_new_line(&mem);

    Vector3 scale;
    sscanf(mem, "scale %f %f %f\n", &scale.x, &scale.y, &scale.z);
    skip_past_new_line(&mem);

    Color color;
    color.a = 255;
    sscanf(mem, "color %hhu %hhu %hhu\n", &color.r, &color.g, &color.b);
    skip_past_new_line(&mem);

    i32 hp;
    sscanf(mem, "hp %d\n", &hp);
    skip_past_new_line(&mem);

    i32 damage;
    sscanf(mem, "damage %d\n", &hp);
    skip_past_new_line(&mem);

    f32 attack_range;
    sscanf(mem, "attack_range %f\n", &attack_range);
    skip_past_new_line(&mem);

    f32 attack_speed;
    sscanf(mem, "attack_speed %f\n", &attack_speed);
    skip_past_new_line(&mem);

    u32 id;
    sscanf(mem, "id %u\n", &id);
    skip_past_new_line(&mem);

    u32 activates_id;
    sscanf(mem, "activates_id %u\n", &activates_id);
    skip_past_new_line(&mem);

    u8 disabled;
    sscanf(mem, "disabled %hhu\n", &disabled);
    skip_past_new_line(&mem);

    mem += 1;

    Object* object;
    i32 str_len = strlen(type_str);

#define IS_TYPE_STRING(s) (memcmp(type_str, #s, str_len < strlen(#s) ? strlen(#s) : str_len) == 0)

    if (IS_TYPE_STRING(OBJECT_TYPE_NONE)) {

    } else if (IS_TYPE_STRING(OBJECT_TYPE_PLAYER)) {

    } else if (IS_TYPE_STRING(OBJECT_TYPE_WALL)) {
      object = wall_spawn(position, scale, color);
    } else if (IS_TYPE_STRING(OBJECT_TYPE_PEBBLE)) {
      object = pebble_spawn(position, color);
    } else if (IS_TYPE_STRING(OBJECT_TYPE_ENEMY)) {
      object = enemy_spawn(position, scale, color);
    } else if (IS_TYPE_STRING(OBJECT_TYPE_TURRET)) {
      object = turret_spawn(position, color);
    } else if (IS_TYPE_STRING(OBJECT_TYPE_PLAYER_SPAWN_POINT)) {
      object = player_spawn_point_spawn(position, color);
      player_move_to_spawn_point(object);
    } else if (IS_TYPE_STRING(OBJECT_TYPE_BUTTON)) {
      object = button_spawn(position, color);
    } else if (IS_TYPE_STRING(OBJECT_TYPE_KEY)) {
      object = key_spawn(position, color);
    } else if (IS_TYPE_STRING(OBJECT_TYPE_GATE)) {
      object = gate_spawn(position, rotation, color);
    }

    object->hp           = hp;
    object->damage       = damage;
    object->attack_range = attack_range;
    object->attack_speed = attack_speed;
    object->id           = id;
    object->activates_id = activates_id;
    object->disabled     = disabled;
  }
  munmap(mem, file_stat.st_size);
  close(fd);
}

int main(void) {
  InitWindow(1280, 720, "test");
  InitAudioDevice();
  SetTargetFPS(60);

  // TODO uncomment in the release mode
  // SetExitKey(KEY_NULL);

  rlImGuiSetup(true);
  imgui_io = igGetIO_Nil();

  sound_dash          = LoadSound("assets/sfx/dash.wav");
  sound_jump          = LoadSound("assets/sfx/jump.wav");
  sound_pebble_impact = LoadSound("assets/sfx/pebble_impact.wav");
  sound_pebble_throw  = LoadSound("assets/sfx/pebble_throw.wav");
  sound_slam          = LoadSound("assets/sfx/slam.wav");

  mesh_shader = LoadShader("shaders/mesh_vert.glsl", "shaders/mesh_frag.glsl");
  i32 ligth_pos_loc = GetShaderLocation(mesh_shader, "light_pos");
  i32 shadow_map_loc = GetShaderLocation(mesh_shader, "shadow_map");
  i32 light_vp_loc   = GetShaderLocation(mesh_shader, "light_vp");

  depth_shader = LoadShader(
      "shaders/depth_vert.glsl",
      "shaders/depth_frag.glsl"
  );
  RenderTexture2D depth_texture = create_depth_texture(DEPTH_TEXTURE_RESOLUTION, DEPTH_TEXTURE_RESOLUTION);

  player_mesh  = GenMeshSphere(0.5f, 32, 32);
  player_model = LoadModelFromMesh(player_mesh);
  player_model.materials[0].shader = mesh_shader;

  player_spawn_point_mesh  = GenMeshCube(1.0f, 0.2f, 1.0f);
  player_spawn_point_model = LoadModelFromMesh(player_spawn_point_mesh);
  player_spawn_point_model.materials[0].shader = mesh_shader;

  pebble_mesh  = GenMeshSphere(0.25f, 32, 32);
  pebble_model = LoadModelFromMesh(player_mesh);

  enemy_mesh  = GenMeshCube(1.0f, 1.0f, 1.0f);
  enemy_model = LoadModelFromMesh(enemy_mesh);
  enemy_model.materials[0].shader = mesh_shader;

  turret_body_mesh  = GenMeshSphere(0.5f, 32, 32);
  turret_body_model = LoadModelFromMesh(turret_body_mesh);
  turret_body_model.materials[0].shader = mesh_shader;
  turret_gun_mesh   = GenMeshCylinder(0.1f, 1.0f, 32);
  turret_material   = LoadMaterialDefault();
  turret_material.shader = mesh_shader;
  turret_material.maps[0].color = DARKGRAY;

  button_mesh  = GenMeshCube(1.0f, 0.2f, 1.0f);
  button_model = LoadModelFromMesh(button_mesh);
  button_model.materials[0].shader = mesh_shader;

  key_mesh  = GenMeshCube(0.2f, 0.2f, 0.2f);
  key_model = LoadModelFromMesh(key_mesh);
  key_model.materials[0].shader = mesh_shader;

  gate_mesh  = GenMeshCube(1.5f, 2.0f, 0.25f);
  gate_model = LoadModelFromMesh(gate_mesh);
  gate_model.materials[0].shader = mesh_shader;

  cube_mesh  = GenMeshCube(1.0f, 1.0f, 1.0f);
  cube_model = LoadModelFromMesh(cube_mesh);
  cube_model.materials[0].shader = mesh_shader;
  wall_material               = LoadMaterialDefault();
  wall_material.shader        = mesh_shader;
  wall_material.maps[0].color = BROWN;

  b3WorldDef world_def = b3DefaultWorldDef();
  world_def.gravity = (b3Vec3){0.0f, -10.0f, 0.0f};
  world_id = b3CreateWorld(&world_def);

  camera_init_default(&free_camera);
  camera_init_default(&game_camera);
  light_camera.position = (Vector3){-3.0f, 8.0f, -3.0f};
  light_camera.target   = (Vector3){0.0f, 0.0f,   0.0f};
  light_camera.up       = (Vector3){0.0f, 1.0f,   0.0f};
  light_camera.fovy     = 20.0f;
  light_camera.projection = CAMERA_ORTHOGRAPHIC;

  // floor
  {
    b3BodyDef body_def = b3DefaultBodyDef();
    body_def.type      = b3_staticBody;
    body_def.position  = (b3Vec3){0.0f, 0.0f, 0.0f};
    floor_body_id = b3CreateBody(world_id, &body_def);

    b3BoxHull box  = b3MakeBoxHull(20.0f, 0.5f, 20.0f);
    b3ShapeDef shape_def = b3DefaultShapeDef();
    shape_def.filter.categoryBits = PHYSICS_CATEGORY_LEVEL;
    shape_def.filter.maskBits     = PHYSICS_CATEGORY_PLAYER | PHYSICS_CATEGORY_PEBBLE;
    b3CreateHullShape(floor_body_id, &shape_def, &box.base);
  }

  player_spawn();

  memcpy(level_name, "./levels/level_1.level", 22);
  level_load();

  // defaul level setup
  // pebble_spawn((b3Vec3){10.0f, 5.0f, -2.0f}, MAROON);
  // pebble_spawn((b3Vec3){10.0f, 5.0f, 0.0f},  PURPLE);
  // pebble_spawn((b3Vec3){10.0f, 5.0f, 2.0f},  DARKBROWN);
  //
  // wall_spawn((b3Vec3){0.0f,  5.0f, -20.0f}, (Vector3){40.0f, 10.0f,  1.0f}, BROWN);
  // wall_spawn((b3Vec3){20.0f, 1.0f,   0.0f}, (Vector3){1.0f,   1.0f, 40.0f}, BROWN);
  // wall_spawn((b3Vec3){0.0f,  1.0f,  20.0f}, (Vector3){40.0f,  1.0f,  1.0f}, BROWN);
  //
  // enemy_spawn((b3Vec3){0.0f, 1.0f, -5.0f},  (Vector3){0.85f, 1.0f, 0.85f}, (Color){200, 93, 82, 255});
  // enemy_spawn((b3Vec3){0.0f, 1.0f, -10.0f}, (Vector3){0.85f, 1.0f, 0.85f}, (Color){200, 93, 82, 255});
  // enemy_spawn((b3Vec3){0.0f, 1.0f, -15.0f}, (Vector3){0.85f, 1.0f, 0.85f}, (Color){200, 93, 82, 255});
  //
  // turret_spawn((b3Vec3){-10.0f, 0.5f,  0.0f}, BLACK);
  // turret_spawn((b3Vec3){-10.0f, 0.5f, -5.0f}, BLACK);

  while (!WindowShouldClose()) {
    f32 dt = GetFrameTime();
    dt /= (f32)game_slow_mode;

    if (!game_paused) b3World_Step(world_id, dt, 4);

    if (IsKeyPressed(KEY_F1)) {
      game_mode = GAME_MODE_EDITOR;
    }
    if (IsKeyPressed(KEY_F2)) {
      game_mode = GAME_MODE_GAME;
      EnableCursor();
    }
    if (IsKeyPressed(KEY_Z)) {
      game_show_debug_ui = !game_show_debug_ui;
    }
    if (IsKeyPressed(KEY_F3)) {
      game_slow_mode += 1;
    }
    if (IsKeyPressed(KEY_F4)) {
      game_slow_mode -= 1;
      if (game_slow_mode == 0) {
        game_slow_mode = 1;
      }
    }
    if (IsKeyPressed(KEY_F5)) {
      game_paused = !game_paused;
    }

    if (game_mode == GAME_MODE_EDITOR) {
      if (!imgui_wants_to_handle_events()) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
          UpdateCamera(&free_camera, CAMERA_FREE);
          DisableCursor();
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
          EnableCursor();
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
          selected_object = camera_ray_cast_object(free_camera);
        }
      }
    }

    if (!imgui_wants_to_handle_events() && game_mode == GAME_MODE_GAME) {
      input_slam_key_pressed          = IsKeyPressed(KEY_SPACE) ||
                                        IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
      input_gravity_key_pressed       = IsKeyPressed(KEY_Q);
      input_gravity_shoot_key_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
      input_dash_key_pressed          = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);

      input_forward = IsKeyDown(KEY_W);
      input_back    = IsKeyDown(KEY_S);
      input_left    = IsKeyDown(KEY_A);
      input_right   = IsKeyDown(KEY_D);

      input_gamepad_axis_x = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
      input_gamepad_axis_y = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
    }

    if (!game_paused) {
      game_process_sensor_events();

      if (!player_in_slam_mode && input_slam_key_pressed) {
        player_in_slam_mode = true;
        PlaySound(sound_jump);
      }

      if (input_gravity_key_pressed) {
        b3Shape_EnableSensorEvents(player_gravity_sensor_id,
                                   !b3Shape_AreSensorEventsEnabled(player_gravity_sensor_id));
      }

      if (input_gravity_shoot_key_pressed) {
        if (player_gravity_bodies.count) {
          b3BodyId body_id = player_gravity_bodies.items[0];
          fixed_array_remove_body_id(&player_gravity_bodies, body_id);

          b3Vec3 body_position = b3Body_GetPosition(body_id);
          b3Vec3 target_velocity = b3MulSV(player_gravity_shoot_strength,
                                           b3Normalize(b3Sub(player_aim, body_position)));
          b3Body_SetLinearVelocity(body_id, target_velocity);
          PlaySound(sound_pebble_throw);
        }
      }
      if (input_dash_key_pressed) {
          player_in_dash_mode = true;
          b3Vec3 player_position = b3Body_GetPosition(player_body_id);
          b3Vec3 dash = b3MulSV(player_dash_legth,
                                           b3Normalize(b3Sub(player_aim, player_position)));
          b3Vec3 final_position = b3Add(player_position, dash);
          player_dash_target = final_position;
          PlaySound(sound_dash);
      }
      player_move(&game_camera, dt);

      b3Vec3 player_position = b3Body_GetPosition(player_body_id);
      if (player_slam_finished) {
        player_slam_finished = false;
        PlaySound(sound_slam);

        b3HullData* cylinder = b3CreateCylinder(0.5f, player_slam_radius, 0.0f, 32);
        const b3Vec3* points = b3GetHullPoints(cylinder);
        b3ShapeProxy proxy = { points, cylinder->vertexCount, 0.0f };

        b3QueryFilter filter = b3DefaultQueryFilter();
        filter.categoryBits  = PHYSICS_CATEGORY_PLAYER_SLAM_FIELD;
        filter.maskBits      = PHYSICS_CATEGORY_ENEMY;

        b3World_CastShape(world_id, player_position, &proxy, b3Vec3_zero, filter, slam_shape_cast_fn, NULL);
        b3DestroyHull(cylinder);
      }

      player_update_gravity_objects_velocities(player_position);
      turrets_aim_and_shoot(dt);

      if (game_mode == GAME_MODE_GAME) {
        camera_follow_player(&game_camera, vec3_b3p_to_rl(player_position));
        player_aim = vec3_rl_to_b3v(camera_world_ray_cast(game_camera));
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
    SetShaderValue(mesh_shader, ligth_pos_loc, &light_camera.position, SHADER_UNIFORM_VEC3);

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
      DrawText(TextFormat("slow_mode: x%u", game_slow_mode), 10, text_offset, 32, RED);
      text_offset += 32;
      DrawText(
        TextFormat(
          "player sensor enabled: %s",
          b3Shape_AreSensorEventsEnabled(player_gravity_sensor_id) ? "yes" : "no"),
        10, text_offset, 32, RED);
      text_offset += 32;
      DrawText(TextFormat("player in dash: %s", player_in_dash_mode ? "yes" : "no"), 10, text_offset, 32, RED);
      text_offset += 32;
      DrawText(TextFormat("player in slam: %s", player_in_slam_mode ? "yes" : "no"), 10, text_offset, 32, RED);
      text_offset += 32;

      if (game_show_debug_ui) {
        rlImGuiBegin();
          bool open = true;
          igBegin("Editor window", &open, 0);
          igInputText("Level name", level_name, ARRAY_LEN(level_name), 0, NULL, NULL);

          if (igButton("Save level", (ImVec2_c){0})) {
            level_save();
          }

          if (igButton("Load level", (ImVec2_c){0})) {
            level_load();
          }

         if (igButton("Add wall", (ImVec2_c){0})) {
            wall_spawn((b3Vec3){0.0f,  1.0f, 0.0f}, (Vector3){1.0f, 1.0f, 1.0f}, BROWN);
          }
          if (igButton("Add enemy", (ImVec2_c){0})) {
            enemy_spawn((b3Vec3){0.0f, 1.0f, 0.0f}, (Vector3){1.0f, 1.0f, 1.0f}, (Color){200, 93, 82, 255});
          }
          if (igButton("Add pebble", (ImVec2_c){0})) {
            pebble_spawn((b3Vec3){0.0f, 1.0f, 0.0f}, MAROON);
          }
          if (igButton("Add turret", (ImVec2_c){0})) {
            turret_spawn((b3Vec3){0.0f, 1.0f, 0.0f}, BLACK);
          }
          if (igButton("Add spawn point", (ImVec2_c){0})) {
            player_spawn_point_spawn((b3Vec3){0.0f, 0.5f, 0.0f}, BLUE);
          }
          if (igButton("Add button", (ImVec2_c){0})) {
            button_spawn((b3Vec3){0.0f, 0.5f, 0.0f}, RED);
          }
          if (igButton("Add key", (ImVec2_c){0})) {
            key_spawn((b3Vec3){0.0f, 1.0f, 0.0f}, GOLD);
          }
          if (igButton("Add gate", (ImVec2_c){0})) {
            gate_spawn((b3Vec3){0.0f, 1.0f, 0.0f}, b3Quat_identity, GOLD);
          }

          if (selected_object) {
            switch (selected_object->type) {
              case OBJECT_TYPE_NONE: break;
              case OBJECT_TYPE_PLAYER: {
                IMGUI_DRAG_FLOAT(player_speed, 0.0f, 1000.0f);
                IMGUI_DRAG_FLOAT(player_friction, 0.0f, 30.0f);
                IMGUI_DRAG_FLOAT(player_gravity_shoot_strength, 50.0f, 200.0f);
                IMGUI_DRAG_FLOAT(player_dash_legth, 1.0f, 20.0f);
                IMGUI_DRAG_FLOAT_001(player_dash_time, 0.001f, 0.5f);
                igDragInt("dash damage", &player_dash_damage, 1, 1, 100, NULL, 0);
                IMGUI_DRAG_FLOAT_001(player_slam_up_time,   0.01f, 1.0f);
                IMGUI_DRAG_FLOAT_001(player_slam_hold_time, 0.01f, 1.0f);
                IMGUI_DRAG_FLOAT_001(player_slam_down_time, 0.01f, 1.0f);
                IMGUI_DRAG_FLOAT(player_slam_height, 1.0f, 100.0f);
                igDragInt("slam damage", &player_slam_damage, 1, 1, 100, NULL, 0);
                IMGUI_DRAG_FLOAT(player_slam_radius, 1.0f, 20.0f);
              } break;
              default: {
                const char *names[] = {
                  "OBJECT_TYPE_NONE",
                  "OBJECT_TYPE_PLAYER",
                  "OBJECT_TYPE_PLAYER_SPAWN_POINT",
                  "OBJECT_TYPE_WALL",
                  "OBJECT_TYPE_PEBBLE",
                  "OBJECT_TYPE_ENEMY",
                  "OBJECT_TYPE_TURRET",
                  "OBJECT_TYPE_BUTTON",
                  "OBJECT_TYPE_KEY",
                  "OBJECT_TYPE_GATE",
                };
                igCombo_Str_arr("type", &selected_object->type, names, ARRAY_LEN(names), 0);

                igValue_Int("id", selected_object->id);
                igValue_Int("body_id", *(u64*)&selected_object->body_id);
                igValue_Int("shape_id", *(u64*)&selected_object->shape_id);
                igValue_Int("sensor_shape_id", *(u64*)&selected_object->sensor_shape_id);

                b3Vec3 position = b3Body_GetPosition(selected_object->body_id);
                if (igDragFloat3("position", (f32*)&position, 0.5f, -100.0f, 100.0f, NULL, 0)) {
                  b3Body_SetTransform(selected_object->body_id, position,
                                      b3Body_GetRotation(selected_object->body_id));
                  if (selected_object->type == OBJECT_TYPE_GATE) {
                    b3Body_SetTransform(selected_object->ground_id, b3Sub(position, (b3Vec3){0.0f, 1.0f, 0.0f}),
                                        b3Body_GetRotation(selected_object->body_id));
                  }
                }
                if (selected_object->type == OBJECT_TYPE_WALL) {
                  if (igDragFloat3("scale", (f32*)&selected_object->scale, 1.0f, 0.0f, 100.0f, NULL, 0)) {
                    b3BoxHull box = b3MakeBoxHull(selected_object->scale.x / 2.0f,
                                                  selected_object->scale.y / 2.0f,
                                                  selected_object->scale.z / 2.0f);
                    b3Shape_SetHull(selected_object->shape_id, &box.base);
                  }
                }
                if (igDragFloat3("rotation", (f32*)&selected_object->rotation, 0.1f, -100.0f, 100.0f, NULL, 0)) {
                  b3Quat qx = b3MakeQuatFromAxisAngle((b3Vec3){1.0f, 0.0f, 0.0f}, selected_object->rotation.x);
                  b3Quat qy = b3MakeQuatFromAxisAngle((b3Vec3){0.0f, 1.0f, 0.0f}, selected_object->rotation.y);
                  b3Quat qz = b3MakeQuatFromAxisAngle((b3Vec3){0.0f, 0.0f, 1.0f}, selected_object->rotation.z);
                  b3Quat q = b3MulQuat(qx, b3MulQuat(qy, qz));
                  b3Body_SetTransform(selected_object->body_id, position, q);
                  if (selected_object->type == OBJECT_TYPE_GATE) {
                    b3Body_SetTransform(selected_object->ground_id,
                                        b3Sub(position, (b3Vec3){0.0f, 1.0f, 0.0f}),
                                        q);
                  }
                }
                IMGUI_EDIT_COLOR(selected_object->color);
                igDragInt("hp", &selected_object->hp, 1, 1, 100, NULL, 0);
                igDragInt("damage", &selected_object->damage, 1, 1, 100, NULL, 0);
                igCheckbox("disabled", &selected_object->disabled);
                igDragInt("activates_id", &selected_object->activates_id, 1, 1, 100, NULL, 0);

                if (igButton("Remove", (ImVec2_c){0})) {
                  object_destroy(selected_object);
                  selected_object = NULL;
                }
              } break;
            }
          }

          igEnd();
        rlImGuiEnd();
      }
    EndDrawing();
  }
  rlImGuiShutdown();
  CloseAudioDevice();
  CloseWindow();

  b3DestroyWorld(world_id);
  return 0;
}
