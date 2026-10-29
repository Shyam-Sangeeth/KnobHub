// Knob OS simulator: runs the real Knob OS interface in a window on the PC, at 2x so it's easy to see, as one of the
// boards Knob OS knows (program.exe --board crowpanel-1.46; the 1.28" CrowPanel by default). The mouse stands in for
// what the knob has, nothing more:
//   Turn:  mouse wheel
//   Press: middle click; held for half a second it's the knob's long press (back) instead
//   Touch: left mouse button (tap, swipe; swipe right = back)
// Knob Hub (or any test client) connects to 127.0.0.1:5718 and exchanges one JSON message per line, exactly like
// the knob over USB.
#define SDL_MAIN_HANDLED
#include <winsock2.h>
#include <ws2tcpip.h>

#include <SDL2/SDL.h>
#include <lvgl.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

#include "core/hal.h"
#include "core/knob_os.h"

namespace {

constexpr unsigned short LinkPort = 5718;

lv_display_t* display = nullptr;
std::vector<KnobInput> pending;  // collected from SDL events, handled in the main loop
bool window_moved = false;       // the window was moved: save where it is (in the main loop)

// The knob's button: a press when let go quickly, back (the long press) once held long enough, never both.
constexpr uint32_t LongPressMs = 500;
struct KnobButton {
    bool down = false;
    bool long_sent = false;
    uint32_t since = 0;
} button;

void button_down() {
    if (button.down) return;
    button = {true, false, SDL_GetTicks()};
}

void button_up() {
    if (!button.down) return;
    if (!button.long_sent) pending.push_back(KnobInput::Press);
    button.down = false;
}

struct TouchReport {
    int x, y;
    bool down;
};
std::vector<TouchReport> touch_reports;  // the mouse as the knob's touch screen, reported to Knob Hub

int SDLCALL watch_input(void*, SDL_Event* e) {
    switch (e->type) {
        case SDL_WINDOWEVENT:
            if (e->window.event == SDL_WINDOWEVENT_MOVED) window_moved = true;
            break;
        case SDL_MOUSEWHEEL:
            if (e->wheel.y != 0) pending.push_back(e->wheel.y < 0 ? KnobInput::TurnRight : KnobInput::TurnLeft);
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (e->button.button == SDL_BUTTON_MIDDLE) button_down();
            else if (e->button.button == SDL_BUTTON_LEFT) touch_reports.push_back({e->button.x, e->button.y, true});
            break;
        case SDL_MOUSEBUTTONUP:
            if (e->button.button == SDL_BUTTON_LEFT) touch_reports.push_back({e->button.x, e->button.y, false});
            else if (e->button.button == SDL_BUTTON_MIDDLE) button_up();
            break;
        case SDL_MOUSEMOTION:
            if (e->motion.state & SDL_BUTTON_LMASK) touch_reports.push_back({e->motion.x, e->motion.y, true});
            break;
        default: break;
    }
    return 0;
}

// --- The link: a local TCP socket standing in for the knob's USB serial ---

SOCKET listener = INVALID_SOCKET;
SOCKET client = INVALID_SOCKET;
std::string received;

// The board the simulator pretends to be: --board <id> (see core/boards.cpp), the 1.28" CrowPanel by default.
const Board* board = nullptr;

void set_title(bool connected) {
    std::string title = std::string("Knob OS simulator (") + board->name + ") - " +
                        (connected ? "Knob Hub connected" : "waiting for Knob Hub on 127.0.0.1:5718");
    lv_sdl_window_set_title(display, title.c_str());
}

void link_start() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LinkPort);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(listener, 1) != 0) {
        std::printf("Knob OS simulator: port %u is busy, so Knob Hub can't connect.\n", LinkPort);
        closesocket(listener);
        listener = INVALID_SOCKET;
        return;
    }
    u_long nonblocking = 1;
    ioctlsocket(listener, FIONBIO, &nonblocking);
    std::printf("Knob OS simulator: waiting for Knob Hub on 127.0.0.1:%u\n", LinkPort);
}

void link_drop() {
    closesocket(client);
    client = INVALID_SOCKET;
    received.clear();
    std::printf("Knob OS simulator: Knob Hub disconnected\n");
    set_title(false);
    knob_os_link_connected(false);
}

void link_poll() {
    if (listener == INVALID_SOCKET) return;
    if (client == INVALID_SOCKET) {
        client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        u_long nonblocking = 1;
        ioctlsocket(client, FIONBIO, &nonblocking);
        std::printf("Knob OS simulator: Knob Hub connected\n");
        set_title(true);
        knob_os_link_connected(true);
        return;
    }
    char buffer[4096];
    while (true) {
        int n = recv(client, buffer, sizeof buffer, 0);
        if (n > 0) {
            received.append(buffer, n);
            continue;
        }
        if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
            link_drop();
            return;
        }
        break;
    }
    size_t newline;
    while ((newline = received.find('\n')) != std::string::npos) {
        std::string line = received.substr(0, newline);
        received.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) knob_os_link_line(line.c_str());
        if (client == INVALID_SOCKET) return;
    }
}

// The window opens where it was last put (kept in %LOCALAPPDATA%\KnobHub\simulator-window.txt), or the first time on a
// monitor other than the main one (the main one is often busy with a game).
std::string window_folder() {
    const char* local = std::getenv("LOCALAPPDATA");
    return local ? std::string(local) + "/KnobHub" : std::string();
}

std::string window_file() {
    std::string folder = window_folder();
    return folder.empty() ? folder : folder + "/simulator-window.txt";
}

void save_window_position() {
    SDL_Window* window = SDL_GetWindowFromID(1);
    std::string path = window_file();
    if (!window || path.empty()) return;
    int x, y;
    SDL_GetWindowPosition(window, &x, &y);
    CreateDirectoryA(window_folder().c_str(), nullptr);
    if (FILE* f = std::fopen(path.c_str(), "w")) {
        std::fprintf(f, "%d %d\n", x, y);
        std::fclose(f);
    }
}

void place_window() {
    SDL_Window* window = SDL_GetWindowFromID(1);  // LVGL's window, the only one
    if (!window) return;
    int w, h;
    SDL_GetWindowSize(window, &w, &h);

    // Where it was, if that's still on a monitor.
    std::string path = window_file();
    if (FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "r")) {
        int x, y;
        bool read = std::fscanf(f, "%d %d", &x, &y) == 2;
        std::fclose(f);
        for (int i = 0; read && i < SDL_GetNumVideoDisplays(); i++) {
            SDL_Rect usable;
            if (SDL_GetDisplayUsableBounds(i, &usable) != 0) continue;
            if (x + w > usable.x && x < usable.x + usable.w && y + h / 2 > usable.y && y < usable.y + usable.h) {
                SDL_SetWindowPosition(window, x, y);
                return;
            }
        }
    }

    for (int i = 0; i < SDL_GetNumVideoDisplays(); i++) {
        SDL_Rect bounds, usable;
        if (SDL_GetDisplayBounds(i, &bounds) != 0 || SDL_GetDisplayUsableBounds(i, &usable) != 0) continue;
        if (bounds.x == 0 && bounds.y == 0) continue;  // the main monitor starts at (0, 0)
        SDL_SetWindowPosition(window, usable.x + (usable.w - w) / 2, usable.y + (usable.h - h) / 2);
        return;
    }
}

// --- Brightness: a dark layer over everything ---

lv_obj_t* dimmer = nullptr;

}  // namespace

const Board& hal_board() { return *board; }

// The simulator has no motors: it prints what they'd do, in the log window.
void hal_motor_feel(const haptics::Feel& feel, int strength) {
    static const char* const Detents[] = {"fine", "medium", "firm"};
    std::printf("motor: %d positions, at %d, %s, %s detents, %d%%\n", feel.positions, feel.position,
                feel.wrap ? "wraps" : feel.bounded() ? "end stops" : "endless", Detents[static_cast<int>(feel.detent)],
                strength);
    std::fflush(stdout);
}

void hal_vibrate(haptics::Effect effect, int strength) {
    static const char* const Effects[] = {"tick", "bump", "click", "alert"};
    std::printf("vibration: %s, %d%%\n", Effects[static_cast<int>(effect)], strength);
    std::fflush(stdout);
}

void hal_set_leds(const uint32_t* colors, int count, int arc_start, int arc_sweep) {
    std::printf("leds:");
    for (int i = 0; i < count; i++) std::printf(" #%06X", static_cast<unsigned>(colors[i]));
    std::printf(" (arc from %d, %d degrees)\n", arc_start, arc_sweep);
    std::fflush(stdout);
}

bool hal_local_time(std::tm& out) {
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    if (!local) return false;
    out = *local;
    return true;
}

void hal_link_send(const char* line) {
    if (client == INVALID_SOCKET) return;
    std::string message = std::string(line) + "\n";
    size_t sent = 0;
    for (int tries = 0; sent < message.size() && tries < 200; tries++) {
        int n = send(client, message.data() + sent, static_cast<int>(message.size() - sent), 0);
        if (n > 0) sent += n;
        else if (WSAGetLastError() == WSAEWOULDBLOCK) SDL_Delay(1);
        else break;
    }
}

// The simulator's flash: files in %LOCALAPPDATA%\KnobHub\Simulator.
static std::string store_path(const char* name) {
    std::string folder = window_folder();
    return folder.empty() ? folder : folder + "/Simulator/" + name;
}

bool hal_store_read(const char* name, std::vector<uint8_t>& out) {
    std::string path = store_path(name);
    FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    uint8_t buffer[4096];
    size_t n;
    while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) out.insert(out.end(), buffer, buffer + n);
    std::fclose(f);
    return true;
}

void hal_store_write(const char* name, const std::vector<uint8_t>& data) {
    std::string path = store_path(name);
    if (path.empty()) return;
    CreateDirectoryA(window_folder().c_str(), nullptr);
    CreateDirectoryA((window_folder() + "/Simulator").c_str(), nullptr);
    // Written aside, then swapped in, so a crash mid-write leaves the old file.
    std::string temp = path + ".tmp";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fclose(f) == 0 && ok;
    if (ok) MoveFileExA(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void hal_set_brightness(int percent) {
    if (!dimmer) {
        dimmer = lv_obj_create(lv_layer_sys());
        lv_obj_remove_style_all(dimmer);
        lv_obj_set_size(dimmer, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(dimmer, lv_color_black(), 0);
        lv_obj_remove_flag(dimmer, LV_OBJ_FLAG_CLICKABLE);
    }
    int opa = (100 - percent) * 200 / 100;  // 100% = clear, 5% = nearly dark
    lv_obj_set_style_bg_opa(dimmer, static_cast<lv_opa_t>(opa < 0 ? 0 : opa), 0);
}

// The log window: when it was opened just for the simulator (started from Explorer or another app), minimised, and
// on the simulator's monitor when you open it. Left alone when the simulator runs inside a terminal you already have
// open.
void tuck_away_own_console() {
    DWORD processes[2];
    HWND console = GetConsoleWindow();
    if (!console || GetConsoleProcessList(processes, 2) != 1) return;
    WINDOWPLACEMENT placement = {sizeof placement};
    if (!GetWindowPlacement(console, &placement)) return;
    if (SDL_Window* window = SDL_GetWindowFromID(1)) {
        int x, y;
        SDL_GetWindowPosition(window, &x, &y);
        MONITORINFO monitor = {sizeof monitor};
        if (GetMonitorInfoA(MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTONEAREST), &monitor)) {
            // Its normal (restored) place: centred on that monitor's work area.
            RECT& r = placement.rcNormalPosition;
            const RECT& work = monitor.rcWork;
            int w = r.right - r.left, h = r.bottom - r.top;
            r.left = work.left + (work.right - work.left - w) / 2;
            r.top = work.top + (work.bottom - work.top - h) / 2;
            r.right = r.left + w;
            r.bottom = r.top + h;
        }
    }
    placement.showCmd = SW_SHOWMINNOACTIVE;
    SetWindowPlacement(console, &placement);
}

int main(int argc, char** argv) {
    int count = 0;
    board = &boards::all(count)[0];
    for (int i = 1; i + 1 < argc; i++) {
        if (std::string(argv[i]) != "--board") continue;
        board = boards::find(argv[i + 1]);
        if (!board) {
            std::fprintf(stderr, "Unknown board %s. Boards:", argv[i + 1]);
            for (int b = 0; b < count; b++) std::fprintf(stderr, " %s", boards::all(count)[b].id);
            std::fprintf(stderr, "\n");
            return 1;
        }
    }

    SDL_SetMainReady();
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return SDL_GetTicks(); });

    // SDL's Direct3D 11 renderer washes the colours out on some PCs (0x07080A shows as 0x1D2129); software is exact.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    display = lv_sdl_window_create(board->size, board->size);
    lv_sdl_window_set_zoom(display, 2);
    // The knob's screen is a fixed size: resizing would make LVGL's display bigger than the knob's, putting touches
    // (and what's mirrored to Knob Hub) in the wrong place.
    if (SDL_Window* window = SDL_GetWindowFromID(1)) SDL_SetWindowResizable(window, SDL_FALSE);
    place_window();
    tuck_away_own_console();
    SDL_AddEventWatch(watch_input, nullptr);

    link_start();
    set_title(false);
    knob_os_start();
    // Touch. After Knob OS, which adds a pointer of its own (Knob Hub's preview touches): LVGL's SDL driver feeds the
    // mouse to the newest pointer device.
    lv_sdl_mouse_create();

    while (true) {
        uint32_t wait = lv_timer_handler();
        if (button.down && !button.long_sent && SDL_GetTicks() - button.since >= LongPressMs) {
            button.long_sent = true;
            pending.push_back(KnobInput::Back);
        }
        std::vector<KnobInput> inputs;
        inputs.swap(pending);
        for (KnobInput input : inputs) knob_os_input(input);
        std::vector<TouchReport> touches;
        touches.swap(touch_reports);
        int zoom = lv_sdl_window_get_zoom(display);
        for (const TouchReport& t : touches) knob_os_report_touch(t.x / zoom, t.y / zoom, t.down);
        link_poll();
        if (window_moved) {
            window_moved = false;
            save_window_position();
        }
        SDL_Delay(wait < 5 ? 5 : (wait > 20 ? 20 : wait));
    }
}
