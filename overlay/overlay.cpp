/*
    Reads game memory and draws on a transparent window
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

#include "../uma/uma.h"

struct Vec3 {
  float x, y, z;
};

struct Entity {
  Vec3 position;
  float health;
  int team;
};

class Overlay {
private:
  Display *display;
  Window window;
  int screen;
  cairo_surface_t *surface;
  cairo_t *cr;
  int width, height;

public:
  Overlay() {
    display = XOpenDisplay(nullptr);
    if (!display) {
      throw std::runtime_error("Cannot open X display");
    }

    screen = DefaultScreen(display);
    width = DisplayWidth(display, screen);
    height = DisplayHeight(display, screen);

    // Create transparent window
    XVisualInfo vinfo;
    XMatchVisualInfo(display, screen, 32, TrueColor, &vinfo);

    XSetWindowAttributes attr;
    attr.colormap = XCreateColormap(display, DefaultRootWindow(display),
                                    vinfo.visual, AllocNone);
    attr.border_pixel = 0;
    attr.background_pixel = 0;
    attr.override_redirect = True; // No window decorations

    window = XCreateWindow(
        display, DefaultRootWindow(display), 0, 0, width, height, 0,
        vinfo.depth, InputOutput, vinfo.visual,
        CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect, &attr);

    // Make click-through
    XRectangle rect = {0, 0, 0, 0};
    XserverRegion region = XFixesCreateRegion(display, &rect, 1);
    XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(display, region);

    // Always on top
    Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
    Atom wmAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(display, window, wmState, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&wmAbove, 1);

    XMapWindow(display, window);

    // Cairo for drawing
    surface =
        cairo_xlib_surface_create(display, window, vinfo.visual, width, height);
    cr = cairo_create(surface);
  }

  ~Overlay() {
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    XDestroyWindow(display, window);
    XCloseDisplay(display);
  }

  void clear() {
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  }

  void drawText(int x, int y, const char *text, float r, float g, float b) {
    cairo_set_source_rgb(cr, r, g, b);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, text);
  }

  void drawBox(int x, int y, int w, int h, float r, float g, float b) {
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 2);
    cairo_rectangle(cr, x, y, w, h);
    cairo_stroke(cr);
  }

  void drawHealthBar(int x, int y, int w, float health, float maxHealth) {
    float ratio = health / maxHealth;

    // Background
    cairo_set_source_rgba(cr, 0, 0, 0, 0.5);
    cairo_rectangle(cr, x, y, w, 4);
    cairo_fill(cr);

    // Health bar (green to red based on health)
    cairo_set_source_rgb(cr, 1.0 - ratio, ratio, 0);
    cairo_rectangle(cr, x, y, w * ratio, 4);
    cairo_fill(cr);
  }

  void drawLine(int x1, int y1, int x2, int y2, float r, float g, float b) {
    cairo_set_source_rgb(cr, r, g, b);
    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
  }

  void flush() {
    cairo_surface_flush(surface);
    XFlush(display);
  }

  int getWidth() const { return width; }
  int getHeight() const { return height; }
};

// Example: world to screen projection (simplified)
bool worldToScreen(const Vec3 &world, const Vec3 &camera, int screenW,
                   int screenH, int &sx, int &sy) {
  // This is a placeholder - real implementation needs view/projection matrices
  // from game
  float dx = world.x - camera.x;
  float dy = world.y - camera.y;
  float dz = world.z - camera.z;

  if (dz <= 0)
    return false; // Behind camera

  float fov = 90.0f;
  float scale = (screenW / 2.0f) / tanf(fov * 0.5f * M_PI / 180.0f);

  sx = screenW / 2 + (int)(dx / dz * scale);
  sy = screenH / 2 - (int)(dy / dz * scale);

  return (sx >= 0 && sx < screenW && sy >= 0 && sy < screenH);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <process_name>" << std::endl;
    std::cerr << "Example: " << argv[0] << " test.app" << std::endl;
    return 1;
  }

  try {
    Uma::Uma uma;
    uma.Attach(argv[1]);
    std::cout << "Attached to " << argv[1] << std::endl;

    Overlay overlay;
    std::cout << "Overlay created: " << overlay.getWidth() << "x"
              << overlay.getHeight() << std::endl;

    uintptr_t base = uma.ReadModule(argv[1]);
    std::cout << "Module base: 0x" << std::hex << base << std::dec << std::endl;

    // Example offsets - you'd find these by reversing your game
    // uintptr_t entityListOffset = 0x123456;
    // uintptr_t localPlayerOffset = 0x789ABC;

    while (true) {
      overlay.clear();

      // === READ GAME DATA HERE ===
      // Example (replace with real offsets):
      //
      // uintptr_t entityList = uma.ReadMemory<uintptr_t>(base +
      // entityListOffset); int entityCount = uma.ReadMemory<int>(base +
      // entityCountOffset);
      //
      // for (int i = 0; i < entityCount; i++) {
      //     uintptr_t entity = uma.ReadMemory<uintptr_t>(entityList + i * 8);
      //     Vec3 pos = uma.ReadMemory<Vec3>(entity + posOffset);
      //     float health = uma.ReadMemory<float>(entity + healthOffset);
      //
      //     int sx, sy;
      //     if (worldToScreen(pos, cameraPos, overlay.getWidth(),
      //     overlay.getHeight(), sx, sy)) {
      //         overlay.drawBox(sx - 20, sy - 40, 40, 80, 1, 0, 0);
      //         overlay.drawHealthBar(sx - 20, sy - 45, 40, health, 100);
      //         overlay.drawText(sx - 10, sy - 50,
      //         std::to_string((int)health).c_str(), 1, 1, 1);
      //     }
      // }

      // Demo: Draw some placeholder info
      char buf[256];
      snprintf(buf, sizeof(buf), "Base: 0x%lx", base);
      overlay.drawText(10, 30, buf, 0, 1, 0);
      overlay.drawText(10, 50, "Overlay active - reading memory...", 1, 1, 1);

      // Demo box in center
      int cx = overlay.getWidth() / 2;
      int cy = overlay.getHeight() / 2;
      overlay.drawBox(cx - 50, cy - 50, 100, 100, 1, 0, 0);
      overlay.drawHealthBar(cx - 50, cy + 55, 100, 75, 100);
      overlay.drawText(cx - 20, cy, "DEMO", 1, 1, 0);

      overlay.flush();

      std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
