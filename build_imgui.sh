mkdir -p imgui_build

cd imgui_build

zig c++ \
  -c \
  -O0 \
  -lstdc++ \
  -I../cimgui/ \
  -I../cimgui/imgui \
  -I../raylib/src \
  -I../rlImGui \
  ../cimgui/imgui/imgui.cpp \
  ../cimgui/imgui/imgui_widgets.cpp \
  ../cimgui/imgui/imgui_tables.cpp \
  ../cimgui/imgui/imgui_draw.cpp \
  ../cimgui/imgui/imgui_demo.cpp \
  ../cimgui/cimgui.cpp \
  ../rlImGui/rlImGui.cpp \

zig ar rcs libimgui.a *.o
