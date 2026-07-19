mkdir -p web
cp index.html web
cd web

emcc \
  -DRELEASE \
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
  -DPLATFORM_WEB \
  -s MAX_WEBGL_VERSION=2 \
  ../game.c \
  ../raylib/src/libraylib.a \
  ../box3d/build_web/src/libbox3d.a \
  -sFORCE_FILESYSTEM=1 \
  --embed-file ../shaders@/shaders \
  --embed-file ../levels@/levels \
  -o game.js
