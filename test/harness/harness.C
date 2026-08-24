// harness.C — deterministic headless QA harness for XAsteroids (plan task 9, D17.3/B7).
//
// Driver: pre-flight -> Xvfb :99 (fixed geometry) -> game under XAST_SEED ->
// window find by WM_NAME "Asteroids" (XQueryTree walk — qa/capture-x11.sh's
// proven approach) -> scripted XTest input injected ONLY between stable frame
// boundaries -> client-area captures via XGetImage -> optional masked diff vs
// a reference directory -> exit code = pass/fail.
//
// Frame sync (--handshake / XAST_HANDSHAKE):
//   quiescence (default; PRE-endFrame tree): consecutive identical XGetImage
//     grabs mean the tree is stable; a boundary ticks once the tree has been
//     still >= tick-gap ms after the last observed change. Injections land in
//     the quiet tail of the 62.5ms frame period (frame work is a few ms), so
//     events are never consumed mid-frame. See test/harness/script-format.md.
//   counter (task 12+, D17.3/O5-M3): the game publishes a monotonically
//     increasing frame counter into _XAST_FRAME_COUNTER (CARDINAL) on its
//     window inside endFrame(); the harness samples exactly ONCE per published
//     value. A stalled or non-monotonic counter aborts as a hang/corruption.
//
// Build:  make harness            (produces obj/harness; links -lX11 -lXtst)
// Usage:  obj/harness --seed N --script file --out dir [--ref dir]
//         [--handshake quiescence|counter] [--display :99]
//         [--geometry 1280x1024x24] [--game ./XAsteroids]
//         [--hiscore qa/fixtures/hiScore.data] [--settle 10] [--poll-ms 10]
//         [--stability 3] [--tick-gap-ms 30] [--max-run 900] [--keep] [--quiet]
//
// Exit codes: 0 pass (or capture-only success); 1 pixel-diff failure;
//             2 infrastructure/environment failure.

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

// ------------------------------------------------------------------ config
struct Checkpoint {
    std::string name;
    std::string path;
    long boundary = 0;
    double tMs = 0;
    int w = 0, h = 0;
    bool hasDiff = false;
    double ae = -1;
    std::string maskPath = "-";
    bool pass = false;
};

struct Config {
    unsigned long seed = 1;
    std::string scriptPath;
    std::string outDir;
    std::string refDir;                 // empty => capture-only run
    std::string handshake;              // resolved from XAST_HANDSHAKE if empty
    std::string display = ":99";
    std::string geometry = "1280x1024x24";
    std::string gamePath;               // resolved against repo root
    std::string hiscoreFixture;         // copied into game cwd as hiScore.data
    int settleSecs = 10;
    int pollMs = 10;
    int stability = 3;
    int tickGapMs = 30;
    int idleEscapeMs = 150;             // stillness beyond any inter-frame gap => static screen
    int stallTimeoutMs = 5000;          // counter mode
    int maxRunSecs = 900;
    bool keepWork = false;
    bool quiet = false;
};

// Task-36 tiered-regime anchor: optional per-leg crop rects applied before
// the diff. The GL window geometry is font-metric-derived and differs from
// the X11 baseline (stb substitutions), so pixel-addressable comparison of
// the bitmap-content domain (the 640x512 play area) needs each leg's own
// origin. Format "X,Y,W,H"; empty => uncropped (legacy behavior).
struct CropRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool valid = false;
};
static CropRect g_mineCrop, g_refCrop;
static bool parseCropRect(const std::string& s, CropRect& r) {
    return sscanf(s.c_str(), "%d,%d,%d,%d", &r.x, &r.y, &r.w, &r.h) == 4 &&
           r.w > 0 && r.h > 0 && (r.valid = true);
}
static Config cfg;

static const char* GAME_WM_NAME       = "Asteroids";               // playingField.H:559
static const char* FRAME_COUNTER_PROP = "_XAST_FRAME_COUNTER";     // published by endFrame(), task 12+
static const char* FONT_FAMILIES[] = {                             // stage.H:134-138 (O5-N2)
    "white_shadow-48",
    "-schumacher-clean-bold-r-normal--10-100-75-75-c-60-iso8859-1",
    "-ibm-ergonomic-bold-r-normal--20-140-100-100-c-120-iso8859-9",
    "-urw-courier-bold-r-normal--40-300-100-100-m-240-iso8859-9",
    "-adobe-helvetica-bold-r-normal--14-100-100-100-p-82-iso8859-1"
};

// ------------------------------------------------------------------ logging
static void logLine(const char* tag, const std::string& msg) {
    fprintf(stderr, "[%s] %s\n", tag, msg.c_str());
    fflush(stderr);
}
#define LOG(msg)   logLine("harness", msg)
#define ERR(msg)   logLine("ERROR", msg)
[[noreturn]] static void die(const std::string& msg) {
    ERR("BLOCKED: " + msg);
    throw std::runtime_error(msg);
}

static double nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
static void sleepMs(int ms) { usleep(useconds_t(ms) * 1000u); }

static bool fileExists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
static bool dirExists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// ------------------------------------------------------------------ processes
static pid_t g_xvfbPid = -1, g_gamePid = -1;

static std::string findOnPath(const char* tool) {
    const char* path = ::getenv("PATH");
    if (!path) return "";
    std::string p(path), cand;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find(':', i);
        std::string dir = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (dir.empty()) dir = ".";
        cand = dir + "/" + tool;
        if (::access(cand.c_str(), X_OK) == 0) return cand;
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return "";
}

static bool displayAlive(const std::string& disp) {
    std::string cmd = "xdpyinfo -display " + disp + " >/dev/null 2>&1";
    return ::system(cmd.c_str()) == 0;
}

static void killChild(pid_t& pid, const char* name) {
    if (pid > 0) {
        ::kill(pid, SIGTERM);
        for (int i = 0; i < 30; ++i) {
            if (waitpid(pid, nullptr, WNOHANG) == pid) break;
            usleep(100000);
        }
        if (waitpid(pid, nullptr, WNOHANG) != pid) {
            ::kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        printf("[cleanup] killed %s pid %d\n", name, int(pid));
        fflush(stdout);
        pid = -1;
    }
}
static void cleanupAll() {
    killChild(g_gamePid, "XAsteroids");
    killChild(g_xvfbPid, "Xvfb");
}
static void onSignal(int) {
    cleanupAll();
    _exit(2);
}

// ------------------------------------------------------------------ X globals
static Display* g_dpy = nullptr;
static int g_screen = 0;
static Window g_win = None;
static unsigned g_clientW = 0, g_clientH = 0, g_border = 0, g_depth = 0;
static int g_absX = 0, g_absY = 0;    // client-area origin in root coords

static int xerrHandler(Display*, XErrorEvent* e) {
    char buf[256];
    XGetErrorText(e->display, e->error_code, buf, sizeof buf);
    fprintf(stderr, "[ERROR] X error %d (%s) request=%d\n",
            e->error_code, buf, int(e->request_code));
    return 0;
}

// ------------------------------------------------------------------ frames
struct Frame {
    XImage* img = nullptr;
    int w = 0, h = 0;
    void release() { if (img) { XDestroyImage(img); img = nullptr; } }
    ~Frame() { release(); }
    bool identicalTo(const Frame& o) const {
        return img && o.img && w == o.w && h == o.h &&
               img->bytes_per_line == o.img->bytes_per_line &&
               img->bits_per_pixel == o.img->bits_per_pixel &&
               ::memcmp(img->data, o.img->data,
                        size_t(img->bytes_per_line) * unsigned(h)) == 0;
    }
};

// Grab the CLIENT AREA: XGetGeometry's w/h exclude the border, so source (0,0)
// is the client origin — equivalent to capture-x11.sh's
// `xwd | convert -crop WxH+BW+BW`, without leaving the process.
static bool grabClient(Frame& f) {
    f.release();
    f.w = int(g_clientW);
    f.h = int(g_clientH);
    f.img = XGetImage(g_dpy, g_win, 0, 0, g_clientW, g_clientH, AllPlanes, ZPixmap);
    return f.img != nullptr;
}

static unsigned maskShift(unsigned long m) {
    unsigned s = 0;
    if (!m) return 0;
    while (!(m & 1ul)) { m >>= 1; ++s; }
    return s;
}
static unsigned maskBits(unsigned long m) {
    unsigned b = 0;
    while (m) { b += unsigned(m & 1ul); m >>= 1; }
    return b;
}

// Write an XImage as PNG through ImageMagick raw rgb: stdin (convert is
// already a hard dependency of the repo's QA tooling).
static bool writePng(const Frame& f, const std::string& path) {
    const XImage* im = f.img;
    if (!im) return false;
    const int w = f.w, h = f.h;
    std::vector<unsigned char> row(size_t(w) * 3u);

    const unsigned long rm = im->red_mask, gm = im->green_mask, bm = im->blue_mask;
    const unsigned rs = maskShift(rm), rbits = maskBits(rm);
    const unsigned gs = maskShift(gm), gbits = maskBits(gm);
    const unsigned bs = maskShift(bm), bbits = maskBits(bm);
    const int bpp = im->bits_per_pixel / 8;
    const bool msbFirst = (im->byte_order == MSBFirst);

    const std::string cmd = "convert -size " + std::to_string(w) + "x" + std::to_string(h) +
                            " -depth 8 rgb:- png:" + path;
    FILE* p = popen(cmd.c_str(), "w");
    if (!p) return false;
    bool ok = true;
    for (int y = 0; y < h && ok; ++y) {
        const unsigned char* line =
            reinterpret_cast<const unsigned char*>(im->data) + size_t(y) * im->bytes_per_line;
        for (int x = 0; x < w; ++x) {
            unsigned long v = 0;
            const unsigned char* px = line + size_t(x) * bpp;
            if (bpp >= 4)
                v = msbFirst ? (static_cast<unsigned long>(px[0]) << 24 | static_cast<unsigned long>(px[1]) << 16 |
                                static_cast<unsigned long>(px[2]) << 8  | px[3])
                             : (static_cast<unsigned long>(px[3]) << 24 | static_cast<unsigned long>(px[2]) << 16 |
                                static_cast<unsigned long>(px[1]) << 8  | px[0]);
            else if (bpp == 3)
                v = msbFirst ? (static_cast<unsigned long>(px[0]) << 16 | static_cast<unsigned long>(px[1]) << 8 | px[2])
                             : (static_cast<unsigned long>(px[2]) << 16 | static_cast<unsigned long>(px[1]) << 8 | px[0]);
            else
                v = *px;
            auto full = [](unsigned bits) -> unsigned long {
                return bits == 0 || bits >= 8 ? 255ul : ((1ul << bits) - 1ul) << (8 - bits);
            };
            row[size_t(x) * 3u + 0] = static_cast<unsigned char>(((v & rm) >> rs) * 255ul / (full(rbits) | 1ul));
            row[size_t(x) * 3u + 1] = static_cast<unsigned char>(((v & gm) >> gs) * 255ul / (full(gbits) | 1ul));
            row[size_t(x) * 3u + 2] = static_cast<unsigned char>(((v & bm) >> bs) * 255ul / (full(bbits) | 1ul));
        }
        if (fwrite(row.data(), 1, row.size(), p) != row.size()) ok = false;
    }
    const int rc = pclose(p);
    return ok && rc == 0 && fileExists(path);
}

// ------------------------------------------------------------------ script
struct Line {
    long target = 0;             // boundary index at which to execute
    std::string action, arg;
    int srcLine = 0;
};

static std::vector<Line> loadScript(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) die("cannot open script " + path);
    std::vector<Line> lines;
    long autoTarget = 0;
    char buf[1024];
    int n = 0;
    while (fgets(buf, sizeof buf, f)) {
        ++n;
        std::string s(buf);
        const size_t hc = s.find('#');
        if (hc != std::string::npos) s.resize(hc);
        // tolerate the spec's bracketed/comma form: [seq, action, arg]
        std::replace(s.begin(), s.end(), '[', ' ');
        std::replace(s.begin(), s.end(), ']', ' ');
        std::replace(s.begin(), s.end(), ',', ' ');
        std::vector<std::string> tok;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && isspace(unsigned(s[i]))) ++i;
            size_t j = i;
            while (j < s.size() && !isspace(unsigned(s[j]))) ++j;
            if (j > i) tok.push_back(s.substr(i, j - i));
            i = j;
        }
        if (tok.empty()) continue;
        Line L;
        L.srcLine = n;
        size_t ai = 0;
        if (isdigit(unsigned(tok[0][0]))) {
            L.target = strtol(tok[0].c_str(), nullptr, 10);
            ai = 1;
            if (L.target < autoTarget)
                die(std::string(path) + ":" + std::to_string(n) +
                    ": seq goes backwards (boundaries are monotonic)");
            autoTarget = L.target;
        } else {
            L.target = ++autoTarget;
        }
        if (ai >= tok.size())
            die(std::string(path) + ":" + std::to_string(n) + ": missing action");
        L.action = tok[ai++];
        if (L.action == "wait") {           // pure pacing directive
            if (ai >= tok.size())
                die(std::string(path) + ":" + std::to_string(n) + ": wait needs a count");
            autoTarget += strtol(tok[ai++].c_str(), nullptr, 10);
            L.action = "nop";
            L.arg = std::to_string(autoTarget);
        } else {
            while (ai < tok.size()) {
                if (!L.arg.empty()) L.arg += " ";
                L.arg += tok[ai++];
            }
        }
        lines.push_back(L);
    }
    fclose(f);
    if (lines.empty()) die("script " + path + " has no executable lines");
    LOG("script loaded: " + path + " (" + std::to_string(lines.size()) + " lines)");
    return lines;
}

// ------------------------------------------------------------------ input
static KeyCode keycodeForChar(const std::string& ch) {
    const std::string sym = (ch == "space") ? "space" : ch;
    const KeySym ks = XStringToKeysym(sym.c_str());
    if (ks == NoSymbol) die("no keysym for '" + ch + "'");
    const KeyCode kc = XKeysymToKeycode(g_dpy, ks);
    if (!kc) die("no keycode for keysym '" + sym + "' (US layout under Xvfb expected)");
    return kc;
}
static void injectKey(const std::string& ch, bool down) {
    const KeyCode kc = keycodeForChar(ch);
    XTestFakeKeyEvent(g_dpy, kc, down ? True : False, CurrentTime);
    XSync(g_dpy, False);            // server has queued the event before we return
}
static void warpPointer(int rootX, int rootY) {
    XTestFakeMotionEvent(g_dpy, g_screen, rootX, rootY, CurrentTime);
    XSync(g_dpy, False);
}
static void clientToRoot(int cx, int cy, int& rx, int& ry) {
    rx = g_absX + cx;
    ry = g_absY + cy;
}
static void injectButtonAt(int cx, int cy, unsigned button, bool press) {
    int rx = 0, ry = 0;
    clientToRoot(cx, cy, rx, ry);
    warpPointer(rx, ry);
    XTestFakeButtonEvent(g_dpy, button, press ? True : False, CurrentTime);
    XSync(g_dpy, False);
}

// ------------------------------------------------------------------ diff engine
// IM7 semantics verified empirically: `compare -metric AE [-read-mask M] a b null:`
// prints the count on stderr; mask WHITE pixels are compared, BLACK ignored.
// Pass iff the printed value is exactly 0.
static bool diffAgainstRef(const std::string& mine, const std::string& ref,
                           const std::string& mask, double& aeOut) {
    std::string cmd = "compare -metric AE ";
    if (mask != "-") cmd += "-read-mask " + mask + " ";
    cmd += mine + " " + ref + " null: 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { aeOut = -1; return false; }
    char buf[512] = {0};
    const size_t got = fread(buf, 1, sizeof buf - 1, p);
    pclose(p);
    if (!got) { aeOut = -1; return false; }
    char* end = nullptr;
    aeOut = strtod(buf, &end);
    return end && end != buf;
}

static bool cropPng(const std::string& src, const CropRect& r,
                    const std::string& dst) {
    const std::string cmd =
        "convert '" + src + "' -crop " + std::to_string(r.w) + "x" +
        std::to_string(r.h) + "+" + std::to_string(r.x) + "+" +
        std::to_string(r.y) + " +repage png:'" + dst + "'";
    return ::system(cmd.c_str()) == 0 && fileExists(dst);
}

// ------------------------------------------------------------------ manifest
static std::vector<Checkpoint> g_checkpoints;

static void writeManifest(const std::string& path, const std::string& workDir,
                          double runMs, int gameExitRc,
                          bool haveDiff, bool diffPass) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    struct stat st;
    fprintf(f, "# harness manifest (task 9, D17.3)\n");
    fprintf(f, "seed: %lu\n", cfg.seed);
    fprintf(f, "handshake: %s\n", cfg.handshake.c_str());
    fprintf(f, "display: %s\n", cfg.display.c_str());
    fprintf(f, "xvfb_geometry: %s\n", cfg.geometry.c_str());
    fprintf(f, "script: %s\n", cfg.scriptPath.c_str());
    fprintf(f, "binary: %s\n", cfg.gamePath.c_str());
    if (stat(cfg.gamePath.c_str(), &st) == 0)
        fprintf(f, "binary_size_mtime: %ld %ld\n", long(st.st_size), long(st.st_mtime));
    fprintf(f, "hiscore_fixture: %s\n",
            cfg.hiscoreFixture.empty() ? "-" : cfg.hiscoreFixture.c_str());
    fprintf(f, "game_cwd: %s\n", workDir.c_str());
    fprintf(f, "client_geometry: %ux%u border=%u depth=%u at +%d+%d\n",
            g_clientW, g_clientH, g_border, g_depth, g_absX, g_absY);
    fprintf(f, "sync_params: poll_ms=%d stability=%d tick_gap_ms=%d\n",
            cfg.pollMs, cfg.stability, cfg.tickGapMs);
    fprintf(f, "run_ms: %.0f\n", runMs);
    fprintf(f, "game_exit_rc: %d\n", gameExitRc);
    fprintf(f, "checkpoints: %zu\n", g_checkpoints.size());
    for (const auto& c : g_checkpoints)
        fprintf(f, "  cp %s boundary=%ld t=%.0fms %dx%d %s\n",
                c.name.c_str(), c.boundary, c.tMs, c.w, c.h, c.path.c_str());
    if (haveDiff) {
        fprintf(f, "diff_reference: %s\n", cfg.refDir.c_str());
        if (g_mineCrop.valid)
            fprintf(f, "mine_crop: %d,%d,%d,%d\n", g_mineCrop.x, g_mineCrop.y,
                    g_mineCrop.w, g_mineCrop.h);
        if (g_refCrop.valid)
            fprintf(f, "ref_crop: %d,%d,%d,%d\n", g_refCrop.x, g_refCrop.y,
                    g_refCrop.w, g_refCrop.h);
        for (const auto& c : g_checkpoints)
            fprintf(f, "  diff %s AE=%.6f mask=%s %s\n", c.name.c_str(), c.ae,
                    c.maskPath.c_str(),
                    c.hasDiff ? (c.pass ? "PASS" : "FAIL") : "MISSING-REF");
        fprintf(f, "result: %s\n", diffPass ? "PASS" : "FAIL");
    } else {
        fprintf(f, "result: CAPTURED-ONLY (no --ref)\n");
    }
    fclose(f);
}

// ------------------------------------------------------------------ phases
static void preflight() {
    const char* tools[] = {"Xvfb", "xdpyinfo", "xlsfonts", "xwd", "convert", "compare"};
    std::vector<std::string> missing;
    for (const char* t : tools)
        if (findOnPath(t).empty()) missing.push_back(t);
    const char* fontsDir = ::getenv("XAST_FONTS");
    const char* home = ::getenv("HOME");
    std::string fd = fontsDir ? std::string(fontsDir)
                              : std::string(home ? home : "") + "/.local/xast-env/fonts";
    if (!dirExists(fd)) missing.push_back("font dir " + fd);
    if (!missing.empty()) {
        std::string m = "environment pre-flight failed, missing:";
        for (const auto& x : missing) m += " " + x;
        die(m + " (source qa/env/env.sh first?)");
    }
    LOG("pre-flight OK: Xvfb/xdpyinfo/xlsfonts/xwd/convert/compare + fonts dir " + fd);
}

static void startXvfb() {
    if (displayAlive(cfg.display))
        die("display " + cfg.display + " already has an X server — pick another --display");
    std::string dispNum = cfg.display;
    const size_t colon = dispNum.find(':');
    if (colon != std::string::npos) dispNum = dispNum.substr(colon + 1);
    const char* fontsDir = ::getenv("XAST_FONTS");
    const char* home = ::getenv("HOME");
    std::string fd = fontsDir ? std::string(fontsDir)
                              : std::string(home ? home : "") + "/.local/xast-env/fonts";
    const std::string prog = findOnPath("Xvfb");
    const pid_t pid = fork();
    if (pid < 0) die(std::string("fork: ") + strerror(errno));
    if (pid == 0) {
        setsid();
        const int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
        execl(prog.c_str(), "Xvfb", (":" + dispNum).c_str(), "-screen", "0",
              cfg.geometry.c_str(), "-fp", fd.c_str(), (char*)nullptr);
        _exit(127);
    }
    g_xvfbPid = pid;
    bool up = false;
    for (int i = 0; i < 50 && !up; ++i) up = displayAlive(cfg.display), sleepMs(200);
    if (!up) die("Xvfb " + cfg.display + " did not come up");
    LOG("Xvfb " + cfg.display + " up (" + cfg.geometry + ", fp=" + fd + ")");
}

static void fontPreflight() {
    for (const char* fam : FONT_FAMILIES) {
        std::string cmd = "xlsfonts -display " + cfg.display + " -fn '" +
                          std::string(fam) + "' 2>/dev/null";
        FILE* p = popen(cmd.c_str(), "r");
        char buf[64] = {0};
        const size_t got = p ? fread(buf, 1, sizeof buf - 1, p) : 0;
        if (p) pclose(p);
        if (!got) die(std::string("font family not resolvable on ") + cfg.display +
                      ": " + fam + " (stage.H:134-138; rebuild ~/.local/xast-env/fonts)");
    }
    LOG("font pre-flight OK: all 5 stage.H:134-138 families resolve");
}

static std::string makeWorkDir() {
    std::string tpl = (::getenv("TMPDIR") ? ::getenv("TMPDIR") : "/tmp");
    if (!tpl.empty() && tpl.back() == '/') tpl.pop_back();
    tpl += "/xast-harness.XXXXXX";
    std::vector<char> buf(tpl.begin(), tpl.end());
    buf.push_back('\0');
    char* dir = mkdtemp(buf.data());
    if (!dir) die(std::string("mkdtemp: ") + strerror(errno));
    return std::string(dir);
}

static void launchGame(const std::string& gameAbs, const std::string& workDir) {
    const std::string gameLog = workDir + "/game.log";
    const pid_t pid = fork();
    if (pid < 0) die(std::string("fork: ") + strerror(errno));
    if (pid == 0) {
        setenv("DISPLAY", cfg.display.c_str(), 1);
        setenv("XAST_SEED", std::to_string(cfg.seed).c_str(), 1);
        if (chdir(workDir.c_str()) != 0) _exit(126);
        const int fd = open(gameLog.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        execl(gameAbs.c_str(), "XAsteroids", (char*)nullptr);
        _exit(127);
    }
    g_gamePid = pid;
    LOG("game pid " + std::to_string(pid) + " (DISPLAY=" + cfg.display +
        " XAST_SEED=" + std::to_string(cfg.seed) + ", cwd=" + workDir + ")");
}

static Window findWindowByName(Display* d, Window w, const char* name) {
    if (w != DefaultRootWindow(d)) {
        char* wn = nullptr;
        if (XFetchName(d, w, &wn)) {
            const bool match = wn && (::strcmp(wn, name) == 0);
            if (wn) XFree(wn);
            if (match) return w;
        }
    }
    Window root = None, parent = None, *children = nullptr;
    unsigned n = 0;
    if (!XQueryTree(d, w, &root, &parent, &children, &n)) return None;
    Window found = None;
    for (unsigned i = 0; i < n && found == None; ++i)
        found = findWindowByName(d, children[i], name);
    if (children) XFree(children);
    return found;
}

static void findGameWindow(int settleSecs) {
    const double deadline = nowMs() + (settleSecs + 40) * 1000.0;
    while (true) {
        g_win = findWindowByName(g_dpy, DefaultRootWindow(g_dpy), GAME_WM_NAME);
        if (g_win != None) {
            Window rootReturn = None, child = None;
            int x = 0, y = 0;
            unsigned w = 0, h = 0, bw = 0, depth = 0;
            if (XGetGeometry(g_dpy, g_win, &rootReturn, &x, &y, &w, &h, &bw, &depth) &&
                w > 50 && h > 50) {
                g_clientW = w;
                g_clientH = h;
                g_border = bw;
                g_depth = depth;
                XTranslateCoordinates(g_dpy, g_win, DefaultRootWindow(g_dpy),
                                      0, 0, &g_absX, &g_absY, &child);
                LOG("window 0x" + std::to_string(long(g_win)) + " \"" + GAME_WM_NAME +
                    "\" client " + std::to_string(w) + "x" + std::to_string(h) +
                    " border=" + std::to_string(bw) + " depth=" + std::to_string(depth) +
                    " at +" + std::to_string(g_absX) + "+" + std::to_string(g_absY));
                return;
            }
            g_win = None;
        }
        if (g_gamePid > 0 && waitpid(g_gamePid, nullptr, WNOHANG) == g_gamePid)
            die("game exited before its window appeared (see game.log in the work dir)");
        if (nowMs() > deadline)
            die("no window named '" + std::string(GAME_WM_NAME) + "' appeared");
        sleepMs(500);
    }
}

// ------------------------------------------------------------------ execution
struct RunStats {
    long boundaries = 0;
    double runMs = 0;
    int gameExitRc = -1;
    bool gameExited = false;
};

static void doCapture(const std::string& name, long boundary, Frame& scratch) {
    if (!grabClient(scratch)) die("XGetImage failed during capture '" + name + "'");
    const std::string path = cfg.outDir + "/" + name + ".png";
    if (!writePng(scratch, path)) die("PNG write failed for " + path);
    Checkpoint c;
    c.name = name;
    c.path = path;
    c.boundary = boundary;
    c.tMs = nowMs();
    c.w = scratch.w;
    c.h = scratch.h;
    g_checkpoints.push_back(c);
    LOG("captured " + name + ".png (" + std::to_string(c.w) + "x" + std::to_string(c.h) +
        ") @boundary " + std::to_string(boundary));
}

static void executeAction(const Line& L, long boundary, Frame& scratch,
                          bool& stopNow, bool& waitForExit) {
    const std::string& a = L.action;
    if (a == "nop") return;
    if (a == "keydown" || a == "keyup") {
        if (L.arg.empty()) die("line " + std::to_string(L.srcLine) + ": " + a + " needs a char");
        injectKey(L.arg, a == "keydown");
        return;
    }
    if (a == "mousedown" || a == "mouseup") {
        int cx = 0, cy = 0;
        unsigned btn = Button1;
        if (sscanf(L.arg.c_str(), "%d %d %u", &cx, &cy, &btn) < 2)
            die("line " + std::to_string(L.srcLine) + ": " + a + " needs \"X Y [BTN]\"");
        injectButtonAt(cx, cy, btn, a == "mousedown");
        return;
    }
    if (a == "move") {
        int cx = 0, cy = 0;
        if (sscanf(L.arg.c_str(), "%d %d", &cx, &cy) < 2)
            die("line " + std::to_string(L.srcLine) + ": move needs \"X Y\"");
        int rx = 0, ry = 0;
        clientToRoot(cx, cy, rx, ry);
        warpPointer(rx, ry);
        return;
    }
    if (a == "enter") {                       // pointer into client center
        int rx = 0, ry = 0;
        clientToRoot(int(g_clientW) / 2, int(g_clientH) / 2, rx, ry);
        warpPointer(rx, ry);
        return;
    }
    if (a == "leave") {                       // pointer far outside the window
        warpPointer(g_absX > 120 ? g_absX - 100 : 2, g_absY > 120 ? g_absY - 100 : 2);
        return;
    }
    if (a == "resize") {                      // programmatic ConfigureNotify driver (Q15/R6)
        int w = 0, h = 0;
        if (sscanf(L.arg.c_str(), "%d %d", &w, &h) < 2)
            die("line " + std::to_string(L.srcLine) + ": resize needs \"W H\"");
        XResizeWindow(g_dpy, g_win, unsigned(w), unsigned(h));
        XSync(g_dpy, False);
        return;
    }
    if (a == "capture") {
        if (L.arg.empty()) die("line " + std::to_string(L.srcLine) + ": capture needs a name");
        doCapture(L.arg, boundary, scratch);
        return;
    }
    if (a == "exit") {
        stopNow = true;
        waitForExit = true;
        return;
    }
    die("line " + std::to_string(L.srcLine) + ": unknown action '" + a + "'");
}

// --- quiescence handshake --------------------------------------------------
//
// Boundary semantics (PRE-endFrame tree): a boundary ticks when a NEW visible
// change (any pixel differing from the previous grab) has settled — i.e. the
// harness has observed the k-th distinct rendered state of the frame buffer.
// That makes the boundary counter a DETERMINISTIC FUNCTION of the simulation
// trajectory (same seed => same renders => same change sequence), which plain
// wall-clock pacing could never guarantee: it is what lets two runs capture
// the same sim frame and inject inputs between the same frames. Static
// screens (help/hi-score) never change; there an idle escape ticks boundaries
// on a timer instead — safe because nothing simulates while the outer event
// loop blocks in XNextEvent, so any injection instant is equivalent there.
static void runQuiescence(const std::vector<Line>& lines, RunStats& stats) {
    size_t li = 0;
    long boundary = 0;
    Frame prev, cur, scratch;
    const double t0 = nowMs();

    if (!grabClient(prev)) die("initial XGetImage failed");
    double lastChangeAt = nowMs(), lastTickAt = 0;
    int stableRun = 0;
    bool freshChange = false;
    bool stopNow = false, waitForExit = false;

    while (true) {
        if (nowMs() - t0 > cfg.maxRunSecs * 1000.0)
            die("max run time exceeded (" + std::to_string(cfg.maxRunSecs) + "s)");
        if (g_gamePid > 0 && waitpid(g_gamePid, nullptr, WNOHANG) == g_gamePid) {
            g_gamePid = -1;                  // reaped; game self-exited ('q')
            stats.gameExited = true;
            LOG("game process exited");
            break;
        }
        sleepMs(cfg.pollMs);
        if (!grabClient(cur)) {
            // Window gone: benign iff the game self-exited ('q' / WM_DELETE).
            // Sanitizer builds stay alive for seconds INSIDE exit() (LSan
            // scan) AFTER the game destroyed its window, so poll for self-
            // exit instead of a single-shot WNOHANG (task 12 ASan gate).
            int st = 0;
            bool reaped = false;
            const double tGone = nowMs();
            while (nowMs() - tGone < 20000.0) {
                if (g_gamePid > 0 && waitpid(g_gamePid, &st, WNOHANG) == g_gamePid) {
                    reaped = true;
                    break;
                }
                sleepMs(cfg.pollMs);
            }
            if (reaped) {
                stats.gameExited = true;
                stats.gameExitRc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
                g_gamePid = -1;
                LOG("game process exited (rc=" + std::to_string(stats.gameExitRc) + ")");
                break;
            }
            die("XGetImage failed (window gone?)");
        }
        const bool changed = !cur.identicalTo(prev);
        if (changed) {
            freshChange = true;
            stableRun = 0;
            lastChangeAt = nowMs();
        } else {
            ++stableRun;
        }
        prev.release();
        std::swap(prev.img, cur.img);
        prev.w = cur.w;
        prev.h = cur.h;

        const double t = nowMs();
        const bool settledChange = freshChange && stableRun >= cfg.stability;
        const bool idleEscape = !freshChange &&
                                (t - lastChangeAt) >= cfg.idleEscapeMs &&
                                (t - lastTickAt) >= cfg.tickGapMs;
        if ((settledChange && (t - lastTickAt) >= cfg.tickGapMs) || idleEscape) {
            lastTickAt = t;
            freshChange = false;
            stableRun = 0;
            ++boundary;
            stats.boundaries = boundary;
            while (li < lines.size() && lines[li].target <= boundary) {
                if (lines[li].target < boundary)
                    LOG("note: line " + std::to_string(lines[li].srcLine) +
                        " fired late (target " + std::to_string(lines[li].target) +
                        " < boundary " + std::to_string(boundary) + ")");
                executeAction(lines[li], boundary, scratch, stopNow, waitForExit);
                ++li;
            }
            if (stopNow || li >= lines.size()) break;
        }
    }
    stats.runMs = nowMs() - t0;

    if (waitForExit && !stats.gameExited && g_gamePid > 0) {
        LOG("waiting up to 20s for natural game exit...");
        for (int i = 0; i < 200 && !stats.gameExited; ++i) {
            int st = 0;
            const pid_t r = waitpid(g_gamePid, &st, WNOHANG);
            if (r == g_gamePid) {
                stats.gameExited = true;
                stats.gameExitRc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
                g_gamePid = -1;
            } else sleepMs(100);
        }
        if (!stats.gameExited) LOG("game did not exit after 'exit'; will be terminated");
    }
    LOG("quiescence run done: " + std::to_string(stats.boundaries) + " boundaries in " +
        std::to_string(stats.runMs / 1000.0) + "s, checkpoints " +
        std::to_string(g_checkpoints.size()));
}

// --- published-frame-counter handshake (task 12+) ---------------------------
static void runCounter(const std::vector<Line>& lines, RunStats& stats) {
    const Atom prop = XInternAtom(g_dpy, FRAME_COUNTER_PROP, False);
    const Atom type = XInternAtom(g_dpy, "CARDINAL", False);
    size_t li = 0;
    long seen = -1;
    double lastAdvance = nowMs(), t0 = nowMs();
    Frame scratch;
    bool stopNow = false, waitForExit = false;

    LOG("counter mode: polling " + std::string(FRAME_COUNTER_PROP) +
        " (requires the task-12+ endFrame() publisher; stalls abort)");
    while (true) {
        if (nowMs() - t0 > cfg.maxRunSecs * 1000.0)
            die("max run time exceeded");
        unsigned char* data = nullptr;
        Atom actualType = None;
        int actualFormat = 0;
        unsigned long nitems = 0, bytesAfter = 0;
        const int st = XGetWindowProperty(g_dpy, g_win, prop, 0, 1, False, type,
                                          &actualType, &actualFormat,
                                          &nitems, &bytesAfter, &data);
        if (st == Success && actualType == type && actualFormat == 32 && nitems >= 1) {
            const long v = long(data[0]);
            XFree(data);
            if (v == seen)
                die("frame counter repeated value " + std::to_string(v) +
                    " (uniqueness violation — D17.3)");
            if (v < seen)
                die("frame counter went backwards: " + std::to_string(seen) + " -> " +
                    std::to_string(v));
            if (seen != -1 && v > seen + 1)
                LOG("warning: skipped counter values " + std::to_string(seen + 1) + ".." +
                    std::to_string(v - 1) + " (nearest-completed-frame matching)");
            seen = v;
            lastAdvance = nowMs();
            stats.boundaries = v;
            while (li < lines.size() && lines[li].target <= v) {
                executeAction(lines[li], v, scratch, stopNow, waitForExit);
                ++li;
            }
            if (stopNow || li >= lines.size()) break;
        } else if (data) {
            XFree(data);
        }
        if (nowMs() - lastAdvance > cfg.stallTimeoutMs)
            die("frame counter stalled > " + std::to_string(cfg.stallTimeoutMs) +
                "ms (hang) or never published (endFrame() absent on this tree)");
        if (g_gamePid > 0 && waitpid(g_gamePid, nullptr, WNOHANG) == g_gamePid) {
            g_gamePid = -1;
            stats.gameExited = true;
            break;
        }
        usleep(2000);
    }
    stats.runMs = nowMs() - t0;
    LOG("counter run done at frame " + std::to_string(seen));
}

// ------------------------------------------------------------------ main
int main(int argc, char** argv) {
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    // repo root inferred from the binary location (obj/../ = repo root)
    std::string repoRoot = ".";
    {
        char buf[4096];
        const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n > 0) {
            buf[n] = '\0';
            const std::string exepath(buf);
            const size_t slash = exepath.rfind('/');
            if (slash != std::string::npos) {
                const std::string dir = exepath.substr(0, slash);   // .../obj
                const size_t s2 = dir.rfind('/');
                if (s2 != std::string::npos) repoRoot = dir.substr(0, s2);
            }
        }
    }

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* what) -> std::string {
            if (i + 1 >= argc) die(a + " requires " + what);
            return argv[++i];
        };
        if (a == "--seed") cfg.seed = strtoul(need("N").c_str(), nullptr, 10);
        else if (a == "--script") cfg.scriptPath = need("file");
        else if (a == "--out") cfg.outDir = need("dir");
        else if (a == "--ref") cfg.refDir = need("dir");
        else if (a == "--handshake") cfg.handshake = need("mode");
        else if (a == "--display") cfg.display = need("disp");
        else if (a == "--geometry") cfg.geometry = need("WxHxD");
        else if (a == "--game") cfg.gamePath = need("path");
        else if (a == "--hiscore") cfg.hiscoreFixture = need("fixture");
        else if (a == "--mine-rect") { if (!parseCropRect(need("X,Y,W,H"), g_mineCrop)) die("bad --mine-rect"); }
        else if (a == "--ref-rect") { if (!parseCropRect(need("X,Y,W,H"), g_refCrop)) die("bad --ref-rect"); }
        else if (a == "--settle") cfg.settleSecs = atoi(need("secs").c_str());
        else if (a == "--poll-ms") cfg.pollMs = atoi(need("ms").c_str());
        else if (a == "--stability") cfg.stability = atoi(need("N").c_str());
        else if (a == "--tick-gap-ms") cfg.tickGapMs = atoi(need("ms").c_str());
        else if (a == "--idle-escape-ms") cfg.idleEscapeMs = atoi(need("ms").c_str());
        else if (a == "--stall-timeout-ms") cfg.stallTimeoutMs = atoi(need("ms").c_str());
        else if (a == "--max-run") cfg.maxRunSecs = atoi(need("secs").c_str());
        else if (a == "--keep") cfg.keepWork = true;
        else if (a == "--quiet") cfg.quiet = true;
        else die("unknown option " + a);
    }
    if (cfg.scriptPath.empty() || cfg.outDir.empty())
        die("usage: harness --seed N --script file --out dir [--ref dir] [options]");
    if (cfg.handshake.empty()) {
        const char* env = ::getenv("XAST_HANDSHAKE");
        cfg.handshake = env ? env : "quiescence";
    }
    if (cfg.handshake != "quiescence" && cfg.handshake != "counter")
        die("--handshake must be quiescence|counter");
    if (cfg.gamePath.empty()) cfg.gamePath = repoRoot + "/XAsteroids";
    if (!fileExists(cfg.gamePath))
        die("game binary not found: " + cfg.gamePath + " (make XAsteroids first)");

    try {
        preflight();
        if (!dirExists(cfg.outDir) && mkdir(cfg.outDir.c_str(), 0755) != 0)
            die("cannot create out dir " + cfg.outDir);
        if (!cfg.refDir.empty() && !dirExists(cfg.refDir))
            die("reference dir does not exist: " + cfg.refDir);

        startXvfb();
        g_dpy = XOpenDisplay(cfg.display.c_str());
        if (!g_dpy) die("cannot open display " + cfg.display);
        XSetErrorHandler(xerrHandler);
        g_screen = DefaultScreen(g_dpy);
        int ev = 0, er = 0, maj = 0, min = 0;
        if (!XTestQueryExtension(g_dpy, &ev, &er, &maj, &min))
            die("XTEST extension unavailable on " + cfg.display + " (libXtst missing?)");
        fontPreflight();

        const std::string workDir = makeWorkDir();
        LOG("work dir: " + workDir + (cfg.keepWork ? " (kept)" : ""));
        if (!cfg.hiscoreFixture.empty()) {
            if (!fileExists(cfg.hiscoreFixture))
                die("hi-score fixture not found: " + cfg.hiscoreFixture);
            const std::string dst = workDir + "/hiScore.data";
            const std::string cmd = "cp '" + cfg.hiscoreFixture + "' '" + dst + "'";
            if (::system(cmd.c_str()) != 0) die("cannot stage hi-score fixture copy");
            LOG("staged " + cfg.hiscoreFixture + " as " + dst +
                " (score.H cwd candidate picks it up)");
        }

        launchGame(cfg.gamePath, workDir);
        LOG("settling " + std::to_string(cfg.settleSecs) + "s for static init");
        sleepMs(cfg.settleSecs * 1000);
        findGameWindow(cfg.settleSecs);

        // Park the pointer INSIDE the client area and focus the window:
        // prevents stray LeaveNotify (the game PAUSES on LeaveNotify during
        // play, playingField.H:348 nested loop) and mirrors capture-x11.sh's
        // XSetInputFocus-before-injection pattern.
        XSetInputFocus(g_dpy, g_win, RevertToParent, CurrentTime);
        {
            int rx = 0, ry = 0;
            clientToRoot(int(g_clientW) / 2, int(g_clientH) / 2, rx, ry);
            warpPointer(rx, ry);
        }
        XSync(g_dpy, False);
        sleepMs(300);

        const std::vector<Line> lines = loadScript(cfg.scriptPath);
        RunStats stats;
        if (cfg.handshake == "counter") runCounter(lines, stats);
        else                            runQuiescence(lines, stats);

        // ---- diff phase
        bool haveDiff = false, diffPass = true;
        if (!cfg.refDir.empty()) {
            haveDiff = true;
            LOG("diff vs reference " + cfg.refDir + ":");
            for (auto& c : g_checkpoints) {
                const std::string ref = cfg.refDir + "/" + c.name + ".png";
                const std::string mask = cfg.refDir + "/" + c.name + ".mask.png";
                if (!fileExists(ref)) {
                    ERR("missing reference for checkpoint '" + c.name + "': " + ref);
                    diffPass = false;
                    continue;
                }
                // Task-36: per-leg crop anchoring (see CropRect note).
                std::string mineCmp = c.path, refCmp = ref;
                if (g_mineCrop.valid) {
                    mineCmp = cfg.outDir + "/.crop_mine_" + c.name + ".png";
                    if (!cropPng(c.path, g_mineCrop, mineCmp)) {
                        ERR("mine crop failed for " + c.name);
                        diffPass = false;
                        continue;
                    }
                }
                if (g_refCrop.valid) {
                    refCmp = cfg.outDir + "/.crop_ref_" + c.name + ".png";
                    if (!cropPng(ref, g_refCrop, refCmp)) {
                        ERR("ref crop failed for " + c.name);
                        diffPass = false;
                        continue;
                    }
                }
                if (fileExists(mask)) c.maskPath = mask;
                double ae = -1;
                if (!diffAgainstRef(mineCmp, refCmp, c.maskPath, ae)) {
                    ERR("compare failed for " + c.name);
                    diffPass = false;
                    continue;
                }
                c.hasDiff = true;
                c.ae = ae;
                c.pass = (ae == 0.0);
                if (!c.pass) diffPass = false;
                logLine(c.pass ? "diff-ok" : "DIFF-FAIL",
                        c.name + " AE=" + std::to_string(ae) + " mask=" + c.maskPath);
            }
        }

        writeManifest(cfg.outDir + "/manifest.txt", workDir, stats.runMs,
                      stats.gameExitRc, haveDiff, diffPass);
        LOG("manifest: " + cfg.outDir + "/manifest.txt");

        cleanupAll();
        if (!cfg.keepWork) {
            const std::string cmd = "rm -rf '" + workDir + "'";
            ::system(cmd.c_str());
        }
        if (haveDiff) {
            printf("%s\n", diffPass ? "RESULT: PASS (all checkpoints AE=0)"
                                    : "RESULT: FAIL (pixel diffs above)");
            return diffPass ? 0 : 1;
        }
        printf("RESULT: captured %zu checkpoints (no --ref)\n", g_checkpoints.size());
        return 0;
    } catch (const std::exception& e) {
        ERR(std::string("fatal: ") + e.what());
        cleanupAll();
        return 2;
    }
}
