zig cc \
  game.c \
  -o game \
  -I$RAYLIB_INCLUDE_PATH \
  -L$RAYLIB_LIBRARY_PATH \
  -lraylib \
  -I./box3d/include \
  -L./box3d/build/src \
  -lbox3d \
  -lm
