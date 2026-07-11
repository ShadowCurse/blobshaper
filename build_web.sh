mkdir -p web
cd web

emcc \
  -Os \
  -Wall \
  -I.. \
  -I../raylib/src \
  -I../box3d/include \
  -L.. \
  -L../raylib/src \
  -I../box3d/build_web/src \
  -s USE_GLFW=3 \
  -s ASYNCIFY \
  --shell-file ../raylib/src/shell.html \
  -DPLATFORM_WEB \
  -s MAX_WEBGL_VERSION=2 \
  ../game.c \
  ../raylib/src/libraylib.a \
  ../box3d/build_web/src/libbox3d.a \
  -o game.html \
