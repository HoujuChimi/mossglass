// -----------------------------------------------------------------------------
//  Native Swamp Bottle - a tiny desktop pixel ecosystem (Windows / SDL2)
//
//  A 320x120 borderless, transparent-edged, always-on-top desktop widget.
//  All pixel art is generated in code from a hand-built, low-saturation
//  palette: deep teal water, layered moss greens, warm mud, cream highlights
//  and amber/rust accents.  No image, font or audio files are loaded.
//
//  A quiet native swamp jar with a rounded 3D glass rim and living plant
//  communities.  Five linked roles: plant clusters, rice shrimp, damselfly
//  larvae, bitterling fish and detritus worms.  Animals carry inherited
//  size/speed/oxygen traits, grow through juvenile/subadult/adult stages and
//  leave sinking, fading corpses.
//
//  Modules in this file:
//      1. palette + pixel sprites + generic sprite renderer/scaler
//      2. CPU load sampler (GetSystemTimes, once per second)
//      2b. desktop ambience: idle / volume / battery, at most once per second
//      3. closed-loop ecosystem: growth, genetics, predation, decay, corpses,
//         tubifex detritus chain, epiphyte/cleaning, fish territory, sediment
//         fertility, slow daylight and seasonal water level
//      4. bottle renderer (direct pixel drawing, no assets)
//      5. transparent/topmost Win32 window + layered pixel observation panel
//      6. 30 fps main loop + drag interaction + panel event routing
//
//  Build notes are repeated at the very bottom of this file.
// -----------------------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// =============================================================================
//  1. Palette, sprites and generic pixel rendering
// =============================================================================
namespace art {

constexpr int kScreenW = 320;
constexpr int kScreenH = 120;
constexpr int kMudTop = 104;   // nominal ground, actual contour is procedural
constexpr int kWaterTop = 24;

// A richer low-saturation swamp palette.  The first three colours remain the
// original pixel inks; the rest are muted depth, plant and mineral shades.
constexpr SDL_Color kDeep       = { 13,  22,  21, 255};
constexpr SDL_Color kWaterDark  = { 23,  40,  42, 255};
constexpr SDL_Color kWaterMid   = { 35,  62,  66, 255};
constexpr SDL_Color kWaterLight = { 54,  88,  91, 255};
constexpr SDL_Color kTeal       = { 98, 145, 138, 255};
constexpr SDL_Color kMossDark   = { 45,  66,  45, 255};
constexpr SDL_Color kMoss       = { 78, 103,  62, 255};
constexpr SDL_Color kLeaf       = { 140, 160, 94, 255};
constexpr SDL_Color kMudDark    = { 40,  31,  24, 255};
constexpr SDL_Color kMud        = { 66,  51,  35, 255};
constexpr SDL_Color kMudLight   = { 94,  72,  47, 255};
// Dry upper shore.  Distinctly paler and warmer than the wet silt bed, so the
// far bank and the bottom read as two different materials.
constexpr SDL_Color kClayDark   = { 78,  64,  46, 255};
constexpr SDL_Color kClay       = {112,  93,  64, 255};
constexpr SDL_Color kClayLight  = {150, 126,  86, 255};
constexpr SDL_Color kAmber      = { 188, 137, 79, 255};
constexpr SDL_Color kRust       = { 139,  83, 58, 255};
constexpr SDL_Color kCream      = { 229, 218, 187, 255};
constexpr SDL_Color kWhiteCream = { 244, 236, 212, 255};

// Backwards-compatible names used by the old renderer vocabulary.
constexpr SDL_Color kGreen = kMoss;

constexpr Uint8 kEmptyAlpha = 1;

constexpr char kInkMud      = 'M';
constexpr char kInkMudMid   = 'B';
constexpr char kInkMossDark = 'D';
constexpr char kInkGreen    = 'G';
constexpr char kInkLeaf     = 'L';
constexpr char kInkCream    = 'W';
constexpr char kInkWhite    = 'X';
constexpr char kInkAmber    = 'A';
constexpr char kInkRust     = 'R';
constexpr char kInkTeal     = 'T';

// Sprite convention:
//   [frame][row][column], 16x16, NUL padded.  ' ' = transparent.
//   M/B = dark/mid mud, D/G/L = deep/mid/light green, W/X = cream, A/R = warm accents.
const char kAlgaeFrames[2][16][17] = {
{
"                ",
"   W            ",
"   L     W      ",
"  L       L     ",
" G        L     ",
" G   W    G     ",
"LG   LL  LG  WL ",
" G  L     G  L  ",
" G  G    G   L  ",
"  DLD   DL  LDWL",
"   DD   D   D L ",
"   D D  D   D L ",
"  GD DGGD  DGGD ",
"   D  D D  D  D ",
"   D  D  D D  D ",
"  D   D   DD D  "
}, {
"                ",
"   W            ",
"   L    W       ",
"   L     L      ",
"  G      L      ",
"  G   W   G     ",
" GL  LL   GLW   ",
" G   L    G L   ",
" G   G A  G  L  ",
"LD  DL  LD   LW ",
"  D D    D   DL ",
"  D D   D   D L ",
"   GD   DG GD DG",
"   D D  D   D D ",
"   D D  D  D  D ",
"   D  D  D D  D "
}
};

const char kShrimpFrames[2][16][17] = {
{
"                ",
"                ",
"                ",
"              W ",
"          MMMW  ",
"     MMMMMWWAWW ",
"   R RWWWWWDWM  ",
"   ARWWW WAAM   ",
"   ARAAAAAM     ",
"   RGMMMMM G    ",
"      G G G     ",
"                ",
"                ",
"                ",
"                ",
"                "
}, {
"                ",
"                ",
"              W ",
"             W  ",
"          MMM W ",
"     MMMMMWWAW  ",
"   ARRWWWWWDWM  ",
"   R WWW WAAM   ",
"   R AAAAAM     ",
"   ARMMMMM G    ",
"    GG G G      ",
"                ",
"                ",
"                ",
"                ",
"                "
}
};

const char kWormFrames[2][16][17] = {
{
"                ",
"                ",
"                ",
"                ",
"                ",
"                ",
"                ",
"        W       ",
"       WR       ",
"       RB       ",
"        RB      ",
"        WR      ",
"       BR       ",
"        WR      ",
"       RB       ",
"       RB       "
}, {
"                ",
"                ",
"                ",
"                ",
"                ",
"                ",
"         W      ",
"        WR      ",
"        RB      ",
"       RB       ",
"       WR       ",
"        RB      ",
"       WR       ",
"        RB      ",
"        RB      ",
"         RB     "
}
};

// Bitterling: a small olive/silver fish with a warm amber belly and rust
// fins.  Two frames flick the tail and shift the body by one pixel.
// Bitterling: an olive, teal and amber fish with rust fins.  Two frames
// flick the forked tail while the body holds a steady shoaling pose.
const char kFishFrames[2][16][17] = {
{
"                ",
"                ",
"                ",
"          L     ",
"        BBBBBB  ",
"   RR BBGGGGGGB ",
"  RR BGGGGTTWWWB",
" RR ATTTTWWWMAAB",
"  RRABWAAWAAAAB ",
"   RR BAAAAAAB  ",
"       BBBBBB   ",
"                ",
"                ",
"                ",
"                ",
"                "
}, {
"                ",
"                ",
"                ",
"          L     ",
"        BBBBBB  ",
"   RR BBGGGGGGB ",
"  R  BGGGGTTWWWB",
" RR ATTTTWWWMAAB",
" RR ABWAAWAAAAB ",
"  RRR BAAAAAAB  ",
"       BBBBBB   ",
"                ",
"                ",
"                ",
"                ",
"                "
}
};

// Damselfly larva: long segmented abdomen, three tail gills, six legs.
const char kDamselFrames[2][16][17] = {
{
"                ",
"                ",
"                ",
"                ",
"                ",
"            WB  ",
" BB      BBBGMB ",
"BLTBBBBBBGGGLMB ",
" BBDGGDGGAAAGB  ",
"BTLDGDGDGGGGB   ",
" BBTBBABABAB    ",
"   B  BABAB     ",
"       B B      ",
"                ",
"                ",
"                "
}, {
"                ",
"                ",
"                ",
"                ",
"                ",
"   B        WB  ",
" BBLB    BBBGMB ",
"BGTBBBBBBGGGLMB ",
" BBDGGDGGAAAGB  ",
"BTLDGDGDGGGGB   ",
" BTTBBBABABB    ",
"  BB BABABAB    ",
"      B B B     ",
"                ",
"                ",
"                "
}
};

// Tubifex / red midge worm: tiny rust-red detritus grazer.
const char kTubifexFrames[2][8][9] = {
{
"        ",
"        ",
"        ",
"        ",
"    ARR ",
" WRR  A ",
"  BARR  ",
"        "
}, {
"        ",
"        ",
"        ",
"        ",
"      R ",
"   RARA ",
" WRARR  ",
"  B     "
}
};

// Attached egg clutch: pale amber/cream beads on a plant stem.
const char kEggFrames[2][6][7] = {
{
"      ",
"      ",
" A A  ",
"  AAW ",
" WAW  ",
"  DD  "
}, {
"      ",
"      ",
" WWA  ",
" AA W ",
" WWA  ",
"  DD  "
}
};

// Tall stem plant: a vertical community with alternating leaf blades.
const char kTallPlantFrames[2][48][17] = {
{
"       L        ",
"       GD       ",
"       DG       ",
"       L        ",
"       GDG      ",
"        DG      ",
"        DG      ",
"     LGGDGGGGL  ",
"      DDDGDDD   ",
"      LGDGGGL   ",
"       DDGDD    ",
"    LGGGDGGL    ",
"     DDDDGD     ",
"     LGGDGGGGL  ",
"      DDDGDDD   ",
"      LGDGGGL   ",
"       DDGDD    ",
"    LGGGDGGL    ",
"     DDDDGD     ",
"     LGGDGGGGL  ",
"      DDDGDDD   ",
"     LGDGGGL    ",
"      DDGDD     ",
"   LGGGDGGL     ",
"    DDDDGD      ",
"    LGGDGGGGL   ",
"     DDDGDDD    ",
"     LGDGGGL    ",
"      DG DD     ",
"  LGGGDGGL      ",
"   DDDDGD       ",
"   LGGDGGGGL    ",
"    DDDGDDD     ",
"    LGDGGGL     ",
"     DDGDD      ",
"  LGGGDGGL      ",
"   DDDDGD       ",
"   LGGDGGGGL    ",
"    DDDGDDD     ",
"    LGDGGGL     ",
"     DDGDD      ",
"  LGGGDGGL      ",
"   DDDDGD       ",
"   LGGDGGGGL    ",
"    DDDGDDD     ",
"    LGDGGGL     ",
"     D DDD      ",
"       DG       "
}, {
"       L        ",
"       GD       ",
"       DG       ",
"       L        ",
"       GDG      ",
"        DG      ",
"        DG      ",
"     LGGDGGGGL  ",
"      DDDGDDD   ",
"      LGDGGGL   ",
"       DDGDD    ",
"    LGGGDGGL    ",
"     DDDDGD     ",
"     LGGDGGGGL  ",
"      DDDGDDD   ",
"      LGDGGGL   ",
"       DDGDD    ",
"    LGGGDGGL    ",
"     DDDG D     ",
"    LGGDGGGGL   ",
"     DDDGDDD    ",
"     LGDGGGL    ",
"      DDGDD     ",
"   LGGGDGGL     ",
"    DDDG D      ",
"   LGGDGGGGL    ",
"    DDDGDDD     ",
"    LGDGGGL     ",
"     DDGDD      ",
"  LGGGDGGL      ",
"   DDDDGD       ",
"   LGGDGGGGL    ",
"    DDDGDDD     ",
"    LGDGGGL     ",
"     DDGDD      ",
"  LGGGDGGL      ",
"   DDDDGD       ",
"   LGGDGGGGL    ",
"    DDDGDDD     ",
"    LGDGGGL     ",
"     DDGDD      ",
"  LGGGDGGL      ",
"   DDD DD       ",
"    LGGDGGGGL   ",
"     DDDGDDD    ",
"     LGDGGGL    ",
"      DDGDD     ",
"       DG       "
}
};

// Rosette / floating-leaf plant: broad leaves spreading at the surface.
const char kRosetteFrames[2][12][25] = {
{
"                        ",
"                        ",
"                        ",
"                        ",
"         LL   L         ",
"      L  LG  LL   L     ",
"     LLG  GG G  GL      ",
"   L    GG G GGGG  LLL  ",
"   LLGGGGGGGGGGGGGGLL   ",
"         GG A GGGGGGLL  ",
"            B     GLLL  ",
"            B           "
}, {
"                        ",
"                        ",
"                        ",
"                        ",
"          L   LL        ",
"      L   LL  L   L     ",
"      LGG GG GG  LLL    ",
"    LL  GG G G GG   L   ",
"   LLGGGGGGGGGGGGGGGLL  ",
"         GG A GGGG      ",
"            B   GGGGLL  ",
"            B           "
}
};

inline SDL_Color inkColor(char ink) {
    switch (ink) {
    case kInkMossDark: return kMossDark;
    case kInkGreen:    return kMoss;
    case kInkLeaf:     return kLeaf;
    case kInkCream:    return kCream;
    case kInkWhite:    return kWhiteCream;
    case kInkAmber:    return kAmber;
    case kInkRust:     return kRust;
    case kInkMudMid:   return kMud;
    case kInkTeal:     return kTeal;
    default:           return kMudDark;
    }
}

inline void setInk(SDL_Renderer* r, char ink) {
    const SDL_Color c = inkColor(ink);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
}

// Generic nearest-neighbour blitter used for stage growth, plant communities
// and death-sink animations.  It preserves the pixel grid: no filtering, no
// rotation, only integer scaling plus an optional row lean.
inline void drawSpriteScaled(SDL_Renderer* r, const char* sprite,
                             int sw, int sh, int stride,
                             int x, int y, float scaleX, float scaleY,
                             bool flipX, bool flipY, float lean,
                             SDL_Color tint, float tintAmount, Uint8 alpha) {
    if (!sprite || alpha == 0) return;
    const int dw = std::max(1, static_cast<int>(std::lround(sw * scaleX)));
    const int dh = std::max(1, static_cast<int>(std::lround(sh * scaleY)));

    static constexpr char kInks[10] = {
        kInkMud, kInkMudMid, kInkMossDark, kInkGreen, kInkLeaf,
        kInkCream, kInkWhite, kInkAmber, kInkRust, kInkTeal
    };
    if (alpha < 255) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    for (char ink : kInks) {
        SDL_Color c = inkColor(ink);
        if (tintAmount > 0.0f) {
            c.r = static_cast<Uint8>(std::lround(c.r + (tint.r - c.r) * tintAmount));
            c.g = static_cast<Uint8>(std::lround(c.g + (tint.g - c.g) * tintAmount));
            c.b = static_cast<Uint8>(std::lround(c.b + (tint.b - c.b) * tintAmount));
        }
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, alpha);

        for (int dy = 0; dy < dh; ++dy) {
            const int sy0 = std::min(sh - 1, dy * sh / dh);
            const int sy = flipY ? (sh - 1 - sy0) : sy0;
            const int rowLean = static_cast<int>(std::lround(
                lean * static_cast<float>(dh - 1 - dy) / static_cast<float>(std::max(1, dh - 1))));
            for (int dx = 0; dx < dw; ++dx) {
                const int sx0 = std::min(sw - 1, dx * sw / dw);
                const int sx = flipX ? (sw - 1 - sx0) : sx0;
                if (sprite[sy * stride + sx] != ink) continue;
                SDL_RenderDrawPoint(r, x + dx + rowLean, y + dy);
            }
        }
    }
    if (alpha < 255) SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// Buffer variant used by the cached plant-community layer.  Writing into a
// 320x120 ARGB buffer once every few frames is much cheaper than re-blitting
// every blade at 30 fps.
inline void drawSpriteScaledToBuffer(Uint32* dst, int dstW, int dstH,
                                     const char* sprite,
                                     int sw, int sh, int stride,
                                     int x, int y, float scaleX, float scaleY,
                                     bool flipX, bool flipY, float lean,
                                     SDL_Color tint, float tintAmount, Uint8 alpha) {
    if (!sprite || alpha == 0) return;
    const int dw = std::max(1, static_cast<int>(std::lround(sw * scaleX)));
    const int dh = std::max(1, static_cast<int>(std::lround(sh * scaleY)));
    for (int dy = 0; dy < dh; ++dy) {
        const int sy0 = std::min(sh - 1, dy * sh / dh);
        const int sy = flipY ? (sh - 1 - sy0) : sy0;
        const int rowLean = static_cast<int>(std::lround(
            lean * static_cast<float>(dh - 1 - dy) / static_cast<float>(std::max(1, dh - 1))));
        const int py = y + dy;
        if (py < 0 || py >= dstH) continue;
        for (int dx = 0; dx < dw; ++dx) {
            const int sx0 = std::min(sw - 1, dx * sw / dw);
            const int sx = flipX ? (sw - 1 - sx0) : sx0;
            const char ch = sprite[sy * stride + sx];
            if (ch == ' ') continue;
            SDL_Color c = inkColor(ch);
            if (tintAmount > 0.0f) {
                c.r = static_cast<Uint8>(std::lround(c.r + (tint.r - c.r) * tintAmount));
                c.g = static_cast<Uint8>(std::lround(c.g + (tint.g - c.g) * tintAmount));
                c.b = static_cast<Uint8>(std::lround(c.b + (tint.b - c.b) * tintAmount));
            }
            const int px = x + dx + rowLean;
            if (px < 0 || px >= dstW) continue;
            dst[py * dstW + px] = (static_cast<Uint32>(alpha) << 24)
                                | (static_cast<Uint32>(c.r) << 16)
                                | (static_cast<Uint32>(c.g) << 8)
                                | static_cast<Uint32>(c.b);
        }
    }
}

// Stable integer hash used for procedural sediment and depth texture.
inline std::uint32_t hash2(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 374761393u
                    + static_cast<std::uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

} // namespace art


// =============================================================================
//  2. CPU load sampler
// =============================================================================
// GetSystemTimes() gives cumulative idle/kernel/user times.  Sampling once a
// second is enough, costs almost nothing and needs no PDH / WMI dependency.
class CpuLoadSampler {
public:
    CpuLoadSampler() {
        read(prevIdle_, prevKernel_, prevUser_);
        lastSample_ = Clock::now();
        usage_ = 0.25f;  // neutral starting guess, refined after one second
    }

    // Call every frame; it only recomputes when one second has elapsed.
    void update() {
        const auto now = Clock::now();
        const float elapsed = std::chrono::duration<float>(now - lastSample_).count();
        if (elapsed < 1.0f) {
            return;
        }

        FILETIME idle{}, kernel{}, user{};
        if (!read(idle, kernel, user)) {
            lastSample_ = now;
            return;
        }

        const std::uint64_t idleNow   = toU64(idle);
        const std::uint64_t kernelNow = toU64(kernel);
        const std::uint64_t userNow   = toU64(user);

        const std::uint64_t idleDelta   = idleNow   - toU64(prevIdle_);
        const std::uint64_t kernelDelta = kernelNow - toU64(prevKernel_);
        const std::uint64_t userDelta   = userNow   - toU64(prevUser_);
        const std::uint64_t totalDelta  = kernelDelta + userDelta;

        if (totalDelta > 0) {
            const float busy = static_cast<float>(totalDelta - std::min(idleDelta, totalDelta));
            usage_ = std::clamp(busy / static_cast<float>(totalDelta), 0.0f, 1.0f);
        }

        prevIdle_ = idle;
        prevKernel_ = kernel;
        prevUser_ = user;
        lastSample_ = now;
    }

    float usage() const { return usage_; }

private:
    using Clock = std::chrono::steady_clock;

    static std::uint64_t toU64(const FILETIME& ft) {
        ULARGE_INTEGER v;
        v.LowPart = ft.dwLowDateTime;
        v.HighPart = ft.dwHighDateTime;
        return v.QuadPart;
    }

    static bool read(FILETIME& idle, FILETIME& kernel, FILETIME& user) {
        return GetSystemTimes(&idle, &kernel, &user) != 0;
    }

    FILETIME prevIdle_{};
    FILETIME prevKernel_{};
    FILETIME prevUser_{};
    Clock::time_point lastSample_{};
    float usage_ = 0.25f;
};

// =============================================================================
//  2b. Low-frequency desktop ambience
// =============================================================================
// All desktop probes run at most once a second.  They only tint the simulation
// and the picture; none of them can directly kill an organism.
struct AmbientFactors {
    float cpu = 0.25f;
    float night = 0.0f;
    float volume = 0.5f;
    float battery = 1.0f;
    float batteryScale = 1.0f;
    float lightScale = 1.0f;
    float nutrientBonus = 1.0f;
    float activityScale = 1.0f;
    float agitation = 0.65f;
    bool onBattery = false;
    bool resting = false;
};

class AmbientEnvironment {
public:
    AmbientEnvironment() {
        const HRESULT hr = CoInitializeEx(nullptr,
            COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        comInitialized_ = SUCCEEDED(hr);
        factors_.volume = readVolume();
        volume_ = factors_.volume;
        readPower();
        recompute(true);
    }

    ~AmbientEnvironment() {
        if (comInitialized_) CoUninitialize();
    }

    void update(float dt) {
        accumulator_ += std::clamp(dt, 0.0f, 0.25f);
        if (accumulator_ < 1.0f) return;
        accumulator_ = 0.0f;

        cpu_.update();

        LASTINPUTINFO input{};
        input.cbSize = sizeof(input);
        if (GetLastInputInfo(&input)) {
            const DWORD now = GetTickCount();
            const DWORD idle = now - input.dwTime;
            idleSeconds_ = static_cast<float>(idle) * 0.001f;
        }

        float volume = readVolume();
        volume_ += (volume - volume_) * 0.25f;
        readPower();

        const float targetNight = smoothstep(
#ifdef ECO_DEBUG_FAST_IDLE
            5.0f, 15.0f,
#else
            180.0f, 300.0f,
#endif
            idleSeconds_);
        night_ += (targetNight - night_) * 0.06f;
        recompute(false);
    }

    const AmbientFactors& factors() const { return factors_; }

private:
    static float smoothstep(float edge0, float edge1, float x) {
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void recompute(bool initial) {
        if (initial) night_ = 0.0f;
        factors_.cpu = cpu_.usage();
        factors_.night = night_;
        factors_.volume = volume_;
        factors_.resting = night_ > 0.55f;

        float dim = 1.0f;
        if (factors_.onBattery) {
            const float b = std::clamp(factors_.battery, 0.0f, 1.0f);
            if (b < 0.20f) dim = 0.84f + 0.16f * (b / 0.20f);
            factors_.batteryScale = 0.86f + 0.14f * std::clamp(b / 0.20f, 0.0f, 1.0f);
            factors_.nutrientBonus = 1.0f;
        } else {
            factors_.batteryScale = 1.0f;
            factors_.nutrientBonus = 1.12f;
        }
        factors_.lightScale = (1.0f - 0.62f * night_) * dim;
        factors_.activityScale = (1.0f - 0.65f * night_)
                               * (0.94f + 0.12f * volume_);
        factors_.agitation = 0.35f + 0.65f * volume_;
    }

    void readPower() {
        SYSTEM_POWER_STATUS power{};
        if (!GetSystemPowerStatus(&power)) {
            factors_.onBattery = false;
            factors_.battery = 1.0f;
            return;
        }
        factors_.onBattery = (power.ACLineStatus == 0);
        if (factors_.onBattery && power.BatteryLifePercent <= 100) {
            factors_.battery = static_cast<float>(power.BatteryLifePercent) / 100.0f;
        } else {
            factors_.battery = 1.0f;
        }
    }

    float readVolume() {
        if (!comInitialized_) return 0.5f;

        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* device = nullptr;
        IAudioEndpointVolume* endpoint = nullptr;
        float value = 0.5f;

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                      CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(hr) && enumerator) {
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        }
        if (SUCCEEDED(hr) && device) {
            hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL,
                                  nullptr, reinterpret_cast<void**>(&endpoint));
        }
        if (SUCCEEDED(hr) && endpoint) {
            float scalar = 0.5f;
            if (SUCCEEDED(endpoint->GetMasterVolumeLevelScalar(&scalar))) {
                value = std::clamp(scalar, 0.0f, 1.0f);
            }
            BOOL muted = FALSE;
            if (SUCCEEDED(endpoint->GetMute(&muted)) && muted) value = 0.0f;
        }

        if (endpoint) endpoint->Release();
        if (device) device->Release();
        if (enumerator) enumerator->Release();
        return value;
    }

    CpuLoadSampler cpu_;
    AmbientFactors factors_{};
    bool comInitialized_ = false;
    float volume_ = 0.5f;
    float night_ = 0.0f;
    float idleSeconds_ = 0.0f;
    float accumulator_ = 0.0f;
};

// =============================================================================
//  2c. User settings: system behaviour + ecology tuning
// =============================================================================
//  Settings are deliberately plain ints so they can be edited from the pixel
//  panel with stepper buttons and persisted as a tiny key=value file next to
//  the executable.  Every field is clamped on load and on change.
struct Settings {
    // system
    int maxFps = 30;          // 15..60, loop cap
    int scale = 100;          // 100 / 200 percent, integer nearest-neighbour
    int topmost = 1;          // stay above other windows
    int opacity = 100;        // 50..100 percent
    int clickThrough = 0;     // left click passes to the desktop
    int autoStart = 0;        // launch with Windows (HKCU Run entry)
    // ecology
    int seasonMinutes = 20;   // full spring->winter cycle
    int maxPlants = 10;
    int maxShrimp = 7;
    int maxFish = 3;
    int maxDamsel = 5;
    int maxWorms = 5;
    int maxTubifex = 20;
    int breeding = 100;       // percent, scales reproduction cooldowns
    int disturbance = 100;    // percent, natural leaf/sediment events
    int eventFrequency = 100;  // percent, ecological event cadence
    int eventWeight[8] = {100, 100, 100, 100, 100, 100, 100, 100};
    int refuge = 1;           // dormant egg/cyst rescue after 90 s
};

Settings g_settings;

int viewScale() {
    return g_settings.scale >= 150 ? 2 : 1;
}

int scaledScreenW() {
    return art::kScreenW * viewScale();
}

int scaledScreenH() {
    return art::kScreenH * viewScale();
}

void sanitizeSettings() {
    g_settings.maxFps = std::clamp(g_settings.maxFps, 15, 60);
    g_settings.maxFps = ((g_settings.maxFps + 2) / 5) * 5;
    g_settings.scale = g_settings.scale >= 150 ? 200 : 100;
    g_settings.topmost = g_settings.topmost ? 1 : 0;
    g_settings.opacity = std::clamp(g_settings.opacity, 50, 100);
    g_settings.opacity = ((g_settings.opacity + 2) / 5) * 5;
    g_settings.clickThrough = g_settings.clickThrough ? 1 : 0;
    g_settings.autoStart = g_settings.autoStart ? 1 : 0;
    g_settings.seasonMinutes = std::clamp(g_settings.seasonMinutes, 5, 60);
    g_settings.maxPlants = std::clamp(g_settings.maxPlants, 3, 16);
    g_settings.maxShrimp = std::clamp(g_settings.maxShrimp, 1, 14);
    g_settings.maxFish = std::clamp(g_settings.maxFish, 1, 6);
    g_settings.maxDamsel = std::clamp(g_settings.maxDamsel, 1, 10);
    g_settings.maxWorms = std::clamp(g_settings.maxWorms, 1, 10);
    g_settings.maxTubifex = std::clamp(g_settings.maxTubifex, 2, 32);
    g_settings.breeding = std::clamp(g_settings.breeding, 25, 200);
    g_settings.disturbance = std::clamp(g_settings.disturbance, 0, 300);
    g_settings.eventFrequency = std::clamp(g_settings.eventFrequency, 0, 300);
    for (int& w : g_settings.eventWeight) w = std::clamp(w, 0, 300);
    g_settings.refuge = g_settings.refuge ? 1 : 0;
}

std::wstring settingsFilePath() {
    wchar_t module[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, module, MAX_PATH);
    std::wstring path(module, len);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash + 1);
    else path.clear();
    return path + L"ecobottle.cfg";
}

std::wstring stateFilePath() {
    wchar_t module[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, module, MAX_PATH);
    std::wstring path(module, len);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash + 1);
    else path.clear();
    return path + L"ecobottle.state";
}

namespace platform {
// Adds or removes the per-user Run entry used by the "开机自启" toggle.
// HKCU needs no elevation, and the value always points at the executable that
// is currently running, so moving the folder only requires toggling it again.
bool setAutoStart(bool enabled) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return false;
    }
    LONG result = ERROR_SUCCESS;
    if (enabled) {
        wchar_t module[MAX_PATH] = {};
        const DWORD len = GetModuleFileNameW(nullptr, module, MAX_PATH);
        if (len == 0) {
            result = ERROR_FILE_NOT_FOUND;
        } else {
            const std::wstring command = L"\"" + std::wstring(module, len) + L"\"";
            result = RegSetValueExW(key, L"EcoBottle", 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(command.c_str()),
                                    static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        result = RegDeleteValueW(key, L"EcoBottle");
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
}  // namespace platform

void loadSettings() {
    FILE* file = _wfopen(settingsFilePath().c_str(), L"rb");
    if (!file) return;
    char line[160];
    while (std::fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char key[64] = {};
        int value = 0;
        if (std::sscanf(line, "%63[^=]=%d", key, &value) != 2) continue;
        if (std::strcmp(key, "max_fps") == 0) g_settings.maxFps = value;
        else if (std::strcmp(key, "scale") == 0) g_settings.scale = value;
        else if (std::strcmp(key, "topmost") == 0) g_settings.topmost = value;
        else if (std::strcmp(key, "opacity") == 0) g_settings.opacity = value;
        else if (std::strcmp(key, "click_through") == 0) g_settings.clickThrough = value;
        else if (std::strcmp(key, "auto_start") == 0) g_settings.autoStart = value;
        else if (std::strcmp(key, "season_minutes") == 0) g_settings.seasonMinutes = value;
        else if (std::strcmp(key, "max_plants") == 0) g_settings.maxPlants = value;
        else if (std::strcmp(key, "max_shrimp") == 0) g_settings.maxShrimp = value;
        else if (std::strcmp(key, "max_fish") == 0) g_settings.maxFish = value;
        else if (std::strcmp(key, "max_damsel") == 0) g_settings.maxDamsel = value;
        else if (std::strcmp(key, "max_worms") == 0) g_settings.maxWorms = value;
        else if (std::strcmp(key, "max_tubifex") == 0) g_settings.maxTubifex = value;
        else if (std::strcmp(key, "breeding") == 0) g_settings.breeding = value;
        else if (std::strcmp(key, "disturbance") == 0) g_settings.disturbance = value;
        else if (std::strcmp(key, "event_frequency") == 0) g_settings.eventFrequency = value;
        else if (std::strcmp(key, "refuge") == 0) g_settings.refuge = value;
        else {
            static const char* weightKeys[8] = {
                "event_weight_rain", "event_weight_drought",
                "event_weight_heat", "event_weight_cold",
                "event_weight_bloom", "event_weight_overturn",
                "event_weight_leaf", "event_weight_food"};
            for (int i = 0; i < 8; ++i) {
                if (std::strcmp(key, weightKeys[i]) == 0) {
                    g_settings.eventWeight[i] = value;
                    break;
                }
            }
        }
    }
    std::fclose(file);
    sanitizeSettings();
}

void saveSettings() {
    sanitizeSettings();
    FILE* file = _wfopen(settingsFilePath().c_str(), L"wb");
    if (!file) return;
    std::fprintf(file, "# Native Swamp Bottle settings\n");
    std::fprintf(file, "max_fps=%d\n", g_settings.maxFps);
    std::fprintf(file, "scale=%d\n", g_settings.scale);
    std::fprintf(file, "topmost=%d\n", g_settings.topmost);
    std::fprintf(file, "opacity=%d\n", g_settings.opacity);
    std::fprintf(file, "click_through=%d\n", g_settings.clickThrough);
    std::fprintf(file, "auto_start=%d\n", g_settings.autoStart);
    std::fprintf(file, "season_minutes=%d\n", g_settings.seasonMinutes);
    std::fprintf(file, "max_plants=%d\n", g_settings.maxPlants);
    std::fprintf(file, "max_shrimp=%d\n", g_settings.maxShrimp);
    std::fprintf(file, "max_fish=%d\n", g_settings.maxFish);
    std::fprintf(file, "max_damsel=%d\n", g_settings.maxDamsel);
    std::fprintf(file, "max_worms=%d\n", g_settings.maxWorms);
    std::fprintf(file, "max_tubifex=%d\n", g_settings.maxTubifex);
    std::fprintf(file, "breeding=%d\n", g_settings.breeding);
    std::fprintf(file, "disturbance=%d\n", g_settings.disturbance);
    std::fprintf(file, "event_frequency=%d\n", g_settings.eventFrequency);
    static const char* weightKeys[8] = {
        "event_weight_rain", "event_weight_drought", "event_weight_heat",
        "event_weight_cold", "event_weight_bloom", "event_weight_overturn",
        "event_weight_leaf", "event_weight_food"};
    for (int i = 0; i < 8; ++i)
        std::fprintf(file, "%s=%d\n", weightKeys[i], g_settings.eventWeight[i]);
    std::fprintf(file, "refuge=%d\n", g_settings.refuge);
    std::fclose(file);
}

// =============================================================================
//  3. Closed-loop ecosystem with age stages, genetics and plant communities
// =============================================================================
// Producers are now plant *clusters*, not single filaments.  Each cluster has
// a type (low grass / tall stem / rosette), a growth stage, health and a small
// set of heritable traits.  Animals have three age stages, growth values,
// inherited size/speed/oxygen traits, and leave visible sinking corpses.

namespace savestate {

constexpr uint32_t kMagic = 0x534F4345u;  // "ECOS" (little endian)
constexpr uint32_t kVersion = 2;

template <typename T>
bool writePod(std::FILE* file, const T& value) {
    return std::fwrite(&value, sizeof(T), 1, file) == 1;
}

template <typename T>
bool readPod(std::FILE* file, T& value) {
    return std::fread(&value, sizeof(T), 1, file) == 1;
}

template <typename... Ts>
bool writeAll(std::FILE* file, const Ts&... values) {
    return (writePod(file, values) && ...);
}

template <typename... Ts>
bool readAll(std::FILE* file, Ts&... values) {
    return (readPod(file, values) && ...);
}

}  // namespace savestate

class Ecosystem {
public:
    static constexpr int kMaxPlants  = 16;
    static constexpr int kMaxShrimp  = 14;
    static constexpr int kMaxFish    = 6;
    static constexpr int kMaxDamsel  = 10;
    static constexpr int kMaxWorms   = 10;
    static constexpr int kMaxTubifex = 32;
    static constexpr int kMaxCorpses = 10;
    static constexpr int kMaxShrubs = 12;
    static constexpr int kMaxRafts = 8;
    // The border reeds occupy roughly the outer 45px at each side and are
    // painted last, so rafts seeded under them would never be visible.
    static constexpr float kRaftMinX = 52.0f;
    static constexpr float kRaftMaxX = 268.0f;
    static constexpr int kSedimentBins = 16;

    struct Genes {
        float size = 1.0f;
        float speed = 1.0f;
        float oxygen = 1.0f;
        float lineage = 0.0f;  // -1 cool, +1 warm; visual marker only
        float temp = 1.0f;     // temperature tolerance (Q10 ecology)
    };

    struct PlantGenes {
        float height = 1.0f;
        float density = 1.0f;
        float shade = 1.0f;
        float graze = 1.0f;
    };

    struct Plant {
        float x = 0.0f;
        float phase = 0.0f;
        float growth = 0.35f;
        float energy = 55.0f;
        float health = 1.0f;
        float age = 0.0f;
        float wilt = 0.0f;
        float epiphyte = 0.0f;
        PlantGenes g;
        int type = 0;          // 0 low grass, 1 tall stem, 2 rosette
        int stage = 0;         // 0 seedling, 1 growing, 2 mature, 3 senescent
        int flip = 0;
        bool dying = false;
        uint32_t id = 0;
    };

    // A shore shrub is deliberately outside the water simulation: it never
    // reads or writes oxygen, humus, nutrient, phosphorus or any animal
    // population.  Only the season clock and its own age touch it, so the
    // land can live its own spring/summer/autumn/winter cycle while the pond
    // below stays an independent system.
    struct Shrub {
        float x = 0.0f;
        float age = 0.0f;
        float growth = 0.55f;    // 0 seedling .. 1 full bush
        float leaf = 1.0f;       // 0 bare .. 1 full foliage
        float colorBlend = 0.0f; // 0 green .. 1 autumn rust
        float vigor = 1.0f;
        float seedTimer = 0.0f;
        float swaySeed = 0.0f;
        uint32_t seed = 0;
    };

    // A duckweed raft is the pond's only free-floating producer.  It is a
    // real, if small, ecological member: it draws N and P from the water
    // column, shades the submerged stand a little, is grazed by shrimp at
    // the surface and returns dead fronds to the detritus pool.  It lives in
    // its own array so the plant community, the 8-group diversity metric and
    // the save layout tag of the main pond stay untouched.
    struct Raft {
        float x = 0.0f;
        float mass = 0.55f;   // 0.05 thin skin .. 1.25 dense mat
        float age = 0.0f;
        float bobSeed = 0.0f;
    };

    struct Shrimp {
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        float dirTimer = 0.0f, energy = 55.0f, health = 1.0f;
        float age = 0.0f, growth = 1.0f;
        float targetX = 0.0f, targetY = 72.0f;
        float eatCooldown = 0.0f, breedCooldown = 0.0f;
        float epiphyte = 0.0f;
        float cleanCooldown = 0.0f;
        float startleTimer = 0.0f, startleCooldown = 0.0f;
        float startleX = 0.0f, startleY = 0.0f;
        Genes g;
        int stage = 2;
        int targetPlant = -1;
        uint32_t id = 0;
    };

    struct Damsel {
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        float dirTimer = 0.0f, energy = 55.0f, health = 1.0f;
        float age = 0.0f, growth = 1.0f;
        float targetX = 0.0f, targetY = 58.0f;
        float huntCooldown = 0.0f, breedCooldown = 0.0f;
        float epiphyte = 0.0f;
        float startleTimer = 0.0f, startleCooldown = 0.0f;
        float startleX = 0.0f, startleY = 0.0f;
        Genes g;
        int stage = 2;
        int targetShrimp = -1;
        bool hunting = false;
        uint32_t id = 0;
    };

    struct Fish {
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        float dirTimer = 0.0f, energy = 56.0f, health = 1.0f;
        float age = 0.0f, growth = 1.0f;
        float targetX = 0.0f, targetY = 48.0f;
        float huntCooldown = 0.0f, breedCooldown = 0.0f;
        float epiphyte = 0.0f;
        float territoryX = 0.0f, territoryTimer = 0.0f;
        float chaseTimer = 0.0f;
        float territoryBlend = 0.0f;
        float retargetTimer = 0.0f;
        float startleTimer = 0.0f, startleCooldown = 0.0f;
        float startleX = 0.0f, startleY = 0.0f;
        Genes g;
        int stage = 2;
        int targetKind = -1;   // 0 shrimp, 1 damsel
        int targetIndex = -1;
        int chaseFish = -1;
        bool territory = false;
        bool hunting = false;
        uint32_t id = 0;
    };

    struct Worm {
        float x = 0.0f, phase = 0.0f, vx = 0.0f, dirTimer = 0.0f;
        float energy = 55.0f, health = 1.0f;
        float age = 0.0f, growth = 1.0f;
        float breedCooldown = 0.0f;
        Genes g;
        int stage = 2;
        uint32_t id = 0;
    };

    // Tubifex / red midge worm: tiny detritus grazer, lives in loose clusters
    // on the mud and around the roots of aging plants.
    struct Tubifex {
        float x = 0.0f, y = 0.0f, phase = 0.0f, vx = 0.0f;
        float energy = 44.0f, health = 1.0f;
        float age = 0.0f, growth = 0.55f;
        float breedCooldown = 0.0f;
        float epiphyte = 0.0f;
        Genes g;
        int stage = 0;
        uint32_t id = 0;
    };

    struct Corpse {
        int kind = 0;          // 0 shrimp, 1 fish, 2 damsel, 3 worm, 4 tubifex
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        float age = 0.0f, duration = 8.0f;
        float scale = 1.0f, lean = 0.0f;
        Genes g;
        int stage = 2;
        bool flipY = false;
    };

    // 16x8 micro-habitat cell.  Only the slow grid tick computes diffusion;
    // every organism just reads the cached cell under its body.
    struct MicroCell {
        float light = 50.0f;
        float o2 = 60.0f;
        float nutrient = 50.0f;
        float tannin = 4.0f;
        float toxin = 0.0f;
        float aerobic = 0.35f;
        float anaerobic = 0.15f;
        float preyScent = 0.0f;
        float alarmScent = 0.0f;
        float breedScent = 0.0f;
        float density = 0.0f;
        float speciesDensity[6] = {0, 0, 0, 0, 0, 0};
    };
    static constexpr int kGridW = 16;
    static constexpr int kGridH = 8;
    static constexpr int kGridN = kGridW * kGridH;
    static constexpr int kMaxEggs = 12;

    struct Egg {
        int kind = 0;          // 0 shrimp, 1 fish, 2 damsel
        int plant = -1;
        float x = 0.0f, y = 0.0f;
        float timer = 0.0f;
        float epiphyte = 0.0f;
        Genes g;
        bool alive = true;
    };

    struct StartleField {
        float x = 0.0f, y = 0.0f, age = 0.0f, duration = 1.6f;
        float radius = 48.0f;
    };
    struct FoodPulse {
        float x = 0.0f, y = 0.0f, age = 0.0f, duration = 5.5f;
        int specks = 9;
    };
    struct LightPatch {
        float x = 0.0f, y = 0.0f, age = 0.0f, duration = 8.0f;
        float radius = 34.0f, amount = 0.0f;
    };

    // Purely non-lethal ecological events.  Every entry only bends light,
    // oxygen, nutrient, temperature, scent or sediment for a while; health is
    // never reduced to zero by an event, and the hard refuge in ensureRefuges
    // guarantees that no species can ever disappear.
    struct EcoEvent {
        int type = 0;
        float age = 0.0f;
        float duration = 60.0f;
        float intensity = 1.0f;
        float x = 0.0f;   // optional focus cell for local events
    };
    static constexpr int kMaxEvents = 3;
    static constexpr int kEventKinds = 8;

    // Read-only history of the most recent ecological events.  This is purely
    // observational: it records what already happened and never feeds back
    // into the simulation, so it cannot influence mortality or spawning.
    struct EventLogEntry {
        int type = 0;
        float age = 0.0f;       // seconds since the event was triggered
        float duration = 1.0f;  // planned length, used to mark "ongoing"
        float intensity = 1.0f;
    };
    static constexpr int kEventLogSize = 8;

    struct Decor {
        int type = 0;     // 0 pebble, 1 driftwood, 2 shell
        float x = 0.0f;
        float y = 90.0f;
        float scale = 1.0f;
        int variant = 0;
        uint32_t id = 0;
    };
    static constexpr int kMaxDecors = 8;

    // A readable, immutable copy of one individual for the observation panel.
    struct CreatureInfo {
        bool valid = false;
        int kind = 0;       // 0 shrimp, 1 fish, 2 damsel, 3 worm, 4 tubifex, 5 plant
        uint32_t id = 0;
        float x = 0.0f, y = 0.0f;
        float age = 0.0f, energy = 0.0f, health = 1.0f, growth = 0.0f;
        float epiphyte = 0.0f;
        float breedCooldown = 0.0f;
        int stage = 0;
        int plantType = 0;
        bool territory = false;
        bool dying = false;
        Genes g;
        PlantGenes pg;
    };

    Ecosystem() { reset(); }

    static float stageScale(int stage) {
        return (stage == 0) ? 0.60f : (stage == 1 ? 0.80f : 1.00f);
    }
    // Reproduction speed from the settings panel.  Cooldowns shrink below
    // 100% and stretch above it, so the ecology rules themselves stay intact.
    static float breedingRate() {
        return std::clamp(static_cast<float>(g_settings.breeding) / 100.0f,
                          0.25f, 2.0f);
    }
    // Rendering-only continuous size.  Behaviour still switches at the
    // 35% / 70% growth thresholds, but the sprite never jumps at a threshold.
    static float visualScale(float growth) {
        const float g = std::clamp(growth, 0.0f, 1.0f);
        if (g < 0.35f) return 0.60f + 0.13f * (g / 0.35f);
        if (g < 0.70f) return 0.73f + 0.20f * ((g - 0.35f) / 0.35f);
        return 0.93f + 0.07f * ((g - 0.70f) / 0.30f);
    }
    static float plantVisualScale(float growth) {
        const float g = std::clamp(growth, 0.0f, 1.0f);
        if (g < 0.35f) return 0.55f + 0.19f * (g / 0.35f);
        if (g < 0.70f) return 0.74f + 0.26f * ((g - 0.35f) / 0.35f);
        return 1.00f;
    }
    static float speciesBase(int kind) {
        switch (kind) {
        case 0: return 1.20f;  // shrimp
        case 1: return 2.00f;  // bitterling
        case 2: return 1.30f;  // damselfly larva
        default: return 1.05f; // worm
        }
    }

    void reset() {
        light_ = 50.0f;
        oxygen_ = 72.0f;
        humus_ = 32.0f;
        nutrient_ = 56.0f;
        night_ = 0.0f;
        volume_ = 0.5f;
        agitation_ = 0.65f;
        batteryScale_ = 1.0f;
        nutrientBonus_ = 1.12f;
        resting_ = false;
        plantO2_ = 0.0f;
        plantBiomass_ = 0.0f;
        dayPhase_ = 0.35f;
        seasonPhase_ = 1.10f;
        daylight_ = 1.0f;
        seasonWater_ = 0.0f;
        for (int i = 0; i < kSedimentBins; ++i) {
            const float wave = 0.5f + 0.5f * std::sin(i * 0.72f + 0.4f);
            sediment_[i] = 28.0f + 46.0f * wave;
        }
        fishEggs_ = 1;
        fishEggTimer_ = 0.0f;
        damselEggs_ = 1;
        damselEggTimer_ = 0.0f;
        shrimpEggs_ = 2;
        shrimpEggTimer_ = 0.0f;
        fishEggBankTimer_ = 0.0f;
        damselEggBankTimer_ = 0.0f;
        shrimpEggBankTimer_ = 0.0f;
        tubifexEggs_ = 4;
        tubifexEggTimer_ = 0.0f;
        wormCysts_ = 2;
        wormCystTimer_ = 0.0f;
        plantSeeds_ = 3;
        plantSeedTimer_ = 0.0f;
        for (int t = 0; t < 3; ++t) {
            plantTypeSeeds_[t] = 1;
            plantTypeSeedTimer_[t] = 0.0f;
            plantTypeSeedX_[t] = 160.0f;
        }
        // A dense established shore to start the land's own cycle.
        shrubCount_ = 0;
        shrubRescue_ = 0.0f;
        spawnShrub(36.0f, 0.85f);
        spawnShrub(86.0f, 0.62f);
        spawnShrub(134.0f, 1.0f);
        spawnShrub(182.0f, 0.74f);
        spawnShrub(232.0f, 0.92f);
        spawnShrub(284.0f, 0.55f);
        // Duckweed starts as a scatter of small rafts along the surface.
        raftCount_ = 0;
        raftRescue_ = 0.0f;
        duckweedCover_ = 0.0f;
        bankMoisture_ = 0.35f;
        shoreFoliage_ = 0.0f;
        bankWash_ = 0.0f;
        insectTimer_ = 30.0f;
        static constexpr float kStartRaftX[6] = {48.0f, 96.0f, 152.0f,
                                                 208.0f, 262.0f, 298.0f};
        for (int i = 0; i < 6; ++i) {
            spawnRaft(kStartRaftX[i], 0.85f - 0.08f * static_cast<float>(i % 3));
        }
        fishEggGene_ = makeGenes(1.0f);
        damselEggGene_ = makeGenes(1.0f);
        shrimpEggGene_ = makeGenes(1.0f);
        corpseCount_ = 0;
        eggCount_ = 0;
        startleCount_ = 0;
        foodCount_ = 0;
        lightCount_ = 0;
        feedCooldown_ = 0.0f;
        temperature_ = 22.0f;
        q10_ = 1.0f;
        gridFrame_ = 0;
        phosphorus_ = 46.0f;
        eventCount_ = 0;
        eventCooldown_ = 12.0f;
        eventLogCount_ = 0;
        eventLogHead_ = 0;
        decorCount_ = 0;
        nextId_ = 1;
        selectedId_ = 0;
        selectedKind_ = -1;

        plantCount_ = 0;
        // Three low cushions, three tall stems, two rosettes.
        addInitialPlant(0, 38.0f, 0.50f, 0.9f);
        addInitialPlant(0, 120.0f, 0.95f, 1.05f);
        addInitialPlant(0, 278.0f, 0.32f, 0.95f);
        addInitialPlant(1, 92.0f, 0.85f, 1.08f);
        addInitialPlant(1, 202.0f, 0.62f, 0.92f);
        addInitialPlant(1, 250.0f, 0.24f, 1.0f);
        addInitialPlant(2, 150.0f, 0.90f, 1.0f);
        addInitialPlant(2, 300.0f, 0.55f, 0.9f);
        addInitialPlant(0, 210.0f, 0.55f, 0.92f);
        if (plantCount_ > 0) {
            plants_[0].age = 470.0f;
            plants_[0].health = 0.60f;
            plants_[0].growth = 0.45f;
            plants_[0].stage = 3;
        }

        shrimpCount_ = 0;
        spawnShrimp(42.0f, 34.0f, 1, makeGenes(1.0f), 1.0f);
        spawnShrimp(86.0f, 58.0f, 1, makeGenes(1.0f), 1.0f);
        spawnShrimp(132.0f, 44.0f, 1, makeGenes(0.95f), 0.55f);
        spawnShrimp(180.0f, 66.0f, 0, makeGenes(1.0f), 0.18f);
        spawnShrimp(232.0f, 36.0f, 1, makeGenes(1.05f), 0.60f);
        spawnShrimp(286.0f, 52.0f, 2, makeGenes(1.0f), 1.0f);

        damselCount_ = 0;
        spawnDamsel(112.0f, 50.0f, 1, makeGenes(1.0f), 0.55f);
        spawnDamsel(244.0f, 40.0f, 2, makeGenes(1.0f), 1.0f);

        fishCount_ = 0;
        spawnFish(176.0f, 40.0f, 1, makeGenes(1.0f), 0.55f);
        spawnFish(262.0f, 54.0f, 2, makeGenes(1.0f), 1.0f);

        wormCount_ = 0;
        spawnWorm(72.0f, 1, makeGenes(1.0f), 0.55f, 58.0f);
        spawnWorm(142.0f, 2, makeGenes(1.0f), 1.0f, 62.0f);
        spawnWorm(216.0f, 1, makeGenes(0.95f), 0.55f, 58.0f);
        spawnWorm(292.0f, 0, makeGenes(1.0f), 0.18f, 48.0f);

        tubifexCount_ = 0;
        const float tubifexClusters[4] = {45.0f, 125.0f, 210.0f, 284.0f};
        for (int i = 0; i < 14; ++i) {
            const float x = tubifexClusters[i % 4] + rnd(-8.0f, 8.0f);
            spawnTubifex(x, makeGenes(1.0f));
        }
        // A little visible epiphyte on the oldest growth and one fish.
        if (plantCount_ > 1) {
            plants_[0].epiphyte = 0.24f;
            plants_[1].epiphyte = 0.15f;
        }
        if (plantCount_ > 3) plants_[3].epiphyte = 0.18f;
        if (fishCount_ > 0) fish_[0].epiphyte = 0.20f;
        if (fishCount_ > 1) fish_[1].epiphyte = 0.12f;
        if (damselCount_ > 0) damsel_[0].epiphyte = 0.15f;
        layEgg(0, makeGenes(1.0f), 160.0f);
        layEgg(2, makeGenes(1.0f), 210.0f);
        layEgg(1, makeGenes(1.0f), 250.0f);

#ifdef ECO_DEBUG_CORPSE
        addCorpse(1, 165.0f, 32.0f, speciesBase(1) * stageScale(2),
                  2, makeGenes(1.0f), true);
        addCorpse(0, 90.0f, 48.0f, speciesBase(0) * stageScale(1),
                  1, makeGenes(1.0f), true);
        addCorpse(2, 238.0f, 44.0f, speciesBase(2) * stageScale(2),
                  2, makeGenes(1.0f), false);
        addCorpse(4, 128.0f, 95.0f, 1.0f, 0, makeGenes(1.0f), false);
        if (plantCount_ > 0) {
            plants_[0].dying = true;
            plants_[0].wilt = 0.45f;
            plants_[0].stage = 3;
        }
#endif

        animTime_ = 0.0f;
        simTime_ = 0.0f;
        debugTimer_ = 0.0f;
        initMicroGrid();
    }

    void update(float dt, const AmbientFactors& ambient) {
        dt = std::clamp(dt, 0.0f, 0.1f);
        if (dt <= 0.0f) return;
#ifdef ECO_DEBUG_FAST
        dt *= 4.0f;
#endif

        night_ = std::clamp(ambient.night, 0.0f, 1.0f);
        volume_ = std::clamp(ambient.volume, 0.0f, 1.0f);
        agitation_ = std::clamp(ambient.agitation, 0.0f, 1.0f);
        batteryScale_ = std::clamp(ambient.batteryScale, 0.5f, 1.0f);
        nutrientBonus_ = std::clamp(ambient.nutrientBonus, 0.8f, 1.25f);
        resting_ = ambient.resting;

        const float cpu = std::clamp(ambient.cpu, 0.0f, 1.0f);
        dayPhase_ += dt * kTwoPi / 480.0f;
        if (dayPhase_ > kTwoPi) dayPhase_ -= kTwoPi;
        float seasonSpeed = 1.0f;
#ifdef ECO_DEBUG_STATS
        seasonSpeed = 40.0f;  // fast four-season cycle for visual testing
#endif
        seasonPhase_ += dt * kTwoPi * seasonSpeed
                      / std::max(60.0f, static_cast<float>(g_settings.seasonMinutes) * 60.0f);
        if (seasonPhase_ > kTwoPi) seasonPhase_ -= kTwoPi;
        updateEvents(dt);
        daylight_ = 0.78f + 0.22f * (0.5f + 0.5f * std::sin(dayPhase_));
        seasonWater_ = 13.0f * std::sin(seasonPhase_)
                     + 0.80f * std::sin(seasonPhase_ * 2.0f)
                     + eventWater_;
        // The sealed bottle's air gap is what makes rain, heat shimmer and
        // pollen visible.  The surface may rise and fall with the seasons,
        // but it is never allowed to reach the upper shore: at its highest it
        // still sits a few pixels below the bank's lowest point, so the land
        // never looks flooded over.
        seasonWater_ = std::clamp(seasonWater_, 2.0f, 20.0f);
        temperature_ = 21.0f + 5.0f * std::sin(seasonPhase_ - 1.5707963f)
                     + eventTemp_;
        temperature_ = std::clamp(temperature_, 12.0f, 31.0f);
        q10_ = std::pow(2.0f, (temperature_ - 22.0f) / 10.0f);
        updateInteractions(dt);
        const float baseLight = 26.0f + 68.0f * std::pow(cpu, 0.85f);
        const float targetLight = baseLight * std::clamp(ambient.lightScale, 0.20f, 1.0f)
                                * daylight_ * eventLightMul_;
        const float lightTau = (night_ > 0.15f) ? 4.0f : 2.5f;
        light_ += (targetLight - light_) * std::min(1.0f, dt / lightTau);
        light_ = std::clamp(light_, 0.0f, 100.0f);

        animTime_ += dt;
        simTime_ += dt;

        const float oxygenFactor = std::clamp((oxygen_ - 18.0f) / 55.0f, 0.0f, 1.0f);
        const float lightEnergy = 0.50f + 0.50f * (light_ / 100.0f);
        const float activity = (0.32f + 0.68f * oxygenFactor) * lightEnergy
                             * std::clamp(ambient.activityScale, 0.25f, 1.1f);

        updateDuckweed(dt);
        updatePlants(dt);
        updateShrubs(dt);
        updateBank(dt);
        updateTubifex(dt);
        updateShrimp(dt, activity);
        updateDamsel(dt, activity);
        updateFish(dt, activity);
        updateWorms(dt, activity);
        updateEggs(dt);
        updateEpiphyte(dt);
        updateSediment(dt);
        updateOxygen(dt, activity);
        updateChemistry(dt);
        updateCorpses(dt);
        updateMicroGrid(dt);
        ensureRefuges(dt);

#ifdef ECO_DEBUG_STATS
        debugTimer_ += dt;
        if (debugTimer_ >= 10.0f) {
            debugTimer_ = 0.0f;
            float hpMin = 1.0f, hpSum = 0.0f;
            float enMin = 100.0f;
            float hpKind[3] = {1.0f, 1.0f, 1.0f};
            float enKind[3] = {100.0f, 100.0f, 100.0f};
            int hpN = 0;
            int damselPhase[3] = {0, 0, 0};
            float damselPhaseEn[3] = {100.0f, 100.0f, 100.0f};
            auto sampleHp = [&](float v) { hpMin = std::min(hpMin, v); hpSum += v; ++hpN; };
            for (int i = 0; i < shrimpCount_; ++i) {
                sampleHp(shrimp_[i].health);
                hpKind[0] = std::min(hpKind[0], shrimp_[i].health);
                enKind[0] = std::min(enKind[0], shrimp_[i].energy);
            }
            for (int i = 0; i < damselCount_; ++i) {
                sampleHp(damsel_[i].health);
                hpKind[1] = std::min(hpKind[1], damsel_[i].health);
                enKind[1] = std::min(enKind[1], damsel_[i].energy);
                ++damselPhase[damsel_[i].stage];
                damselPhaseEn[damsel_[i].stage] =
                    std::min(damselPhaseEn[damsel_[i].stage], damsel_[i].energy);
            }
            for (int i = 0; i < fishCount_; ++i) {
                sampleHp(fish_[i].health);
                hpKind[2] = std::min(hpKind[2], fish_[i].health);
                enKind[2] = std::min(enKind[2], fish_[i].energy);
            }
            for (int i = 0; i < 3; ++i) enMin = std::min(enMin, enKind[i]);
            std::fprintf(stderr,
                "[sim] t=%5.0f light=%5.1f O2=%5.1f humus=%5.1f nut=%5.1f "
                "night=%4.2f plants=%d shrimp=%d damsel=%d fish=%d worms=%d "
                "tubifex=%d corpses=%d eggs=%d/%d/%d lay=%d temp=%4.1f day=%4.2f sed=%4.1f terr=%d "
                "ev=%d:%d:%d hp=%4.2f/%4.2f en=%5.1f "
                "hpk=%4.2f/%4.2f/%4.2f enk=%5.1f/%5.1f/%5.1f "
                "dstg=%d/%d/%d den=%4.1f/%4.1f/%4.1f\n",
                static_cast<double>(simTime_), static_cast<double>(light_),
                static_cast<double>(oxygen_), static_cast<double>(humus_),
                static_cast<double>(nutrient_), static_cast<double>(night_),
                plantCount_, shrimpCount_, damselCount_, fishCount_, wormCount_,
                tubifexCount_, corpseCount_, fishEggs_, damselEggs_, shrimpEggs_,
                eggCount_, static_cast<double>(temperature_),
                static_cast<double>(daylight_),
                static_cast<double>(sedimentAtX(160.0f)), territoryCount(),
                eventCount_ > 0 ? events_[0].type : -1,
                eventCount_ > 1 ? events_[1].type : -1,
                eventCount_ > 2 ? events_[2].type : -1,
                static_cast<double>(hpMin),
                static_cast<double>(hpN > 0 ? hpSum / static_cast<float>(hpN) : 1.0f),
                static_cast<double>(enMin),
                static_cast<double>(hpKind[0]), static_cast<double>(hpKind[1]),
                static_cast<double>(hpKind[2]), static_cast<double>(enKind[0]),
                static_cast<double>(enKind[1]), static_cast<double>(enKind[2]),
                damselPhase[0], damselPhase[1], damselPhase[2],
                static_cast<double>(damselPhaseEn[0]),
                static_cast<double>(damselPhaseEn[1]),
                static_cast<double>(damselPhaseEn[2]));
        }
#endif
    }

    float light() const { return light_; }
    float oxygen() const { return oxygen_; }
    float humus() const { return humus_; }
    float nutrient() const { return nutrient_; }
    float nightFactor() const { return night_; }
    float waterAgitation() const { return agitation_; }
    float animTime() const { return animTime_; }
    int animFrame() const { return static_cast<int>(animTime_ * 2.0f) & 1; }
    float daylight() const { return daylight_; }
    float dayPhase() const { return dayPhase_; }
    float seasonPhase() const { return seasonPhase_; }
    int waterLevelOffset() const { return static_cast<int>(std::lround(seasonWater_)); }
    // Screen-space water surface.  Negative offset means more water, so the
    // surface sits higher (smaller y).
    float waterSurfaceY() const { return art::kWaterTop + seasonWater_; }
    float sedimentAtX(float x) const {
        const int bin = std::clamp(static_cast<int>(x * kSedimentBins / 320.0f),
                                   0, kSedimentBins - 1);
        return sediment_[bin];
    }
    const MicroCell& cellAt(float x, float y) const {
        const int gx = std::clamp(static_cast<int>(x / 20.0f), 0, kGridW - 1);
        const int gy = std::clamp(static_cast<int>((y - 12.0f) / 12.5f), 0, kGridH - 1);
        return grid_[gy * kGridW + gx];
    }
    MicroCell& cellAtMut(float x, float y) {
        const int gx = std::clamp(static_cast<int>(x / 20.0f), 0, kGridW - 1);
        const int gy = std::clamp(static_cast<int>((y - 12.0f) / 12.5f), 0, kGridH - 1);
        return grid_[gy * kGridW + gx];
    }
    const MicroCell& gridCell(int gx, int gy) const {
        gx = std::clamp(gx, 0, kGridW - 1);
        gy = std::clamp(gy, 0, kGridH - 1);
        return grid_[gy * kGridW + gx];
    }
    float temperature() const { return temperature_; }
    float stressAt(float x, float y, int kind, const Genes& g) const {
        return localStressAt(x, y, kind, g);
    }

    void startleAt(float x, float y) {
#ifdef ECO_DEBUG_STATS
        std::fprintf(stderr, "[interact] startle %.1f,%.1f\n",
                     static_cast<double>(x), static_cast<double>(y));
        std::fflush(stderr);
#endif
        if (startleCount_ >= static_cast<int>(startleFields_.size())) {
            for (int i = 0; i < startleCount_ - 1; ++i) startleFields_[i] = startleFields_[i + 1];
            --startleCount_;
        }
        StartleField f;
        f.x = x;
        f.y = y;
        f.age = 0.0f;
        f.radius = 48.0f;
        startleFields_[startleCount_++] = f;

        auto disturb = [&](float ax, float ay, float sensitivity,
                           float& timer, float& cooldown,
                           float& sx, float& sy) {
            if (cooldown > 0.0f) return;
            const float dx = ax - x;
            const float dy = ay - y;
            if (dx * dx + dy * dy > f.radius * f.radius) return;
            timer = std::max(timer, 0.45f + 0.55f * sensitivity);
            cooldown = 2.6f + rnd(0.0f, 1.4f);
            sx = x;
            sy = y;
        };
        for (int i = 0; i < shrimpCount_; ++i) {
            const float sens = (shrimp_[i].stage == 0) ? 1.45f
                             : (shrimp_[i].stage == 1 ? 1.15f : 0.85f);
            disturb(shrimp_[i].x, shrimp_[i].y, sens,
                    shrimp_[i].startleTimer, shrimp_[i].startleCooldown,
                    shrimp_[i].startleX, shrimp_[i].startleY);
        }
        for (int i = 0; i < damselCount_; ++i) {
            const float sens = (damsel_[i].stage == 0) ? 0.95f : 0.72f;
            disturb(damsel_[i].x, damsel_[i].y, sens,
                    damsel_[i].startleTimer, damsel_[i].startleCooldown,
                    damsel_[i].startleX, damsel_[i].startleY);
        }
        for (int i = 0; i < fishCount_; ++i) {
            disturb(fish_[i].x, fish_[i].y, 0.34f,
                    fish_[i].startleTimer, fish_[i].startleCooldown,
                    fish_[i].startleX, fish_[i].startleY);
        }
        // Tubifex and detritus worms are deliberately unaffected: they are
        // bottom detritivores and do not flee a clicking finger.
    }

    void feedAt(float x, float y) {
#ifdef ECO_DEBUG_STATS
        std::fprintf(stderr, "[interact] feed %.1f,%.1f\n",
                     static_cast<double>(x), static_cast<double>(y));
        std::fflush(stderr);
#endif
        if (feedCooldown_ > 0.0f) return;
        feedCooldown_ = 0.55f;
        if (foodCount_ >= static_cast<int>(foodPulses_.size())) {
            for (int i = 0; i < foodCount_ - 1; ++i) foodPulses_[i] = foodPulses_[i + 1];
            --foodCount_;
        }
        FoodPulse p;
        p.x = std::clamp(x, 12.0f, 306.0f);
        p.y = std::clamp(y, 20.0f, 92.0f);
        p.age = 0.0f;
        p.specks = 9;
        foodPulses_[foodCount_++] = p;
        humus_ = std::min(100.0f, humus_ + 1.25f);
        nutrient_ = std::min(100.0f, nutrient_ + 0.45f);
        addSediment(p.x, 1.4f);
    }

    void adjustLightAt(float x, float y, float delta) {
#ifdef ECO_DEBUG_STATS
        std::fprintf(stderr, "[interact] light %.1f,%.1f d=%.1f\n",
                     static_cast<double>(x), static_cast<double>(y),
                     static_cast<double>(delta));
        std::fflush(stderr);
#endif
        if (lightCount_ >= static_cast<int>(lightPatches_.size())) {
            for (int i = 0; i < lightCount_ - 1; ++i) lightPatches_[i] = lightPatches_[i + 1];
            --lightCount_;
        }
        LightPatch p;
        p.x = std::clamp(x, 12.0f, 306.0f);
        p.y = std::clamp(y, 16.0f, 96.0f);
        p.age = 0.0f;
        p.radius = 34.0f;
        p.amount = std::clamp(delta, -10.0f, 10.0f);
        lightPatches_[lightCount_++] = p;
    }

    int startleCount() const { return startleCount_; }
    int foodCount() const { return foodCount_; }
    int lightCount() const { return lightCount_; }
    const StartleField* startleFields() const { return startleFields_.data(); }
    const FoodPulse* foodPulses() const { return foodPulses_.data(); }
    const LightPatch* lightPatches() const { return lightPatches_.data(); }

    float lightBoostAt(float x, float y) const {
        float boost = 0.0f;
        for (int i = 0; i < lightCount_; ++i) {
            const LightPatch& p = lightPatches_[i];
            const float dx = x - p.x;
            const float dy = y - p.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d >= p.radius) continue;
            const float fall = 1.0f - d / p.radius;
            const float life = 1.0f - std::clamp(p.age / p.duration, 0.0f, 1.0f);
            boost += p.amount * fall * life;
        }
        return std::clamp(boost, -18.0f, 18.0f);
    }

    const FoodPulse* nearestFood(float x, float y) const {
        const FoodPulse* best = nullptr;
        float bestD = 1e9f;
        for (int i = 0; i < foodCount_; ++i) {
            const float dx = foodPulses_[i].x - x;
            const float dy = foodPulses_[i].y - y;
            const float d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = &foodPulses_[i]; }
        }
        return best;
    }

    void updateInteractions(float dt) {
        feedCooldown_ = std::max(0.0f, feedCooldown_ - dt);
        for (int i = startleCount_ - 1; i >= 0; --i) {
            startleFields_[i].age += dt;
            if (startleFields_[i].age >= startleFields_[i].duration) {
                for (int j = i; j < startleCount_ - 1; ++j) {
                    startleFields_[j] = startleFields_[j + 1];
                }
                --startleCount_;
            }
        }
        for (int i = foodCount_ - 1; i >= 0; --i) {
            foodPulses_[i].age += dt;
            if (foodPulses_[i].age >= foodPulses_[i].duration) {
                for (int j = i; j < foodCount_ - 1; ++j) {
                    foodPulses_[j] = foodPulses_[j + 1];
                }
                --foodCount_;
            }
        }
        for (int i = lightCount_ - 1; i >= 0; --i) {
            lightPatches_[i].age += dt;
            if (lightPatches_[i].age >= lightPatches_[i].duration) {
                for (int j = i; j < lightCount_ - 1; ++j) {
                    lightPatches_[j] = lightPatches_[j + 1];
                }
                --lightCount_;
            }
        }
    }


    // -----------------------------------------------------------------------
    //  Non-lethal ecological events
    // -----------------------------------------------------------------------
    //  Eight weather / habitat events bend the environment for a while without
    //  ever directly killing anything.  They only change light, oxygen,
    //  temperature, water level, nutrient ratio, tannin, toxin and scent, so
    //  the food web can respond but the bottle can never be wiped out by a
    //  single event.  Disease and parasite outbreaks are deliberately absent.
    void spawnEvent() {
        const int season = static_cast<int>(seasonPhase_ / kTwoPi * 4.0f) & 3;
        int weight[kEventKinds];
        if (season == 0) {
            const int w[kEventKinds] = {18, 4, 2, 8, 5, 3, 10, 8};
            std::copy(w, w + kEventKinds, weight);
        } else if (season == 1) {
            const int w[kEventKinds] = {14, 12, 16, 1, 14, 2, 3, 10};
            std::copy(w, w + kEventKinds, weight);
        } else if (season == 2) {
            const int w[kEventKinds] = {10, 6, 4, 6, 6, 12, 16, 8};
            std::copy(w, w + kEventKinds, weight);
        } else {
            const int w[kEventKinds] = {8, 4, 1, 16, 2, 8, 8, 8};
            std::copy(w, w + kEventKinds, weight);
        }
        //  Per-event user weights (0% disables that kind entirely, 100% keeps the
        //  season's baseline ratio).  They only reshape the relative mix; the
        //  overall cadence stays under "eventFrequency".
        for (int i = 0; i < kEventKinds; ++i) {
            weight[i] = (weight[i] * g_settings.eventWeight[i] + 50) / 100;
        }
        for (int i = 0; i < eventCount_; ++i) weight[events_[i].type] = 0;
        int total = 0;
        for (int i = 0; i < kEventKinds; ++i) total += weight[i];
        if (total <= 0) return;
        int roll = rndInt(0, total - 1);
        int type = 0;
        for (; type < kEventKinds; ++type) {
            if (roll < weight[type]) break;
            roll -= weight[type];
        }
        if (type >= kEventKinds) return;

        EcoEvent e;
        e.type = type;
        e.age = 0.0f;
        switch (type) {
        case 0: e.duration = rnd(35.0f, 60.0f); e.intensity = rnd(0.60f, 1.10f); break;
        case 1: e.duration = rnd(70.0f, 120.0f); e.intensity = rnd(0.60f, 1.20f); break;
        case 2: e.duration = rnd(50.0f, 90.0f); e.intensity = rnd(0.70f, 1.20f); break;
        case 3: e.duration = rnd(50.0f, 90.0f); e.intensity = rnd(0.70f, 1.20f); break;
        case 4: e.duration = rnd(70.0f, 120.0f); e.intensity = rnd(0.60f, 1.15f); break;
        case 5: e.duration = rnd(30.0f, 50.0f); e.intensity = rnd(0.70f, 1.20f); break;
        case 6: e.duration = rnd(30.0f, 50.0f); e.intensity = rnd(0.70f, 1.20f); break;
        default: e.duration = rnd(40.0f, 70.0f); e.intensity = rnd(0.60f, 1.10f); break;
        }
        e.x = (type == 1 || type == 3 || type == 5) ? 0.0f : rnd(20.0f, 300.0f);
        if (eventCount_ < kMaxEvents) {
            events_[eventCount_++] = e;
        } else {
            events_[0] = events_[1];
            events_[1] = events_[2];
            events_[2] = e;
        }
        eventFoodTimer_ = 0.0f;
        logEvent(e);
    }

    // Append to the observation-only ring buffer.  Old entries are simply
    // overwritten; the simulation never reads this history back.
    void logEvent(const EcoEvent& e) {
        EventLogEntry& slot = eventLog_[eventLogHead_];
        slot.type = e.type;
        slot.age = 0.0f;
        slot.duration = std::max(1.0f, e.duration);
        slot.intensity = e.intensity;
        eventLogHead_ = (eventLogHead_ + 1) % kEventLogSize;
        if (eventLogCount_ < kEventLogSize) ++eventLogCount_;
    }

    static float eventEnvelope(const EcoEvent& e) {
        const float left = e.duration - e.age;
        return std::clamp(std::min(e.age / 2.5f, left / 3.0f), 0.0f, 1.0f);
    }

    void updateEvents(float dt) {
        eventWater_ = 0.0f;
        eventTemp_ = 0.0f;
        eventLightMul_ = 1.0f;
        eventO2Rate_ = 0.0f;
        eventNutrientRate_ = 0.0f;
        eventPhosRate_ = 0.0f;
        eventHumusRate_ = 0.0f;
        eventTanninRate_ = 0.0f;
        eventToxinRate_ = 0.0f;
        eventBloom_ = 0.0f;

        for (int i = eventCount_ - 1; i >= 0; --i) {
            EcoEvent& e = events_[i];
            e.age += dt;
            if (e.age >= e.duration) {
                for (int j = i; j < eventCount_ - 1; ++j) events_[j] = events_[j + 1];
                --eventCount_;
            }
        }
        for (int i = 0; i < eventLogCount_; ++i) eventLog_[i].age += dt;

        const float freq = std::clamp(static_cast<float>(g_settings.eventFrequency) / 100.0f,
                                      0.0f, 3.0f);
        if (freq > 0.001f) {
            eventCooldown_ -= dt * freq;
            if (eventCooldown_ <= 0.0f) {
                eventCooldown_ = rnd(55.0f, 120.0f) / std::max(0.35f, freq);
                spawnEvent();
            }
        }

        float bloomIntensity = 0.0f;
        for (int i = 0; i < eventCount_; ++i) {
            const EcoEvent& e = events_[i];
            const float env = eventEnvelope(e) * e.intensity;
            switch (e.type) {
            case 0:  // rainstorm: fresh water, dimmer, more mixing
                // Negative seasonWater_ is the "wetter" direction, matching
                // wetSeason in updateOxygen/updateChemistry.
                eventWater_ -= 6.0f * env;
                eventLightMul_ *= 1.0f - 0.30f * env;
                eventO2Rate_ += 2.4f * env;
                eventNutrientRate_ += 0.55f * env;
                eventTanninRate_ -= 0.45f * env;
                break;
            case 1:  // drought: low water, warmer, less oxygen
                eventWater_ += 6.0f * env;
                eventTemp_ += 1.1f * env;
                eventO2Rate_ -= 1.7f * env;
                eventNutrientRate_ -= 0.35f * env;
                break;
            case 2:  // heatwave: warm, oxygen saturation drops
                eventTemp_ += 3.0f * env;
                eventO2Rate_ -= 1.5f * env;
                eventPhosRate_ += 0.22f * env;
                break;
            case 3:  // cold snap: slow metabolism, slightly better O2
                eventTemp_ -= 2.8f * env;
                eventO2Rate_ += 0.35f * env;
                break;
            case 4:  // algal bloom: shaded water, inverse day/night oxygen
                eventLightMul_ *= 1.0f - 0.14f * env;
                eventO2Rate_ += (daylight_ > 0.72f ? 1.8f : -2.2f) * env;
                eventNutrientRate_ -= 0.50f * env;
                eventPhosRate_ -= 0.70f * env;
                bloomIntensity += env;
                break;
            case 5:  // autumn overturn: sediment releases nutrients and humic acid
                eventO2Rate_ -= 1.8f * env;
                eventNutrientRate_ += 1.1f * env;
                eventPhosRate_ += 1.0f * env;
                eventTanninRate_ += 0.32f * env;
                eventToxinRate_ += 0.06f * env;
                break;
            case 6:  // leaf fall: humus and tannin input
                eventHumusRate_ += 0.50f * env;
                eventTanninRate_ += 0.26f * env;
                eventNutrientRate_ += 0.08f * env;
                break;
            default:  // food bloom: fine detritus drifts in
                eventHumusRate_ += 0.32f * env;
                eventFoodTimer_ -= dt;
                if (eventFoodTimer_ <= 0.0f) {
                    eventFoodTimer_ = rnd(7.0f, 12.0f);
                    if (foodCount_ < static_cast<int>(foodPulses_.size())) {
                        FoodPulse p;
                        p.x = std::clamp(e.x + rnd(-40.0f, 40.0f), 16.0f, 304.0f);
                        p.y = rnd(58.0f, 90.0f);
                        p.specks = 11;
                        foodPulses_[foodCount_++] = p;
                    }
                }
                break;
            }
        }
        eventBloom_ = std::clamp(bloomIntensity, 0.0f, 1.4f);
        eventWater_ = std::clamp(eventWater_, -12.0f, 12.0f);
        eventTemp_ = std::clamp(eventTemp_, -4.5f, 4.5f);
        // Overlapping weather cannot drain the same oxygen pool at full rate.
        // Aerobic decay in the mud and the water column stalls once the bottle
        // is lean, so demand fades out with the remaining oxygen instead of
        // pulling the last of it to zero.  This is what keeps a heatwave on
        // top of a drought and an overturn from sterilising the bottle.
        if (eventO2Rate_ < 0.0f) {
            const float scarcity = std::clamp((oxygen_ - 12.0f) / 48.0f, 0.0f, 1.0f);
            eventO2Rate_ *= 0.18f + 0.82f * scarcity * scarcity;
        }
    }

    // Holling functional responses.  Type II saturates smoothly, type III is
    // sigmoid and gives prey a low-density refuge.  Both are normalised around
    // a typical density so ordinary behaviour stays close to the old capture
    // rates while very sparse prey becomes much harder to catch.
    static float functionalResponse2(int prey, float halfSat, float reference) {
        const float p = static_cast<float>(std::max(0, prey));
        const float raw = p / (p + halfSat);
        const float ref = reference / (reference + halfSat);
        return std::clamp(raw / std::max(0.05f, ref), 0.30f, 1.30f);
    }
    static float functionalResponse3(int prey, float halfSat, float reference) {
        const float p = static_cast<float>(std::max(0, prey));
        const float raw = (p * p) / (p * p + halfSat * halfSat);
        const float ref = (reference * reference)
                        / (reference * reference + halfSat * halfSat);
        return std::clamp(raw / std::max(0.05f, ref), 0.25f, 1.30f);
    }

    int plantCount() const { return plantCount_; }
    int shrimpCount() const { return shrimpCount_; }
    int damselCount() const { return damselCount_; }
    int fishCount() const { return fishCount_; }
    int wormCount() const { return wormCount_; }
    int tubifexCount() const { return tubifexCount_; }
    int corpseCount() const { return corpseCount_; }
    int eggCount() const { return eggCount_; }
    int shrubCount() const { return shrubCount_; }
    const Shrub& shrubAt(int i) const { return shrubs_[static_cast<size_t>(i)]; }
    int raftCount() const { return raftCount_; }
    const Raft& raftAt(int i) const { return rafts_[static_cast<size_t>(i)]; }
    // 0..1 total surface cover, used for the slight light shading of the
    // submerged stand.
    float duckweedCover() const { return duckweedCover_; }
    int territoryCount() const {
        int n = 0;
        for (int i = 0; i < fishCount_; ++i) if (fish_[i].territory) ++n;
        return n;
    }
    int fishEggs() const { return fishEggs_; }
    int damselEggs() const { return damselEggs_; }
    int shrimpEggs() const { return shrimpEggs_; }
    int tubifexEggs() const { return tubifexEggs_; }
    int wormCysts() const { return wormCysts_; }
    int plantSeeds() const {
        return plantSeeds_ + plantTypeSeeds_[0] + plantTypeSeeds_[1]
             + plantTypeSeeds_[2];
    }
    float plantBiomass() const { return plantBiomass_; }
    const float* sedimentBins() const { return sediment_; }

    const Plant* plants() const { return plants_.data(); }
    const Shrimp* shrimp() const { return shrimp_.data(); }
    const Damsel* damsel() const { return damsel_.data(); }
    const Fish* fish() const { return fish_.data(); }
    const Worm* worms() const { return worms_.data(); }
    const Tubifex* tubifex() const { return tubifex_.data(); }
    const Corpse* corpses() const { return corpses_.data(); }
    const Egg* eggs() const { return eggs_.data(); }
    const Decor* decors() const { return decors_.data(); }
    int decorCount() const { return decorCount_; }
    float phosphorus() const { return phosphorus_; }
    int eventCount() const { return eventCount_; }
    const EcoEvent* events() const { return events_.data(); }
    int eventLogCount() const { return eventLogCount_; }
    // 0 is the newest entry.  Returns an empty sentinel when out of range.
    const EventLogEntry& eventLogAt(int newestIndex) const {
        static const EventLogEntry empty{};
        if (newestIndex < 0 || newestIndex >= eventLogCount_) return empty;
        const int i = (eventLogHead_ - 1 - newestIndex + kEventLogSize)
                    % kEventLogSize;
        return eventLog_[i];
    }
    // Current profile selection is transient (panel-only, never saved).
    uint32_t selectedId() const { return selectedId_; }
    int selectedKind() const { return selectedKind_; }
    void setSelection(uint32_t id, int kind) { selectedId_ = id; selectedKind_ = kind; }
    void clearSelection() { selectedId_ = 0; selectedKind_ = -1; }

    // Human-readable ecological event name for the panel.  Never empty when
    // an event is active.
    static const wchar_t* eventName(int type) {
        switch (type) {
        case 0: return L"暴雨";
        case 1: return L"干旱";
        case 2: return L"热浪";
        case 3: return L"寒潮";
        case 4: return L"藻华";
        case 5: return L"秋季翻底";
        case 6: return L"落叶输入";
        case 7: return L"食物丰度";
        default: return L"平稳";
        }
    }

    // Pick the nearest individual to a bottle-space point.  Each kind gets its
    // own hit ellipse: plants are tall columns and animals are wider than they
    // are tall, so a plain circle either missed the visible body or grabbed the
    // wrong neighbour.  Ties are resolved by normalised distance, which keeps
    // the closest silhouette on top.
    bool creatureAt(float x, float y, float radius, CreatureInfo& out) const {
        float best = 1.0f;
        int bestKind = -1;
        int bestIndex = -1;
        auto consider = [&](float cx, float cy, float rx, float ry,
                            int kind, int index) {
            const float dx = (cx - x) / rx;
            const float dy = (cy - y) / ry;
            const float d = dx * dx + dy * dy;
            if (d <= 1.0f && d < best) {
                best = d;
                bestKind = kind;
                bestIndex = index;
            }
        };
        for (int i = 0; i < shrimpCount_; ++i)
            consider(shrimp_[i].x, shrimp_[i].y, radius * 0.8f, radius * 0.7f, 0, i);
        for (int i = 0; i < fishCount_; ++i)
            consider(fish_[i].x, fish_[i].y, radius * 1.3f, radius * 0.7f, 1, i);
        for (int i = 0; i < damselCount_; ++i)
            consider(damsel_[i].x, damsel_[i].y, radius * 0.95f, radius * 0.6f, 2, i);
        for (int i = 0; i < wormCount_; ++i)
            consider(worms_[i].x, 92.0f, radius * 0.95f, radius * 0.5f, 3, i);
        for (int i = 0; i < tubifexCount_; ++i)
            consider(tubifex_[i].x, tubifex_[i].y, radius * 0.75f, radius * 0.5f, 4, i);
        for (int i = 0; i < plantCount_; ++i)
            // Fronds reach well above the root, so the column spans most of
            // the water.  A fish sitting inside it still wins on distance.
            consider(plants_[i].x, 60.0f, radius * 0.7f, radius * 2.0f, 5, i);
        if (bestKind < 0) return false;
        return describe(bestKind, bestIndex, out);
    }

    bool creatureById(uint32_t id, int kind, CreatureInfo& out) const {
        if (id == 0) return false;
        int index = -1;
        switch (kind) {
        case 0:
            for (int i = 0; i < shrimpCount_; ++i) if (shrimp_[i].id == id) index = i;
            break;
        case 1:
            for (int i = 0; i < fishCount_; ++i) if (fish_[i].id == id) index = i;
            break;
        case 2:
            for (int i = 0; i < damselCount_; ++i) if (damsel_[i].id == id) index = i;
            break;
        case 3:
            for (int i = 0; i < wormCount_; ++i) if (worms_[i].id == id) index = i;
            break;
        case 4:
            for (int i = 0; i < tubifexCount_; ++i) if (tubifex_[i].id == id) index = i;
            break;
        case 5:
            for (int i = 0; i < plantCount_; ++i) if (plants_[i].id == id) index = i;
            break;
        default:
            return false;
        }
        return index >= 0 && describe(kind, index, out);
    }

    void placeDecor(int type, float x, float y) {
        type = std::clamp(type, 0, 2);
        if (decorCount_ >= kMaxDecors) {
            for (int i = 0; i < decorCount_ - 1; ++i) decors_[i] = decors_[i + 1];
            --decorCount_;
        }
        Decor d;
        d.type = type;
        d.x = std::clamp(x, 16.0f, 304.0f);
        d.y = std::clamp(y, 82.0f, 104.0f);
        d.scale = (type == 1) ? 1.0f : (type == 2 ? 0.85f : 0.95f);
        d.variant = rndInt(0, 2);
        d.id = nextId_++;
        decors_[decorCount_++] = d;
        if (type == 2) phosphorus_ = std::min(100.0f, phosphorus_ + 1.5f);
    }

    // Scenery always rests on the bottom, so the click's horizontal position
    // is what identifies it.  Matching y as well made clicks on the visible
    // pebble fail whenever it had been dropped from higher up the water.
    bool removeDecorAt(float x, float /*y*/) {
        int best = -1;
        float bestD = 16.0f * 16.0f;
        for (int i = 0; i < decorCount_; ++i) {
            const float dx = decors_[i].x - x;
            const float d = dx * dx;
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best < 0) return false;
        for (int i = best; i < decorCount_ - 1; ++i) decors_[i] = decors_[i + 1];
        --decorCount_;
        return true;
    }

    // --- persistence ---------------------------------------------------------
    // Everything that makes this specific pond unique: phase clocks, entity
    // arrays, egg banks, the micro-habitat grid and the RNG stream.  The blob
    // is tagged with a layout checksum, so a build with different struct sizes
    // starts a fresh pond instead of loading garbage.
    bool saveState(std::FILE* file) const {
        using namespace savestate;
        if (!writeAll(file, kMagic, kVersion, layoutTag(),
                      light_, oxygen_, humus_, nutrient_, night_, volume_,
                      agitation_, batteryScale_, nutrientBonus_, plantO2_,
                      plantBiomass_, resting_, dayPhase_, seasonPhase_,
                      daylight_, seasonWater_, temperature_, q10_, gridFrame_,
                      simTime_, animTime_, feedCooldown_, phosphorus_)) {
            return false;
        }
        if (!writePod(file, sediment_)) return false;
        if (!writeAll(file, plantCount_, shrimpCount_, damselCount_, fishCount_,
                      wormCount_, tubifexCount_, corpseCount_, eggCount_,
                      startleCount_, foodCount_, lightCount_)) {
            return false;
        }
        if (!writeAll(file, fishEggs_, fishEggTimer_, damselEggs_, damselEggTimer_,
                      shrimpEggs_, shrimpEggTimer_, fishEggGene_, damselEggGene_,
                      shrimpEggGene_, fishEggBankTimer_, damselEggBankTimer_,
                      shrimpEggBankTimer_, tubifexEggs_, tubifexEggTimer_,
                      wormCysts_, wormCystTimer_, plantSeeds_, plantSeedTimer_,
                      plantTypeSeeds_, plantTypeSeedTimer_, plantTypeSeedX_)) {
            return false;
        }
        if (!writePod(file, refugeTimer_)) return false;
        if (!writePod(file, plantTypeRefugeTimer_)) return false;
        if (!writePod(file, events_) || !writePod(file, decors_)) return false;
        if (!writeAll(file, eventCount_, decorCount_, nextId_, eventCooldown_)) {
            return false;
        }
        if (!writePod(file, plants_) || !writePod(file, shrimp_)
            || !writePod(file, damsel_) || !writePod(file, fish_)
            || !writePod(file, worms_) || !writePod(file, tubifex_)
            || !writePod(file, corpses_) || !writePod(file, eggs_)) {
            return false;
        }
        if (!writePod(file, startleFields_) || !writePod(file, foodPulses_)
            || !writePod(file, lightPatches_) || !writePod(file, grid_)) {
            return false;
        }
        std::ostringstream out;
        out << rng_;
        const std::string text = out.str();
        const uint32_t length = static_cast<uint32_t>(text.size());
        if (!writePod(file, length)) return false;
        if (!text.empty() && std::fwrite(text.data(), 1, text.size(), file) != text.size()) {
            return false;
        }
        // Optional trailing block: the shore's independent shrubs.  Older
        // saves end at the RNG text above and simply regrow a fresh shore.
        if (!writePod(file, shrubCount_)) return false;
        if (!writePod(file, shrubs_)) return false;
        // Another optional trailing block: surface duckweed rafts.  Saves
        // written before the rafts existed keep the freshly reset mat.
        if (!writePod(file, raftCount_)) return false;
        if (!writePod(file, rafts_)) return false;
        // Another optional trailing block: how rain-soaked the shore is.
        // Foliage and splash stain are recomputed from live shrubs/weather.
        return writePod(file, bankMoisture_);
    }

    bool loadState(std::FILE* file) {
        using namespace savestate;
        uint32_t magic = 0;
        uint32_t version = 0;
        uint32_t layout = 0;
        if (!readAll(file, magic, version, layout)) return false;
        if (magic != kMagic || version != kVersion || layout != layoutTag()) {
            return false;
        }
        if (!readAll(file, light_, oxygen_, humus_, nutrient_, night_, volume_,
                     agitation_, batteryScale_, nutrientBonus_, plantO2_,
                     plantBiomass_, resting_, dayPhase_, seasonPhase_,
                     daylight_, seasonWater_, temperature_, q10_, gridFrame_,
                     simTime_, animTime_, feedCooldown_, phosphorus_)) {
            return false;
        }
        if (!readPod(file, sediment_)) return false;
        if (!readAll(file, plantCount_, shrimpCount_, damselCount_, fishCount_,
                     wormCount_, tubifexCount_, corpseCount_, eggCount_,
                     startleCount_, foodCount_, lightCount_)) {
            return false;
        }
        if (plantCount_ < 0 || plantCount_ > kMaxPlants
            || shrimpCount_ < 0 || shrimpCount_ > kMaxShrimp
            || damselCount_ < 0 || damselCount_ > kMaxDamsel
            || fishCount_ < 0 || fishCount_ > kMaxFish
            || wormCount_ < 0 || wormCount_ > kMaxWorms
            || tubifexCount_ < 0 || tubifexCount_ > kMaxTubifex
            || corpseCount_ < 0 || corpseCount_ > kMaxCorpses
            || eggCount_ < 0 || eggCount_ > kMaxEggs
            || startleCount_ < 0 || startleCount_ > 4
            || foodCount_ < 0 || foodCount_ > 3
            || lightCount_ < 0 || lightCount_ > 3) {
            return false;
        }
        if (!readAll(file, fishEggs_, fishEggTimer_, damselEggs_, damselEggTimer_,
                     shrimpEggs_, shrimpEggTimer_, fishEggGene_, damselEggGene_,
                     shrimpEggGene_, fishEggBankTimer_, damselEggBankTimer_,
                     shrimpEggBankTimer_, tubifexEggs_, tubifexEggTimer_,
                     wormCysts_, wormCystTimer_, plantSeeds_, plantSeedTimer_,
                     plantTypeSeeds_, plantTypeSeedTimer_, plantTypeSeedX_)) {
            return false;
        }
        if (!readPod(file, refugeTimer_)) return false;
        if (!readPod(file, plantTypeRefugeTimer_)) return false;
        if (!readPod(file, events_) || !readPod(file, decors_)) return false;
        if (!readAll(file, eventCount_, decorCount_, nextId_, eventCooldown_)) {
            return false;
        }
        if (eventCount_ < 0 || eventCount_ > kMaxEvents
            || decorCount_ < 0 || decorCount_ > kMaxDecors) {
            return false;
        }
        if (!readPod(file, plants_) || !readPod(file, shrimp_)
            || !readPod(file, damsel_) || !readPod(file, fish_)
            || !readPod(file, worms_) || !readPod(file, tubifex_)
            || !readPod(file, corpses_) || !readPod(file, eggs_)) {
            return false;
        }
        if (!readPod(file, startleFields_) || !readPod(file, foodPulses_)
            || !readPod(file, lightPatches_) || !readPod(file, grid_)) {
            return false;
        }
        uint32_t length = 0;
        if (!readPod(file, length) || length > 8192) return false;
        std::string text(length, '\0');
        if (length != 0
            && std::fread(text.data(), 1, length, file) != length) {
            return false;
        }
        std::istringstream in(text);
        if (!(in >> rng_)) return false;
        // Optional trailing shore-shrub block.  Absent in older saves, in
        // which case the fresh shore from reset() is kept as-is.
        int savedShrubs = 0;
        if (readPod(file, savedShrubs) && savedShrubs >= 0
            && savedShrubs <= kMaxShrubs && readPod(file, shrubs_)) {
            shrubCount_ = savedShrubs;
        }
        // Optional trailing duckweed block.  Absent in older saves, in which
        // case the fresh mat from reset() is kept.
        int savedRafts = 0;
        if (readPod(file, savedRafts) && savedRafts >= 0
            && savedRafts <= kMaxRafts && readPod(file, rafts_)) {
            raftCount_ = savedRafts;
            // Saves made before the mat was kept clear of the border reeds may
            // hold rafts under them, where they would be invisible.
            for (int i = 0; i < raftCount_; ++i) {
                rafts_[static_cast<size_t>(i)].x =
                    std::clamp(rafts_[static_cast<size_t>(i)].x,
                               kRaftMinX, kRaftMaxX);
            }
        }
        // Optional trailing shore-bank moisture block.
        float savedMoisture = 0.0f;
        if (readPod(file, savedMoisture)) {
            bankMoisture_ = std::clamp(savedMoisture, 0.08f, 1.0f);
        }
#ifdef ECO_DEBUG_STATS
        std::fprintf(stderr, "[state] loaded t=%.1f plants=%d shrimp=%d "
                             "fish=%d damsel=%d tubifex=%d eggs=%d\n",
                     static_cast<double>(simTime_), plantCount_, shrimpCount_,
                     fishCount_, damselCount_, tubifexCount_, eggCount_);
        std::fflush(stderr);
#endif
        return true;
    }

private:
    bool describe(int kind, int index, CreatureInfo& out) const {
        out = CreatureInfo{};
        out.valid = true;
        out.kind = kind;
        switch (kind) {
        case 0: {
            if (index < 0 || index >= shrimpCount_) return false;
            const Shrimp& s = shrimp_[index];
            out.id = s.id; out.x = s.x; out.y = s.y;
            out.age = s.age; out.energy = s.energy; out.health = s.health;
            out.growth = s.growth; out.epiphyte = s.epiphyte;
            out.breedCooldown = s.breedCooldown;
            out.stage = s.stage; out.g = s.g;
            break;
        }
        case 1: {
            if (index < 0 || index >= fishCount_) return false;
            const Fish& f = fish_[index];
            out.id = f.id; out.x = f.x; out.y = f.y;
            out.age = f.age; out.energy = f.energy; out.health = f.health;
            out.growth = f.growth; out.epiphyte = f.epiphyte;
            out.breedCooldown = f.breedCooldown;
            out.stage = f.stage; out.g = f.g; out.territory = f.territory;
            break;
        }
        case 2: {
            if (index < 0 || index >= damselCount_) return false;
            const Damsel& d = damsel_[index];
            out.id = d.id; out.x = d.x; out.y = d.y;
            out.age = d.age; out.energy = d.energy; out.health = d.health;
            out.growth = d.growth; out.epiphyte = d.epiphyte;
            out.breedCooldown = d.breedCooldown;
            out.stage = d.stage; out.g = d.g;
            break;
        }
        case 3: {
            if (index < 0 || index >= wormCount_) return false;
            const Worm& w = worms_[index];
            out.id = w.id; out.x = w.x; out.y = 96.0f;
            out.age = w.age; out.energy = w.energy; out.health = w.health;
            out.growth = w.growth; out.stage = w.stage; out.g = w.g;
            out.breedCooldown = w.breedCooldown;
            break;
        }
        case 4: {
            if (index < 0 || index >= tubifexCount_) return false;
            const Tubifex& t = tubifex_[index];
            out.id = t.id; out.x = t.x; out.y = t.y;
            out.age = t.age; out.energy = t.energy; out.health = t.health;
            out.growth = t.growth; out.epiphyte = t.epiphyte;
            out.breedCooldown = t.breedCooldown;
            out.stage = t.stage; out.g = t.g;
            break;
        }
        case 5: {
            if (index < 0 || index >= plantCount_) return false;
            const Plant& p = plants_[index];
            out.id = p.id; out.x = p.x; out.y = 86.0f;
            out.age = p.age; out.energy = p.energy; out.health = p.health;
            out.growth = p.growth; out.epiphyte = p.epiphyte;
            out.stage = p.stage; out.plantType = p.type;
            out.dying = p.dying; out.pg = p.g;
            break;
        }
        default:
            return false;
        }
        return true;
    }

    static uint32_t layoutTag() {
        return static_cast<uint32_t>(
            sizeof(Plant) ^ sizeof(Shrimp) ^ sizeof(Damsel) ^ sizeof(Fish)
            ^ sizeof(Worm) ^ sizeof(Tubifex) ^ sizeof(Corpse) ^ sizeof(Egg)
            ^ sizeof(MicroCell) ^ sizeof(StartleField) ^ sizeof(FoodPulse)
            ^ sizeof(LightPatch) ^ sizeof(Genes) ^ sizeof(PlantGenes)
            ^ sizeof(EcoEvent) ^ sizeof(Decor));
    }

    static constexpr float kTwoPi = 6.283185307179586f;

    float rnd(float lo, float hi) {
        std::uniform_real_distribution<float> d(lo, hi);
        return d(rng_);
    }
    int rndInt(int lo, int hi) {
        std::uniform_int_distribution<int> d(lo, hi);
        return d(rng_);
    }

    Genes makeGenes(float base) {
        Genes g{
            std::clamp(base + rnd(-0.055f, 0.055f), 0.88f, 1.12f),
            std::clamp(base + rnd(-0.060f, 0.060f), 0.88f, 1.12f),
            std::clamp(base + rnd(-0.060f, 0.060f), 0.88f, 1.12f)
        };
        g.temp = std::clamp(base + rnd(-0.07f, 0.07f), 0.85f, 1.15f);
        g.lineage = std::clamp((g.size - 1.0f) * 2.4f
                             + (g.speed - 1.0f) * 1.3f
                             + (g.oxygen - 1.0f) * 1.4f
                             + (g.temp - 1.0f) * 1.0f, -1.0f, 1.0f);
        return g;
    }
    Genes childGenes(const Genes& p) {
        Genes g{
            std::clamp(p.size + rnd(-0.045f, 0.045f), 0.88f, 1.12f),
            std::clamp(p.speed + rnd(-0.050f, 0.050f), 0.88f, 1.12f),
            std::clamp(p.oxygen + rnd(-0.050f, 0.050f), 0.88f, 1.12f)
        };
        g.temp = std::clamp(p.temp + rnd(-0.060f, 0.060f), 0.85f, 1.15f);
        // Inbreeding depression: a small population occasionally passes on a
        // mildly deleterious allele.  It weakens the individual but is never
        // lethal, and the hard floor keeps the lineage recoverable.
        if (rnd(0.0f, 1.0f) < 0.14f) {
            const float drag = rnd(0.010f, 0.026f);
            g.size = std::clamp(g.size - drag, 0.88f, 1.12f);
            g.oxygen = std::clamp(g.oxygen - drag, 0.88f, 1.12f);
        }
        g.lineage = std::clamp(p.lineage * 0.82f
                             + (g.size - 1.0f) * 0.8f
                             + (g.speed - 1.0f) * 0.5f
                             + (g.oxygen - 1.0f) * 0.5f
                             + (g.temp - 1.0f) * 0.4f
                             + rnd(-0.05f, 0.05f), -1.0f, 1.0f);
        return g;
    }
    PlantGenes childPlantGenes(const PlantGenes& p) {
        return PlantGenes{
            std::clamp(p.height + rnd(-0.055f, 0.055f), 0.88f, 1.12f),
            std::clamp(p.density + rnd(-0.070f, 0.070f), 0.86f, 1.14f),
            std::clamp(p.shade + rnd(-0.050f, 0.050f), 0.88f, 1.12f),
            std::clamp(p.graze + rnd(-0.055f, 0.055f), 0.88f, 1.12f)
        };
    }

    static int stageFromGrowth(float growth) {
        return (growth < 0.35f) ? 0 : (growth < 0.70f ? 1 : 2);
    }
    // Ontogenetic maturation: an animal also grows with age between meals.
    // Nutrition and health set the pace, so a starving individual stalls.
    // Growth used to come only from captures, which left damselfly nymphs and
    // bitterling juvenile for their entire life even in a well-fed bottle.
    static float maturationStep(float energy, float health, float rate, float dt) {
        const float nutrition = std::clamp(energy / 62.0f, 0.10f, 1.0f)
                              * (0.35f + 0.65f * std::clamp(health, 0.0f, 1.0f));
        return dt * rate * nutrition;
    }
    static float motionScale(float growth) {
        return 0.58f + 0.42f * std::clamp(growth, 0.0f, 1.0f);
    }
    static float toleranceScale(float growth) {
        return 1.35f - 0.35f * std::clamp(growth, 0.0f, 1.0f);
    }
    // Individual longevity, not a brood-wide number.  Siblings that hatch in
    // the same clutch used to reach old age on the same day and die together;
    // a small, stable spread per individual keeps the deaths staggered so a
    // single generation cannot vanish all at once.
    static float lifespanScale(const Genes& g, std::uint32_t id) {
        const float base = 0.85f + 0.15f * g.size;
        const float spread = 0.86f + 0.28f
            * static_cast<float>(art::hash2(static_cast<int>(id), 17) % 1000u) / 1000.0f;
        return base * spread;
    }
    // Density-dependent mortality: the fewer members of a species are left,
    // the more room, food and clean water each survivor has, so environmental
    // stress costs them less.  This is the demographic brake that makes a
    // total wipe practically impossible without ever conjuring an animal:
    // the rarer a lineage becomes, the harder it is to kill the last few.
    static float rareSpeciesRelief(int count, int cap) {
        if (cap <= 0) return 1.0f;
        const float frac = static_cast<float>(count) / static_cast<float>(cap);
        // A sparse lineage is released from crowding stress long before it is
        // common: at a tenth of capacity a survivor takes roughly a third of
        // the damage a crowded individual would, which is what stops a bad
        // night from taking the last few animals with it.
        return std::clamp(0.32f + 0.68f * frac / 0.50f, 0.32f, 1.0f);
    }
    static float plantTypeO2(int type) {
        return (type == 1 ? 1.25f : (type == 2 ? 0.95f : 0.70f));
    }
    static float plantTypeShelter(int type) {
        return (type == 1 ? 1.25f : (type == 2 ? 0.95f : 0.70f));
    }

    void addInitialPlant(int type, float x, float growth, float heightGene) {
        if (plantCount_ >= g_settings.maxPlants) return;
        Plant p;
        p.type = type;
        p.x = x;
        p.phase = rnd(0.0f, kTwoPi);
        p.growth = growth;
        p.energy = rnd(45.0f, 75.0f);
        p.health = rnd(0.75f, 1.0f);
        p.age = rnd(20.0f, 120.0f);
        p.stage = (growth < 0.25f) ? 0 : (growth < 0.55f ? 1 : 2);
        p.flip = rndInt(0, 1);
        p.g.height = heightGene;
        p.g.density = rnd(0.9f, 1.1f);
        p.g.shade = rnd(0.9f, 1.1f);
        p.g.graze = rnd(0.9f, 1.1f);
        p.id = nextId_++;
        plants_[plantCount_++] = p;
    }

    void mutatePlantStage(Plant& p) {
        if (p.dying) { p.stage = 3; return; }
        if (p.age > 900.0f || p.health <= 0.02f) {
            p.dying = true;
            p.wilt = 0.0f;
            p.stage = 3;
            return;
        }
        if (p.age > 720.0f || p.health < 0.35f) {
            p.stage = 3;
            return;
        }
        const int s = stageFromGrowth(p.growth);
        p.stage = (s > 2) ? 2 : s;
    }

    void spawnPlant(int type, float x, const PlantGenes& g) {
        if (plantCount_ >= g_settings.maxPlants) return;
        float site = x;
        if (rnd(0.0f, 1.0f) < 0.35f) {
            // Some propagules drift before settling.  Without this every
            // generation roots within ±16 px of its parent and the whole
            // community slowly contracts into the middle of the bottle.
            float bestX = site, bestGap = -1.0f;
            for (int k = 0; k < 6; ++k) {
                const float candidate = rnd(24.0f, 296.0f);
                float gap = 1e9f;
                for (int j = 0; j < plantCount_; ++j) {
                    gap = std::min(gap, std::fabs(candidate - plants_[j].x));
                }
                if (gap > bestGap) { bestGap = gap; bestX = candidate; }
            }
            site = bestX;
        }
        Plant p;
        p.type = type;
        p.x = std::clamp(site + rnd(-16.0f, 16.0f), 10.0f, 306.0f);
        p.phase = rnd(0.0f, kTwoPi);
        p.growth = 0.05f;
        p.energy = 38.0f;
        p.health = 1.0f;
        p.age = 0.0f;
        p.stage = 0;
        p.flip = rndInt(0, 1);
        p.g = childPlantGenes(g);
        p.id = nextId_++;
        plants_[plantCount_++] = p;
    }

    void removePlant(int index, bool recycle) {
        if (index < 0 || index >= plantCount_) return;
        if (recycle) {
            addSediment(plants_[index].x, 4.0f);
            humus_ = std::min(100.0f, humus_ + 2.0f);
            nutrient_ = std::min(100.0f, nutrient_ + 1.0f);
            // Tissue phosphate returns to the water column, closing the
            // plant half of the P cycle.
            phosphorus_ = std::min(100.0f, phosphorus_
                        + 0.6f + 1.4f * plants_[index].growth);
            const int type = plants_[index].type;
            if (type >= 0 && type < 3) {
                plantTypeSeeds_[type] = std::min(3, plantTypeSeeds_[type] + 1);
                plantTypeSeedX_[type] = plants_[index].x;
                plantTypeSeedTimer_[type] = 0.0f;
            }
        }
        for (int i = index; i < plantCount_ - 1; ++i) plants_[i] = plants_[i + 1];
        --plantCount_;
    }

    float plantShelterAt(float x) const {
        float shelter = 0.0f;
        for (int i = 0; i < plantCount_; ++i) {
            const Plant& p = plants_[i];
            if (p.dying) continue;
            const float proximity = 1.0f - std::min(1.0f, std::fabs(p.x - x) / 34.0f);
            shelter += proximity * plantTypeShelter(p.type) * p.growth
                     * p.health * p.g.density * (p.stage >= 2 ? 1.0f : 0.55f);
        }
        return std::clamp(shelter * 0.42f, 0.0f, 1.0f);
    }

    // Local surface cover of the duckweed mat at a given x, 0..1.  Used for
    // shallow shading, oxygen exchange and as juvenile cover near the top.
    float duckweedCoverAt(float x) const {
        float cover = 0.0f;
        for (int i = 0; i < raftCount_; ++i) {
            const Raft& r = rafts_[static_cast<size_t>(i)];
            const float prox = 1.0f - std::min(1.0f, std::fabs(r.x - x) / 22.0f);
            cover += prox * std::clamp(r.mass / 1.25f, 0.0f, 1.0f);
        }
        return std::clamp(cover * 0.55f, 0.0f, 1.0f);
    }

    // Extra structural cover for prey hiding directly under a raft.  Fades
    // out below the surface layer, so it cannot protect the whole column.
    float duckweedShelterAt(float x, float y) const {
        const float dy = std::fabs(y - waterSurfaceY());
        if (dy > 15.0f) return 0.0f;
        const float fade = 1.0f - dy / 15.0f;
        return std::clamp(duckweedCoverAt(x) * fade * 0.55f, 0.0f, 0.55f);
    }

    int nearestRaft(float x, float maxDist) const {
        int best = -1;
        float bestD = maxDist;
        for (int i = 0; i < raftCount_; ++i) {
            if (rafts_[static_cast<size_t>(i)].mass <= 0.10f) continue;
            const float d = std::fabs(rafts_[static_cast<size_t>(i)].x - x);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    float nearestPlantX(float x, bool preferTall) const {
        float best = x;
        float score = 1e9f;
        for (int i = 0; i < plantCount_; ++i) {
            if (plants_[i].dying) continue;
            float s = std::fabs(plants_[i].x - x);
            if (preferTall) s -= plants_[i].type == 1 ? 20.0f : 0.0f;
            if (s < score) { score = s; best = plants_[i].x; }
        }
        return best;
    }

    // Stem for a nymph.  Its own nearest stalk is what a settling larva would
    // choose, but every individual also prefers one plant in a stable rotation
    // of the colony, so a brood spreads along the vegetation instead of all
    // piling onto the single closest tall shoot.
    float damselStemX(float x, uint32_t id) const {
        int viable = 0;
        for (int i = 0; i < plantCount_; ++i)
            if (!plants_[i].dying) ++viable;
        if (viable <= 0) return nearestPlantX(x, true);
        const int want = static_cast<int>(id % static_cast<uint32_t>(viable));
        int seen = 0;
        float chosen = x;
        float bestScore = 1e9f;
        for (int i = 0; i < plantCount_; ++i) {
            const Plant& p = plants_[i];
            if (p.dying) continue;
            float s = std::fabs(p.x - x);
            if (p.type == 1) s -= 20.0f;
            // The assigned stem outweighs travel distance: a larva spreading
            // to its own shoot is the point, even when that means crossing
            // some open water.
            if (seen == want) s -= 120.0f;
            ++seen;
            if (s < bestScore) { bestScore = s; chosen = p.x; }
        }
        return chosen;
    }

    int nearestPlant(float x) const {
        int best = -1;
        float bestD = 1e9f;
        for (int i = 0; i < plantCount_; ++i) {
            if (plants_[i].dying) continue;
            const float d = std::fabs(plants_[i].x - x);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    float nearestMaturePlantX(float x) const {
        int best = -1;
        float bestD = 1e9f;
        for (int i = 0; i < plantCount_; ++i) {
            const Plant& p = plants_[i];
            if (p.dying || p.stage < 2 || p.health < 0.45f) continue;
            const float d = std::fabs(p.x - x) - (p.type == 1 ? 18.0f : 0.0f)
                          - p.g.density * 4.0f;
            if (d < bestD) { bestD = d; best = i; }
        }
        return best >= 0 ? plants_[best].x : -1.0f;
    }

    static float fishStrength(const Fish& f) {
        return f.g.size * 0.50f + f.g.speed * 0.28f + f.g.oxygen * 0.22f
             + (f.stage == 2 ? 0.10f : 0.0f);
    }

    // ---------------------------------------------------------------------
    //  Shore shrubs: their own slow phenology.  Fully disconnected from the
    //  water chemistry, so whatever the pond does they keep their own rhythm.
    // ---------------------------------------------------------------------
    void spawnShrub(float x, float growth) {
        if (shrubCount_ >= kMaxShrubs) return;
        Shrub& s = shrubs_[static_cast<size_t>(shrubCount_++)];
        s = Shrub{};
        s.x = std::clamp(x, 12.0f, 307.0f);
        s.growth = std::clamp(growth, 0.0f, 1.0f);
        s.leaf = 0.30f + 0.70f * s.growth;
        s.seed = static_cast<uint32_t>(rnd(0.0f, 100000.0f));
        s.swaySeed = rnd(0.0f, 6.28f);
        s.seedTimer = rnd(50.0f, 95.0f);
        s.age = rnd(0.0f, 90.0f);
    }

    void removeShrub(int i) {
        if (i < 0 || i >= shrubCount_) return;
        for (int j = i; j + 1 < shrubCount_; ++j) {
            shrubs_[static_cast<size_t>(j)] = shrubs_[static_cast<size_t>(j + 1)];
        }
        --shrubCount_;
    }

    void updateShrubs(float dt) {
        const float local = seasonPhase_ / kTwoPi * 4.0f;
        const int season = static_cast<int>(local) & 3;
        const float grow = (season == 0) ? 0.020f
                         : (season == 1) ? 0.008f
                         : (season == 2) ? 0.002f : 0.0f;
        for (int i = shrubCount_ - 1; i >= 0; --i) {
            Shrub& s = shrubs_[static_cast<size_t>(i)];
            s.age += dt;
            s.growth = std::min(1.0f, s.growth + dt * grow * s.vigor);

            // Foliage: full in spring and summer, thinning in autumn, bare
            // through the winter dormancy.
            const float targetLeaf = (season == 2) ? 0.72f : (season == 3) ? 0.0f : 1.0f;
            s.leaf = std::clamp(s.leaf + (targetLeaf - s.leaf) * std::min(1.0f, dt * 0.30f),
                                0.0f, 1.0f);
            const float targetRust = (season == 2) ? 1.0f : (season == 3) ? 0.65f : 0.0f;
            s.colorBlend += (targetRust - s.colorBlend) * std::min(1.0f, dt * 0.22f);
            const float targetVigor = (season == 3) ? 0.42f : 1.0f;
            s.vigor += (targetVigor - s.vigor) * std::min(1.0f, dt * 0.18f);

            // Old age catches up with a shrub during its dormant winter.
            if (s.age > 420.0f && s.leaf < 0.15f
                && rnd(0.0f, 1.0f) < dt * 0.010f) {
                removeShrub(i);
                continue;
            }
            // Autumn seeding: a mature shrub drops one new shoot nearby.
            if (season == 2 && s.growth > 0.70f && shrubCount_ < kMaxShrubs) {
                s.seedTimer -= dt;
                if (s.seedTimer <= 0.0f) {
                    s.seedTimer = rnd(50.0f, 95.0f);
                    const float nx = std::clamp(s.x + rnd(-26.0f, 26.0f), 14.0f, 305.0f);
                    bool clear = true;
                    for (int j = 0; j < shrubCount_; ++j) {
                        if (std::fabs(shrubs_[static_cast<size_t>(j)].x - nx) < 16.0f) {
                            clear = false;
                            break;
                        }
                    }
                    if (clear) spawnShrub(nx, 0.18f);
                }
            }
        }
        // Hard guarantee: the shore may thin out in a hard winter, but it
        // never dies out.  Below five living shrubs a dormant root sends up
        // a new shoot every few seconds, whatever the season.
        if (shrubCount_ < 5) {
            shrubRescue_ += dt;
            if (shrubRescue_ >= 6.0f) {
                shrubRescue_ = 0.0f;
                float bestX = 160.0f;
                float bestGap = -1.0f;
                for (int k = 0; k <= 24; ++k) {
                    const float cand = 14.0f + static_cast<float>(k) * 12.0f;
                    float gap = 1.0e9f;
                    for (int j = 0; j < shrubCount_; ++j) {
                        gap = std::min(gap, std::fabs(shrubs_[static_cast<size_t>(j)].x - cand));
                    }
                    if (gap > bestGap) { bestGap = gap; bestX = cand; }
                }
                spawnShrub(bestX, 0.16f);
            }
        } else {
            shrubRescue_ = 0.0f;
        }
    }

    // ---------------------------------------------------------------------
    //  Shore bank: a one-way donor to the pond.
    //  It reads the weather and its own shrubs, then releases small amounts of
    //  water, humus, nitrogen and phosphate downhill.  No oxygen, animal count
    //  or water chemistry is ever read back into the shrub layer, so the land
    //  keeps its independent life cycle.
    // ---------------------------------------------------------------------
    void updateBank(float dt) {
        const int season = static_cast<int>(seasonPhase_ / kTwoPi * 4.0f) & 3;
        float rain = 0.0f;
        float drought = 0.0f;
        float heat = 0.0f;
        for (int i = 0; i < eventCount_; ++i) {
            const EcoEvent& e = events_[i];
            const float strength = eventEnvelope(e) * std::clamp(e.intensity, 0.0f, 1.2f);
            if (e.type == 0) rain = std::max(rain, strength);
            else if (e.type == 1) drought = std::max(drought, strength);
            else if (e.type == 2) heat = std::max(heat, strength);
        }

        // Damp clay holds water for a while.  Rain soaks in, drought and heat
        // dry it out; spring and winter keep a floor of residual moisture.
        const float wetSeason = (season == 0 || season == 3) ? 1.0f : 0.0f;
        bankMoisture_ = std::clamp(bankMoisture_
            + dt * (0.045f * rain + 0.010f * wetSeason
                    - 0.030f * drought - 0.020f * heat - 0.004f),
            0.08f, 1.0f);

        // Total living foliage on the shore, read one-way.
        float foliage = 0.0f;
        for (int i = 0; i < shrubCount_; ++i) {
            foliage += shrubs_[static_cast<size_t>(i)].growth
                     * shrubs_[static_cast<size_t>(i)].leaf;
        }
        shoreFoliage_ = std::clamp(foliage / 6.0f, 0.0f, 1.0f);

        // Runoff: only moisture above field capacity drains downhill.  The
        // trickle is far below plant uptake, so it tops up a lean pool instead
        // of driving an algal bloom.
        const float seep = std::max(0.0f, bankMoisture_ - 0.45f)
                         * (0.010f + 0.010f * shoreFoliage_);
        nutrient_ = std::min(100.0f, nutrient_ + dt * seep * 1.6f);
        phosphorus_ = std::min(100.0f, phosphorus_ + dt * seep * 0.9f);
        humus_ = std::min(100.0f, humus_ + dt * seep * 0.8f);

        // Autumn litter follows the actual shore stand rather than a dice
        // roll, and stays a trickle even at full foliage.
        if (season == 2) {
            humus_ = std::min(100.0f, humus_ + dt * 0.006f * shoreFoliage_);
            nutrient_ = std::min(100.0f, nutrient_ + dt * 0.0012f * shoreFoliage_);
        }

        // Rain beating on the bank lifts a whisper of clay and humic matter
        // into the top of the water.  The storm event still dilutes tannin
        // overall; this only stains the first two rows under the surface.
        bankWash_ = std::max(bankWash_ - dt * 0.25f, std::clamp(rain, 0.0f, 1.0f));

        // Summer insects: a mature shore now and then drops a morsel, giving
        // surface fish a tiny snack.  Three specks, one pulse every 30-55 s.
        if (season == 1 && shoreFoliage_ > 0.30f) {
            insectTimer_ -= dt;
            if (insectTimer_ <= 0.0f) {
                insectTimer_ = rnd(30.0f, 55.0f);
                if (foodCount_ < static_cast<int>(foodPulses_.size())) {
                    FoodPulse p;
                    p.x = std::clamp(rnd(18.0f, 302.0f), 16.0f, 304.0f);
                    p.y = std::clamp(waterSurfaceY() + rnd(10.0f, 20.0f), 30.0f, 74.0f);
                    p.specks = 3;
                    foodPulses_[foodCount_++] = p;
                }
            }
        } else {
            insectTimer_ = std::max(insectTimer_, 18.0f);
        }
    }

    // ---------------------------------------------------------------------
    //  Duckweed: the free-floating surface producer.  Slow, conservative
    //  coupling to the water chemistry and to shrimp grazing; winter leaves
    //  a couple of turions instead of wiping the mat out.
    // ---------------------------------------------------------------------
    void spawnRaft(float x, float mass) {
        if (raftCount_ >= kMaxRafts) return;
        Raft& r = rafts_[static_cast<size_t>(raftCount_++)];
        r = Raft{};
        r.x = std::clamp(x, kRaftMinX, kRaftMaxX);
        r.mass = std::clamp(mass, 0.05f, 1.25f);
        r.bobSeed = rnd(0.0f, kTwoPi);
        r.age = rnd(0.0f, 40.0f);
    }

    void updateDuckweed(float dt) {
        const int season = static_cast<int>(seasonPhase_ / kTwoPi * 4.0f) & 3;
        const float phenology = (season == 0) ? 1.15f
                              : (season == 1) ? 0.95f
                              : (season == 2) ? 0.45f : 0.20f;
        const float npLimit = std::min(nutrient_, phosphorus_ * 1.25f);
        const float nutrientFactor = std::clamp(npLimit / 60.0f, 0.15f, 1.0f);
        const float lightFactor = 0.25f + 0.75f * (light_ / 100.0f);
        const float thin = (season == 3) ? 0.030f : (season == 2) ? 0.006f : 0.0f;
        // Ongoing heat speeds the mat up; heavy rain pummels it back down.
        // Both are gentle multipliers and never remove a raft outright.
        float rain = 0.0f;
        float heat = 0.0f;
        for (int i = 0; i < eventCount_; ++i) {
            const float strength = std::clamp(events_[i].intensity, 0.0f, 1.0f);
            if (events_[i].type == 0) rain = std::max(rain, strength);
            else if (events_[i].type == 2) heat = std::max(heat, strength);
        }
        const float warmBoost = 1.0f + 0.22f * heat;
        const float rainThin = 0.0028f * rain;

        float totalMass = 0.0f;
        for (int i = raftCount_ - 1; i >= 0; --i) {
            Raft& r = rafts_[static_cast<size_t>(i)];
            r.age += dt;
            const float matDensity = std::min(1.0f, r.mass / 1.25f);
            r.mass += dt * 0.010f * phenology * nutrientFactor * lightFactor
                    * std::clamp(temperatureQ10(), 0.55f, 1.35f) * warmBoost
                    * (1.0f - 0.55f * matDensity)
                    * (1.0f - 0.10f * matDensity);
            r.mass -= dt * (thin + rainThin);
            // A raft never disappears on its own.  Winter leaves a small green
            // turion on every raft, and the other seasons bottom out at a thin
            // speck, so the mat can be grazed back but not wiped out.
            r.mass = std::max(r.mass, (season == 3) ? 0.12f : 0.05f);
            // A dense raft buds a small daughter patch nearby in the warm
            // season, roughly one every hundred seconds.  The age clock is
            // used as the timer so no per-frame RNG draw is needed.
            if (r.mass > 0.95f && raftCount_ < kMaxRafts && phenology > 0.40f
                && std::fmod(r.age, 110.0f) < dt) {
                const float nx = std::clamp(r.x + rnd(-34.0f, 34.0f), 12.0f, 302.0f);
                bool clear = true;
                for (int j = 0; j < raftCount_; ++j) {
                    if (std::fabs(rafts_[static_cast<size_t>(j)].x - nx) < 14.0f) {
                        clear = false;
                        break;
                    }
                }
                if (clear) spawnRaft(nx, 0.12f);
            }
            totalMass += r.mass;
        }

        duckweedCover_ = std::clamp(totalMass / (0.85f * static_cast<float>(kMaxRafts)),
                                    0.0f, 1.0f);
        if (totalMass > 0.01f) {
            // Uptake is deliberately small and floored, so the mat trims the
            // water column without locking nitrogen or phosphate at zero.
            if (nutrient_ > 16.0f) {
                nutrient_ = std::max(16.0f, nutrient_ - dt * 0.0011f * totalMass);
            }
            if (phosphorus_ > 8.0f) {
                phosphorus_ = std::max(8.0f, phosphorus_ - dt * 0.00055f * totalMass);
            }
            humus_ = std::min(100.0f, humus_ + dt * 0.0004f * totalMass);
        }
        // Hard guarantee: the mat may shrink to a few turions in winter but
        // never disappears.  Below two rafts a dormant bud floats up.
        if (raftCount_ < 2) {
            raftRescue_ += dt;
            if (raftRescue_ >= 8.0f) {
                raftRescue_ = 0.0f;
                float bestX = 120.0f;
                float bestGap = -1.0f;
                for (int k = 0; k <= 20; ++k) {
                    const float cand = 20.0f + static_cast<float>(k) * 14.0f;
                    float gap = 1.0e9f;
                    for (int j = 0; j < raftCount_; ++j) {
                        gap = std::min(gap, std::fabs(
                            rafts_[static_cast<size_t>(j)].x - cand));
                    }
                    if (gap > bestGap) { bestGap = gap; bestX = cand; }
                }
                spawnRaft(bestX, 0.14f);
            }
        } else {
            raftRescue_ = 0.0f;
        }
    }

    void updatePlants(float dt) {
        const float crowd = static_cast<float>(plantCount_) / static_cast<float>(g_settings.maxPlants);
        plantO2_ = 0.0f;
        plantBiomass_ = 0.0f;

        // Phenology: spring flush, summer steady, autumn decline and winter
        // dormancy.  Winter plants barely grow, so biomass and oxygen output
        // visibly drop even without any lethal mechanic.
        const int seasonNow = static_cast<int>(seasonPhase_ / kTwoPi * 4.0f) & 3;
        const float phenology = (seasonNow == 0) ? 1.10f
                              : (seasonNow == 1) ? 1.00f
                              : (seasonNow == 2) ? 0.86f : 0.58f;

        float tallCover = 0.0f;
        for (int i = 0; i < plantCount_; ++i) {
            if (plants_[i].type == 1 && !plants_[i].dying) tallCover += plants_[i].growth;
        }
        tallCover = std::min(1.0f, tallCover / 3.0f);
        // P3: midsummer canopy competition.  Tall stems intercept a little
        // more light from the low grass, capped at +5 percentage points.
        const float canopyGain = 0.25f + 0.05f * summerFactor();

        for (int i = plantCount_ - 1; i >= 0; --i) {
            Plant& p = plants_[i];
            p.age += dt;

            // Liebig's law of the minimum: growth follows whichever of N or P
            // is scarcer, so a single nutrient can no longer mask a deficit.
            const float npLimit = std::min(nutrient_, phosphorus_ * 1.25f);
            const float nutrientFactor = 0.35f + 0.65f * (npLimit / 100.0f);
            const float sedimentFactor = std::clamp(0.45f + 0.65f
                                        * (sedimentAtX(p.x) / 70.0f), 0.25f, 1.15f);
            const float epiFactor = 1.0f - 0.34f * p.epiphyte;
            float shade = 1.0f - 0.25f * crowd;
            // A dense duckweed mat intercepts some of the incoming light
            // right above this plant.  Capped at 22% so it trims the stand
            // instead of starving it.
            shade *= 1.0f - 0.22f * duckweedCoverAt(p.x);
            if (p.type == 0) shade *= 1.0f - canopyGain * tallCover * (1.15f - 0.15f * p.g.shade);
            const float lightFactor = (light_ / 100.0f) * shade
                                    * (0.85f + 0.15f * p.g.shade)
                                    * (1.0f - 0.12f * (light_ / 100.0f) * p.g.shade);
            const float typeGrowth = (p.type == 1 ? 1.10f : (p.type == 2 ? 0.85f : 1.0f));
            // Seasonal rotation keeps the three forms from settling into one
            // fixed ratio: spring lifts the low cushions, high summer the
            // tall stems, autumn the rosettes.  The swing is only +-15%, so
            // it is a slow succession rather than a die-off.
            const float typeSeason = (p.type == 0)
                ? ((seasonNow == 0) ? 1.15f : (seasonNow == 2) ? 0.92f : 1.0f)
                : (p.type == 1)
                ? ((seasonNow == 1) ? 1.15f : (seasonNow == 3) ? 0.90f : 1.0f)
                : ((seasonNow == 2) ? 1.15f : (seasonNow == 0) ? 0.92f : 1.0f);

            if (!p.dying) {
                // Maintenance respiration used to be a flat 0.5/s whatever the
                // light, which put the break-even point around light 25.  A jar
                // on a dim or idle desktop sits at 15-30 most of the day, so
                // the stand spent that time draining its reserves to zero and
                // could never reach the energy a new offshoot costs.  Respiration
                // now scales with the light actually available, which puts the
                // compensation point near light 7 and leaves a real surplus at
                // desktop brightness.
                const float upkeep = 0.12f + 0.25f * lightFactor;
                p.energy = std::clamp(p.energy + dt * (lightFactor * nutrientFactor
                                  * sedimentFactor * epiFactor * typeGrowth * typeSeason
                                  * 5.0f * phenology - upkeep),
                                  0.0f, 100.0f);
                p.growth = std::clamp(p.growth + dt * (0.006f + 0.020f * lightFactor
                                  * nutrientFactor * sedimentFactor * epiFactor
                                  * typeGrowth * typeSeason)
                                  * phenology
                                  * (1.0f - 0.35f * crowd)
                                  * std::clamp(temperatureQ10(), 0.55f, 1.45f),
                                  0.0f, 1.0f);
                addSediment(p.x, -dt * (0.010f + 0.020f * p.growth));
                // Phosphate uptake is a maintenance draw small enough that the
                // whole stand stays below the mineralisation supply, so the
                // P channel settles at an equilibrium instead of sinking to
                // its floor.
                phosphorus_ = std::max(4.0f, phosphorus_
                                       - dt * (0.0006f + 0.0014f * p.growth));
                const float plantStress = localStressAt(p.x, 72.0f, 5, Genes{});
                p.health -= dt * plantStress * 0.0025f;

                // Recovery used to switch off the moment the stand filled the
                // jar (crowd > 0.95).  That is exactly the state a healthy
                // meadow spends most of its time in, so a full cohort lost
                // health in lockstep, the whole stand reached senescence at
                // once, and recruitment (health > 0.6 and energy > 80) never
                // lined up with a living parent.  Crowding now trims recovery
                // instead of forbidding it, so a dense stand still ages but
                // keeps a reproductive window.
                const float crowdStrain = std::max(0.0f, crowd - 0.80f);
                if (nutrient_ < 22.0f || sedimentAtX(p.x) < 20.0f
                    || light_ < 10.0f) {
                    p.health -= dt * (0.0008f + 0.0008f * crowd);
                } else {
                    p.health = std::min(1.0f, p.health
                                        + dt * 0.0032f * (1.0f - 1.6f * crowdStrain));
                }
                p.health = std::clamp(p.health - dt * (1.0f - p.g.shade) * 0.0004f
                                      - dt * 0.0008f * p.epiphyte,
                                      0.0f, 1.0f);

                mutatePlantStage(p);
                // A parent only needs enough surplus to bank one propagule.
                // The old floor (energy 80 at light 22) meant a dim jar never
                // recruited at all and the stand had to be rebuilt from the
                // finite seed bank instead of from its own living stems.
                // A dim screen (battery, idle desktop) is a shaded day, not a
                // night: weeds keep a slow positive carbon balance down to
                // about light 10, so recruitment should not be gated higher
                // than the light the stand can actually live at.
                if (p.stage == 2 && p.energy > 60.0f && p.health > 0.60f
                    && plantCount_ < g_settings.maxPlants && light_ > 12.0f
                    && sedimentAtX(p.x) > 26.0f && plantStress < 0.65f) {
                    spawnPlant(p.type, p.x, p.g);
                    p.energy -= 30.0f;
                    nutrient_ = std::max(0.0f, nutrient_ - 1.5f);
                    addSediment(p.x, -2.5f);
                }
            } else {
                p.wilt = std::min(1.0f, p.wilt + dt / 8.0f);
                p.health = std::max(0.0f, p.health - dt * 0.04f);
                p.stage = 3;
                if (p.wilt >= 1.0f) {
                    removePlant(i, true);
                    continue;
                }
            }

            const float maturity = (p.stage >= 2) ? 1.0f : (p.stage == 1 ? 0.55f : 0.18f);
            const float viable = p.dying ? 0.20f : 1.0f;
            const float epiO2 = 1.0f - 0.30f * p.epiphyte;
            plantO2_ += plantTypeO2(p.type) * p.growth * p.health
                      * p.g.density * maturity * viable * epiO2;
            plantBiomass_ += plantTypeO2(p.type) * p.growth * p.health
                           * p.g.density * viable;
        }
        plantBiomass_ = std::clamp(plantBiomass_, 0.0f, 8.0f);
        plantO2_ = std::clamp(plantO2_, 0.0f, 6.0f);
        updatePlantSeeds(dt);
    }

    void updatePlantSeeds(float dt) {
        for (int t = 0; t < 3; ++t) {
            bool present = false;
            for (int i = 0; i < plantCount_; ++i) {
                if (!plants_[i].dying && plants_[i].type == t) {
                    present = true;
                    break;
                }
            }
            if (present) {
                plantTypeSeedTimer_[t] = 0.0f;
                continue;
            }
            if (plantTypeSeeds_[t] <= 0 || plantCount_ >= g_settings.maxPlants
                || light_ < 10.0f || oxygen_ < 30.0f) {
                continue;
            }
            const int season = static_cast<int>(seasonPhase_ / kTwoPi * 4.0f) & 3;
            // Succession: each plant type germinates best in its own season,
            // which lets one community hand over to the next instead of all
            // three sprouting at identical rates.
            float seasonBoost = 1.0f;
            if (t == 0) seasonBoost = (season == 2 || season == 3) ? 1.55f : 0.85f;
            else if (t == 1) seasonBoost = (season == 1) ? 1.40f : 0.90f;
            else seasonBoost = (season == 0 || season == 1) ? 1.20f : 0.95f;
            plantTypeSeedTimer_[t] += dt * seasonBoost;
            if (plantTypeSeedTimer_[t] < 24.0f) continue;
            plantTypeSeedTimer_[t] = 0.0f;
            if (sedimentAtX(plantTypeSeedX_[t]) < 18.0f) continue;
            PlantGenes pg{1.0f, 1.0f, 1.0f, 1.0f};
            spawnPlant(t, plantTypeSeedX_[t], pg);
            --plantTypeSeeds_[t];
        }
    }

    void updateShrimp(float dt, float activity) {
        for (int i = shrimpCount_ - 1; i >= 0; --i) {
            Shrimp& s = shrimp_[i];
            s.age += dt;
            s.growth = std::min(1.0f, s.growth + maturationStep(
                s.energy, s.health, 0.0050f, dt));
            s.stage = stageFromGrowth(s.growth);
            const float speedStage = motionScale(s.growth);
            s.energy = std::max(0.0f, s.energy - dt * 0.14f
                                * (0.85f + 0.15f * s.g.size)
                                * (0.90f + 0.20f * s.g.speed * s.g.speed)
                                * (0.72f + 0.28f * stageScale(s.stage))
                                * (1.0f + 0.30f * s.epiphyte)
                                * temperatureQ10());
            s.eatCooldown -= dt;
            s.breedCooldown -= dt;
            s.cleanCooldown -= dt;
            s.startleCooldown -= dt;
            s.startleTimer = std::max(0.0f, s.startleTimer - dt);
            // Reachable band, recomputed as the water level moves.  Every
            // steering goal is clamped into it below so a shrimp can never be
            // driven past a wall (which flipped vx every frame) or pinned
            // under the surface without ever "arriving".
            const float shrimpMinY = std::max(16.0f, waterSurfaceY() + 3.0f);
            const float shrimpGoalMinY = shrimpMinY + 2.0f;
            const float shrimpGoalMaxY = 84.0f;
            constexpr float kShrimpMinX = 9.0f;
            constexpr float kShrimpMaxX = 295.0f;

            const bool juvenile = s.stage == 0;
            // The desktop "resting" state only quiets a well-fed shrimp.  A
            // hungry one still forages, otherwise a few idle minutes would
            // freeze the whole tank and every animal would slowly starve.
            const bool hide = juvenile || (resting_ && s.energy > 62.0f);
            const int plant = nearestPlant(s.x);
            const bool wantsClean = s.cleanCooldown <= 0.0f && plant >= 0
                                 && plants_[plant].epiphyte > 0.08f;
            float tx = s.targetX;
            float ty = s.targetY;
            if (plant >= 0 && (hide || s.energy < 70.0f || wantsClean)) {
                tx = plants_[plant].x;
                ty = 68.0f;
            }
            if (hide && plant >= 0) {
                s.targetPlant = plant;
            }
            if (juvenile) {
                const MicroCell& c = cellAt(s.x, s.y);
                if (c.alarmScent > 0.18f) {
                    float ax = s.x, ay = s.y;
                    float best = 1e9f;
                    for (int fi = 0; fi < fishCount_; ++fi) {
                        const float d = std::fabs(fish_[fi].x - s.x)
                                      + std::fabs(fish_[fi].y - s.y);
                        if (d < best) { best = d; ax = fish_[fi].x; ay = fish_[fi].y; }
                    }
                    // P2: only flee from a predator that is genuinely close.
                    // Once the shrimp has swum clear the scent decays and the
                    // reaction stops by itself, so there is no panic loop.
                    if (best < 60.0f) {
                        tx = s.x + (s.x - ax) * 1.2f;
                        ty = s.y + (s.y - ay) * 1.2f;
                    }
                } else if (c.nutrient > 25.0f) {
                    float cx = 0.0f, cy = 0.0f;
                    int n = 0;
                    for (int sj = 0; sj < shrimpCount_; ++sj) {
                        if (sj == i || shrimp_[sj].stage != 0) continue;
                        if (std::fabs(shrimp_[sj].x - s.x) > 46.0f) continue;
                        cx += shrimp_[sj].x;
                        cy += shrimp_[sj].y;
                        ++n;
                    }
                    if (n > 0) {
                        tx = cx / n + std::sin(animTime_ * 0.7f + i) * 6.0f;
                        ty = cy / n + 3.0f;
                    }
                }
            }

            if (s.health < 0.25f) {
                // Moribund: it stops foraging and sinks to the mud.
                tx = std::clamp(s.x + std::sin(animTime_ * 0.4f + i) * 5.0f, 8.0f, 296.0f);
                ty = 89.0f;
            } else if (s.startleTimer > 0.0f) {
                tx = s.x + (s.x - s.startleX) * 1.7f;
                ty = s.y + (s.y - s.startleY) * 1.3f;
            } else if (!resting_ || s.energy < 80.0f) {
                const FoodPulse* food = nearestFood(s.x, s.y);
                if (food) {
                    const float fdx = food->x - s.x;
                    const float fdy = food->y - s.y;
                    if (fdx * fdx + fdy * fdy < 78.0f * 78.0f) {
                        tx = food->x;
                        ty = food->y;
                    }
                }
            }
            // Every branch above (plant, flee, food, moribund) can name a spot
            // outside the tank.  Fold it back into the reachable band before
            // steering so the shrimp keeps travelling instead of juddering.
            tx = std::clamp(tx, kShrimpMinX, kShrimpMaxX);
            ty = std::clamp(ty, shrimpGoalMinY, shrimpGoalMaxY);
            const float dx = tx - s.x;
            const float dy = ty - s.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (!hide && s.energy >= 70.0f) {
                s.dirTimer -= dt;
                if (s.dirTimer <= 0.0f) {
                    s.dirTimer = rnd(2.5f, 7.5f);
                    float nx = std::clamp(s.x + rnd(-34.0f, 34.0f), kShrimpMinX, kShrimpMaxX);
                    // Never pick a target almost on top of the shrimp: that
                    // made it jitter in place instead of travelling.
                    if (std::fabs(nx - s.x) < 12.0f) {
                        nx = s.x + (nx >= s.x ? 12.0f : -12.0f);
                        nx = std::clamp(nx, kShrimpMinX, kShrimpMaxX);
                    }
                    s.targetX = nx;
                    s.targetY = std::clamp(s.y + rnd(-10.0f, 10.0f)
                                           - nightO2Lift(s.x, s.y)
                                           + dielDepthBias(5.0f),
                                           shrimpGoalMinY, shrimpGoalMaxY);
                }
            }
            // Steer toward the target in every state, not only while hiding.
            // Arrival bleeds off the leftover velocity, otherwise the shrimp
            // keeps coasting past the target and ends up ping-ponging
            // between the side walls.
            if (dist > 4.0f) {
                const bool urgent = hide || s.energy < 70.0f;
                const float sp = (urgent ? 8.0f : 3.4f) * s.g.speed * speedStage
                               * (hide ? 0.75f : 1.0f)
                               * std::clamp(dist / 12.0f, 0.15f, 1.0f);
                const float k = urgent ? 0.65f : 0.45f;
                s.vx += (dx / dist * sp - s.vx) * std::min(1.0f, dt * k);
                s.vy += (dy / dist * sp * 0.42f - s.vy) * std::min(1.0f, dt * k);
            } else {
                const float damp = std::min(1.0f, dt * 2.2f);
                s.vx -= s.vx * damp;
                s.vy -= s.vy * damp;
            }

            const float speed = (0.35f + activity) * speedStage * s.g.speed
                              * (1.0f - 0.10f * s.epiphyte)
                              * (1.0f + (s.startleTimer > 0.0f ? 0.55f : 0.0f));
            s.x += s.vx * dt * speed;
            s.y += s.vy * dt * speed;
            s.x = std::clamp(s.x, 7.0f, 297.0f);
            // Keep shrimp under the seasonal surface even in a dry spell.
            s.y = std::clamp(s.y, shrimpMinY, 86.0f);
            if (s.x <= 7.0f) s.vx = std::fabs(s.vx);
            if (s.x >= 297.0f) s.vx = -std::fabs(s.vx);
            if (s.y <= shrimpMinY) s.vy = std::fabs(s.vy);
            if (s.y >= 86.0f) s.vy = -std::fabs(s.vy);
            s.vx = std::clamp(s.vx, -12.0f, 12.0f);
            s.vy = std::clamp(s.vy, -12.0f, 12.0f);
            cellAtMut(s.x, s.y).preyScent += dt * 0.40f;
            if (s.stage == 2) cellAtMut(s.x, s.y).breedScent += dt * 0.08f;

            // A hungry shrimp keeps grazing even while the desktop rests.
            if (!resting_ || s.energy < 74.0f) {
                if (s.energy < 92.0f && s.eatCooldown <= 0.0f) {
                    bool ateSurface = false;
                    // Surface grazing: shrimp pick fronds off the duckweed mat.
                    if (s.y < waterSurfaceY() + 9.0f) {
                        int ri = -1;
                        float best = 9.0f;
                        for (int k = 0; k < raftCount_; ++k) {
                            const float d = std::fabs(rafts_[static_cast<size_t>(k)].x - s.x);
                            if (d < best && rafts_[static_cast<size_t>(k)].mass > 0.10f) {
                                best = d;
                                ri = k;
                            }
                        }
                        if (ri >= 0) {
                            Raft& r = rafts_[static_cast<size_t>(ri)];
                            const float eaten = std::min(r.mass, 0.055f);
                            r.mass -= eaten;
                            s.energy = std::min(100.0f, s.energy + 7.0f);
                            s.growth = std::min(1.0f, s.growth + 0.020f);
                            s.eatCooldown = rnd(12.0f, 18.0f);
                            ateSurface = true;
                            // Grazing thins a raft but never deletes it: the
                            // same floor updateDuckweed enforces is re-applied
                            // here so a bite cannot punch out the last speck.
                            r.mass = std::max(r.mass, 0.05f);
                        }
                    }
                    const int pi = ateSurface ? -1 : nearestPlant(s.x);
                    if (pi >= 0 && std::fabs(plants_[pi].x - s.x) < 9.0f && s.y > 62.0f) {
                        Plant& p = plants_[pi];
                        const float bite = 11.0f / (0.80f + 0.40f * p.g.graze);
                        p.energy = std::max(0.0f, p.energy - bite);
                        // Grazing trims a cluster but should not kill it; the
                        // old damage rate wiped out the meadow within a minute.
                        p.health = std::max(0.0f, p.health - bite * 0.0009f);
                        s.energy = std::min(100.0f, s.energy + 11.0f);
                        s.growth = std::min(1.0f, s.growth + 0.030f);
                        s.eatCooldown = rnd(9.0f, 15.0f);
                        humus_ = std::min(100.0f, humus_ + 0.2f);
                    }
                }
            }

            // Juvenile shrimp pick tiny tubifex from the mud.
            if ((!resting_ || s.energy < 80.0f) && s.stage == 0
                && s.energy < 86.0f && s.eatCooldown <= 0.0f) {
                const int ti = nearestTubifexIndex(s.x, s.y, 13.0f);
                if (ti >= 0) {
                    removeTubifex(ti);
                    s.energy = std::min(100.0f, s.energy + 8.5f);
                    s.growth = std::min(1.0f, s.growth + 0.055f);
                    s.eatCooldown = rnd(7.0f, 12.0f);
                }
            }

            // Cleaning symbiosis: scrape epiphyte from plants and other animals.
            if ((!resting_ || s.energy < 80.0f) && s.cleanCooldown <= 0.0f) {
                int cleaned = 0;
                for (int p = 0; p < plantCount_ && cleaned < 2; ++p) {
                    if (plants_[p].epiphyte > 0.06f
                        && std::fabs(plants_[p].x - s.x) < 9.0f && s.y > 58.0f) {
                        plants_[p].epiphyte = std::max(0.0f,
                            plants_[p].epiphyte - 0.075f);
                        ++cleaned;
                    }
                }
                for (int d = 0; d < damselCount_ && cleaned < 2; ++d) {
                    if (damsel_[d].epiphyte > 0.05f
                        && std::fabs(damsel_[d].x - s.x) < 10.0f
                        && std::fabs(damsel_[d].y - s.y) < 8.0f) {
                        damsel_[d].epiphyte = std::max(0.0f,
                            damsel_[d].epiphyte - 0.070f);
                        ++cleaned;
                    }
                }
                for (int f = 0; f < fishCount_ && cleaned < 2; ++f) {
                    if (fish_[f].epiphyte > 0.05f
                        && std::fabs(fish_[f].x - s.x) < 11.0f
                        && std::fabs(fish_[f].y - s.y) < 9.0f) {
                        fish_[f].epiphyte = std::max(0.0f,
                            fish_[f].epiphyte - 0.060f);
                        ++cleaned;
                    }
                }
                for (int e = 0; e < eggCount_ && cleaned < 2; ++e) {
                    if (eggs_[e].epiphyte > 0.05f
                        && std::fabs(eggs_[e].x - s.x) < 10.0f
                        && std::fabs(eggs_[e].y - s.y) < 8.0f) {
                        eggs_[e].epiphyte = std::max(0.0f,
                            eggs_[e].epiphyte - 0.080f);
                        ++cleaned;
                    }
                }
                if (cleaned > 0) {
                    s.energy = std::min(100.0f, s.energy + 2.0f * cleaned);
                    s.growth = std::min(1.0f, s.growth + 0.004f * cleaned);
                    s.cleanCooldown = rnd(5.0f, 9.0f);
                }
            }

            const float relief = rareSpeciesRelief(shrimpCount_, g_settings.maxShrimp);
            const float localStress = localStressAt(s.x, s.y, 0, s.g);
            s.health -= dt * stressDamage(localStress) * 0.0110f * relief;
            s.energy = std::max(0.0f, s.energy - dt * localStress * 0.055f);
            const float hypoxia = std::clamp((22.0f - oxygen_) / 22.0f, 0.0f, 1.0f);
            const float tolerance = s.g.oxygen * (0.92f + 0.08f * s.g.size) * toleranceScale(s.growth);
            if (hypoxia > 0.0f) s.health -= dt * hypoxia * 0.0040f / tolerance * relief;
            if (s.energy <= 0.0f) s.health -= dt * 0.0090f;
            const float lifespan = 300.0f * lifespanScale(s.g, s.id);
            if (s.age > lifespan) {
                s.health -= dt * 0.0012f;
            } else if (s.health < 1.0f && s.energy > 35.0f && localStress < 0.70f) {
                // Healing is what makes the cycle robust.  When health could
                // only ever fall, a run of poor nights dragged every shrimp,
                // nymph and fish down at the same rate, so all three lineages
                // died within minutes of each other and the bottle went empty.
                // It works best in clean water and merely ticks over in water
                // that is only tolerable, so a bad spell is survived.
                const float calm = 1.0f - localStress / 0.70f;
                s.health = std::min(1.0f, s.health + dt * (0.0008f + 0.0022f * calm));
            }

            if (s.health <= 0.0f) {
                killShrimp(i);
                continue;
            }

            // Breeding wants well-oxygenated water, not merely survivable
            // water.  Laying at 44% saturation built a crowd the jar could not
            // oxygenate, and the whole standing crop then slid downhill at the
            // same rate.  A higher spawning threshold makes the population
            // stop growing before the nightly oxygen debt does.
            if (s.stage == 2 && s.health > 0.40f && s.energy >= 52.0f
                && s.breedCooldown <= 0.0f && oxygen_ > 52.0f
                && plantCount_ >= 3 && shrimpCount_ < g_settings.maxShrimp
                && eggCount_ < kMaxEggs
                && localStressAt(s.x, s.y, 0, s.g) < 0.65f) {
                if (layEgg(0, childGenes(s.g), s.x)) {
                    s.energy -= 20.0f;
                    s.breedCooldown = rnd(18.0f, 28.0f) * (0.90f + 0.20f * s.g.size)
                                    / breedingRate();
                    shrimpEggs_ = std::min(6, shrimpEggs_ + 1);
                    shrimpEggGene_ = s.g;
                }
            }
        }
    }

    int nearestShrimpIndex(float x, float y, float maxDist) const {
        int best = -1;
        float bestD = maxDist * maxDist;
        for (int i = 0; i < shrimpCount_; ++i) {
            const float dx = shrimp_[i].x - x;
            const float dy = shrimp_[i].y - y;
            const float d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }
    int adultShrimpCount() const {
        int n = 0;
        for (int i = 0; i < shrimpCount_; ++i) n += shrimp_[i].stage == 2;
        return n;
    }
    int juvenileShrimpNear(float x, float y, int exclude) const {
        int n = 0;
        for (int i = 0; i < shrimpCount_; ++i) {
            if (i == exclude || shrimp_[i].stage != 0) continue;
            if (std::fabs(shrimp_[i].x - x) < 26.0f
                && std::fabs(shrimp_[i].y - y) < 18.0f) ++n;
        }
        return n;
    }
    int adultDamselCount() const {
        int n = 0;
        for (int i = 0; i < damselCount_; ++i) n += damsel_[i].stage == 2;
        return n;
    }

    void updateDamsel(float dt, float activity) {
        for (int i = damselCount_ - 1; i >= 0; --i) {
            Damsel& d = damsel_[i];
            d.age += dt;
            d.growth = std::min(1.0f, d.growth + maturationStep(
                d.energy, d.health, 0.0048f, dt));
            d.stage = stageFromGrowth(d.growth);
            const float size = stageScale(d.stage) * d.g.size;
            d.energy = std::max(0.0f, d.energy - dt
                                * (shrimpCount_ <= 3 ? 0.30f : 0.15f) * (0.85f + 0.15f * d.g.size)
                                * (0.90f + 0.20f * d.g.speed * d.g.speed)
                                * (0.72f + 0.28f * stageScale(d.stage))
                                * (1.0f + 0.28f * d.epiphyte)
                                * temperatureQ10());
            d.huntCooldown -= dt;
            d.breedCooldown -= dt;
            d.startleCooldown -= dt;
            d.startleTimer = std::max(0.0f, d.startleTimer - dt);
            // Reachable band for this frame.  Goals outside it used to pin the
            // nymph against a wall or the surface, where it visibly shuddered
            // left/right or stopped moving altogether.
            const float damselMinY = std::max(18.0f, waterSurfaceY() + 4.0f);
            const float damselGoalMinY = damselMinY + 2.0f;
            const float damselGoalMaxY = 82.0f;

            bool nearFish = false;
            for (int f = 0; f < fishCount_; ++f) {
                if (std::fabs(fish_[f].x - d.x) < 38.0f
                    && std::fabs(fish_[f].y - d.y) < 24.0f) {
                    nearFish = true;
                    break;
                }
            }
            if (cellAt(d.x, d.y).alarmScent > 0.30f && d.stage < 2) {
                // P2: the scent alone is not enough -- a fish has to be within
                // a short distance, otherwise a stale plume would keep the
                // nymph pinned to cover forever.
                for (int f = 0; f < fishCount_; ++f) {
                    if (std::fabs(fish_[f].x - d.x) < 70.0f
                        && std::fabs(fish_[f].y - d.y) < 45.0f) {
                        nearFish = true;
                        break;
                    }
                }
            }

            int target = -1;
            const int adultShrimp = adultShrimpCount();
            // A nymph can handle prey close to its own body, and a hungry one
            // will tackle food it would ignore when full.  The old rule
            // compared bare stage scales, so every nymph could only ever take
            // a shrimp hatchling; once the shrimp bank matured, whole broods
            // sat next to food they were forbidden to touch.
            const float hunger = std::clamp(1.0f - d.energy / 62.0f, 0.0f, 1.0f);
            const float ownBody = speciesBase(2) * size;
            auto palatable = [&](int idx) {
                if (idx < 0) return false;
                const float preyBody = speciesBase(0) * stageScale(shrimp_[idx].stage)
                                     * shrimp_[idx].g.size;
                if (preyBody >= ownBody * (0.85f + 0.45f * hunger)) return false;
                if (adultShrimp <= 1 && shrimp_[idx].stage == 2) return false;
                return true;
            };
            auto nearestPrey = [&](float reach) {
                int best = -1;
                float bestD = reach * reach;
                for (int s = 0; s < shrimpCount_; ++s) {
                    if (!palatable(s)) continue;
                    const float sdx = shrimp_[s].x - d.x;
                    const float sdy = shrimp_[s].y - d.y;
                    const float dd = sdx * sdx + sdy * sdy;
                    if (dd < bestD) { bestD = dd; best = s; }
                }
                return best;
            };
            // Starvation overrides caution.  A nymph with a full gut hides
            // from a cruising fish, but one that has not eaten for a long
            // while takes the risk instead: sitting in cover until it starves
            // is not a survival strategy either.  With several fish patrolling
            // a bottle this narrow, "a fish is nearby" is true most of the
            // time, so the old absolute rule kept the whole brood pinned to
            // stems and turned every lean spell into a one-way decline.
            const float hungerRisk = std::clamp((52.0f - d.energy) / 30.0f, 0.0f, 1.0f);
            const bool hideFromFish = nearFish && hungerRisk < 0.55f;
            // A fed nymph does not hunt.  The old gate kept every nymph on the
            // prowl until it was almost full, so a brood of ten patrolled the
            // bottle non-stop and could take a herd of fourteen shrimp down to
            // a handful in under a minute.  Hunting only when genuinely hungry
            // spreads the pressure out and leaves the survivors room to breed.
            const bool hungry = d.energy < 62.0f;
            if ((!resting_ || d.energy < 55.0f) && !hideFromFish && light_ < 60.0f
                && shrimpCount_ > 3 && hungry) {
                target = nearestPrey(76.0f);
            }

            // A weakened nymph stops chasing, but it still snaps at prey that
            // drifts within reach.  Treating "below a quarter health" as "never
            // eats again" turned every bad night into an irreversible death
            // sentence: the animal could no longer earn the energy its own
            // recovery needs, so the whole brood could be written off in one
            // event no matter how calm the bottle was afterwards.
            const bool moribund = d.health < 0.25f && target < 0;
            // A nymph does not cling the whole night through.  Each individual
            // follows a slow rhythm of resting on a stem and patrolling, so
            // part of the brood is always on the move and the colony cannot
            // settle into one knot on the same plant.
            const float restWave = std::sin(simTime_ * 0.045f
                + static_cast<float>(d.id % 360u) * 0.0175f);
            const bool clinging = (resting_ && d.energy > 62.0f && restWave > 0.0f)
                               || (light_ > 60.0f && restWave > 0.45f);
            // The strike itself is shared by the chasing nymph and the one
            // sitting in cover: an ambush predator still takes whatever walks
            // past its jaws, it simply does not go looking for it.
            auto tryCatch = [&](int prey, float reach, float odds) {
                if (prey < 0 || d.huntCooldown > 0.0f || shrimpCount_ <= 1) return false;
                const float cdx = shrimp_[prey].x - d.x;
                const float cdy = shrimp_[prey].y - d.y;
                if (cdx * cdx + cdy * cdy > reach * reach) return false;
                const float shelter = std::clamp(plantShelterAt(shrimp_[prey].x)
                    + duckweedShelterAt(shrimp_[prey].x, shrimp_[prey].y), 0.0f, 1.0f);
                const float dilution = 1.0f / (1.0f
                    + 0.20f * juvenileShrimpNear(shrimp_[prey].x, shrimp_[prey].y, prey));
                // Wrestling down prey almost as large as itself is harder
                // than snapping up a hatchling, so the odds fall as the two
                // bodies approach the same size.
                const float ratio = std::clamp(speciesBase(0)
                    * stageScale(shrimp_[prey].stage) * shrimp_[prey].g.size
                    / (speciesBase(2) * size), 0.0f, 1.0f);
                const float grip = std::clamp(1.25f - 0.55f * ratio, 0.40f, 1.0f);
                // Predator pit: a Holling curve with a floor still hands a
                // nymph a catch at any prey density, so a brood could pick off
                // the last shrimp one recruit at a time and never let the herd
                // rebuild.  Sparse prey is hard to find in a real jar -- the
                // survivors hide in the weed and the search time between
                // encounters explodes -- so capture falls away to nothing below
                // a few shrimp.
                const float preyRefuge = std::clamp(
                    (static_cast<float>(shrimpCount_) - 2.0f) / 6.0f, 0.0f, 1.0f);
                bool ate = false;
                if (rnd(0.0f, 1.0f) < odds * (1.0f - 0.35f * shelter) * dilution * grip
                                          * functionalResponse2(shrimpCount_, 4.0f, 10.0f)
                                          * preyRefuge) {
                    removeShrimp(prey, false);
                    d.energy = std::min(100.0f, d.energy + 16.0f);
                    d.growth = std::min(1.0f, d.growth + 0.030f);
                    ate = true;
                }
                // Only a real catch earns the long digestion pause.  A miss
                // that also shuts the nymph out of patrol for ten seconds is
                // how a lean brood starves itself while standing next to food.
                // The pause after a meal is long because a fed nymph simply
                // does not hunt again until the gut is empty; that is also
                // what stops one individual from working through a herd.
                d.huntCooldown = ate ? rnd(26.0f, 40.0f) : rnd(1.2f, 2.2f);
                return ate;
            };
            // Strike whatever drifts within a body length, whether the nymph is
            // chasing it, hiding from a fish or lying half dead in the silt.
            // A nymph is an ambush hunter: it does not out-swim a shrimp over
            // open water, so a strike that only happens after a successful
            // six-pixel chase almost never lands and the lean individuals
            // starve while visibly stalking prey.
            if (d.huntCooldown <= 0.0f) {
                const int strike = nearestPrey(12.0f);
                if (strike >= 0) {
                    tryCatch(strike, 12.0f, 0.60f);
                    // A catch reshuffles the shrimp array, so this frame's
                    // chase target cannot be trusted any more.
                    target = -1;
                }
            }
            if (moribund) {
                // Moribund damselfly: it no longer hunts.  It clings to the
                // nearest stem and sways with the water until it dies.
                target = -1;
                d.hunting = false;
                d.targetShrimp = -1;
                const float stem = nearestPlantX(d.x, true);
                const float tx = std::clamp(stem, 11.0f, 293.0f);
                const float ty = std::clamp(d.y, 42.0f, 70.0f);
                const float dx = tx - d.x;
                const float dy = ty - d.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 2.0f) {
                    const float sp = 14.0f * motionScale(d.growth);
                    d.vx += (dx / dist * sp - d.vx) * std::min(1.0f, dt * 0.8f);
                    d.vy += (dy / dist * sp - d.vy) * std::min(1.0f, dt * 0.8f);
                } else {
                    const float damp = std::min(1.0f, dt * 3.0f);
                    d.vx -= d.vx * damp;
                    d.vy -= d.vy * damp;
                }
            } else if (hideFromFish || clinging || d.startleTimer > 0.0f) {
                // Cling to its own stem, offset sideways by an amount that
                // depends on the individual, so several nymphs on one shoot
                // spread along the leaves instead of sharing the same pixels.
                const float side = (static_cast<float>(d.id % 5u) - 2.0f) * 5.0f;
                const float tx = (d.startleTimer > 0.0f)
                               ? std::clamp(d.x + (d.x - d.startleX) * 1.6f, 11.0f, 293.0f)
                               : std::clamp(damselStemX(d.x, d.id) + side, 11.0f, 293.0f);
                // A clinging nymph keeps shifting its grip along the stem
                // instead of freezing in place, so it still reads as alive.
                const float clingPhase = static_cast<float>(d.id % 628u) * 0.01f;
                const float clingY = 62.0f
                    + std::sin(simTime_ * 0.30f + clingPhase) * 7.0f;
                const float ty = (d.startleTimer > 0.0f)
                               ? std::clamp(d.y + (d.y - d.startleY) * 1.4f,
                                            damselGoalMinY, damselGoalMaxY)
                               : std::clamp(clingY, damselGoalMinY, damselGoalMaxY);
                const float dx = tx - d.x;
                const float dy = ty - d.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 1.5f) {
                    const float sp = 10.0f * d.g.speed * motionScale(d.growth);
                    d.vx += (dx / dist * sp - d.vx) * std::min(1.0f, dt * 0.9f);
                    d.vy += (dy / dist * sp * 0.35f - d.vy) * std::min(1.0f, dt * 0.9f);
                }
                d.hunting = false;
            } else if (target >= 0) {
                const float tx = std::clamp(shrimp_[target].x, 11.0f, 293.0f);
                const float ty = std::clamp(shrimp_[target].y, damselGoalMinY, damselGoalMaxY);
                const float dx = tx - d.x;
                const float dy = ty - d.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                d.hunting = true;
                d.targetShrimp = target;
                if (dist > 1.5f) {
                    const float sp = 11.0f * d.g.speed * motionScale(d.growth);
                    d.vx += (dx / dist * sp - d.vx) * std::min(1.0f, dt * 0.85f);
                    d.vy += (dy / dist * sp * 0.40f - d.vy) * std::min(1.0f, dt * 0.85f);
                }
                if (dist < 1.5f) {
                    // Already on top of it; hold position until the cooldown
                    // from the last strike expires.
                    d.hunting = true;
                }
            } else {
                d.hunting = false;
                d.dirTimer -= dt;
                if (d.dirTimer <= 0.0f) {
                    d.dirTimer = rnd(6.0f, 12.0f);
                    // Part of the time it patrols open water rather than heading
                    // for yet another stem; otherwise a brood stays glued to the
                    // two or three shoots it can reach and reads as one lump.
                    // The step is a nearby limb so the nymph actually travels.
                    const float roam = (rnd(0.0f, 1.0f) < 0.45f)
                                     ? std::clamp(d.x + rnd(-55.0f, 55.0f), 20.0f, 280.0f)
                                     : damselStemX(d.x, d.id) + rnd(-9.0f, 9.0f);
                    d.targetX = std::clamp(roam, 11.0f, 293.0f);
                    // A hungry nymph works the silt, where the worms are,
                    // instead of patrolling open water that has nothing in it
                    // small enough to eat.  Adults do this too: once they have
                    // outgrown shrimp-sized prey the mud is their only table.
                    const float roamY = (d.energy < 58.0f)
                                      ? rnd(64.0f, 82.0f)
                                      : rnd(28.0f, 68.0f);
                    d.targetY = std::clamp(roamY
                                           - nightO2Lift(d.x, d.y)
                                           + dielDepthBias(8.0f),
                                           damselGoalMinY, damselGoalMaxY);
                }
                const MicroCell& scent = cellAt(d.x, d.y);
                if (scent.preyScent > 0.12f) {
                    float bestScent = scent.preyScent;
                    float bx = d.x, by = d.y;
                    bool found = false;
                    const float ox[4] = {-20, 20, 0, 0};
                    const float oy[4] = {0, 0, -13, 13};
                    for (int k = 0; k < 4; ++k) {
                        const auto& c = cellAt(d.x + ox[k], d.y + oy[k]);
                        // A nymph deposits prey scent under itself, so its own
                        // cell is often the local maximum.  Following that cell
                        // would pin it in place for good; only a clearly
                        // stronger neighbouring cell is worth walking to.
                        if (c.preyScent > bestScent + 0.02f) {
                            bestScent = c.preyScent;
                            bx = d.x + ox[k];
                            by = d.y + oy[k];
                            found = true;
                        }
                    }
                    if (found) {
                        d.targetX = std::clamp(bx, 11.0f, 293.0f);
                        d.targetY = std::clamp(by, damselGoalMinY, damselGoalMaxY);
                    }
                }
                d.targetX = std::clamp(d.targetX, 11.0f, 293.0f);
                d.targetY = std::clamp(d.targetY, damselGoalMinY, damselGoalMaxY);
                const float dx = d.targetX - d.x;
                const float dy = d.targetY - d.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 4.0f) {
                    const float sp = 7.5f * d.g.speed * motionScale(d.growth)
                                   * std::clamp(dist / 14.0f, 0.15f, 1.0f);
                    d.vx += (dx / dist * sp - d.vx) * std::min(1.0f, dt * 0.8f);
                    d.vy += (dy / dist * sp * 0.35f - d.vy) * std::min(1.0f, dt * 0.8f);
                } else {
                    const float damp = std::min(1.0f, dt * 2.0f);
                    d.vx -= d.vx * damp;
                    d.vy -= d.vy * damp;
                }
            }

            const float speed = (0.40f + activity) * motionScale(d.growth) * d.g.speed
                              * (1.0f - 0.10f * d.epiphyte)
                              * (1.0f + (d.startleTimer > 0.0f ? 0.40f : 0.0f));
            d.x += d.vx * dt * speed;
            d.y += d.vy * dt * speed;
            d.x = std::clamp(d.x, 10.0f, 294.0f);
            d.y = std::clamp(d.y, damselMinY, 84.0f);
            if (d.x <= 10.0f) d.vx = std::fabs(d.vx);
            if (d.x >= 294.0f) d.vx = -std::fabs(d.vx);
            if (d.y <= damselMinY) d.vy = std::fabs(d.vy);
            if (d.y >= 84.0f) d.vy = -std::fabs(d.vy);
            d.vx = std::clamp(d.vx, -16.0f, 16.0f);
            d.vy = std::clamp(d.vy, -16.0f, 16.0f);
            // A light lateral shove keeps nymphs a body apart.  Without it a
            // brood that shares one shoot sits in the same pixels and reads as
            // a single clump rather than several larvae.
            for (int j = 0; j < i; ++j) {
                Damsel& other = damsel_[j];
                const float ddx = other.x - d.x;
                const float ddy = other.y - d.y;
                if (std::fabs(ddy) > 6.0f) continue;
                const float adx = std::fabs(ddx);
                if (adx >= 9.0f) continue;
                const float push = (9.0f - adx) * std::min(1.0f, dt * 2.0f) * 0.45f;
                const float dir = (ddx >= 0.0f) ? 1.0f : -1.0f;
                d.x = std::clamp(d.x - dir * push, 10.0f, 294.0f);
                other.x = std::clamp(other.x + dir * push, 10.0f, 294.0f);
            }
            cellAtMut(d.x, d.y).preyScent += dt * 0.30f;
            if (d.stage == 2) cellAtMut(d.x, d.y).breedScent += dt * 0.07f;

            // Damselfly nymphs also grub worms out of the mud.  Every instar
            // does this, adults included: an adult that had outgrown the
            // shrimp and could not reach the silt was left with no food at all,
            // and the whole mature half of the brood starved together while
            // twenty tubifex sat inches below it.  The reach covers the drop
            // from the lowest swimming band (y 84) to the mud surface.
            if ((!resting_ || d.energy < 70.0f) && !moribund
                && d.energy < 78.0f && d.huntCooldown <= 0.0f) {
                const int ti = nearestTubifexIndex(d.x, d.y, 26.0f);
                if (ti >= 0 && rnd(0.0f, 1.0f) < 0.75f * functionalResponse3(tubifexCount_, 2.5f, 6.0f)) {
                    removeTubifex(ti);
                    d.energy = std::min(100.0f, d.energy + 12.0f);
                    d.growth = std::min(1.0f, d.growth + 0.045f);
                    d.huntCooldown = rnd(18.0f, 28.0f);
                }
            }

            // Starvation fallback: very rare cannibalism of a much smaller
            // juvenile.  This never runs in normal well-fed conditions.
            if (d.energy < 8.0f && damselCount_ > 2
                && rnd(0.0f, 1.0f) < 0.00008f) {
                int smallest = -1;
                float smallestSize = 1e9f;
                for (int j = 0; j < damselCount_; ++j) {
                    if (j == i || damsel_[j].stage >= 2) continue;
                    const float sz = stageScale(damsel_[j].stage) * damsel_[j].g.size;
                    if (sz < size * 0.72f && sz < smallestSize) {
                        smallestSize = sz;
                        smallest = j;
                    }
                }
                if (smallest >= 0) {
                    removeDamsel(smallest, false);
                    d.energy = std::min(100.0f, d.energy + 9.0f);
                    d.growth = std::min(1.0f, d.growth + 0.020f);
                }
            }

            const float relief = rareSpeciesRelief(damselCount_, g_settings.maxDamsel);
            const float localStress = localStressAt(d.x, d.y, 2, d.g);
            d.health -= dt * stressDamage(localStress) * 0.0115f * relief;
            d.energy = std::max(0.0f, d.energy - dt * localStress * 0.055f);
            const float hypoxia = std::clamp((22.0f - oxygen_) / 22.0f, 0.0f, 1.0f);
            const float tolerance = d.g.oxygen * (0.92f + 0.08f * d.g.size) * toleranceScale(d.growth);
            if (hypoxia > 0.0f) d.health -= dt * hypoxia * 0.0035f * relief / tolerance;
            if (d.energy <= 0.0f) d.health -= dt * 0.0080f;
            const float lifespan = 330.0f * lifespanScale(d.g, d.id);
            if (d.age > lifespan) {
                d.health -= dt * 0.0012f;
            } else if (d.health < 1.0f && d.energy > 35.0f && localStress < 0.70f) {
                const float calm = 1.0f - localStress / 0.70f;
                d.health = std::min(1.0f, d.health + dt * (0.0008f + 0.0022f * calm));
            }

            if (d.health <= 0.0f) {
                killDamsel(i);
                continue;
            }

            if (d.stage == 2 && d.health > 0.40f && d.energy >= 60.0f
                && d.breedCooldown <= 0.0f && oxygen_ > 54.0f
                && plantCount_ >= 2
                // Recruitment is food-limited.  Ten nymphs can strip a jar
                // this size before they mature, so an adult only lays while
                // there is a standing prey base to feed the brood: shrimp are
                // the staple, tubifex a coarser fallback.  The floor is four,
                // not seven: at seven a pond that settled at five shrimp kept
                // the lineage at zero for good.
                && shrimpCount_ + tubifexCount_ / 10 >= 4
                && damselCount_ < g_settings.maxDamsel && eggCount_ < kMaxEggs
                && localStressAt(d.x, d.y, 2, d.g) < 0.65f) {
                if (layEgg(2, childGenes(d.g), d.x)) {
                    d.energy -= 22.0f;
                    d.breedCooldown = rnd(22.0f, 34.0f) * (0.90f + 0.20f * d.g.size)
                                    / breedingRate();
                    damselEggs_ = std::min(6, damselEggs_ + 1);
                    damselEggGene_ = d.g;
                }
            }
        }
    }

    void updateFish(float dt, float activity) {
        for (int i = fishCount_ - 1; i >= 0; --i) {
            Fish& f = fish_[i];
            f.age += dt;
            f.growth = std::min(1.0f, f.growth + maturationStep(
                f.energy, f.health, 0.0042f, dt));
            f.stage = stageFromGrowth(f.growth);
            const float fishSize = stageScale(f.stage) * f.g.size;
            const bool lowPrey = shrimpCount_ <= 3 && damselCount_ <= 1;
            f.energy = std::max(0.0f, f.energy - dt
                                * (lowPrey ? 0.18f : 0.14f) * (0.85f + 0.15f * f.g.size)
                                * (0.90f + 0.20f * f.g.speed * f.g.speed)
                                * (0.72f + 0.28f * stageScale(f.stage))
                                * (1.0f + 0.28f * f.epiphyte)
                                * temperatureQ10());
            f.huntCooldown -= dt;
            f.breedCooldown -= dt;
            f.chaseTimer -= dt;
            f.retargetTimer -= dt;
            f.startleCooldown -= dt;
            f.startleTimer = std::max(0.0f, f.startleTimer - dt);
            const bool startled = f.startleTimer > 0.0f;
            // Reachable band.  Targets that fell outside it (a plant stem past
            // the wall, a depth above the seasonal surface) used to pin the
            // fish against the glass, flipping vx every frame.
            const float fishMinY = std::max(20.0f, waterSurfaceY() + 4.0f);
            const float fishGoalMinY = fishMinY + 2.0f;
            const float fishGoalMaxY = 80.0f;
            constexpr float kFishMinX = 14.0f;
            constexpr float kFishMaxX = 290.0f;

            if (f.stage == 2 && !f.territory && plantCount_ >= 2
                && f.energy > 34.0f) {
                const float tx = nearestMaturePlantX(f.x);
                bool occupied = false;
                for (int j = 0; j < fishCount_; ++j) {
                    if (j == i || !fish_[j].territory) continue;
                    if (std::fabs(fish_[j].territoryX - tx) < 28.0f) {
                        occupied = true;
                        break;
                    }
                }
                if (tx >= 0.0f && !occupied && rnd(0.0f, 1.0f) < 0.18f) {
                    f.territory = true;
                    f.territoryX = tx;
                    f.territoryTimer = rnd(20.0f, 40.0f);
                }
            }
            if (f.stage != 2) f.territory = false;
            if (f.territory) f.territoryTimer -= dt;
            if (f.territory && (f.territoryTimer <= 0.0f || f.energy < 24.0f)) {
                f.territory = false;
                f.territoryTimer = 0.0f;
            }
            const float territoryTarget = f.territory ? 1.0f : 0.0f;
            f.territoryBlend += (territoryTarget - f.territoryBlend)
                              * std::min(1.0f, dt * 0.8f);

            int intruder = -1;
            float intruderStrength = 0.0f;
            if (f.territory && f.energy > 28.0f && f.chaseTimer <= 0.0f && !startled) {
                for (int j = 0; j < fishCount_; ++j) {
                    if (j == i || fish_[j].stage != 2) continue;
                    // Two territory holders ignore each other; only a
                    // non-territorial adult is chased out of a territory.
                    if (fish_[j].territory) continue;
                    if (std::fabs(fish_[j].x - f.territoryX) > 18.0f) continue;
                    if (std::fabs(fish_[j].x - f.x) > 30.0f) continue;
                    const float st = fishStrength(fish_[j]);
                    if (st > intruderStrength) { intruderStrength = st; intruder = j; }
                }
                if (intruder >= 0 && intruderStrength > fishStrength(f) + 0.055f
                    && f.energy < 48.0f && rnd(0.0f, 1.0f) < dt * 0.20f) {
                    f.territory = false;
                    f.territoryX = 0.0f;
                    f.chaseTimer = 2.0f;
                    intruder = -1;
                }
            }

            const float shadeX = nearestMaturePlantX(f.x);
            const bool brightHide = light_ > 71.0f && shadeX >= 0.0f && intruder < 0;

            int kind = -1, index = -1;
            if ((!resting_ || f.energy < 55.0f) && !brightHide && !startled
                && intruder < 0 && (f.health > 0.28f || f.energy < 30.0f)
                && f.energy < 95.0f) {
                if (f.retargetTimer > 0.0f && f.targetKind >= 0) {
                    kind = f.targetKind;
                    index = f.targetIndex;
                    if ((kind == 0 && (index < 0 || index >= shrimpCount_))
                        || (kind == 1 && (index < 0 || index >= damselCount_))) {
                        kind = -1;
                        index = -1;
                    }
                }
                if (kind < 0) {
                    float bestScore = 1e9f;
                    const float minPrey = 0.60f * speciesBase(1) * fishSize;
                    const int adultSh = adultShrimpCount();
                    const int adultD = adultDamselCount();
                    for (int s = 0; s < shrimpCount_; ++s) {
                        if (shrimpCount_ <= 2) break;
                        if (adultSh <= 1 && shrimp_[s].stage == 2) continue;
                        const float preySize = speciesBase(0) * stageScale(shrimp_[s].stage)
                                             * shrimp_[s].g.size;
                        if (preySize < minPrey) continue;
                        const float dist = std::sqrt((shrimp_[s].x - f.x) * (shrimp_[s].x - f.x)
                                                   + (shrimp_[s].y - f.y) * (shrimp_[s].y - f.y));
                        if (dist > 82.0f) continue;
                        const float score = dist + std::fabs(preySize - 0.58f * fishSize) * 45.0f
                                          - shrimp_[s].g.speed * 4.0f;
                        if (score < bestScore) { bestScore = score; kind = 0; index = s; }
                    }
                    for (int d = 0; d < damselCount_; ++d) {
                        if (damselCount_ <= 1) break;
                        if (adultD <= 1 && damsel_[d].stage == 2) continue;
                        const float preySize = speciesBase(2) * stageScale(damsel_[d].stage)
                                             * damsel_[d].g.size;
                        if (preySize < minPrey) continue;
                        const float dist = std::sqrt((damsel_[d].x - f.x) * (damsel_[d].x - f.x)
                                                   + (damsel_[d].y - f.y) * (damsel_[d].y - f.y));
                        if (dist > 82.0f) continue;
                        const float score = dist + std::fabs(preySize - 0.58f * fishSize) * 45.0f
                                          - damsel_[d].g.speed * 4.0f;
                        if (score < bestScore) { bestScore = score; kind = 1; index = d; }
                    }
                    f.retargetTimer = (kind >= 0) ? rnd(0.9f, 1.5f) : 0.0f;
                }
            } else {
                f.targetKind = -1;
                f.targetIndex = -1;
                f.retargetTimer = 0.0f;
            }

            int browseRaft = -1;
            if (intruder >= 0) {
                const float tx = fish_[intruder].x;
                const float ty = fish_[intruder].y;
                const float dx = tx - f.x;
                const float dy = ty - f.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                f.chaseFish = intruder;
                f.hunting = false;
                f.targetX = tx;
                f.targetY = ty;
                if (dist > 1.0f) {
                    const float sp = 14.0f * f.g.speed;
                    f.vx += (dx / dist * sp - f.vx) * std::min(1.0f, dt * 1.2f);
                    f.vy += (dy / dist * sp * 0.40f - f.vy) * std::min(1.0f, dt * 1.2f);
                }
                if (dist < 4.5f) {
                    Fish& other = fish_[intruder];
                    // A one-off bounded shove.  Adding a fixed +-10 every frame
                    // while the two overlapped built unbounded velocity and
                    // made the pair whip left and right at high speed.
                    const float away = (other.x >= f.x ? 1.0f : -1.0f);
                    other.vx = std::clamp(other.vx + away * 2.0f, -9.0f, 9.0f);
                    other.x = std::clamp(other.x + away * 1.5f, kFishMinX, kFishMaxX);
                    f.energy = std::max(0.0f, f.energy - dt * 0.3f);
                }
            } else if (kind >= 0) {
                f.chaseFish = -1;
                const float tx = std::clamp((kind == 0) ? shrimp_[index].x : damsel_[index].x,
                                            kFishMinX, kFishMaxX);
                const float ty = std::clamp((kind == 0) ? shrimp_[index].y : damsel_[index].y,
                                            fishGoalMinY, fishGoalMaxY);
                const float dx = tx - f.x;
                const float dy = ty - f.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                f.hunting = true;
                f.targetKind = kind;
                f.targetIndex = index;
                f.targetX = tx;
                f.targetY = ty;
                if (dist > 1.5f) {
                    const float sp = 12.0f * f.g.speed * motionScale(f.growth);
                    f.vx += (dx / dist * sp - f.vx) * std::min(1.0f, dt * 1.0f);
                    f.vy += (dy / dist * sp * 0.42f - f.vy) * std::min(1.0f, dt * 1.0f);
                }
                if (dist < 6.5f && f.huntCooldown <= 0.0f) {
                    const bool canEat = (kind == 0) ? shrimpCount_ > 2 : damselCount_ > 1;
                    bool ate = false;
                    if (canEat) {
                        const float preyY = (kind == 0) ? shrimp_[index].y
                                                       : damsel_[index].y;
                        const float shelter = std::clamp(plantShelterAt(tx)
                            + duckweedShelterAt(tx, preyY), 0.0f, 1.0f);
                        const float epiPrey = (kind == 0) ? shrimp_[index].epiphyte
                                                          : damsel_[index].epiphyte;
                        const float dilution = (kind == 0)
                            ? 1.0f / (1.0f + 0.20f * juvenileShrimpNear(
                                shrimp_[index].x, shrimp_[index].y, index))
                            : 1.0f;
                        if (rnd(0.0f, 1.0f) < 0.70f * (1.0f - 0.45f * shelter)
                                              * (1.0f + 0.16f * epiPrey)
                                              * dilution
                                              * functionalResponse3(kind == 0 ? shrimpCount_
                                                                             : damselCount_,
                                                                   3.5f, 9.0f)) {
                            if (kind == 0) removeShrimp(index, false);
                            else removeDamsel(index, false);
                            f.energy = std::min(100.0f, f.energy + 18.0f);
                            f.growth = std::min(1.0f, f.growth + 0.045f);
                            ate = true;
                        }
                    }
                    // A miss must not lock the fish out of every other food
                    // for ten seconds.  Only a real capture earns the long
                    // digestion pause; otherwise it simply tries again.
                    f.huntCooldown = ate ? rnd(9.0f, 14.0f) : rnd(1.4f, 2.4f);
                    f.hunting = false;
                }
            } else {
                f.hunting = false;
                f.chaseFish = -1;
                // A dying fish loses equilibrium and drifts near the surface,
                // but a starving one still rises for duckweed: drifting is a
                // sign of the end, not a licence to skip every meal on the way.
                if (f.health < 0.28f && f.energy > 20.0f) {
                    f.targetX = std::clamp(f.x + std::sin(animTime_ * 0.35f + i * 1.3f) * 6.0f,
                                           kFishMinX, kFishMaxX);
                    f.targetY = fishGoalMinY;
                } else if (startled) {
                    f.targetX = std::clamp(f.x + (f.x - f.startleX) * 1.6f,
                                           kFishMinX, kFishMaxX);
                    f.targetY = std::clamp(f.y + (f.y - f.startleY) * 1.3f,
                                           fishGoalMinY, fishGoalMaxY);
                } else if (resting_ && f.energy > 55.0f) {
                    // A fed fish settles by the nearest plant.  Each one gets
                    // its own height lane, otherwise two fish park on the same
                    // stem at one level and hold a nose-to-nose "kiss".
                    const float lane = (i & 1) ? 7.0f : -7.0f;
                    f.targetX = std::clamp(nearestPlantX(f.x, true), kFishMinX, kFishMaxX);
                    f.targetY = std::clamp(55.0f + lane, fishGoalMinY, fishGoalMaxY);
                } else if (brightHide) {
                    const float lane = (i & 1) ? 7.0f : -7.0f;
                    f.targetX = std::clamp(shadeX, kFishMinX, kFishMaxX);
                    f.targetY = std::clamp(58.0f + lane, fishGoalMinY, fishGoalMaxY);
                } else if (f.energy < 58.0f
                           && (browseRaft = nearestRaft(f.x, 44.0f)) >= 0) {
                    // A hungry, omnivorous fish rises to lip duckweed off the
                    // mat.  It aims at the top of its reachable band, which
                    // sits a few pixels under the seasonal waterline.
                    f.targetX = std::clamp(rafts_[static_cast<size_t>(browseRaft)].x,
                                           kFishMinX, kFishMaxX);
                    f.targetY = fishGoalMinY;
                } else if (f.territory) {
                    const float patrol = std::sin(animTime_ * 0.55f + i * 1.7f) * 7.0f;
                    f.targetX = std::clamp(f.territoryX + patrol, kFishMinX, kFishMaxX);
                    f.targetY = std::clamp(48.0f, fishGoalMinY, fishGoalMaxY);
                } else {
                    if (f.stage == 2 && !f.territory) {
                        const MicroCell& b = cellAt(f.x, f.y);
                        if (b.breedScent > 0.12f) {
                            float best = b.breedScent;
                            float bx = f.x, by = f.y;
                            const float ox[4] = {-20, 20, 0, 0};
                            const float oy[4] = {0, 0, -13, 13};
                            for (int k = 0; k < 4; ++k) {
                                const auto& c = cellAt(f.x + ox[k], f.y + oy[k]);
                                if (c.breedScent > best) {
                                    best = c.breedScent;
                                    bx = f.x + ox[k]; by = f.y + oy[k];
                                }
                            }
                            f.targetX = std::clamp(bx, kFishMinX, kFishMaxX);
                            f.targetY = std::clamp(by, fishGoalMinY, fishGoalMaxY);
                        }
                    }
                    f.dirTimer -= dt;
                    if (f.dirTimer <= 0.0f) {
                        f.dirTimer = rnd(2.5f, 6.5f);
                        float nx = f.x + rnd(-65.0f, 65.0f);
                        if (std::fabs(nx - f.x) < 20.0f) {
                            nx = f.x + (nx >= f.x ? 20.0f : -20.0f);
                        }
                        f.targetX = std::clamp(nx, kFishMinX, kFishMaxX);
                        // Vertical bias is applied once per new goal, not
                        // every frame: the old per-frame accumulation dragged
                        // every target straight onto the clamp bounds and left
                        // fish hovering motionless at the surface or the mud.
                        f.targetY = std::clamp(f.y + rnd(-14.0f, 14.0f)
                                               - nightO2Lift(f.x, f.y)
                                               + dielDepthBias(10.0f),
                                               fishGoalMinY, fishGoalMaxY);
                    }
                }
                f.targetX = std::clamp(f.targetX, kFishMinX, kFishMaxX);
                f.targetY = std::clamp(f.targetY, fishGoalMinY, fishGoalMaxY);
                const float dx = f.targetX - f.x;
                const float dy = f.targetY - f.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 4.0f) {
                    const float sp = (resting_ ? 5.5f : (f.territory ? 8.5f : 7.5f))
                                   * f.g.speed * motionScale(f.growth)
                                   * std::clamp(dist / 14.0f, 0.15f, 1.0f);
                    f.vx += (dx / dist * sp - f.vx) * std::min(1.0f, dt * 0.8f);
                    f.vy += (dy / dist * sp * 0.35f - f.vy) * std::min(1.0f, dt * 0.8f);
                } else {
                    const float damp = std::min(1.0f, dt * 1.8f);
                    f.vx -= f.vx * damp;
                    f.vy -= f.vy * damp;
                }
            }

            // Surface browse: a fish that actually reaches the raft takes a
            // small bite.  The cooldown is long and the bite is capped, so a
            // fish can trim the mat but never strip it.
            if (browseRaft >= 0 && f.huntCooldown <= 0.0f && !startled
                && f.energy < 66.0f
                && std::fabs(f.x - rafts_[static_cast<size_t>(browseRaft)].x) < 7.0f
                && f.y < waterSurfaceY() + 10.0f) {
                Raft& r = rafts_[static_cast<size_t>(browseRaft)];
                r.mass -= std::min(r.mass, 0.040f);
                f.energy = std::min(100.0f, f.energy + 7.5f);
                f.growth = std::min(1.0f, f.growth + 0.012f);
                f.huntCooldown = rnd(16.0f, 24.0f);
                r.mass = std::max(r.mass, 0.05f);
            }

            // Last-resort omnivory: only a starving adult nibbles a soft leaf.
            if ((!resting_ || f.energy < 30.0f) && f.energy < 9.0f
                && f.huntCooldown <= 0.0f && plantCount_ > 0) {
                const int pi = nearestPlant(f.x);
                if (pi >= 0 && std::fabs(plants_[pi].x - f.x) < 12.0f && f.y > 62.0f) {
                    Plant& p = plants_[pi];
                    p.energy = std::max(0.0f, p.energy - 5.5f);
                    p.health = std::max(0.0f, p.health - 0.015f);
                    f.energy = std::min(100.0f, f.energy + 7.0f);
                    f.growth = std::min(1.0f, f.growth + 0.018f);
                    f.huntCooldown = rnd(22.0f, 34.0f);
                }
            }

            const float speed = (0.45f + activity) * motionScale(f.growth) * f.g.speed
                              * (1.0f - 0.10f * f.epiphyte)
                              * (1.0f + (f.startleTimer > 0.0f ? 0.25f : 0.0f));
            f.x += f.vx * dt * speed;
            f.y += f.vy * dt * speed;
            f.x = std::clamp(f.x, 13.0f, 291.0f);
            f.y = std::clamp(f.y, fishMinY, 82.0f);
            if (f.x <= 13.0f) f.vx = std::fabs(f.vx);
            if (f.x >= 291.0f) f.vx = -std::fabs(f.vx);
            if (f.y <= fishMinY) f.vy = std::fabs(f.vy);
            if (f.y >= 82.0f) f.vy = -std::fabs(f.vy);
            f.vx = std::clamp(f.vx, -18.0f, 18.0f);
            f.vy = std::clamp(f.vy, -18.0f, 18.0f);
            if (f.hunting || f.chaseFish >= 0) {
                // P2: cap the deposit so a long chase cannot build an
                // unbounded plume that scares the whole column at once.
                MicroCell& scentCell = cellAtMut(f.x, f.y);
                scentCell.alarmScent = std::min(2.0f,
                    scentCell.alarmScent + dt * 0.75f);
            }
            if (f.territory) cellAtMut(f.x, f.y).breedScent += dt * 0.12f;

            const float localStress = localStressAt(f.x, f.y, 1, f.g);
            const float relief = rareSpeciesRelief(fishCount_, g_settings.maxFish);
            f.health -= dt * stressDamage(localStress) * 0.0110f * relief;
            f.energy = std::max(0.0f, f.energy - dt * localStress * 0.060f);
            const float hypoxia = std::clamp((22.0f - oxygen_) / 22.0f, 0.0f, 1.0f);
            const float tolerance = f.g.oxygen * (0.92f + 0.08f * f.g.size) * toleranceScale(f.growth);
            if (hypoxia > 0.0f) f.health -= dt * hypoxia * 0.0035f * relief / tolerance;
            if (f.energy <= 0.0f) f.health -= dt * 0.0120f;
            const float lifespan = 380.0f * lifespanScale(f.g, f.id);
            if (f.age > lifespan) {
                f.health -= dt * 0.0012f;
            } else if (f.health < 1.0f && f.energy > 35.0f && localStress < 0.70f) {
                const float calm = 1.0f - localStress / 0.70f;
                f.health = std::min(1.0f, f.health + dt * (0.0008f + 0.0022f * calm));
            }

            if (f.health <= 0.0f) {
                fishEggs_ = std::min(6, fishEggs_ + 1);
                fishEggGene_ = f.g;
                killFish(i);
                continue;
            }

            if (f.stage == 2 && f.health > 0.40f && f.energy >= (f.territory ? 56.0f : 58.0f)
                && f.breedCooldown <= 0.0f && oxygen_ > 52.0f
                && plantCount_ >= 3
                && fishCount_ < g_settings.maxFish && eggCount_ < kMaxEggs
                && localStressAt(f.x, f.y, 1, f.g) < 0.65f) {
                if (layEgg(1, childGenes(f.g), f.territory ? f.territoryX : f.x)) {
                    f.energy -= 24.0f;
                    f.breedCooldown = rnd(26.0f, 38.0f) * (0.90f + 0.20f * f.g.size)
                                    / breedingRate();
                    fishEggs_ = std::min(6, fishEggs_ + 2);
                    fishEggGene_ = f.g;
                }
            }
        }

        // Soft body separation: two adult bitterlings can share a territory
        // edge, but never occupy the same point or visibly overlap.  The
        // clearance follows the drawn width (16 px sprite * speciesBase 2.0),
        // so two adults no longer sit with their heads merged into a "kiss".
        for (int a = 0; a < fishCount_; ++a) {
            for (int b = a + 1; b < fishCount_; ++b) {
                Fish& fa = fish_[a];
                Fish& fb = fish_[b];
                float dx = fb.x - fa.x;
                float dy = fb.y - fa.y;
                float dist = std::sqrt(dx * dx + dy * dy);
                const float halfA = 8.0f * speciesBase(1) * visualScale(fa.growth) * fa.g.size;
                const float halfB = 8.0f * speciesBase(1) * visualScale(fb.growth) * fb.g.size;
                const float minD = halfA + halfB + 2.0f;
                if (dist >= minD) continue;
                if (dist < 0.01f) {
                    dx = (a & 1) ? 1.0f : -1.0f;
                    dy = 0.3f;
                    dist = 1.0f;
                }
                // A pair holding the same plant or raft at one height sits
                // dead level and reads as mouth to mouth.  A small fixed
                // stagger keeps them beside each other instead.
                if (std::fabs(dy) < 3.0f) {
                    dy = (a & 1) ? 3.0f : -3.0f;
                    dist = std::sqrt(dx * dx + dy * dy);
                }
                const float push = (minD - dist) * 0.5f;
                const float nx = dx / dist;
                const float ny = dy / dist;
                fa.x -= nx * push;
                fa.y -= ny * push * 0.7f;
                fb.x += nx * push;
                fb.y += ny * push * 0.7f;
                fa.vx -= nx * push * 0.8f;
                fb.vx += nx * push * 0.8f;
                fa.x = std::clamp(fa.x, 13.0f, 291.0f);
                fb.x = std::clamp(fb.x, 13.0f, 291.0f);
                fa.y = std::clamp(fa.y, std::max(20.0f, waterSurfaceY() + 4.0f), 82.0f);
                fb.y = std::clamp(fb.y, std::max(20.0f, waterSurfaceY() + 4.0f), 82.0f);
            }
        }
    }

    void updateWorms(float dt, float activity) {
        (void)activity;
        for (int i = wormCount_ - 1; i >= 0; --i) {
            Worm& w = worms_[i];
            w.age += dt;
            w.growth = std::min(1.0f, w.growth + maturationStep(
                w.energy, w.health, 0.0030f, dt));
            w.stage = stageFromGrowth(w.growth);
            w.breedCooldown -= dt;
            w.dirTimer -= dt;
            if (w.dirTimer <= 0.0f) {
                w.dirTimer = rnd(4.0f, 11.0f);
                w.vx = rnd(-0.7f, 0.7f) * w.g.speed;
            }
            w.x += w.vx * dt * motionScale(w.growth);
            w.x = std::clamp(w.x, 8.0f, 302.0f);
            if (w.x <= 8.0f || w.x >= 302.0f) w.vx = -w.vx;

            const float available = std::clamp((humus_ - 4.0f) / 40.0f, 0.0f, 1.0f);
            const float decomposition = 0.055f * available;
            humus_ = std::max(0.0f, humus_ - decomposition * dt);
            nutrient_ = std::min(100.0f, nutrient_ + decomposition * 0.90f * dt);
            w.energy = std::clamp(w.energy + decomposition * 17.0f * dt
                                  - dt * 0.060f * w.g.size
                                    * (0.72f + 0.28f * stageScale(w.stage))
                                    * temperatureQ10(), 0.0f, 100.0f);
            w.growth = std::min(1.0f, w.growth + decomposition * 0.04f * dt);

            const float wormStress = localStressAt(w.x, 92.0f, 3, w.g);
            w.health -= dt * stressDamage(wormStress) * 0.0100f;
            w.energy = std::max(0.0f, w.energy - dt * wormStress * 0.035f);
            if (w.energy <= 0.0f) w.health -= dt * 0.0060f;
            if (w.age > 320.0f * (0.85f + 0.15f * w.g.size)) {
                w.health -= dt * 0.0015f;
            } else if (w.health < 1.0f && w.energy > 30.0f && wormStress < 0.70f) {
                const float calm = 1.0f - wormStress / 0.70f;
                w.health = std::min(1.0f, w.health + dt * (0.0008f + 0.0020f * calm));
            }
            if (w.health <= 0.0f) {
                killWorm(i);
                continue;
            }

            if (w.stage == 2 && w.health > 0.40f && w.energy >= 76.0f
                && w.breedCooldown <= 0.0f && humus_ > 34.0f
                && oxygen_ > 30.0f && wormCount_ < g_settings.maxWorms) {
                spawnWorm(w.x + rnd(-20.0f, 20.0f), 0, childGenes(w.g), 0.0f, 0.0f);
                w.energy -= 28.0f;
                w.breedCooldown = rnd(35.0f, 55.0f) * (0.90f + 0.20f * w.g.size)
                                / breedingRate();
            }
        }
    }

    void updateTubifex(float dt) {
        for (int i = tubifexCount_ - 1; i >= 0; --i) {
            Tubifex& t = tubifex_[i];
            t.age += dt;
            t.breedCooldown -= dt;
            const float localFood = std::clamp(sedimentAtX(t.x) / 55.0f
                                               + humus_ / 90.0f, 0.0f, 1.3f);
            const float lowO2 = std::clamp((32.0f - oxygen_) / 32.0f, 0.0f, 1.0f);

            t.energy = std::clamp(t.energy
                                  + dt * (0.40f * localFood - 0.10f
                                          - 0.25f * t.epiphyte)
                                  - dt * 0.08f * lowO2, 0.0f, 100.0f);
            const float tubifexStress = localStressAt(t.x, t.y, 4, t.g);
            t.health -= dt * (0.0020f + 0.020f * lowO2
                              + stressDamage(tubifexStress) * 0.0100f)
                      / std::max(0.6f, t.g.oxygen);
            if (t.health < 1.0f && t.energy > 30.0f && tubifexStress < 0.70f) {
                const float calm = 1.0f - tubifexStress / 0.70f;
                t.health = std::min(1.0f, t.health + dt * (0.0008f + 0.0020f * calm));
            }
            t.growth = std::min(1.0f, t.growth + dt * 0.006f * localFood
                                 + maturationStep(t.energy, t.health, 0.0040f, dt));
            t.stage = stageFromGrowth(t.growth);

            // They graze the very top of the mud, not the water column.
            addSediment(t.x, -dt * 0.026f * localFood);
            // Detritus they graze is digested and released back into the water
            // column as ammonium, so the nitrogen loop stays closed instead of
            // leaking a slice of humus away every tick.
            const float grazedHumus = dt * 0.0015f * localFood;
            humus_ = std::max(0.0f, humus_ - grazedHumus);
            nutrient_ = std::min(100.0f, nutrient_ + grazedHumus * 0.85f);
            if (t.energy > 66.0f) {
                addSediment(t.x, dt * 0.006f);
            }

            t.phase += dt * (0.45f + 0.45f * (1.0f - t.health));
            t.vx = std::cos(t.phase * 0.75f) * 0.6f;
            t.x += std::sin(t.phase * 0.75f) * dt * 0.55f;
            // Slow, individually phased crawl along the mud so a cluster
            // gradually spreads instead of staying a frozen pile.
            t.x += std::sin(t.age * 0.045f + t.id * 0.61f) * dt * 0.30f;
            const FoodPulse* food = nearestFood(t.x, t.y);
            if (food) {
                const float fd = food->x - t.x;
                // Only worms close to a food pulse react, otherwise the whole
                // population marches onto the same spot and piles into a ball.
                if (std::fabs(fd) > 2.0f && std::fabs(fd) < 38.0f
                    && std::fabs(food->y - t.y) < 20.0f) {
                    t.x += (fd > 0.0f ? 1.0f : -1.0f) * dt * 2.2f;
                    t.vx = fd > 0.0f ? 2.0f : -2.0f;
                }
            }
            // Soft spacing: overlapping neighbours push apart a little, and a
            // dense knot slowly loosens, so a cluster reads as several worms.
            {
                int close = 0;
                float bias = 0.0f;
                for (int j = 0; j < tubifexCount_; ++j) {
                    if (j == i) continue;
                    const float d = t.x - tubifex_[j].x;
                    if (std::fabs(d) < 6.0f) t.x += (d >= 0.0f ? 1.0f : -1.0f) * dt * 1.0f;
                    if (std::fabs(d) < 12.0f) {
                        ++close;
                        bias += d;
                    }
                }
                if (close >= 3) {
                    t.x += (bias >= 0.0f ? 1.0f : -1.0f) * dt * 0.9f;
                }
            }
            t.x = std::clamp(t.x, 8.0f, 302.0f);
            t.y = 94.6f + std::sin(t.phase * 0.5f + t.x + t.id * 0.11f) * 1.8f;
            cellAtMut(t.x, t.y).preyScent += dt * 0.22f;

            if (t.health <= 0.0f || t.age > 180.0f + 60.0f * t.g.size) {
                killTubifex(i);
                continue;
            }

            if (t.health > 0.40f && t.breedCooldown <= 0.0f && t.energy > 74.0f
                && localFood > 0.66f && oxygen_ > 36.0f
                && tubifexCount_ < g_settings.maxTubifex) {
                spawnTubifex(t.x + rnd(-10.0f, 10.0f), childGenes(t.g));
                t.energy -= 30.0f;
                // They are the jar's detritivore channel, so they multiply
                // while the mud is rich.  Once the nymphs start cropping them
                // this is the term that replaces what was eaten.
                t.breedCooldown = rnd(55.0f, 85.0f);
            }
        }
    }

    void updateEpiphyte(float dt) {
        // An algal bloom coats every submerged surface a little faster.
        const float bloomBoost = 1.0f + 0.90f * eventBloom_;
        for (int i = 0; i < plantCount_; ++i) {
            Plant& p = plants_[i];
            if (p.dying) continue;
            const MicroCell& c = cellAt(p.x, 62.0f);
            const float lightFactor = 0.30f + 0.70f * (c.light / 100.0f);
            const float base = (0.00055f + 0.00125f * lightFactor) * bloomBoost;
            p.epiphyte = std::clamp(p.epiphyte + dt * base
                                    * (0.55f + 0.45f * (1.0f - p.health))
                                    * (1.0f - p.epiphyte), 0.0f, 0.72f);
        }
        auto animalEpiphyte = [&](float& epi, float x, float y) {
            const MicroCell& c = cellAt(x, y);
            const float lightFactor = 0.30f + 0.70f * (c.light / 100.0f);
            epi = std::clamp(epi + dt * (0.00035f + 0.00075f * lightFactor)
                             * bloomBoost * (1.0f - epi), 0.0f, 0.62f);
        };
        for (int i = 0; i < shrimpCount_; ++i) {
            animalEpiphyte(shrimp_[i].epiphyte, shrimp_[i].x, shrimp_[i].y);
        }
        for (int i = 0; i < damselCount_; ++i) {
            animalEpiphyte(damsel_[i].epiphyte, damsel_[i].x, damsel_[i].y);
        }
        for (int i = 0; i < fishCount_; ++i) {
            animalEpiphyte(fish_[i].epiphyte, fish_[i].x, fish_[i].y);
        }
    }

    void updateSediment(float dt) {
        // Silt and the detritus blanket above it are one pool seen at two
        // depths: waste settles into the bed and the bed is worked over and
        // slowly redissolved by roots, worms and the microbial loop.  Writing
        // that exchange as a relaxation keeps each bin near the detritus load
        // that feeds it.
        //
        // The old form turned every unit of humus into a hundred units of
        // silt.  Nothing else in the budget works at that scale, so after a
        // few minutes all sixteen bins sat at 100, the bed ran anoxic for the
        // rest of the run, and the toxin it produced killed the worms that
        // were supposed to keep the humus down.
        // Silt is built by the rooted stand as much as by the drifting
        // detritus.  Tracking humus alone made the bed a mirror of animal
        // waste: once the population thinned, the bed relaxed to 20 + 0.45 *
        // humus (~25), which is below every recruitment threshold on the bed
        // (plants 30, tubifex 26), so nothing could ever be recruited again
        // and the pond settled into a permanent low.  Roots, leaf litter and
        // the litter the stand sheds each cycle hold the bed up instead, so a
        // surviving meadow can rebuild its own habitat.
        const float target = std::clamp(20.0f + humus_ * 0.45f
                                        + plantBiomass_ * 1.8f, 20.0f, 72.0f);
        const float relax = std::min(1.0f, dt * 0.060f);
        for (int i = 0; i < kSedimentBins; ++i) {
            sediment_[i] += (target - sediment_[i]) * relax;
        }
        // Mineralisation liberates a little dissolved phosphorus on the way.
        phosphorus_ = std::min(100.0f, phosphorus_ + dt * 0.020f
                               * std::clamp(humus_ / 40.0f, 0.0f, 1.0f));
        // Gentle lateral diffusion, so a corpse enriches a small neighborhood.
        const float diffusion = 0.018f * dt;
        for (int i = 0; i < kSedimentBins; ++i) {
            const float left = (i > 0) ? sediment_[i - 1] : sediment_[i];
            const float right = (i + 1 < kSedimentBins) ? sediment_[i + 1] : sediment_[i];
            sediment_[i] += (left + right - 2.0f * sediment_[i]) * diffusion;
            sediment_[i] = std::clamp(sediment_[i], 4.0f, 100.0f);
        }
    }

    void addSediment(float x, float amount) {
        const int bin = std::clamp(static_cast<int>(x * kSedimentBins / 320.0f),
                                   0, kSedimentBins - 1);
        sediment_[bin] = std::clamp(sediment_[bin] + amount, 4.0f, 100.0f);
    }

    int nearestTubifexIndex(float x, float y, float maxDist) const {
        int best = -1;
        float bestD = maxDist * maxDist;
        for (int i = 0; i < tubifexCount_; ++i) {
            const float dx = tubifex_[i].x - x;
            const float dy = tubifex_[i].y - y;
            const float d = dx * dx + dy * dy;
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    void initMicroGrid() {
        for (int gy = 0; gy < kGridH; ++gy) {
            for (int gx = 0; gx < kGridW; ++gx) {
                MicroCell c;
                const float x = gx * 20.0f + 10.0f;
                c.light = light_ * (0.92f - 0.035f * gy);
                c.o2 = std::clamp(oxygen_ + (3.0f - gy) * 5.0f, 8.0f, 100.0f);
                c.nutrient = std::clamp(nutrient_ * 0.4f + sedimentAtX(x) * 0.6f,
                                        0.0f, 100.0f);
                c.tannin = 4.0f + (kGridH - 1 - gy) * 0.8f;
                c.aerobic = 0.38f;
                c.anaerobic = 0.12f + gy * 0.018f;
                grid_[gy * kGridW + gx] = c;
            }
        }
        refreshGridDensity();
    }

    void refreshGridDensity() {
        for (MicroCell& c : grid_) {
            c.density = 0.0f;
            for (float& d : c.speciesDensity) d = 0.0f;
        }
        auto add = [&](float x, float y, int species) {
            const int gx = std::clamp(static_cast<int>(x / 20.0f), 0, kGridW - 1);
            const int gy = std::clamp(static_cast<int>((y - 12.0f) / 12.5f),
                                      0, kGridH - 1);
            MicroCell& c = grid_[gy * kGridW + gx];
            c.density += 1.0f;
            c.speciesDensity[species] += 1.0f;
        };
        for (int i = 0; i < plantCount_; ++i) add(plants_[i].x, 84.0f, 5);
        for (int i = 0; i < shrimpCount_; ++i) add(shrimp_[i].x, shrimp_[i].y, 0);
        for (int i = 0; i < fishCount_; ++i) add(fish_[i].x, fish_[i].y, 1);
        for (int i = 0; i < damselCount_; ++i) add(damsel_[i].x, damsel_[i].y, 2);
        for (int i = 0; i < wormCount_; ++i) add(worms_[i].x, 92.0f, 3);
        for (int i = 0; i < tubifexCount_; ++i) add(tubifex_[i].x, tubifex_[i].y, 4);
    }

    void updateMicroGrid(float dt) {
        ++gridFrame_;
        if (gridFrame_ % 30 != 0) return;
        const float tick = std::clamp(dt * 30.0f, 0.35f, 4.5f);
        const float q10 = std::clamp(temperatureQ10(), 0.6f, 1.5f);
        gridScratch_ = grid_;
        const float organic = humus_ / 100.0f + corpseCount_ * 0.018f
                            + eggCount_ * 0.008f;
        // P1: thin night-time oxygen debt.  Surface photosynthesis stops while
        // the sediment keeps respiring, so the lowest rows lose a few points of
        // saturation.  The cap is small on purpose: it can only nudge the
        // existing chronic-stress path, never cause a sudden die-off.
        const float nightWater = std::clamp((0.90f - daylight_) / 0.12f,
                                            0.0f, 1.0f);
        // P3: in warm summer water the surface layer is less willing to mix
        // downwards, so oxygen exchange runs slightly slower.  Capped at 10%
        // so stratification can only deepen an existing gradient and never
        // suffocate the bottom on its own.
        const float summerMix = 1.0f - 0.10f * summerFactor();

        for (int gy = 0; gy < kGridH; ++gy) {
            for (int gx = 0; gx < kGridW; ++gx) {
                const float x = gx * 20.0f + 10.0f;
                float shade = 0.0f;
                for (int p = 0; p < plantCount_; ++p) {
                    if (plants_[p].dying) continue;
                    const float prox = 1.0f - std::min(1.0f,
                        std::fabs(plants_[p].x - x) / 38.0f);
                    shade += prox * plants_[p].growth;
                }
                // Driftwood casts a little shade; pebbles and shells grow a
                // quiet biofilm that feeds the local microbial loop.
                float decorFood = 0.0f;
                for (int d = 0; d < decorCount_; ++d) {
                    const float prox = 1.0f - std::min(1.0f,
                        std::fabs(decors_[d].x - x) / 26.0f);
                    if (prox <= 0.0f) continue;
                    if (decors_[d].type == 1) {
                        shade += prox * 0.30f * decors_[d].scale;
                    } else {
                        decorFood += prox * 0.006f * decors_[d].scale;
                    }
                }
                shade = std::clamp(shade, 0.0f, 1.4f);
                // Duckweed shades only the top of the column.  `shallow` is
                // 1 at the surface row and fades to 0 four rows down.
                const float raftCover = duckweedCoverAt(x);
                const float shallow = std::clamp((3.0f - static_cast<float>(gy)) / 3.0f,
                                                 0.0f, 1.0f);
                // The shrub-covered bank shades and faintly stains only the
                // first two rows under the surface.  Both caps are tiny, so
                // the land can tint the pond edge but never starve it.
                const float bankEdge = std::clamp((2.0f - static_cast<float>(gy)) / 2.0f,
                                                  0.0f, 1.0f);
                MicroCell& c = gridScratch_[gy * kGridW + gx];
                c.tannin = std::clamp(c.tannin
                    + tick * (0.012f * organic + 0.003f - 0.004f * shade
                              - 0.0010f * c.tannin
                              + eventTanninRate_ + 0.004f * bankWash_ * bankEdge),
                    0.0f, 82.0f);
                c.light = std::clamp(light_ * (1.0f - 0.52f * shade)
                          * (1.0f - 0.42f * c.tannin / 82.0f)
                          * (1.0f - 0.30f * raftCover * shallow)
                          * (1.0f - 0.08f * shoreFoliage_ * bankEdge)
                          + lightBoostAt(x, 12.0f + gy * 12.5f), 0.0f, 100.0f);
                c.o2 = std::clamp(oxygen_ + (3.0f - gy) * 5.4f
                       + plantO2_ * 0.65f * shade
                       + raftCover * shallow * 2.0f * daylight_
                       - night_ * 7.0f * (3.0f - gy)
                       - nightWater * std::clamp((gy - 4.0f) / 3.0f, 0.0f, 1.0f) * 5.5f,
                       5.0f, 100.0f);
                c.nutrient = std::clamp(nutrient_ * 0.35f
                           + sedimentAtX(x) * 0.70f
                           + decorFood * tick
                           + eventNutrientRate_ * tick * 2.0f, 0.0f, 100.0f);
                c.aerobic = std::clamp(c.aerobic
                    + tick * (0.17f * q10 * (c.o2 / 100.0f) * organic
                              - 0.055f * c.aerobic), 0.0f, 1.0f);
                // Oxygen demand of the detritus blanket.  Decay has to be able
                // to keep up with production, otherwise every cell below about
                // half saturation sat at anaerobic = 1 for the whole run and
                // the bed produced toxin without pause.
                c.anaerobic = std::clamp(c.anaerobic
                    + tick * (0.070f * q10 * (1.0f - c.o2 / 100.0f) * organic
                              - 0.048f * c.anaerobic), 0.0f, 1.0f);
                c.toxin = std::clamp(c.toxin
                    + tick * (c.anaerobic * 0.055f * q10 - c.toxin * 0.020f
                              + eventToxinRate_ * q10),
                    0.0f, 60.0f);
                c.preyScent *= std::exp(-tick * 0.065f);
                c.alarmScent *= std::exp(-tick * 0.085f);
                c.breedScent *= std::exp(-tick * 0.045f);
            }
        }

        // One low-probability natural disturbance per grid tick: a leaf pulse
        // or a small sediment stir.  Never global, never catastrophic.
        const float disturbanceChance = 0.012f
            * std::clamp(static_cast<float>(g_settings.disturbance) / 100.0f, 0.0f, 3.0f);
        if (rnd(0.0f, 1.0f) < disturbanceChance) {
            const int idx = rndInt(0, kGridN - 1);
            MicroCell& c = gridScratch_[idx];
            if (rnd(0.0f, 1.0f) < 0.65f) {
                c.tannin = std::min(82.0f, c.tannin + rnd(4.0f, 11.0f));
                c.nutrient = std::min(100.0f, c.nutrient + rnd(4.0f, 10.0f));
            } else {
                c.toxin = std::min(60.0f, c.toxin + rnd(2.0f, 6.0f));
                c.anaerobic = std::min(1.0f, c.anaerobic + 0.08f);
            }
        }

        // Slow neighbour diffusion on the cached grid.
        auto diffuse = [&](float MicroCell::*field, float amount) {
            for (int gy = 0; gy < kGridH; ++gy) {
                for (int gx = 0; gx < kGridW; ++gx) {
                    const int i = gy * kGridW + gx;
                    float sum = 0.0f;
                    int n = 0;
                    if (gx > 0) { sum += gridScratch_[i - 1].*field; ++n; }
                    if (gx + 1 < kGridW) { sum += gridScratch_[i + 1].*field; ++n; }
                    if (gy > 0) { sum += gridScratch_[i - kGridW].*field; ++n; }
                    if (gy + 1 < kGridH) { sum += gridScratch_[i + kGridW].*field; ++n; }
                    if (n > 0) {
                        gridScratch_[i].*field = gridScratch_[i].*field * (1.0f - amount)
                                               + (sum / n) * amount;
                    }
                }
            }
        };
        diffuse(&MicroCell::tannin, 0.10f);
        diffuse(&MicroCell::toxin, 0.10f);
        diffuse(&MicroCell::o2, 0.08f * summerMix);
        diffuse(&MicroCell::nutrient, 0.06f);
        diffuse(&MicroCell::aerobic, 0.08f);
        diffuse(&MicroCell::anaerobic, 0.08f);
        diffuse(&MicroCell::preyScent, 0.10f);
        diffuse(&MicroCell::alarmScent, 0.10f);
        diffuse(&MicroCell::breedScent, 0.08f);

        grid_ = gridScratch_;
        refreshGridDensity();
    }

    // P1: gentle upward nudge when an animal sits just above a much poorer
    // oxygen cell at night.  Returns 0 during the day or in a well-mixed
    // column, and is capped at a few pixels so it biases wandering targets
    // instead of teleporting anyone.
    float nightO2Lift(float x, float y) const {
        const float nightWater = std::clamp((0.90f - daylight_) / 0.12f,
                                            0.0f, 1.0f);
        if (nightWater <= 0.02f) return 0.0f;
        const float below = cellAt(x, y + 12.0f).o2;
        const float above = cellAt(x, y - 12.0f).o2;
        const float gradient = std::clamp((above - below - 2.0f) / 10.0f,
                                          0.0f, 1.0f);
        const float poor = std::clamp((52.0f - below) / 20.0f, 0.0f, 1.0f);
        return nightWater * gradient * poor * 6.0f;
    }

    // P2: diel vertical migration bias.  Positive means "deeper" (larger y),
    // so a positive amplitude sinks the animal at night and lifts it by day.
    // It only shifts wandering targets a few pixels; it never teleports or
    // overrides hunting, fleeing or moribund behaviour.
    float dielDepthBias(float amplitude) const {
        const float day = std::clamp((daylight_ - 0.78f) / 0.22f, 0.0f, 1.0f);
        return (0.5f - day) * 2.0f * amplitude;
    }

    // P3: how much summer stratification applies right now.  0 below 23 C,
    // rising to 1 at 26 C.  A heatwave therefore deepens it a little, and the
    // effect fades out on its own once the water cools again.
    float summerFactor() const {
        return std::clamp((temperature_ - 23.0f) / 3.0f, 0.0f, 1.0f);
    }

    // Chronic background stress is survivable.  Only the part that climbs
    // above an animal's tolerance actually injures it; below that line the
    // price is paid in energy and in spawning.  Without this floor a merely
    // mediocre patch of water -- a little tannin, a little silt, a little
    // crowding -- subtracted health every single second, and since no cell
    // ever returns to pristine the whole standing crop was ground down to
    // zero on a fixed schedule.
    static float stressDamage(float stress) {
        return std::max(0.0f, stress - 0.55f);
    }

    float localStressAt(float x, float y, int kind, const Genes& g) const {
        const MicroCell& c = cellAt(x, y);
        const float lowO2 = std::clamp((42.0f - c.o2) / 42.0f, 0.0f, 1.0f);
        const float tanninHigh = std::clamp((c.tannin - 34.0f) / 46.0f, 0.0f, 1.0f);
        const float toxin = std::clamp(c.toxin / 24.0f, 0.0f, 1.0f);
        // Benthic species all share one narrow strip of mud at the bottom of
        // the same 20 px column, so ten worms genuinely live in one cell.  Read
        // their crowding on a coarser scale, or a healthy bed always scores as
        // maximally overcrowded.
        const float crowdScale = (kind >= 3) ? 9.0f : 4.0f;
        const float same = std::clamp((c.speciesDensity[kind] - 3.0f) / crowdScale,
                                      0.0f, 1.0f);
        const float prefY = (kind == 1) ? 42.0f : (kind == 2) ? 62.0f
                          : (kind == 0) ? 72.0f : 90.0f;
        const float layer = std::clamp((std::fabs(y - prefY) - 19.0f) / 30.0f,
                                       0.0f, 1.0f);
        const float tempDiff = std::fabs(temperature_ - 22.0f);
        const float tempTol = 4.0f * std::max(0.7f, g.temp);
        const float temp = std::clamp((tempDiff - tempTol) / 10.0f, 0.0f, 1.0f);
        const float synergy = lowO2 * tanninHigh * 1.2f
                            + toxin * (0.4f + 0.6f * same)
                            + layer * lowO2 * 0.8f;
        const float calm = 0.045f * std::min(1.0f, c.tannin / 30.0f);
        // P1: a low-grade night hypoxia term for animals living on the bottom
        // rows.  It is deliberately capped low and only present when the local
        // cell is already below 50% saturation after the nightly oxygen debt.
        const float nightWater = std::clamp((0.90f - daylight_) / 0.12f,
                                            0.0f, 1.0f);
        const float nightHypoxia = (kind == 5) ? 0.0f
            : nightWater
            * std::clamp((y - 60.0f) / 24.0f, 0.0f, 1.0f)
            * std::clamp((50.0f - c.o2) / 18.0f, 0.0f, 1.0f);
        return std::clamp(0.42f * lowO2 + 0.34f * tanninHigh + 0.58f * toxin
                          + 0.26f * same + 0.22f * layer + 0.16f * temp
                          + 0.16f * nightHypoxia
                          + 0.70f * synergy - calm, 0.0f, 1.6f);
    }

    float temperatureQ10() const {
        return q10_;
    }

    bool findEggSite(int kind, float parentX, float& ex, float& ey, int& plant) {
        int best = -1;
        float bestScore = 1e9f;
        for (int i = 0; i < plantCount_; ++i) {
            const Plant& p = plants_[i];
            if (p.dying || p.stage < 1) continue;
            bool ok = false;
            if (kind == 0) ok = (p.type == 1 || p.type == 2);
            else if (kind == 1) ok = (p.stage >= 2);
            else ok = (p.type == 0 || p.type == 1);
            if (!ok) continue;
            const float score = std::fabs(p.x - parentX)
                              - (p.stage >= 2 ? 8.0f : 0.0f);
            if (score < bestScore) { bestScore = score; best = i; }
        }
        if (best < 0) return false;
        plant = best;
        ex = std::clamp(plants_[best].x + rnd(-3.0f, 3.0f), 12.0f, 308.0f);
        ey = (kind == 1) ? 82.0f : (kind == 2 ? 74.0f : 62.0f);
        return true;
    }

    bool layEgg(int kind, const Genes& g, float parentX) {
        float ex = 0.0f, ey = 0.0f;
        int plant = -1;
        if (eggCount_ >= kMaxEggs) return false;
        if (!findEggSite(kind, parentX, ex, ey, plant)) return false;
        Egg e;
        e.kind = kind;
        e.plant = plant;
        e.x = ex;
        e.y = ey;
        e.timer = 0.0f;
        e.epiphyte = 0.0f;
        e.g = g;
        eggs_[eggCount_++] = e;
        return true;
    }

    void updateAttachedEggs(float dt) {
        for (int i = eggCount_ - 1; i >= 0; --i) {
            Egg& e = eggs_[i];
            const MicroCell& c = cellAt(e.x, e.y);
            const float hatch = std::clamp(0.25f + c.o2 / 100.0f * 1.15f
                                 - c.tannin / 100.0f * 0.55f
                                 - e.epiphyte * 0.80f, 0.08f, 1.55f);
            e.timer += dt * hatch;
            e.epiphyte = std::clamp(e.epiphyte
                         + dt * (0.0006f + 0.0011f * c.light / 100.0f)
                         * (1.0f - e.epiphyte), 0.0f, 0.65f);
            if (e.timer >= 28.0f) {
                if (e.kind == 0) {
                    spawnShrimp(e.x, e.y, 0, childGenes(e.g), 0.0f);
                } else if (e.kind == 1) {
                    spawnFish(e.x, e.y, 0, childGenes(e.g), 0.0f);
                } else {
                    spawnDamsel(e.x, e.y, 0, childGenes(e.g), 0.0f);
                }
                for (int j = i; j < eggCount_ - 1; ++j) eggs_[j] = eggs_[j + 1];
                --eggCount_;
            }
        }
    }

    void ensureRefuges(float dt) {
        // No adult is ever conjured from nothing.  Every lineage keeps a
        // dormant propagule bank (seeds, resting eggs, cysts); if a species
        // dies out completely we only top that bank back up, and the recruit
        // still has to germinate and grow under normal conditions.
        if (g_settings.refuge == 0) return;
        const float wait = 20.0f;
        // A bank is replenished when the species has vanished *or* when no
        // reproductive adult is left.  A lone juvenile cannot breed, so without
        // this the lineage could sit there until it aged out.
        auto keepBank = [&](int active, bool canReproduce, int& bank, float& timer) {
            if (active > 0 && canReproduce) { timer = 0.0f; return; }
            // Only the dormant bank is topped up here, and it is a last resort
            // rather than a way of life: the ordinary cycle below keeps laying
            // fresh eggs, so the community should never need this at all.
            timer += dt;
            if (timer < wait) return;
            timer = 0.0f;
            if (bank < 1) bank = 1;
        };
        bool aliveType[3] = {false, false, false};
        bool matureType[3] = {false, false, false};
        for (int i = 0; i < plantCount_; ++i) {
            if (!plants_[i].dying && plants_[i].type >= 0 && plants_[i].type < 3) {
                aliveType[plants_[i].type] = true;
                if (plants_[i].stage == 2) matureType[plants_[i].type] = true;
            }
        }
        for (int t = 0; t < 3; ++t) {
            if (aliveType[t] && matureType[t]) { plantTypeRefugeTimer_[t] = 0.0f; continue; }
            plantTypeRefugeTimer_[t] += dt;
            if (plantTypeRefugeTimer_[t] < wait) continue;
            plantTypeRefugeTimer_[t] = 0.0f;
            if (plantTypeSeeds_[t] >= 1) continue;
            // A dormant seed survives in the mud; germination still needs a
            // free slot, light, oxygen and enough sediment.
            float sx = 160.0f;
            for (int k = 0; k < 8; ++k) {
                const float candidate = rnd(28.0f, 292.0f);
                if (sedimentAtX(candidate) > 24.0f) { sx = candidate; break; }
            }
            plantTypeSeeds_[t] = 1;
            plantTypeSeedX_[t] = sx;
            plantTypeSeedTimer_[t] = 0.0f;
        }
        bool adultShrimp = false, adultFish = false, adultDamsel = false;
        bool adultWorm = false;
        for (int i = 0; i < shrimpCount_; ++i) adultShrimp |= shrimp_[i].stage == 2;
        for (int i = 0; i < fishCount_; ++i) adultFish |= fish_[i].stage == 2;
        for (int i = 0; i < damselCount_; ++i) adultDamsel |= damsel_[i].stage == 2;
        for (int i = 0; i < wormCount_; ++i) adultWorm |= worms_[i].stage == 2;
        keepBank(shrimpCount_, adultShrimp, shrimpEggs_, refugeTimer_[1]);
        keepBank(fishCount_, adultFish, fishEggs_, refugeTimer_[2]);
        keepBank(damselCount_, adultDamsel, damselEggs_, refugeTimer_[3]);
        keepBank(wormCount_, adultWorm, wormCysts_, refugeTimer_[4]);
        keepBank(tubifexCount_, true, tubifexEggs_, refugeTimer_[5]);
    }


    void updateEggs(float dt) {
        updateAttachedEggs(dt);
        // Hatching and spawning never key off the shrimp population.  A hard
        // "two shrimp alive" gate used to lock the whole bottle: after a shrimp
        // crash the fish and damselfly banks could neither hatch nor refill, so
        // all three lineages could sit empty at the same time.  Oxygen and
        // plant cover are the conditions that actually matter to a recruit.
        if (fishEggs_ > 0 && fishCount_ < g_settings.maxFish && oxygen_ > 42.0f
            && plantCount_ >= 2) {
            fishEggTimer_ += dt;
            if (fishEggTimer_ >= 28.0f) {
                fishEggTimer_ = 0.0f;
                spawnFish(rnd(40.0f, 280.0f), rnd(28.0f, 62.0f), 0,
                          childGenes(fishEggGene_), 0.0f);
                --fishEggs_;
            }
        } else {
            fishEggTimer_ = 0.0f;
        }

        if (damselEggs_ > 0 && damselCount_ < g_settings.maxDamsel && oxygen_ > 44.0f
            && plantCount_ >= 2
            // Same food gate as laying: during a prey trough the dormant
            // clutch simply waits, then resumes once the shrimp herd is back.
            // Three is the least a brood can hatch into and still feed itself.
            && shrimpCount_ + tubifexCount_ / 10 >= 3) {
            damselEggTimer_ += dt;
            if (damselEggTimer_ >= 30.0f) {
                damselEggTimer_ = 0.0f;
                spawnDamsel(rnd(40.0f, 280.0f), rnd(28.0f, 62.0f), 0,
                            childGenes(damselEggGene_), 0.0f);
                --damselEggs_;
            }
        } else {
            damselEggTimer_ = 0.0f;
        }

        if (shrimpEggs_ > 0 && shrimpCount_ < g_settings.maxShrimp && oxygen_ > 44.0f
            && plantCount_ >= 2) {
            shrimpEggTimer_ += dt;
            if (shrimpEggTimer_ >= 20.0f) {
                shrimpEggTimer_ = 0.0f;
                spawnShrimp(rnd(30.0f, 290.0f), rnd(30.0f, 68.0f), 0,
                            childGenes(shrimpEggGene_), 0.0f);
                --shrimpEggs_;
            }
        } else {
            shrimpEggTimer_ = 0.0f;
        }

        // Slow dormant egg banks keep a lineage recoverable without making
        // adults immortal.  The banks only refill while an adult is alive.
        bool adultFish = false, adultDamsel = false, adultShrimp = false;
        bool maturePlant = false;
        for (int i = 0; i < fishCount_; ++i) adultFish |= fish_[i].stage == 2;
        for (int i = 0; i < damselCount_; ++i) adultDamsel |= damsel_[i].stage == 2;
        for (int i = 0; i < shrimpCount_; ++i) adultShrimp |= shrimp_[i].stage == 2;
        for (int i = 0; i < plantCount_; ++i) maturePlant |= plants_[i].stage == 2;
        if (adultFish && rnd(0.0f, 1.0f) < dt * 0.014f * breedingRate()) {
            fishEggs_ = std::min(6, fishEggs_ + 1);
        }
        if (adultDamsel && rnd(0.0f, 1.0f) < dt * 0.022f * breedingRate()) {
            damselEggs_ = std::min(6, damselEggs_ + 1);
        }
        if (adultShrimp && rnd(0.0f, 1.0f) < dt * 0.040f * breedingRate()) {
            shrimpEggs_ = std::min(6, shrimpEggs_ + 1);
        }
        if (maturePlant && rnd(0.0f, 1.0f) < dt * 0.0030f * breedingRate()) {
            plantSeeds_ = std::min(5, plantSeeds_ + 1);
        }

        if (plantCount_ < 2 && plantSeeds_ > 0
            && oxygen_ > 40.0f && light_ > 20.0f) {
            plantSeedTimer_ += dt;
            if (plantSeedTimer_ >= 120.0f) {
                plantSeedTimer_ = 0.0f;
                float x = 160.0f;
                for (int k = 0; k < 10; ++k) {
                    const float candidate = rnd(24.0f, 296.0f);
                    if (sedimentAtX(candidate) > 28.0f) { x = candidate; break; }
                }
                PlantGenes pg{1.0f, 1.0f, 1.0f, 1.0f};
                spawnPlant(rndInt(0, 2), x, pg);
                --plantSeeds_;
            }
        } else {
            plantSeedTimer_ = 0.0f;
        }

        if (tubifexCount_ > 0 && rnd(0.0f, 1.0f) < dt * 0.006f) {
            tubifexEggs_ = std::min(6, tubifexEggs_ + 1);
        }
        if (tubifexCount_ < 6 && tubifexEggs_ > 0
            && sedimentAtX(160.0f) > 22.0f) {
            tubifexEggTimer_ += dt;
            if (tubifexEggTimer_ >= 55.0f) {
                tubifexEggTimer_ = 0.0f;
                spawnTubifex(rnd(24.0f, 296.0f), makeGenes(1.0f));
                --tubifexEggs_;
            }
        } else {
            tubifexEggTimer_ = 0.0f;
        }

        if (wormCount_ > 0 && rnd(0.0f, 1.0f) < dt * 0.004f) {
            wormCysts_ = std::min(4, wormCysts_ + 1);
        }
        // A resting cyst only needs wet mud, not a rich detritus pool.  The
        // old gate demanded humus > 12, which is exactly where a thinned-out
        // pond equilibrates, so the worms could never come back.
        if (wormCount_ == 0 && wormCysts_ > 0 && humus_ > 8.0f) {
            wormCystTimer_ += dt;
            if (wormCystTimer_ >= 90.0f) {
                wormCystTimer_ = 0.0f;
                spawnWorm(rnd(30.0f, 290.0f), 0, makeGenes(1.0f), 0.10f, 42.0f);
                --wormCysts_;
            }
        } else {
            wormCystTimer_ = 0.0f;
        }
    }

    void updateOxygen(float dt, float activity) {
        const float nutrientFactor = 0.55f + 0.45f * (nutrient_ / 100.0f);
        const float photosynthesis = plantO2_ * (0.18f + 0.0060f * light_) * nutrientFactor;
        // The duckweed mat is a small surface producer too.  Its output is
        // capped well below the submerged stand and its night respiration is
        // tiny, so it can nudge oxygen but never swing the whole bottle.
        const float duckweedPhoto = duckweedCover_ * 0.55f
                                  * std::clamp(light_ / 100.0f, 0.0f, 1.0f) * daylight_;
        const float duckweedResp = duckweedCover_ * 0.10f * (1.0f - daylight_);
        const float plantRespiration = plantBiomass_
                                     * (0.045f + 0.040f * (1.0f - light_ / 100.0f))
                                     * (1.0f + 0.25f * night_);
        const float oxygenFactor = std::clamp((oxygen_ - 18.0f) / 55.0f, 0.0f, 1.0f);
        // Animals slide into hypoxic dormancy instead of burning the last of
        // the oxygen.  Metabolism falls away steeply below half saturation, so
        // a warm night can no longer spiral from "a little low" into a dead
        // bottle; the whole community simply breathes less until dawn.
        const float respirationScale = 0.10f + 0.90f * oxygenFactor * oxygenFactor;
        const float shrimpResp = static_cast<float>(shrimpCount_)
                               * (0.09f + 0.08f * activity) * respirationScale;
        const float damselResp = static_cast<float>(damselCount_)
                               * (0.12f + 0.10f * activity) * respirationScale;
        const float fishResp = static_cast<float>(fishCount_)
                             * (0.17f + 0.11f * activity) * respirationScale;
        const float wormResp = static_cast<float>(wormCount_) * 0.030f * respirationScale;
        const float tubifexResp = static_cast<float>(tubifexCount_) * 0.006f
                                * respirationScale;
        // Aerobic decay burns oxygen too.  Without this term the new
        // mineralization would be free energy for the jar; with it, a heavy
        // detritus load slightly depresses oxygen, which then slows the decay
        // itself -- the natural brake.
        const float bacterialResp = 0.045f
            * std::clamp((humus_ - 6.0f) / 34.0f, 0.0f, 1.0f)
            * std::clamp((oxygen_ - 16.0f) / 56.0f, 0.0f, 1.0f);
        const float saturation = 76.0f - 0.65f * (temperature_ - 22.0f);
        const float surfaceExchange = (saturation - oxygen_) * 0.060f;
        const float wetSeason = std::clamp(-seasonWater_ / 9.0f, -1.0f, 1.0f);

        oxygen_ += dt * (photosynthesis + duckweedPhoto - duckweedResp
                         - plantRespiration - shrimpResp
                         - damselResp - fishResp - wormResp - tubifexResp
                         - bacterialResp
                         - 0.12f + surfaceExchange + 0.10f * wetSeason
                         + eventO2Rate_);
        oxygen_ = std::clamp(oxygen_, 0.0f, 100.0f);
    }

    void updateChemistry(float dt) {
        humus_ += dt * (static_cast<float>(shrimpCount_) * 0.006f
                      + static_cast<float>(fishCount_) * 0.010f
                      + static_cast<float>(damselCount_) * 0.007f
                      + eventHumusRate_);
        // Bacterial mineralization.  The worm was the only real decomposer,
        // so whenever the benthos was knocked back the humus simply piled up
        // to the cap: the micro-grid then read organic = 1, the bottom layer
        // went anaerobic and pumped toxin into the silt, which killed the
        // worms -- a closed loop with no way out.  A slow community of
        // aerobic bacteria consumes detritus whether or not a worm is
        // present, faster when the water is well oxygenated and almost not
        // at all in anoxic silt, and hands the nitrogen back to the column.
        // Aerobic decay carries most of the load, but the mud never stops
        // working: anaerobic bacteria keep breaking detritus down (slowly, and
        // at the cost of the reduced compounds the grid already models).  With
        // a purely aerobic term a single bad night shut decay down completely,
        // and the humus then cemented itself at the cap where nothing could
        // reach it.
        const float aerobic = 0.18f
            + 0.82f * std::clamp((oxygen_ - 16.0f) / 56.0f, 0.0f, 1.0f);
        const float humusSubstrate = std::clamp((humus_ - 6.0f) / 34.0f, 0.0f, 1.0f);
        const float mineralized = dt * 0.40f * humusSubstrate * aerobic;
        humus_ = std::max(0.0f, humus_ - mineralized);
        nutrient_ = std::min(100.0f, nutrient_ + mineralized * 0.85f);
        // Nutrient events carry a signed rate: rain and overturn feed the
        // column, drought and algal blooms draw it down.  Apply it once, with
        // the same sign convention the micro-grid uses.
        nutrient_ = std::clamp(nutrient_ - dt * plantBiomass_ * 0.006f
                               + dt * eventNutrientRate_, 0.0f, 100.0f);
        phosphorus_ = std::clamp(phosphorus_
                      - dt * plantBiomass_ * 0.0012f
                      + dt * eventPhosRate_, 4.0f, 100.0f);
        // A shell dissolves very slowly and buffers the phosphate channel.
        for (int i = 0; i < decorCount_; ++i) {
            if (decors_[i].type == 2) phosphorus_ = std::min(100.0f, phosphorus_ + dt * 0.012f);
        }
        // Phosphate is sorbed by the sediment bed and redissolves when the
        // water column is lean (and precipitates when it is rich).  Treating
        // the bed as a buffer -- instead of routing all P through humus --
        // lets the P pool settle near a mid-range equilibrium without
        // competing with the humus that feeds the nitrogen channel.
        float bedMean = 0.0f;
        for (int i = 0; i < kSedimentBins; ++i) bedMean += sediment_[i];
        bedMean /= static_cast<float>(kSedimentBins);
        const float phosphateBuffer = (0.50f * bedMean - phosphorus_) * 0.0040f;
        phosphorus_ = std::clamp(phosphorus_ + dt * phosphateBuffer, 4.0f, 100.0f);
        const float wetSeason = std::clamp(-seasonWater_ / 9.0f, -1.0f, 1.0f);
        if (wetSeason > 0.0f) {
            nutrient_ = std::min(100.0f, nutrient_ + dt * 0.020f * wetSeason);
            for (int i = 0; i < kSedimentBins; ++i) {
                sediment_[i] = std::min(100.0f,
                    sediment_[i] + dt * 0.012f * wetSeason);
            }
        }
        humus_ = std::clamp(humus_, 0.0f, 100.0f);
        nutrient_ = std::clamp(nutrient_, 0.0f, 100.0f);
    }

    // ---- corpses -----------------------------------------------------------
    void addCorpse(int kind, float x, float y, float scale, int stage,
                   const Genes& g, bool flipY) {
        if (corpseCount_ >= kMaxCorpses) {
            humus_ = std::min(100.0f, humus_ + 3.0f);
            for (int i = 0; i < corpseCount_ - 1; ++i) corpses_[i] = corpses_[i + 1];
            --corpseCount_;
        }
        Corpse c;
        c.kind = kind;
        c.x = x;
        c.y = y;
        c.vx = rnd(-3.0f, 3.0f);
        c.vy = 4.0f + scale * 2.0f;
        c.age = 0.0f;
        c.duration = 8.0f;
        c.scale = scale;
        c.stage = stage;
        c.g = g;
        c.flipY = flipY;
        c.lean = rnd(-3.0f, 3.0f);
        corpses_[corpseCount_++] = c;
    }

    void updateCorpses(float dt) {
        for (int i = corpseCount_ - 1; i >= 0; --i) {
            Corpse& c = corpses_[i];
            c.age += dt;
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            if (c.y > 95.0f) { c.y = 95.0f; c.vy = 0.0f; }
            c.x = std::clamp(c.x, 8.0f, 302.0f);
            if (c.age >= c.duration) {
                const float value = (c.kind == 1 ? 7.0f : (c.kind == 0 ? 3.5f
                                  : (c.kind == 2 ? 4.0f
                                  : (c.kind == 4 ? 1.2f : 2.5f))));
                addSediment(c.x, value * 0.95f);
                humus_ = std::min(100.0f, humus_ + value * 0.35f);
                for (int j = i; j < corpseCount_ - 1; ++j) corpses_[j] = corpses_[j + 1];
                --corpseCount_;
            }
        }
    }

    void killShrimp(int index) {
        if (index < 0 || index >= shrimpCount_) return;
        const Shrimp& s = shrimp_[index];
        addCorpse(0, s.x, s.y, speciesBase(0) * stageScale(s.stage) * s.g.size,
                  s.stage, s.g, true);
        removeShrimp(index, false);
    }
    void killFish(int index) {
        if (index < 0 || index >= fishCount_) return;
        const Fish& f = fish_[index];
        addCorpse(1, f.x, f.y, speciesBase(1) * stageScale(f.stage) * f.g.size,
                  f.stage, f.g, true);
        removeFish(index, false);
    }
    void killDamsel(int index) {
        if (index < 0 || index >= damselCount_) return;
        const Damsel& d = damsel_[index];
        damselEggs_ = std::min(6, damselEggs_ + 1);
        damselEggGene_ = d.g;
        addCorpse(2, d.x, d.y, speciesBase(2) * stageScale(d.stage) * d.g.size,
                  d.stage, d.g, false);
        removeDamsel(index, false);
    }
    void killWorm(int index) {
        if (index < 0 || index >= wormCount_) return;
        const Worm& w = worms_[index];
        addCorpse(3, w.x, 92.0f, speciesBase(3) * stageScale(w.stage) * w.g.size,
                  w.stage, w.g, false);
        removeWorm(index, false);
    }
    void killTubifex(int index) {
        if (index < 0 || index >= tubifexCount_) return;
        const Tubifex& t = tubifex_[index];
        addCorpse(4, t.x, t.y, 0.95f, 0, t.g, false);
        removeTubifex(index);
    }

    void removeTubifex(int index) {
        if (index < 0 || index >= tubifexCount_) return;
        for (int i = index; i < tubifexCount_ - 1; ++i) tubifex_[i] = tubifex_[i + 1];
        --tubifexCount_;
    }

    void removeShrimp(int index, bool) {
        if (index < 0 || index >= shrimpCount_) return;
        for (int i = index; i < shrimpCount_ - 1; ++i) shrimp_[i] = shrimp_[i + 1];
        --shrimpCount_;
    }
    void removeDamsel(int index, bool) {
        if (index < 0 || index >= damselCount_) return;
        for (int i = index; i < damselCount_ - 1; ++i) damsel_[i] = damsel_[i + 1];
        --damselCount_;
    }
    void removeFish(int index, bool) {
        if (index < 0 || index >= fishCount_) return;
        for (int i = index; i < fishCount_ - 1; ++i) fish_[i] = fish_[i + 1];
        --fishCount_;
    }
    void removeWorm(int index, bool) {
        if (index < 0 || index >= wormCount_) return;
        for (int i = index; i < wormCount_ - 1; ++i) worms_[i] = worms_[i + 1];
        --wormCount_;
    }

    void spawnShrimp(float x, float y, int stage, const Genes& g, float growth) {
        if (shrimpCount_ >= g_settings.maxShrimp) return;
        Shrimp s;
        s.x = std::clamp(x, 8.0f, 296.0f);
        s.y = std::clamp(y, std::max(16.0f, waterSurfaceY() + 3.0f), 84.0f);
        s.vx = rnd(-8.0f, 8.0f);
        s.vy = rnd(-3.0f, 3.0f);
        s.dirTimer = rnd(2.0f, 7.0f);
        s.energy = (stage == 0 ? 50.0f : 62.0f);
        s.health = 1.0f;
        s.age = (stage == 0) ? 0.0f : rnd(0.0f, 90.0f);
        s.growth = growth;
        s.stage = stage;
        s.g = g;
        s.eatCooldown = rnd(1.0f, 4.0f);
        s.breedCooldown = rnd(28.0f, 45.0f);
        s.id = nextId_++;
        shrimp_[shrimpCount_++] = s;
    }

    void spawnDamsel(float x, float y, int stage, const Genes& g, float growth) {
        if (damselCount_ >= g_settings.maxDamsel) return;
        Damsel d;
        d.x = std::clamp(x, 10.0f, 294.0f);
        d.y = std::clamp(y, std::max(18.0f, waterSurfaceY() + 4.0f), 82.0f);
        d.vx = rnd(-7.0f, 7.0f);
        d.vy = rnd(-2.0f, 2.0f);
        d.dirTimer = rnd(2.0f, 6.0f);
        d.energy = (stage == 0 ? 48.0f : 60.0f);
        d.health = 1.0f;
        d.age = (stage == 0) ? 0.0f : rnd(0.0f, 90.0f);
        d.growth = growth;
        d.stage = stage;
        d.g = g;
        d.huntCooldown = rnd(3.0f, 7.0f);
        d.breedCooldown = rnd(35.0f, 55.0f);
        d.id = nextId_++;
        damsel_[damselCount_++] = d;
    }

    void spawnFish(float x, float y, int stage, const Genes& g, float growth) {
        if (fishCount_ >= g_settings.maxFish) return;
        Fish f;
        f.x = std::clamp(x, 12.0f, 292.0f);
        f.y = std::clamp(y, std::max(20.0f, waterSurfaceY() + 4.0f), 78.0f);
        f.vx = rnd(-8.0f, 8.0f);
        f.vy = rnd(-3.0f, 3.0f);
        f.dirTimer = rnd(1.0f, 4.0f);
        f.energy = (stage == 0 ? 48.0f : 62.0f);
        f.health = 1.0f;
        f.age = (stage == 0) ? 0.0f : rnd(0.0f, 100.0f);
        f.growth = growth;
        f.stage = stage;
        f.g = g;
        f.huntCooldown = rnd(3.0f, 7.0f);
        f.breedCooldown = rnd(35.0f, 55.0f);
        f.id = nextId_++;
        fish_[fishCount_++] = f;
    }

    void spawnWorm(float x, int stage, const Genes& g, float growth, float energy) {
        if (wormCount_ >= g_settings.maxWorms) return;
        Worm w;
        w.x = std::clamp(x, 8.0f, 302.0f);
        w.phase = rnd(0.0f, kTwoPi);
        w.vx = rnd(-0.7f, 0.7f);
        w.dirTimer = rnd(4.0f, 11.0f);
        w.energy = (stage == 0 ? 48.0f : (energy > 0.0f ? energy : 58.0f));
        w.health = 1.0f;
        w.age = (stage == 0) ? 0.0f : rnd(0.0f, 100.0f);
        w.growth = growth;
        w.stage = stage;
        w.g = g;
        w.breedCooldown = rnd(30.0f, 50.0f);
        w.id = nextId_++;
        worms_[wormCount_++] = w;
    }

    void spawnTubifex(float x, const Genes& g) {
        if (tubifexCount_ >= g_settings.maxTubifex) return;
        Tubifex t;
        t.x = std::clamp(x, 8.0f, 302.0f);
        // Give every worm a little personal space.  Without it a cluster
        // collapses into one solid lump of overlapping sprites.
        for (int attempt = 0; attempt < 6; ++attempt) {
            bool crowded = false;
            for (int i = 0; i < tubifexCount_; ++i) {
                if (std::fabs(tubifex_[i].x - t.x) < 5.5f) { crowded = true; break; }
            }
            if (!crowded) break;
            t.x = std::clamp(t.x + rnd(-9.0f, 9.0f), 8.0f, 302.0f);
        }
        t.y = 95.0f;
        t.phase = rnd(0.0f, kTwoPi);
        t.energy = rnd(38.0f, 52.0f);
        t.health = 1.0f;
        t.age = rnd(0.0f, 65.0f);
        t.growth = rnd(0.42f, 0.68f);
        t.stage = stageFromGrowth(t.growth);
        t.breedCooldown = rnd(40.0f, 70.0f);
        t.g = g;
        t.id = nextId_++;
        tubifex_[tubifexCount_++] = t;
    }

    float light_ = 50.0f;
    float oxygen_ = 72.0f;
    float humus_ = 32.0f;
    float nutrient_ = 56.0f;
    float night_ = 0.0f;
    float volume_ = 0.5f;
    float agitation_ = 0.65f;
    float batteryScale_ = 1.0f;
    float nutrientBonus_ = 1.12f;
    float plantO2_ = 0.0f;
    float plantBiomass_ = 0.0f;
    bool resting_ = false;
    float dayPhase_ = 0.0f;
    float seasonPhase_ = 0.0f;
    float daylight_ = 1.0f;
    float seasonWater_ = 0.0f;
    float temperature_ = 22.0f;
    float q10_ = 1.0f;
    int gridFrame_ = 0;
    std::array<MicroCell, kGridN> grid_{};
    std::array<MicroCell, kGridN> gridScratch_{};
    float sediment_[kSedimentBins]{};

    int plantCount_ = 0;
    int shrimpCount_ = 0;
    int damselCount_ = 0;
    int fishCount_ = 0;
    int wormCount_ = 0;
    int tubifexCount_ = 0;
    int corpseCount_ = 0;
    int fishEggs_ = 1;
    float fishEggTimer_ = 0.0f;
    int damselEggs_ = 1;
    float damselEggTimer_ = 0.0f;
    int shrimpEggs_ = 2;
    float shrimpEggTimer_ = 0.0f;
    Genes fishEggGene_{};
    Genes damselEggGene_{};
    Genes shrimpEggGene_{};
    float fishEggBankTimer_ = 0.0f;
    float damselEggBankTimer_ = 0.0f;
    float shrimpEggBankTimer_ = 0.0f;
    int tubifexEggs_ = 4;
    float tubifexEggTimer_ = 0.0f;
    int wormCysts_ = 2;
    float wormCystTimer_ = 0.0f;
    int plantSeeds_ = 3;
    float plantSeedTimer_ = 0.0f;
    std::array<int, 3> plantTypeSeeds_{};
    std::array<float, 3> plantTypeSeedTimer_{};
    std::array<float, 3> plantTypeSeedX_{};
    std::array<Plant, kMaxPlants> plants_{};
    std::array<Shrub, kMaxShrubs> shrubs_{};
    int shrubCount_ = 0;
    float shrubRescue_ = 0.0f;
    std::array<Raft, kMaxRafts> rafts_{};
    int raftCount_ = 0;
    float raftRescue_ = 0.0f;
    float duckweedCover_ = 0.0f;
    // Shore bank forcing.  The bank only ever hands water, humus and a little
    // nutrient down to the pond; nothing in the pond flows back up, so the
    // shrub layer stays independent.
    float bankMoisture_ = 0.35f;
    float shoreFoliage_ = 0.0f;
    float bankWash_ = 0.0f;
    float insectTimer_ = 30.0f;
    std::array<Shrimp, kMaxShrimp> shrimp_{};
    std::array<Damsel, kMaxDamsel> damsel_{};
    std::array<Fish, kMaxFish> fish_{};
    std::array<Worm, kMaxWorms> worms_{};
    std::array<Tubifex, kMaxTubifex> tubifex_{};
    std::array<Corpse, kMaxCorpses> corpses_{};
    std::array<Egg, kMaxEggs> eggs_{};
    int eggCount_ = 0;
    std::array<StartleField, 4> startleFields_{};
    int startleCount_ = 0;
    std::array<FoodPulse, 3> foodPulses_{};
    int foodCount_ = 0;
    std::array<LightPatch, 3> lightPatches_{};
    int lightCount_ = 0;
    float feedCooldown_ = 0.0f;
    float refugeTimer_[6] = {0, 0, 0, 0, 0, 0};
    std::array<float, 3> plantTypeRefugeTimer_{};
    float phosphorus_ = 46.0f;      // second nutrient channel (N/P Liebig limit)
    std::array<EcoEvent, kMaxEvents> events_{};
    int eventCount_ = 0;
    float eventCooldown_ = 12.0f;
    float eventFoodTimer_ = 0.0f;
    std::array<EventLogEntry, kEventLogSize> eventLog_{};
    int eventLogCount_ = 0;
    int eventLogHead_ = 0;
    // Derived per-frame event forcing, recomputed in updateEvents().
    float eventWater_ = 0.0f;
    float eventTemp_ = 0.0f;
    float eventLightMul_ = 1.0f;
    float eventO2Rate_ = 0.0f;
    float eventNutrientRate_ = 0.0f;
    float eventPhosRate_ = 0.0f;
    float eventHumusRate_ = 0.0f;
    float eventTanninRate_ = 0.0f;
    float eventToxinRate_ = 0.0f;
    float eventBloom_ = 0.0f;
    std::array<Decor, kMaxDecors> decors_{};
    int decorCount_ = 0;
    uint32_t nextId_ = 1;
    uint32_t selectedId_ = 0;
    int selectedKind_ = -1;

    float animTime_ = 0.0f;
    float simTime_ = 0.0f;
    float debugTimer_ = 0.0f;
    std::mt19937 rng_{std::random_device{}()};
};

// Atomic save: write a sibling .tmp file, then rename over the previous state.
// A power cut can therefore only lose the last few seconds, never leave a
// half-written save that would corrupt the next launch.
bool saveEcosystemState(const Ecosystem& ecosystem) {
    const std::wstring path = stateFilePath();
    const std::wstring temp = path + L".tmp";
    std::FILE* file = _wfopen(temp.c_str(), L"wb");
    if (!file) return false;
    const bool written = ecosystem.saveState(file);
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        _wremove(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        _wremove(temp.c_str());
        return false;
    }
    return true;
}

bool loadEcosystemState(Ecosystem& ecosystem) {
    std::FILE* file = _wfopen(stateFilePath().c_str(), L"rb");
    if (!file) return false;
    const bool loaded = ecosystem.loadState(file);
    std::fclose(file);
    return loaded;
}

// =============================================================================
//  4. Bottle renderer
// =============================================================================
class BottleRenderer {
public:
    explicit BottleRenderer(SDL_Renderer* renderer) : r_(renderer) {
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
        bakeBackground();
        bakeEdgeFrames();
    }

    ~BottleRenderer() { release(); }

    // Textures must be destroyed while the SDL renderer still exists, so the
    // owner calls this before SDL_DestroyRenderer().
    void release() {
        if (bg_) {
            SDL_DestroyTexture(bg_);
            bg_ = nullptr;
        }
        if (scene_) {
            SDL_DestroyTexture(scene_);
            scene_ = nullptr;
        }
        for (SDL_Texture*& texture : edgeFrames_) {
            if (texture) {
                SDL_DestroyTexture(texture);
                texture = nullptr;
            }
        }
    }

    BottleRenderer(const BottleRenderer&) = delete;
    BottleRenderer& operator=(const BottleRenderer&) = delete;

    // Cursor feedback for the right-click tools (visual only): a hover bracket
    // in profile mode and a ghost footprint where scenery would land.
    void setToolPreview(int mode, int decorKind, int x, int y) {
        previewMode_ = mode;
        previewKind_ = decorKind;
        previewX_ = x;
        previewY_ = y;
    }

    void draw(const Ecosystem& eco, bool paused) {
        SDL_SetRenderDrawColor(r_, 0, 0, 0, art::kEmptyAlpha);
        SDL_RenderClear(r_);

        if (scene_) {
            SDL_RenderCopy(r_, scene_, nullptr, nullptr);
        } else if (bg_) {
            SDL_RenderCopy(r_, bg_, nullptr, nullptr);
        } else {
            SDL_SetRenderDrawColor(r_, art::kWaterDark.r, art::kWaterDark.g, art::kWaterDark.b, 255);
            const SDL_Rect inner{5, 5, 310, 110};
            SDL_RenderFillRect(r_, &inner);
        }

        surfaceTop_ = std::clamp(
            static_cast<int>(std::lround(eco.waterSurfaceY())), 26, 44);
        drawSky(eco);
        drawFarBank(eco);
        drawAmbient(eco);
        drawSeasonWash(eco);
        drawMicroTint(eco);
        drawLightPatches(eco);
        drawSunbeams(eco);
        drawCausticNet(eco);
        drawGroundCaustics(eco);
        drawBubbles(eco);
        drawMotes(eco);
        drawFloatingLeaf(eco);
        drawWeather(eco);
        drawWaterSurface(eco);
        drawRefraction(eco);
        drawGroundShadows(eco);
        drawDecor(eco);
        drawShrubs(eco);
        drawPlants(eco);
        // Floating rafts sit on the surface in front of the submerged stems,
        // so they must be painted after the plants rather than before them.
        drawSurfacePlants(eco);
        drawEggs(eco);
        drawCorpses(eco);
        drawWorms(eco);
        drawTubifex(eco);
        drawShrimp(eco);
        drawDamsel(eco);
        drawFish(eco);
        drawEpiphyte(eco);
        drawFoodAndStartle(eco);
        drawSelection(eco);
        drawToolPreview(eco);
        drawWaterLevel(eco);
        drawSeasonGlyph(eco);
        drawGlassFrame(eco);
        drawEdgePlants(eco);
        if (paused) drawPauseMark();
    }

private:
    static constexpr int kL = 5;
    static constexpr int kR = 314;
    static constexpr int kT = 5;
    static constexpr int kB = 114;
    static constexpr int kEdgeFrameCount = 40;
    static constexpr float kEdgeFrameStep = 0.12f;

    static bool inRoundedRect(int x, int y, int l, int t, int r, int b, int radius) {
        if (x < l || x > r || y < t || y > b) return false;
        const int cx = std::clamp(x, l + radius, r - radius);
        const int cy = std::clamp(y, t + radius, b - radius);
        const int dx = x - cx;
        const int dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    }

    static bool inTank(int x, int y) {
        return inRoundedRect(x, y, 1, 1, 318, 118, 10);
    }

    static bool inInner(int x, int y) {
        return inRoundedRect(x, y, 5, 5, 314, 114, 5);
    }

    // Rolling near shore.  The bed stays low in the frame, but its silhouette
    // keeps a clear wave so the shoreline reads as natural ground rather than
    // a ruled line.
    static int groundY(int x) {
        x = std::clamp(x, kL, kR);
        const float fx = static_cast<float>(x);
        float h = 104.0f
                + 1.6f * std::sin(fx * 0.043f + 0.7f)
                + 0.8f * std::sin(fx * 0.117f + 2.3f)
                + 0.35f * std::sin(fx * 0.240f + 0.4f);
        const float edge = std::min(fx - static_cast<float>(kL),
                                    static_cast<float>(kR) - fx) / 40.0f;
        if (edge < 1.0f) h += (1.0f - edge) * 1.5f;
        return std::clamp(static_cast<int>(std::lround(h)), 100, 108);
    }

    // Upper shore: a permanent far bank whose silhouette undulates against
    // the sky.  The water surface is kept below its lowest point, so the water
    // never becomes level with the land.
    static int bankTopY(int x) {
        x = std::clamp(x, kL, kR);
        const float fx = static_cast<float>(x);
        float h = 18.0f
                + 2.4f * std::sin(fx * 0.029f + 0.6f)
                + 1.1f * std::sin(fx * 0.091f + 2.1f)
                + 0.4f * std::sin(fx * 0.230f + 0.9f);
        const float edge = std::min(fx - static_cast<float>(kL),
                                    static_cast<float>(kR) - fx) / 34.0f;
        if (edge < 1.0f) h += (1.0f - edge) * 1.4f;
        return std::clamp(static_cast<int>(std::lround(h)), 13, 22);
    }

    // Shrub roots swell the soil very slightly, so the crest rises one or two
    // pixels around an established plant.  Deliberately tiny: the shore keeps
    // its gentle roll instead of turning into a row of hills.
    int bankTopAt(const Ecosystem& eco, int x) const {
        int top = bankTopY(x);
        for (int i = 0; i < eco.shrubCount(); ++i) {
            const auto& sh = eco.shrubAt(i);
            const int dx = std::abs(x - static_cast<int>(std::lround(sh.x)));
            if (dx > 8) continue;
            const float fall = 1.0f - static_cast<float>(dx) / 8.0f;
            top -= static_cast<int>(std::lround(fall * (0.3f + 1.5f * sh.growth)));
        }
        return std::clamp(top, 13, 22);
    }

    static SDL_Color blend(SDL_Color a, SDL_Color b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return SDL_Color{
            static_cast<Uint8>(std::lround(a.r + (b.r - a.r) * t)),
            static_cast<Uint8>(std::lround(a.g + (b.g - a.g) * t)),
            static_cast<Uint8>(std::lround(a.b + (b.b - a.b) * t)),
            255
        };
    }

    static Uint32 pack(SDL_Color c, Uint8 a = 255) {
        return (static_cast<Uint32>(a) << 24)
             | (static_cast<Uint32>(c.r) << 16)
             | (static_cast<Uint32>(c.g) << 8)
             | static_cast<Uint32>(c.b);
    }

    static int dither(int x, int y) {
        static constexpr int kBayes[4][4] = {
            { 0,  8,  2, 10},
            {12,  4, 14,  6},
            { 3, 11,  1,  9},
            {15,  7, 13,  5}
        };
        return kBayes[y & 3][x & 3];
    }

    float visualLight(const Ecosystem& eco) const {
        const float base = std::clamp(eco.light() / 100.0f, 0.0f, 1.0f);
        const float t = eco.animTime();
        const float breathing = 0.90f + 0.07f * std::sin(t * 0.11f)
                              + 0.035f * std::sin(t * 0.37f)
                              + 0.020f * std::sin(t * 0.73f);
        return std::clamp(base * breathing, 0.0f, 1.0f);
    }

    // ---------------------------------------------------------------------
    //  One-time background: water depth, uneven mud, plants and minerals.
    // ---------------------------------------------------------------------
    void bakeBackground() {
        const int w = art::kScreenW;
        const int h = art::kScreenH;
        std::vector<Uint32> pixels(static_cast<std::size_t>(w) * h,
                                   pack(SDL_Color{0, 0, 0, 255}, art::kEmptyAlpha));

        auto putRaw = [&](int x, int y, SDL_Color c) {
            if (x < 0 || x >= w || y < 0 || y >= h) return;
            pixels[static_cast<std::size_t>(y) * w + x] = pack(c);
        };
        auto put = [&](int x, int y, SDL_Color c) {
            if (!inInner(x, y)) return;
            putRaw(x, y, c);
        };

        // Rounded 3D glass tank.  The tank itself is baked here; foreground
        // plants later grow over the rim so the silhouette is not a hard box.
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (!inTank(x, y) || inInner(x, y)) continue;
                const bool outerEdge = !inTank(x - 1, y) || !inTank(x + 1, y)
                                     || !inTank(x, y - 1) || !inTank(x, y + 1);
                const bool innerEdge = inInner(x - 1, y) || inInner(x + 1, y)
                                     || inInner(x, y - 1) || inInner(x, y + 1);

                SDL_Color c = art::kWaterLight;
                if (outerEdge) {
                    c = art::kDeep;
                } else if (innerEdge) {
                    if (y < art::kWaterTop || x < 12) {
                        c = ((x * 3 + y * 5) % 13 < 5) ? art::kCream : art::kWaterLight;
                    } else if (y > 108 || x > 306) {
                        c = art::kMudDark;
                    } else {
                        c = art::kTeal;
                    }
                } else {
                    if (y < 8) c = art::kTeal;
                    else if (y > 112) c = art::kMudDark;
                    else c = art::kWaterLight;
                }

                // Top/left bevel catches the room light, bottom/right recedes.
                if (y <= 3 && x < 312) c = blend(art::kTeal, art::kCream, 0.35f);
                if (x <= 3 && y < 112) c = blend(art::kTeal, art::kCream, 0.25f);
                if (y >= 115) {
                    if (y >= 118) c = art::kDeep;
                    else if (y == 117) c = art::kMudDark;
                    else if (y == 116) c = art::kMud;
                    else c = art::kMudLight;
                    if (x < 8 || x > 311) c = art::kMudDark;
                }
                putRaw(x, y, c);
            }
        }

        // Water: a cool teal column, darker at the edges and near the bottom.
        for (int y = kT; y <= kB; ++y) {
            for (int x = kL; x <= kR; ++x) {
                SDL_Color c;
                if (y < art::kWaterTop) {
                    c = blend(art::kDeep, art::kWaterDark, 0.45f);
                    const std::uint32_t hh = art::hash2(x, y);
                    if (hh % 43u == 0u) c = art::kWaterLight;
                    if (hh % 601u == 0u) c = art::kCream;
                } else {
                    const float depth = static_cast<float>(y - art::kWaterTop)
                                      / static_cast<float>(105 - art::kWaterTop);
                    c = (depth < 0.45f)
                        ? blend(art::kWaterLight, art::kWaterMid, depth / 0.45f)
                        : blend(art::kWaterMid, art::kWaterDark, (depth - 0.45f) / 0.55f);

                    const float edge = std::min(static_cast<float>(x - kL),
                                                static_cast<float>(kR - x)) / 24.0f;
                    c = blend(art::kWaterDark, c, 0.35f + 0.65f * std::min(1.0f, edge));

                    const std::uint32_t hh = art::hash2(x, y + 17);
                    if (hh % 2301u == 0u) c = art::kTeal;
                    if (hh % 4201u == 0u) c = art::kCream;
                }
                put(x, y, c);
            }
        }

        // Uneven mud bank, with sediment layers and a mossy edge.
        for (int x = kL; x <= kR; ++x) {
            const int gy = groundY(x);
            for (int y = gy; y <= kB; ++y) {
                const float d = static_cast<float>(y - gy)
                              / std::max(1.0f, static_cast<float>(kB - gy));
                SDL_Color c = blend(art::kMud, art::kMudDark, d * 0.85f);
                if (y <= gy + 1) c = art::kMudDark;
                if (y == 106 || y == 110) c = blend(c, art::kMudDark, 0.55f);

                const std::uint32_t hh = art::hash2(x + 31, y * 3 + 11);
                if (hh % 211u == 0u) c = art::kMudLight;
                if (hh % 809u == 0u) c = art::kCream;
                if (hh % 131u == 0u) c = art::kMossDark;
                put(x, y, c);
            }

            // Mossy, broken shoreline.
            if ((art::hash2(x / 4, 3) % 9u) < 5u) {
                put(x, gy - 1, art::kMossDark);
                if ((art::hash2(x / 2, 5) % 7u) < 2u) put(x, gy - 2, art::kMoss);
            }
            if ((art::hash2(x / 3, 9) % 13u) == 0u) {
                put(x, gy - 1, art::kLeaf);
            }
        }

        // A pair of layered rocks with top light and deep shadows.
        auto rock = [&](int cx, int base, int rw, int rh) {
            const int left = cx - rw / 2;
            for (int dy = 0; dy < rh; ++dy) {
                int inset = 0;
                if (dy == 0) inset = 3;
                else if (dy == 1) inset = 1;
                else if (dy == rh - 1) inset = 2;
                else if (dy % 2 == 0) inset = 1;
                for (int dx = inset; dx < rw - inset; ++dx) {
                    SDL_Color c = art::kMud;
                    if (dy == 0) c = art::kMudLight;
                    else if (dy == 1 && dx < rw / 2) c = blend(art::kMudLight, art::kMud, 0.45f);
                    else if (dy >= rh - 2) c = art::kMudDark;
                    put(left + dx, base - rh + dy, c);
                }
            }
            // damp shadow
            for (int dx = -rw / 2; dx <= rw / 2; ++dx) {
                put(cx + dx, base + 1, art::kMudDark);
            }
            // moss and a mineral glint
            for (int k = 0; k < 5; ++k) {
                const int mx = cx - rw / 2 + 3 + k * 3;
                put(mx, base - rh + (k & 1), k < 2 ? art::kMoss : art::kMossDark);
            }
            put(cx - 3, base - rh + 1, art::kAmber);
            put(cx + 4, base - 1, art::kMudLight);
        };

        const int leftRockBase = groundY(45) + 1;
        const int rightRockBase = groundY(268) + 1;
        rock(45, leftRockBase, 25, 9);
        rock(268, rightRockBase, 18, 7);

        // Driftwood, rising diagonally from the left bank.
        for (int k = 0; k <= 38; ++k) {
            const float u = static_cast<float>(k) / 38.0f;
            const int bx = 8 + static_cast<int>(std::lround(u * 40.0f));
            const int by = 99 - static_cast<int>(std::lround(std::sin(u * 2.7f) * 15.0f));
            put(bx, by, art::kMudDark);
            put(bx, by - 1, art::kMud);
            put(bx + 1, by, art::kMudDark);
            if (k == 12 || k == 25) {
                for (int q = 1; q <= 4; ++q) put(bx + q, by - 2 - q / 2, art::kMud);
            }
            if (k % 9 == 0) put(bx, by - 2, art::kAmber);
        }

        // Tall background reeds: dark silhouettes for depth.
        auto reed = [&](int baseX, int height, float lean, SDL_Color stem, SDL_Color leaf) {
            const int base = groundY(baseX);
            int x = baseX;
            for (int k = 0; k <= height; ++k) {
                x = baseX + static_cast<int>(std::lround(lean * k + std::sin(k * 0.25f) * 1.5f));
                const int y = base - k;
                put(x, y, stem);
                if (k % 4 == 2) {
                    put(x - 1, y, leaf);
                    put(x - 2, y - 1, leaf);
                }
                if (k % 5 == 0) {
                    put(x + 1, y, leaf);
                }
            }
            put(x, base - height, art::kLeaf);
        };

        reed(16, 30, 0.18f, art::kMossDark, art::kMossDark);
        reed(25, 22, -0.10f, art::kMossDark, art::kMoss);
        reed(304, 31, -0.20f, art::kMossDark, art::kMossDark);
        reed(295, 21, 0.12f, art::kMossDark, art::kMoss);

        // Arching ferns in the midground.
        auto fern = [&](int baseX, int height, SDL_Color stem, SDL_Color leaf) {
            const int base = groundY(baseX);
            for (int k = 0; k <= height; ++k) {
                const int x = baseX + static_cast<int>(std::lround(std::sin(k * 0.25f) * 3.0f));
                const int y = base - k;
                put(x, y, stem);
                if (k % 3 == 0) {
                    put(x - 1, y, leaf);
                    put(x - 3, y - 1, leaf);
                    put(x + 1, y, leaf);
                    put(x + 3, y - 1, leaf);
                }
            }
        };
        fern(68, 18, art::kMossDark, art::kMoss);
        fern(246, 16, art::kMossDark, art::kMoss);
        fern(148, 13, art::kMossDark, art::kMoss);

        // Taller reeds behind the main tufts, plus broad-leaf plants for variety.
        reed(112, 24, -0.05f, art::kMossDark, art::kMoss);
        reed(228, 21, 0.06f, art::kMossDark, art::kMoss);

        auto broadleaf = [&](int baseX, int height) {
            const int base = groundY(baseX);
            for (int k = 0; k <= height; ++k) {
                const int x = baseX + static_cast<int>(std::lround(std::sin(k * 0.42f) * 2.0f));
                put(x, base - k, art::kMossDark);
                if (k == 3 || k == 6 || k == height - 1) {
                    put(x - 1, base - k, art::kMossDark);
                    put(x - 2, base - k, art::kMoss);
                    put(x - 3, base - k - 1, art::kLeaf);
                    put(x + 1, base - k, art::kMossDark);
                    put(x + 2, base - k, art::kMoss);
                    put(x + 3, base - k - 1, art::kLeaf);
                }
            }
        };
        broadleaf(88, 9);
        broadleaf(224, 8);

        // The low swamp scrub on the bank is not baked here any more: it is
        // a live shore population with its own seasonal cycle, drawn each
        // frame by drawShrubs().

        // Moss balls and a little empty shell on the mud.
        auto mossBall = [&](int cx, int base, int radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dy * dy > radius * radius) continue;
                    SDL_Color c = (dy > 0) ? art::kMossDark : art::kMoss;
                    if (dx < 0 && dy < 0) c = art::kLeaf;
                    put(cx + dx, base + dy, c);
                }
            }
            put(cx - 1, base - radius + 1, art::kLeaf);
        };
        mossBall(104, groundY(104) - 2, 3);
        mossBall(184, groundY(184) - 2, 4);
        mossBall(306, groundY(306) - 2, 3);

        // Shell: warm cream and rust, a quiet focal detail.
        const int shellY = groundY(222) - 3;
        put(221, shellY, art::kCream);
        put(222, shellY, art::kCream);
        put(220, shellY + 1, art::kCream);
        put(221, shellY + 1, art::kRust);
        put(222, shellY + 1, art::kAmber);
        put(223, shellY + 1, art::kCream);
        put(221, shellY + 2, art::kCream);
        put(222, shellY + 2, art::kCream);

        // Fallen leaves and small pebbles.
        auto leaf = [&](int x, int y, SDL_Color a, SDL_Color b) {
            put(x, y, a);
            put(x + 1, y, b);
            put(x + 2, y, a);
            put(x + 1, y + 1, b);
            put(x + 3, y + 1, a);
        };
        leaf(146, groundY(146) - 1, art::kRust, art::kAmber);
        leaf(248, groundY(248) - 1, art::kAmber, art::kRust);

        auto pebble = [&](int x, int y, int pw) {
            for (int k = 0; k < pw; ++k) put(x + k, y, art::kMudLight);
            put(x, y + 1, art::kMud);
            put(x + pw - 1, y + 1, art::kMudDark);
            if (pw > 3) put(x + 1, y, art::kCream);
        };
        pebble(36, groundY(36) + 2, 4);
        pebble(78, groundY(78) + 3, 3);
        pebble(132, groundY(132) + 2, 5);
        pebble(196, groundY(196) + 4, 3);
        pebble(232, groundY(232) + 2, 4);
        pebble(292, groundY(292) + 3, 3);
        pebble(160, 113, 5);
        pebble(90, 114, 3);
        pebble(120, 114, 4);
        pebble(280, 113, 4);
        pebble(58, 114, 3);
        rock(202, 115, 11, 4);

        // A half-buried twig at the front edge.
        for (int k = 0; k < 12; ++k) {
            put(100 + k, 113 + ((k % 4 == 0) ? -1 : 0),
                (k % 5 == 0) ? art::kMudLight : art::kMudDark);
        }
        leaf(168, 112, art::kRust, art::kAmber);

        // Warm silt patches catch the light and break up the dark mud.
        auto sand = [&](int x0, int x1) {
            for (int x = x0; x <= x1; ++x) {
                const int gy = groundY(x);
                for (int dy = 1; dy <= 4; ++dy) {
                    if (dither(x, gy + dy) < 6) {
                        put(x, gy + dy, (dither(x + 3, gy + dy) < 2)
                                        ? art::kAmber : art::kMudLight);
                    }
                }
            }
        };
        sand(82, 142);
        sand(228, 278);

        // Roots threading into the silt.
        for (int i = 0; i < 5; ++i) {
            const int rx = 32 + i * 62;
            const int len = 6 + (i * 3) % 5;
            for (int k = 0; k < len; ++k) {
                const int x = rx + static_cast<int>(std::lround(std::sin(k * 0.7f + i) * 2.0f));
                const int y = groundY(rx) + 2 + k;
                put(x, y, (k % 3 == 0) ? art::kMudLight : art::kMudDark);
            }
        }

        // Duckweed is not baked in here: the surface moves with the seasons,
        // so the rafts are drawn live at the current waterline.

        bg_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STATIC, w, h);
        if (bg_) {
            if (SDL_UpdateTexture(bg_, nullptr, pixels.data(),
                                  w * static_cast<int>(sizeof(Uint32))) != 0) {
                SDL_DestroyTexture(bg_);
                bg_ = nullptr;
            } else {
                SDL_SetTextureBlendMode(bg_, SDL_BLENDMODE_NONE);
                bgPixels_ = pixels;
            }
        }
    }

    void point(int x, int y, char ink) {
        if (x < kL || x > kR || y < kT || y > kB) return;
        art::setInk(r_, ink);
        SDL_RenderDrawPoint(r_, x, y);
    }

    // Water-column tints have to follow the uneven bank.  A plain rectangle
    // ends on a fixed row, which cut a hard horizontal line across the mud;
    // filling one column at a time keeps the tint inside the water body and
    // stops it from washing over the exposed shore above the surface.
    void fillWaterTint(const SDL_Color& c, int alpha) {
        if (alpha <= 0) return;
        const int top = std::max(art::kWaterTop, surfaceTop_);
        SDL_SetRenderDrawColor(r_, c.r, c.g, c.b,
                               static_cast<Uint8>(std::clamp(alpha, 0, 255)));
        for (int x = kL; x <= kR; ++x) {
            const int gy = groundY(x);
            const int hgt = gy - top;
            if (hgt <= 0) continue;
            const SDL_Rect col{x, top, 1, hgt};
            SDL_RenderFillRect(r_, &col);
        }
    }

    void px(int x, int y, const SDL_Color& c) {
        if (!inInner(x, y)) return;
        SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, 255);
        SDL_RenderDrawPoint(r_, x, y);
    }

    // Permanent far shore across the top of the water column.  Its silhouette
    // rolls with the terrain and stays above the surface at every water level,
    // so the water is never level with the land.  Drawn before the shrubs and
    // the water, which keeps sky and weather behind the shore vegetation.
    void drawFarBank(const Ecosystem& eco) {
        const int surface = std::clamp(
            static_cast<int>(std::lround(eco.waterSurfaceY())), 26, 44);
        for (int x = kL; x <= kR; ++x) {
            const int top = std::min(bankTopAt(eco, x), surface - 4);
            for (int y = top; y < surface; ++y) {
                const float d = static_cast<float>(y - top)
                              / std::max(1.0f, static_cast<float>(surface - top));
                // Dry, pale clay instead of the dark wet silt used on the bed.
                SDL_Color c = blend(art::kClay, art::kClayDark, 0.12f + 0.64f * d);
                if (y == top) {
                    c = (art::hash2(x, 3) % 3u == 0u)
                        ? art::kClayLight
                        : blend(art::kClay, art::kClayLight, 0.55f);
                }
                // Horizontal sediment strata read as packed, dry earth.
                if (y > top && (y - top) % 5 == 2) {
                    c = blend(c, art::kClayDark, 0.32f);
                }
                const std::uint32_t hh = art::hash2(x + 13, y * 3 + 5);
                if (hh % 131u == 0u) c = art::kClayLight;
                if (hh % 307u == 0u) c = art::kClayDark;
                if (hh % 701u == 0u) c = art::kMud;
                if (hh % 89u == 0u) c = art::kCream;
                // The foot of the bank is soaked and turns dark silt again.
                if (y >= surface - 3) c = blend(c, art::kMud, 0.60f);
                px(x, y, c);
            }
            // Dry stubble and a little gravel on the crown.
            const std::uint32_t ch = art::hash2(x / 3, 7);
            if (ch % 5u < 2u) px(x, top - 1, art::kClayLight);
            if (ch % 29u == 0u) {
                px(x, top - 1, art::kMoss);
                px(x, top - 2, art::kMoss);
            }
            if (art::hash2(x / 4, 13) % 53u == 0u) px(x, top - 1, art::kLeaf);
            if (art::hash2(x, 17) % 137u == 0u) {
                px(x, top + 2, art::kClayLight);
                px(x + 1, top + 3, art::kClayDark);
            }
            // A few dry cracks run down toward the waterline.
            if ((art::hash2(x / 2, 19) % 23u) < 1u) {
                const int len = 2 + static_cast<int>(art::hash2(x, 23) % 4u);
                for (int k = 1; k <= len; ++k) {
                    const int yy = surface - 1 - k;
                    if (yy <= top) break;
                    px(x, yy, art::kClayDark);
                }
            }
        }
    }

    // Air band above the water.  The bottle now reads the time of day: a
    // cool indigo night, a warm dawn/dusk horizon, a pale midday, plus a
    // seasonal tint (spring green -> summer gold -> autumn rust -> winter
    // blue).  Stars fade in only when the sun is well below the horizon.
    void drawSky(const Ecosystem& eco) {
        const float t = eco.animTime();
        const float sun = std::sin(eco.dayPhase());
        const float dayK = std::clamp((sun + 0.30f) / 0.85f, 0.0f, 1.0f);
        const float horizonGlow = std::clamp(1.0f - std::fabs(sun) * 1.6f, 0.0f, 1.0f);

        float rain = 0.0f;
        for (int i = 0; i < eco.eventCount(); ++i) {
            const auto& e = eco.events()[i];
            if (e.type != 0) continue;
            const float env = std::clamp(
                std::min(e.age / 2.5f, (e.duration - e.age) / 3.0f), 0.0f, 1.0f);
            rain = std::max(rain, env * e.intensity);
        }

        SDL_Color seasonal = art::kMoss;
        float seasonAmt = 0.0f;
        switch (seasonId(eco)) {
        case 0: seasonal = art::kMoss;  seasonAmt = 0.16f; break;
        case 1: seasonal = art::kAmber; seasonAmt = 0.18f; break;
        case 2: seasonal = art::kRust;  seasonAmt = 0.22f; break;
        default: seasonal = SDL_Color{128, 168, 196, 255}; seasonAmt = 0.20f; break;
        }

        const SDL_Color nightTop{12, 16, 34, 255};
        const SDL_Color nightLow{32, 44, 74, 255};
        const SDL_Color dayTop{92, 146, 176, 255};
        const SDL_Color dayLow{158, 198, 196, 255};
        const SDL_Color dusk{206, 118, 84, 255};
        const SDL_Color storm{74, 84, 98, 255};

        SDL_Color top = blend(blend(nightTop, dayTop, dayK), seasonal, seasonAmt);
        SDL_Color low = blend(blend(nightLow, dayLow, dayK), seasonal, seasonAmt * 0.65f);
        top = blend(top, dusk, horizonGlow * 0.18f);
        low = blend(low, dusk, horizonGlow * 0.55f);
        // Rain rolls in like a grey lid: the sky cools and darkens smoothly.
        top = blend(top, storm, rain * 0.50f);
        low = blend(low, storm, rain * 0.38f);

        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        for (int y = kT; y < art::kWaterTop; ++y) {
            const float u = static_cast<float>(y - kT)
                          / static_cast<float>(art::kWaterTop - kT);
            const SDL_Color c = blend(top, low, std::pow(u, 0.85f));
            // Follow the rounded top corners of the glass.
            int x0 = kL, x1 = kR;
            if (y < kT + 5) {
                const int dy = (kT + 5) - y;
                const int cut = 5 - static_cast<int>(std::lround(
                    std::sqrt(std::max(0, 25 - dy * dy))));
                x0 += cut;
                x1 -= cut;
            }
            SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, 255);
            SDL_RenderDrawLine(r_, x0, y, x1, y);
        }

        // Slow drifting cloud masses.  Each one is a stack of soft rows, so it
        // reads as a volume rather than a ruled line; rain thickens the deck
        // and hurries it along.  The whole deck sits behind the shore and the
        // shrubs, which are drawn later.
        const SDL_Color cloudLit = blend(SDL_Color{240, 244, 238, 255},
                                         SDL_Color{104, 118, 142, 255}, 1.0f - dayK);
        const SDL_Color cloudShade = blend(SDL_Color{176, 190, 190, 255},
                                           SDL_Color{48, 58, 80, 255}, 1.0f - dayK);
        const int puffs = 3 + (rain > 0.15f ? 3 : 0);
        const int span = kR - kL;
        for (int i = 0; i < puffs; ++i) {
            const int w = 12 + static_cast<int>(art::hash2(i * 7 + 2, 5) % 26u);
            const float speed = 1.0f + 0.55f * static_cast<float>(i % 3) + 4.5f * rain;
            const int x0 = kL + (static_cast<int>(art::hash2(i * 11 + 1, 9))
                         + static_cast<int>(std::lround(t * speed))) % span;
            const int y0 = kT + 2 + (i * 3) % 7;
            const int a = std::clamp(
                static_cast<int>(62.0f + 34.0f * dayK + 44.0f * rain), 0, 190);
            const int rows = 3 + (rain > 0.30f ? 1 : 0);
            for (int k = 0; k < w; ++k) {
                const int x = kL + (x0 - kL + k) % span;
                const float u = static_cast<float>(k) / static_cast<float>(std::max(1, w - 1));
                const float bulge = std::sin(u * 3.14159f);
                const int y = y0 + static_cast<int>(std::lround(
                    std::sin(static_cast<float>(k) * 0.33f + t * 0.45f + i) * 0.45f));
                const int thick = rows - (bulge < 0.35f ? 1 : 0);
                for (int r = 0; r < thick; ++r) {
                    const int yy = y + r;
                    if (yy < kT + 1 || yy >= bankTopAt(eco, x) - 1) continue;
                    const SDL_Color c = (r == 0) ? cloudLit : cloudShade;
                    const int aa = (r == 0) ? a : (a * 7) / 10;
                    SDL_SetRenderDrawColor(r_, c.r, c.g, c.b,
                                           static_cast<Uint8>(std::clamp(aa, 0, 255)));
                    SDL_RenderDrawPoint(r_, x, yy);
                }
            }
        }

        // Sun disc arcs across the day: it rises near the left rim, peaks
        // around noon and sets on the right, always staying in the sky strip
        // between the glass lip and the shore.
        if (sun > 0.04f) {
            const float u = std::clamp(eco.dayPhase() / 3.14159265f, 0.0f, 1.0f);
            const int sx = kL + 14 + static_cast<int>(std::lround(u * (span - 28)));
            const int rise = static_cast<int>(std::lround(std::sin(u * 3.14159265f) * 5.0f));
            const int sy = 13 - rise;
            const int yTop = std::max(kT + 2, std::min(sy, bankTopAt(eco, sx) - 4));
            const int glow = static_cast<int>(56 + 96 * horizonGlow);
            SDL_SetRenderDrawColor(r_, 248, 220, 164, static_cast<Uint8>(glow));
            SDL_RenderDrawPoint(r_, sx - 1, yTop);
            SDL_RenderDrawPoint(r_, sx + 2, yTop);
            SDL_RenderDrawPoint(r_, sx, yTop - 1);
            SDL_RenderDrawPoint(r_, sx + 1, yTop - 1);
            SDL_RenderDrawPoint(r_, sx, yTop + 1);
            SDL_RenderDrawPoint(r_, sx + 1, yTop + 1);
            SDL_SetRenderDrawColor(r_, 252, 242, 212, 245);
            SDL_RenderDrawPoint(r_, sx, yTop);
            SDL_RenderDrawPoint(r_, sx + 1, yTop);
        }

        if (dayK < 0.45f) {
            const int alpha = static_cast<int>((0.45f - dayK) / 0.45f * 200.0f
                                               * (1.0f - rain * 0.8f));
            const float twinkle = 0.55f + 0.45f * std::sin(t * 1.3f);
            const int a = std::clamp(static_cast<int>(alpha * twinkle), 0, 255);
            SDL_SetRenderDrawColor(r_, 226, 232, 244, static_cast<Uint8>(a));
            for (int i = 0; i < 14; ++i) {
                const int sx = kL + 9 + static_cast<int>(
                    art::hash2(i, 7) % static_cast<std::uint32_t>(span - 18));
                const int sy = kT + 2 + static_cast<int>(art::hash2(i, 13) % 9u);
                if (sy >= bankTopY(sx) - 1) continue;
                SDL_RenderDrawPoint(r_, sx, sy);
                if (i % 4 == 0) SDL_RenderDrawPoint(r_, sx + 1, sy);
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawAmbient(const Ecosystem& eco) {
        const float light = visualLight(eco);
        const float night = eco.nightFactor();
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);

        // Night is a cool, slightly blue veil; bright daylight is a warm,
        // clearer wash.  Both are overlays only, so the baked depth remains.
        if (night > 0.01f) {
            fillWaterTint(art::kWaterDark,
                          std::min(28, static_cast<int>(night * 28.0f)));
        }
        if (light > 0.20f) {
            const int warm = std::min(10, static_cast<int>((light - 0.20f) * 13.0f));
            fillWaterTint(art::kCream, warm);
            fillWaterTint(art::kTeal, std::max(0, warm / 2));
        }
        const float dayDim = std::clamp(0.90f - eco.daylight(), 0.0f, 0.15f);
        if (dayDim > 0.001f) {
            fillWaterTint(art::kWaterDark,
                          static_cast<int>(dayDim * 95.0f));
        }
        const float tempShift = std::clamp((eco.temperature() - 22.0f) / 5.0f,
                                           -1.0f, 1.0f);
        if (std::fabs(tempShift) > 0.05f) {
            const SDL_Color tint = tempShift > 0.0f ? art::kAmber : art::kTeal;
            fillWaterTint(tint, static_cast<int>(5.0f + std::fabs(tempShift) * 22.0f));
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    int seasonId(const Ecosystem& eco) const {
        return static_cast<int>(eco.seasonPhase() / 6.2831853f * 4.0f) & 3;
    }

    // A readable seasonal wash: spring green, warm summer, rusty autumn and a
    // cold, pale winter.  It fades near the season boundary so the shift is
    // gradual rather than a hard cut.
    void drawSeasonWash(const Ecosystem& eco) {
        const int season = seasonId(eco);
        SDL_Color c = art::kMoss;
        int alpha = 42;
        switch (season) {
        case 0: c = art::kMoss;       alpha = 44; break;
        case 1: c = art::kAmber;      alpha = 36; break;
        case 2: c = art::kRust;       alpha = 52; break;
        default: c = SDL_Color{96, 132, 158, 255}; alpha = 58; break;
        }
        const float local = eco.seasonPhase() / 6.2831853f * 4.0f;
        const float frac = local - std::floor(local);
        const float ramp = std::clamp(std::min(frac, 1.0f - frac) / 0.18f, 0.0f, 1.0f);
        alpha = static_cast<int>(alpha * (0.50f + 0.50f * ramp));

        const SDL_Rect air{kL, kT, kR - kL + 1, art::kWaterTop - kT};
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        fillWaterTint(c, alpha);
        SDL_SetRenderDrawColor(r_, c.r, c.g, c.b, static_cast<Uint8>(alpha / 3));
        SDL_RenderFillRect(r_, &air);
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawSeasonGlyph(const Ecosystem& eco) {
        const int season = seasonId(eco);
        const int x = kL + 6;
        const int y = kT + 4;
        switch (season) {
        case 0:  // sprout
            point(x + 2, y, art::kInkCream);
            point(x + 2, y + 1, art::kInkGreen);
            point(x + 1, y + 2, art::kInkGreen);
            point(x + 2, y + 2, art::kInkGreen);
            point(x + 3, y + 2, art::kInkGreen);
            point(x + 2, y + 3, art::kInkMossDark);
            break;
        case 1:  // sun
            for (int d = 0; d < 5; ++d) {
                point(x + d, y + 2, art::kInkAmber);
                point(x + 2, y + d, art::kInkAmber);
            }
            point(x + 2, y + 2, art::kInkCream);
            break;
        case 2:  // falling leaf
            point(x + 2, y + 1, art::kInkRust);
            point(x + 3, y + 1, art::kInkAmber);
            point(x + 1, y + 2, art::kInkRust);
            point(x + 2, y + 2, art::kInkAmber);
            point(x + 3, y + 2, art::kInkRust);
            point(x + 2, y + 3, art::kInkRust);
            break;
        default:  // snowflake
            point(x + 2, y, art::kInkWhite);
            point(x + 2, y + 4, art::kInkWhite);
            point(x, y + 2, art::kInkWhite);
            point(x + 4, y + 2, art::kInkWhite);
            point(x + 2, y + 2, art::kInkTeal);
            point(x + 1, y + 1, art::kInkTeal);
            point(x + 3, y + 3, art::kInkTeal);
            break;
        }
    }

    void drawMicroTint(const Ecosystem& eco) {
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        for (int gy = 0; gy < Ecosystem::kGridH; ++gy) {
            for (int gx = 0; gx < Ecosystem::kGridW; ++gx) {
                const auto& c = eco.gridCell(gx, gy);
                const int x = gx * 20;
                const int y = art::kWaterTop + gy * 13;
                const int h = std::min(13, 104 - y);
                if (h <= 0) continue;
                const float tannin = std::clamp(c.tannin / 80.0f, 0.0f, 1.0f);
                const float lowO2 = std::clamp((44.0f - c.o2) / 44.0f, 0.0f, 1.0f);
                const float toxin = std::clamp(c.toxin / 26.0f, 0.0f, 1.0f);
                const float strongest = std::max(tannin, std::max(lowO2, toxin));
                if (strongest < 0.095f) continue;
                SDL_Color tint = art::kAmber;
                int alpha = 4 + static_cast<int>(tannin * 30.0f);
                if (lowO2 > tannin && lowO2 >= toxin) {
                    tint = art::kWaterDark;
                    alpha = 3 + static_cast<int>(lowO2 * 22.0f);
                } else if (toxin > tannin && toxin > lowO2) {
                    tint = art::kRust;
                    alpha = 3 + static_cast<int>(toxin * 18.0f);
                }
                SDL_SetRenderDrawColor(r_, tint.r, tint.g, tint.b,
                                       static_cast<Uint8>(std::clamp(alpha, 2, 48)));
                const SDL_Rect rc{x, y, 20, h};
                SDL_RenderFillRect(r_, &rc);
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawLightPatches(const Ecosystem& eco) {
        const auto* patches = eco.lightPatches();
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < eco.lightCount(); ++i) {
            const auto& p = patches[i];
            const float life = 1.0f - std::clamp(p.age / p.duration, 0.0f, 1.0f);
            const int radius = static_cast<int>(p.radius * (0.45f + 0.55f * life));
            const bool bright = p.amount > 0.0f;
            const SDL_Color tint = bright ? art::kCream : art::kDeep;
            const int base = static_cast<int>(4.0f + std::fabs(p.amount) * 0.55f);
            SDL_SetRenderDrawColor(r_, tint.r, tint.g, tint.b,
                                   static_cast<Uint8>(std::min(26, base / 2)));
            for (int dy = -radius; dy <= radius; ++dy) {
                const int half = static_cast<int>(std::sqrt(
                    std::max(0, radius * radius - dy * dy)));
                SDL_RenderDrawLine(r_, static_cast<int>(p.x) - half,
                                   static_cast<int>(p.y) + dy,
                                   static_cast<int>(p.x) + half,
                                   static_cast<int>(p.y) + dy);
            }
            for (int rr = radius; rr >= 3; rr -= 3) {
                const int a = std::max(1, base * (radius - rr + 3) / std::max(1, radius));
                SDL_SetRenderDrawColor(r_, tint.r, tint.g, tint.b,
                                       static_cast<Uint8>(std::min(24, a)));
                for (int k = 0; k < 28; ++k) {
                    const float ang = k * 6.2831853f / 28.0f;
                    SDL_RenderDrawPoint(r_, static_cast<int>(p.x + std::cos(ang) * rr),
                                        static_cast<int>(p.y + std::sin(ang) * rr));
                }
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawFoodAndStartle(const Ecosystem& eco) {
#ifdef ECO_DEBUG_STATS
        static int debugStartleDraws = 0;
        if (eco.startleCount() > 0 && debugStartleDraws < 20) {
            std::fprintf(stderr, "[draw] startle n=%d age=%.2f\n",
                         eco.startleCount(),
                         static_cast<double>(eco.startleFields()[0].age));
            std::fflush(stderr);
            ++debugStartleDraws;
        }
#endif
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        const auto* food = eco.foodPulses();
        for (int i = 0; i < eco.foodCount(); ++i) {
            const auto& p = food[i];
            const float life = 1.0f - std::clamp(p.age / p.duration, 0.0f, 1.0f);
            const int n = std::max(3, static_cast<int>(p.specks * life));
            for (int k = 0; k < n; ++k) {
                const unsigned h = art::hash2(i * 37 + k * 11, 5);
                const float drift = std::sin(p.age * 1.4f + k * 1.9f) * 3.0f;
                const int x = static_cast<int>(p.x + static_cast<int>(h % 15) - 7 + drift);
                const int y = static_cast<int>(p.y + p.age * 7.0f
                              + static_cast<int>((h >> 4) % 9));
                if (x < kL || x > kR || y < 8 || y > 106) continue;
                SDL_SetRenderDrawColor(r_, art::kAmber.r, art::kAmber.g,
                                       art::kAmber.b,
                                       static_cast<Uint8>(40 + life * 130));
                SDL_RenderDrawPoint(r_, x, y);
            }
        }
        const auto* startles = eco.startleFields();
        for (int i = 0; i < eco.startleCount(); ++i) {
            const auto& s = startles[i];
            const float t = std::clamp(s.age / s.duration, 0.0f, 1.0f);
            const int radius = static_cast<int>(8.0f + 38.0f * t);
            const int alpha = static_cast<int>((1.0f - t) * 185.0f);
            SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                                   static_cast<Uint8>(std::min(36, alpha / 4)));
            for (int dy = -radius; dy <= radius; ++dy) {
                const int half = static_cast<int>(std::sqrt(
                    std::max(0, radius * radius - dy * dy)));
                SDL_RenderDrawLine(r_, static_cast<int>(s.x) - half,
                                   static_cast<int>(s.y) + dy,
                                   static_cast<int>(s.x) + half,
                                   static_cast<int>(s.y) + dy);
            }
            SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                                   static_cast<Uint8>(alpha));
            for (int k = 0; k < 40; ++k) {
                if ((k + static_cast<int>(s.age * 40)) % 4 == 0) continue;
                const float ang = k * 6.2831853f / 40.0f;
                SDL_RenderDrawPoint(r_, static_cast<int>(s.x + std::cos(ang) * radius),
                                    static_cast<int>(s.y + std::sin(ang) * radius));
                if (k % 3 == 0) {
                    SDL_RenderDrawPoint(r_, static_cast<int>(s.x + std::cos(ang) * (radius - 3)),
                                        static_cast<int>(s.y + std::sin(ang) * (radius - 3)));
                }
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawSunbeams(const Ecosystem& eco) {
        const float light = visualLight(eco);
        const float strength = std::clamp((light - 0.16f) / 0.77f, 0.0f, 1.0f);
        if (strength <= 0.02f) return;

        const float t = eco.animTime();
        const float bases[3] = {32.0f, 150.0f, 268.0f};
        const float sweep = std::sin(t * 0.11f) * 24.0f
                          + std::sin(t * 0.041f + 1.3f) * 10.0f
                          + std::sin(t * 0.30f + 0.7f) * 5.0f;
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);

        for (int i = 0; i < 3; ++i) {
            const float sway = std::sin(t * 0.16f + i * 2.1f) * 5.0f
                             + std::sin(t * 0.055f + i * 0.9f) * 3.0f;
            const float beamPulse = 0.78f + 0.22f * std::sin(t * 0.075f + i * 1.7f);
            for (int y = art::kWaterTop + 1; y < 104; ++y) {
                const float progress = static_cast<float>(y - art::kWaterTop)
                                     / static_cast<float>(104 - art::kWaterTop);
                const float cx = bases[i] + progress * (44.0f + 10.0f * std::sin(t * 0.04f + i))
                               + sweep * (0.45f + 0.55f * progress)
                               + sway * (0.35f + 0.65f * progress)
                               + std::sin(t * 0.14f + y * 0.05f + i) * 2.5f
                               + std::sin(t * 0.62f + y * 0.08f + i) * 1.2f;
                const float half = (4.0f + progress * 11.0f)
                                 * (0.84f + 0.22f * std::sin(t * 0.09f + i * 1.9f));

                // Cool outer shaft.
                const int shaftL = std::max(kL, static_cast<int>(std::lround(cx - half)));
                const int shaftR = std::min(kR, static_cast<int>(std::lround(cx + half)));
                SDL_SetRenderDrawColor(r_, art::kTeal.r, art::kTeal.g, art::kTeal.b,
                                       static_cast<Uint8>(std::min(
                                           static_cast<int>((7.0f + 22.0f * strength) * beamPulse), 30)));
                SDL_RenderDrawLine(r_, shaftL, y, shaftR, y);

                // Soft edge rays make the shaft's movement readable.
                SDL_SetRenderDrawColor(r_, art::kTeal.r, art::kTeal.g, art::kTeal.b,
                                       static_cast<Uint8>(std::min(
                                           static_cast<int>((12.0f + 18.0f * strength) * beamPulse), 30)));
                SDL_RenderDrawPoint(r_, shaftL, y);
                SDL_RenderDrawPoint(r_, shaftR, y);

                // Warm inner shaft.
                const int inner = static_cast<int>(half * 0.58f);
                SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                                       static_cast<Uint8>(std::min(
                                           static_cast<int>((8.0f + 38.0f * strength) * beamPulse), 52)));
                SDL_RenderDrawLine(r_, std::max(kL, static_cast<int>(std::lround(cx)) - inner), y,
                                   std::min(kR, static_cast<int>(std::lround(cx)) + inner), y);

                // Broken hot core - this is what makes it read as sunlight.
                if ((y + i * 3) % 5 == 0) {
                    SDL_SetRenderDrawColor(r_, art::kWhiteCream.r, art::kWhiteCream.g,
                                           art::kWhiteCream.b,
                                           static_cast<Uint8>(std::min(70 + static_cast<int>(110.0f * strength), 180)));
                    SDL_RenderDrawPoint(r_, static_cast<int>(std::lround(cx)), y);
                    if (strength > 0.55f) {
                        SDL_RenderDrawPoint(r_, static_cast<int>(std::lround(cx)) + 1, y);
                    }
                }
            }
        }

        // Warm surface glow.
        if (strength > 0.3f) {
            for (int y = art::kWaterTop + 1; y < art::kWaterTop + 12; ++y) {
                const int alpha = static_cast<int>(
                    (3.0f + 8.0f * strength) * (1.0f - static_cast<float>(y - art::kWaterTop) / 13.0f));
                SDL_SetRenderDrawColor(r_, art::kAmber.r, art::kAmber.g, art::kAmber.b,
                                       static_cast<Uint8>(std::min(alpha, 10)));
                SDL_RenderDrawLine(r_, kL + 5, y, kR - 5, y);
            }
        }

        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawCausticNet(const Ecosystem& eco) {
        const float light = visualLight(eco);
        const float strength = std::clamp((light - 0.30f) / 0.65f, 0.0f, 1.0f);
        if (strength <= 0.0f) return;

        const float t = eco.animTime();
        const float agitation = eco.waterAgitation();
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < 7; ++i) {
            const int baseY = art::kWaterTop + 4 + i * 7;
            const int shift = static_cast<int>(t * (4.0f + 4.0f * agitation)) + i * 9;
            const Uint8 alpha = static_cast<Uint8>(std::min(4 + static_cast<int>(13.0f * strength), 16));
            SDL_SetRenderDrawColor(r_, (i & 1) ? art::kCream.r : art::kTeal.r,
                                   (i & 1) ? art::kCream.g : art::kTeal.g,
                                   (i & 1) ? art::kCream.b : art::kTeal.b, alpha);
            for (int x = kL + 3; x <= kR - 3; ++x) {
                if ((x + shift) % 19 >= 8) continue;
                const int y = baseY + static_cast<int>(std::lround(
                    std::sin(x * 0.052f + t * 0.32f + i * 1.3f) * 2.0f));
                SDL_RenderDrawPoint(r_, x, y);
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawGroundCaustics(const Ecosystem& eco) {
        const float light = visualLight(eco);
        const float strength = std::clamp((light - 0.32f) / 0.60f, 0.0f, 1.0f);
        if (strength <= 0.0f) return;

        const float t = eco.animTime();
        const float bases[3] = {42.0f, 154.0f, 262.0f};
        const float sweep = std::sin(t * 0.11f) * 24.0f
                          + std::sin(t * 0.041f + 1.3f) * 10.0f;
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < 3; ++i) {
            const float cx = bases[i] + sweep * 0.55f
                           + std::sin(t * 0.19f + i * 1.7f) * 10.0f;
            for (int dy = 0; dy < 4; ++dy) {
                const int alpha = static_cast<int>((28.0f - 4.0f * dy) * strength);
                SDL_SetRenderDrawColor(r_, (dy == 0) ? art::kCream.r : art::kTeal.r,
                                       (dy == 0) ? art::kCream.g : art::kTeal.g,
                                       (dy == 0) ? art::kCream.b : art::kTeal.b,
                                       static_cast<Uint8>(std::min(alpha, 32)));
                for (int dx = -14; dx <= 14; ++dx) {
                    const int x = static_cast<int>(std::lround(cx)) + dx;
                    if (x < kL || x > kR) continue;
                    const float edge = 1.0f - std::fabs(static_cast<float>(dx)) / 15.0f;
                    if (dither(x, dy) < static_cast<int>(11.0f * edge)) {
                        SDL_RenderDrawPoint(r_, x, groundY(x) - 1 + dy);
                    }
                }
            }

            // A sharp dancing vein on top of the soft pool.
            const int veinAlpha = static_cast<int>(70.0f * strength);
            SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                                   static_cast<Uint8>(std::min(veinAlpha, 75)));
            for (int dx = -18; dx <= 18; ++dx) {
                const int x = static_cast<int>(std::lround(cx)) + dx;
                if (x < kL || x > kR) continue;
                if ((dx + static_cast<int>(t * 7.0f) + i) % 3 == 0) continue;
                const int y = groundY(x) - 1 + static_cast<int>(std::lround(
                    std::sin(static_cast<float>(dx) * 0.55f + t * 2.1f + i) * 1.2f));
                SDL_RenderDrawPoint(r_, x, y);
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawRefraction(const Ecosystem& eco) {
        const float light = visualLight(eco);
        if (light < 0.35f) return;
        const float t = eco.animTime();

        struct Glint { int x, y; float phase; };
        const Glint glints[] = {
            {58, groundY(58) - 2, 0.3f},
            {126, groundY(126) - 1, 2.1f},
            {212, groundY(212) - 2, 3.7f},
            {294, groundY(294) - 1, 5.0f},
            {42, 66, 1.2f},
            {282, 70, 4.1f},
            {7, 30, 2.8f}
        };

        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        for (const Glint& g : glints) {
            const float s = std::sin(t * 1.35f + g.phase);
            if (s < 0.55f) continue;
            const int alpha = static_cast<int>(60 + 130 * (s - 0.55f) / 0.45f * light);
            SDL_SetRenderDrawColor(r_, art::kWhiteCream.r, art::kWhiteCream.g,
                                   art::kWhiteCream.b, static_cast<Uint8>(std::min(alpha, 190)));
            SDL_RenderDrawPoint(r_, g.x, g.y);
            SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                                   static_cast<Uint8>(std::min(alpha / 2, 100)));
            SDL_RenderDrawPoint(r_, g.x - 1, g.y);
            SDL_RenderDrawPoint(r_, g.x + 1, g.y);
            SDL_RenderDrawPoint(r_, g.x, g.y - 1);
            SDL_RenderDrawPoint(r_, g.x, g.y + 1);
            if (s > 0.85f) {
                SDL_SetRenderDrawColor(r_, art::kAmber.r, art::kAmber.g, art::kAmber.b,
                                       static_cast<Uint8>(std::min(alpha, 140)));
                SDL_RenderDrawPoint(r_, g.x + 2, g.y + 1);
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawBubbles(const Ecosystem& eco) {
        const float t = eco.animTime();
        for (int i = 0; i < 7; ++i) {
            const float speed = 3.0f + static_cast<float>(i % 3) * 1.1f;
            const float xx = 12.0f + std::fmod(
                static_cast<float>(i) * 43.0f
                + std::sin(t * 0.3f + static_cast<float>(i)) * 7.0f + 280.0f, 292.0f);
            const int gx = static_cast<int>(xx);
            const int base = groundY(gx) - 2;
            const float yy = static_cast<float>(base)
                           - std::fmod(t * speed + static_cast<float>(i) * 17.0f,
                                       static_cast<float>(base - art::kWaterTop - 2));
            if (yy < art::kWaterTop + 2.0f || yy > base) continue;

            const int x = gx;
            const int y = static_cast<int>(yy);
            point(x, y, art::kInkCream);
            if (i % 3 == 0) {
                point(x + 1, y, art::kInkWhite);
                point(x, y + 1, art::kInkTeal);
            } else {
                point(x + 1, y, art::kInkTeal);
            }
        }
    }

    void drawMotes(const Ecosystem& eco) {
        const float t = eco.animTime();
        const float light = visualLight(eco);
        const float agitation = eco.waterAgitation();
        const int count = 6 + static_cast<int>(light * 10.0f);
        for (int i = 0; i < count; ++i) {
            const int layer = i % 3;
            const float speed = (1.2f + layer * 0.8f) * (0.80f + 0.40f * agitation);
            const float xx = 8.0f + std::fmod(
                static_cast<float>(i) * 47.0f
                + std::sin(t * 0.22f + static_cast<float>(i)) * 6.0f + 300.0f, 304.0f);
            const float yy = static_cast<float>(art::kWaterTop + 3) + std::fmod(
                static_cast<float>(i) * 23.0f + t * speed, 84.0f);
            char ink = (layer == 0) ? art::kInkTeal
                     : (layer == 1 ? art::kInkAmber : art::kInkCream);
            if (i % 5 == 0) ink = art::kInkCream;
            point(static_cast<int>(xx), static_cast<int>(yy), ink);
            if (layer == 2 && light > 0.55f && (i % 4 == 0)) {
                point(static_cast<int>(xx) + 1, static_cast<int>(yy), art::kInkCream);
            }
        }
    }

    void drawFloatingLeaf(const Ecosystem& eco) {
        const float t = eco.animTime();
        const int x = 28 + static_cast<int>(std::fmod(t * 2.2f, 252.0f));
        const int y = 28 + static_cast<int>(std::lround(std::sin(t * 0.31f) * 4.0f));
        if (x < kL || x + 6 > kR) return;
        point(x, y, art::kInkRust);
        point(x + 1, y, art::kInkAmber);
        point(x + 2, y, art::kInkAmber);
        point(x + 3, y, art::kInkCream);
        point(x + 4, y, art::kInkRust);
        point(x + 1, y + 1, art::kInkRust);
        point(x + 2, y + 1, art::kInkAmber);
        point(x + 3, y + 1, art::kInkRust);
        point(x + 2, y + 2, art::kInkRust);
    }

    // Seasonal and weather particles.  These are pure overlays: nothing here
    // changes the simulation, so the same state still drives population and
    // chemistry exactly as before.
    void drawWeather(const Ecosystem& eco) {
        const float t = eco.animTime();
        const int season = seasonId(eco);
        const float surface = static_cast<float>(art::kWaterTop + eco.waterLevelOffset());

        auto eventStrength = [&](int type) {
            float s = 0.0f;
            for (int i = 0; i < eco.eventCount(); ++i) {
                const auto& e = eco.events()[i];
                if (e.type != type) continue;
                const float env = std::clamp(
                    std::min(e.age / 2.5f, (e.duration - e.age) / 3.0f), 0.0f, 1.0f);
                s = std::max(s, env * e.intensity);
            }
            return s;
        };

        // Rain: slanted streaks through the air gap and rings on the surface.
        const float rain = eventStrength(0);
        if (rain > 0.06f) {
            const int drops = 6 + static_cast<int>(rain * 12.0f);
            // One streak per horizontal lane plus a little jitter.  Pure hash
            // placement left whole stretches of sky, especially the far left,
            // without a single drop while the rest of the bottle was raining.
            const int innerW = kR - kL - 6;
            const int laneW = std::max(1, innerW / drops);
            SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                                   static_cast<Uint8>(110));
            for (int i = 0; i < drops; ++i) {
                // Lane starts are spread over the whole inner width, so the
                // last lane still reaches the right wall instead of piling up
                // short of it.  The jitter only slides within each lane.
                const int x = kL + 2 + (i * innerW) / drops
                            + static_cast<int>(art::hash2(i * 13 + 3, 5)
                                               % static_cast<uint32_t>(laneW));
                const float span = std::max(6.0f, surface - kT - 2.0f);
                const int y = kT + 1 + static_cast<int>(std::fmod(t * (46.0f + i * 3.0f)
                                             + i * 17.0f, span));
                SDL_RenderDrawPoint(r_, x, y);
                SDL_RenderDrawPoint(r_, x + 1, y + 1);
                SDL_RenderDrawPoint(r_, x + 2, y + 2);
                SDL_SetRenderDrawColor(r_, art::kTeal.r, art::kTeal.g, art::kTeal.b, 95);
                SDL_RenderDrawPoint(r_, x + 3, y + 3);
                if (static_cast<float>(y) >= surface - 4.0f) {
                    SDL_RenderDrawPoint(r_, x, static_cast<int>(surface));
                    SDL_RenderDrawPoint(r_, x + 3, static_cast<int>(surface));
                }
                SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                                       static_cast<Uint8>(110));
            }
            SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
        }

        // Heat: slow horizontal shimmer bands.
        const float heat = eventStrength(2);
        const float hot = std::max(heat, std::clamp((eco.temperature() - 26.5f) / 3.0f,
                                                    0.0f, 1.0f) * 0.55f);
        if (hot > 0.08f) {
            SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                                   static_cast<Uint8>(14 + 26 * hot));
            for (int i = 0; i < 4; ++i) {
                const int yy = art::kWaterTop + 6 + i * 7
                             + static_cast<int>(std::lround(std::sin(t * 0.9f + i) * 1.5f));
                for (int x = kL + 6; x < kR - 6; x += 2) {
                    if ((x + i * 3 + static_cast<int>(t * 6.0f)) % 5 < 2) {
                        SDL_RenderDrawPoint(r_, x, yy);
                    }
                }
            }
            SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
        }

        // Frost: creeping crystals on the glass and along the bottom corners.
        const float cold = std::max(eventStrength(3),
            season == 3 ? std::clamp((17.5f - eco.temperature()) / 3.5f, 0.0f, 1.0f) : 0.0f);
        if (cold > 0.04f) {
            const int n = 10 + static_cast<int>(cold * 30.0f);
            for (int i = 0; i < n; ++i) {
                const std::uint32_t h = art::hash2(i * 7 + 3, 11);
                int x = kL + 4 + static_cast<int>((h >> 3) % 300u);
                int y = kT + 2 + static_cast<int>((h >> 9) % 96u);
                if (h % 3u == 0u) x = kL + 1 + static_cast<int>(h % 12u);
                else if (h % 3u == 1u) x = kR - 1 - static_cast<int>(h % 12u);
                else y = kB - 2 - static_cast<int>(h % 5u);
                point(x, y, (i % 4 == 0) ? art::kInkWhite : art::kInkTeal);
                if (i % 3 == 0) point(x + 1, y, art::kInkCream);
            }
        }

        // Algal bloom: green haze plus suspended cells.
        const float bloom = eventStrength(4);
        if (bloom > 0.05f) {
            SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
            fillWaterTint(art::kMoss, 8 + 22 * bloom);
            SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
            const int cells = 8 + static_cast<int>(bloom * 16.0f);
            for (int i = 0; i < cells; ++i) {
                const int x = kL + 8 + static_cast<int>(art::hash2(i * 11 + 5, 3) % 292u);
                const int y = art::kWaterTop + 4 + static_cast<int>(
                    std::fmod(t * (2.0f + (i % 3) * 0.7f) + i * 13.0f, 76.0f));
                point(x, y, (i % 3 == 0) ? art::kInkGreen : art::kInkTeal);
            }
        }

        // Overturn: silt curls up from the mud.
        const float churn = eventStrength(5);
        if (churn > 0.05f) {
            const int n = 8 + static_cast<int>(churn * 18.0f);
            for (int i = 0; i < n; ++i) {
                const int x = kL + 8 + static_cast<int>(art::hash2(i * 9 + 7, 5) % 292u);
                const int y = 101 - static_cast<int>(std::fmod(
                    t * (5.0f + (i % 4)) + i * 11.0f, 62.0f));
                point(x, y, (i % 3 == 0) ? art::kInkRust : art::kInkMudMid);
                if (i % 5 == 0) point(x, y - 1, art::kInkMud);
            }
        }

        // Falling leaves: stronger in autumn and during leaf-litter events.
        const float leaf = eventStrength(6);
        int leaves = (season == 2 ? 2 : 0) + (leaf > 0.05f ? 3 : 0);
        for (int i = 0; i < leaves; ++i) {
            const int x = kL + 14 + static_cast<int>(art::hash2(i * 5 + 2, 7) % 270u);
            const int y = kT + 4 + static_cast<int>(std::fmod(
                t * (4.0f + i * 1.3f) + i * 31.0f, 92.0f));
            if (y > 102) continue;
            point(x, y, (i % 2) ? art::kInkRust : art::kInkAmber);
            point(x + 1, y, art::kInkRust);
            if (i % 2) point(x + 2, y, art::kInkAmber);
        }

        // Spring pollen: a few bright drifting grains.
        if (season == 0) {
            for (int i = 0; i < 4; ++i) {
                const int x = kL + 12 + static_cast<int>(art::hash2(i * 17 + 9, 13) % 280u);
                const int y = art::kWaterTop + 6 + static_cast<int>(std::fmod(
                    t * (2.4f + i * 0.5f) + i * 23.0f, 70.0f));
                point(x, y, (i % 2) ? art::kInkCream : art::kInkGreen);
            }
        }
    }

    void drawWaterSurface(const Ecosystem& eco) {
        const float t = eco.animTime();
        const float light = visualLight(eco);
        const float agitation = eco.waterAgitation();
        const float amp = 0.55f + 0.85f * agitation;
        int wave[art::kScreenW] = {0};

        art::setInk(r_, art::kInkTeal);
        for (int x = kL + 1; x <= kR - 1; ++x) {
            wave[x] = art::kWaterTop + eco.waterLevelOffset()
                    + static_cast<int>(std::lround(
                std::sin(x * 0.11f + t * (0.70f + 0.45f * agitation)) * amp
                + std::sin(x * 0.033f - t * (0.45f + 0.25f * agitation)) * (amp * 0.70f)
                + std::sin(x * 0.23f + t * 1.8f) * 0.25f * agitation));
            SDL_RenderDrawPoint(r_, x, wave[x]);
            if (art::hash2(x, 7) % 3u == 0u) {
                SDL_RenderDrawPoint(r_, x, wave[x] + 1);
            }
        }

        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        const int sheen = static_cast<int>(4.0f + light * 8.0f);
        SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                               static_cast<Uint8>(std::min(sheen, 12)));
        for (int x = kL + 1; x <= kR - 1; ++x) {
            SDL_RenderDrawPoint(r_, x, wave[x] - 1);
        }

        if (light > 0.28f) {
            SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                                   static_cast<Uint8>(std::min(22 + static_cast<int>(light * 35.0f), 52)));
            const int period = 17 - static_cast<int>(light * 6.0f);
            const int shift = static_cast<int>(t * 8.0f);
            for (int x = kL + 1; x <= kR - 1; ++x) {
                if ((x + shift) % period < 3) {
                    SDL_RenderDrawPoint(r_, x, wave[x] - 1);
                }
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);

        // Tiny travelling surface sparkles.
        if (light > 0.5f) {
            const int sx = 20 + static_cast<int>(std::fmod(t * 9.0f, 280.0f));
            const int sy = wave[std::clamp(sx, kL + 1, kR - 1)] - 2;
            point(sx, sy, art::kInkWhite);
            if (sx + 2 < kR) point(sx + 2, sy, art::kInkCream);
        }
    }

    // Floating duckweed rafts.  Positions and density come from the live
    // duckweed population, so the mat grows, thins in winter and follows the
    // seasonal waterline instead of a baked row.
    void drawSurfacePlants(const Ecosystem& eco) {
        const float t = eco.animTime();
        for (int i = 0; i < eco.raftCount(); ++i) {
            const Ecosystem::Raft& r = eco.raftAt(i);
            const int bob = static_cast<int>(std::lround(
                std::sin(t * 0.6f + r.bobSeed) * 0.6f));
            const int y = std::clamp(surfaceTop_ + bob, 26, 44);
            const int n = std::clamp(
                3 + static_cast<int>(std::lround(r.mass * 4.0f)), 3, 8);
            const int cx = std::clamp(static_cast<int>(std::lround(r.x)),
                                      kL + 2, kR - 2 * (n - 1) - 2);
            for (int k = 0; k < n; ++k) {
                px(cx + k * 2, y, art::kLeaf);
                px(cx + k * 2, y + 1, art::kMossDark);
            }
            // A dense raft gets a shaded underside; a turion stays a speck.
            if (r.mass > 0.55f) {
                const int w = std::min(n, 3);
                for (int k = 0; k < w; ++k) {
                    px(cx + 1 + k * 2, y + 2, blend(art::kMossDark,
                                                    art::kWaterMid, 0.45f));
                }
            }
            // A couple of root threads dangle into the water.
            px(cx + 1, y + 3, art::kMossDark);
            px(cx + 2, y + 4, blend(art::kMossDark, art::kWaterMid, 0.55f));
        }
    }

    void drawGroundShadows(const Ecosystem& eco) {
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r_, art::kDeep.r, art::kDeep.g, art::kDeep.b, 90);
        const auto* plants = eco.plants();
        for (int i = 0; i < eco.plantCount(); ++i) {
            if (plants[i].dying) continue;
            const int x = static_cast<int>(std::lround(plants[i].x));
            const int half = (plants[i].type == 2) ? 6 : (plants[i].type == 1 ? 4 : 5);
            SDL_RenderDrawLine(r_, std::max(kL, x - half), groundY(x),
                               std::min(kR, x + half), groundY(x));
        }
        const auto* worms = eco.worms();
        for (int i = 0; i < eco.wormCount(); ++i) {
            const int x = static_cast<int>(std::lround(worms[i].x));
            SDL_RenderDrawLine(r_, std::max(kL, x - 2), groundY(x),
                               std::min(kR, x + 2), groundY(x));
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawDecor(const Ecosystem& eco) {
        const auto* decors = eco.decors();
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
        for (int i = 0; i < eco.decorCount(); ++i) {
            const auto& d = decors[i];
            const int cx = std::clamp(static_cast<int>(std::lround(d.x)), kL + 6, kR - 6);
            const int gy = groundY(cx) - 1;
            if (d.type == 0) {
                // Smooth river pebble with a damp rim and one cool highlight.
                SDL_SetRenderDrawColor(r_, art::kMudDark.r, art::kMudDark.g,
                                       art::kMudDark.b, 255);
                for (int dx = -5; dx <= 5; ++dx) {
                    const float t = 1.0f - std::fabs(static_cast<float>(dx)) / 5.0f;
                    const int h = 1 + static_cast<int>(std::lround(3.2f * t));
                    SDL_RenderDrawLine(r_, cx + dx, gy - h + 1, cx + dx, gy);
                }
                SDL_SetRenderDrawColor(r_, art::kTeal.r, art::kTeal.g, art::kTeal.b, 255);
                SDL_RenderDrawLine(r_, cx - 2, gy - 1, cx + 1, gy - 1);
                SDL_SetRenderDrawColor(r_, art::kMudLight.r, art::kMudLight.g,
                                       art::kMudLight.b, 255);
                SDL_RenderDrawPoint(r_, cx - 3, gy - 2);
                SDL_RenderDrawPoint(r_, cx - 2, gy - 2);
            } else if (d.type == 1) {
                // Fallen driftwood: dark outline, warm core, short fork.
                SDL_SetRenderDrawColor(r_, art::kMudDark.r, art::kMudDark.g,
                                       art::kMudDark.b, 255);
                SDL_RenderDrawLine(r_, cx - 11, gy, cx + 9, gy - 3);
                SDL_RenderDrawLine(r_, cx + 3, gy - 2, cx + 2, gy - 7);
                SDL_SetRenderDrawColor(r_, art::kMud.r, art::kMud.g, art::kMud.b, 255);
                SDL_RenderDrawLine(r_, cx - 10, gy - 1, cx + 8, gy - 4);
                SDL_SetRenderDrawColor(r_, art::kAmber.r, art::kAmber.g,
                                       art::kAmber.b, 255);
                SDL_RenderDrawLine(r_, cx - 8, gy - 1, cx + 6, gy - 4);
                SDL_RenderDrawLine(r_, cx + 3, gy - 3, cx + 2, gy - 6);
                SDL_SetRenderDrawColor(r_, art::kMudDark.r, art::kMudDark.g,
                                       art::kMudDark.b, 255);
                SDL_RenderDrawPoint(r_, cx + 5, gy - 4);
            } else {
                // Fan shell, ribbed, half buried in the mud.
                for (int dx = -4; dx <= 4; ++dx) {
                    const int h = 4 - std::abs(dx) / 2;
                    SDL_SetRenderDrawColor(r_, art::kRust.r, art::kRust.g,
                                           art::kRust.b, 255);
                    SDL_RenderDrawLine(r_, cx + dx, gy, cx + dx, gy - h);
                    if ((dx & 1) == 0 && h > 1) {
                        SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g,
                                               art::kCream.b, 255);
                        SDL_RenderDrawPoint(r_, cx + dx, gy - h);
                    }
                }
                SDL_SetRenderDrawColor(r_, art::kMudDark.r, art::kMudDark.g,
                                       art::kMudDark.b, 255);
                SDL_RenderDrawPoint(r_, cx - 1, gy);
                SDL_RenderDrawPoint(r_, cx + 1, gy);
            }
        }
    }

    void drawSelection(const Ecosystem& eco) {
        const uint32_t id = eco.selectedId();
        if (id == 0) return;
        Ecosystem::CreatureInfo info;
        if (!eco.creatureById(id, eco.selectedKind(), info)) return;
        int cx = static_cast<int>(std::lround(info.x));
        int cy = static_cast<int>(std::lround(info.y));
        if (info.kind == 5) cy = groundY(cx) - 10;   // plant canopy
        if (info.kind == 3) cy = 92;
        const int rx = (info.kind == 1) ? 11 : (info.kind == 5 ? 13 : 7);
        const int ry = (info.kind == 1) ? 7 : (info.kind == 5 ? 9 : 6);
        const float pulse = 0.5f + 0.5f * std::sin(eco.animTime() * 4.0f);
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b,
                               static_cast<Uint8>(130 + 100 * pulse));
        const int len = 3;
        const int l = std::clamp(cx - rx, kL + 1, kR - 1);
        const int rr = std::clamp(cx + rx, kL + 1, kR - 1);
        const int t = std::clamp(cy - ry, kT + 2, kB - 1);
        const int b = std::clamp(cy + ry, kT + 2, kB - 1);
        SDL_RenderDrawLine(r_, l, t, l + len, t);
        SDL_RenderDrawLine(r_, l, t, l, t + len);
        SDL_RenderDrawLine(r_, rr - len, t, rr, t);
        SDL_RenderDrawLine(r_, rr, t, rr, t + len);
        SDL_RenderDrawLine(r_, l, b, l + len, b);
        SDL_RenderDrawLine(r_, l, b - len, l, b);
        SDL_RenderDrawLine(r_, rr - len, b, rr, b);
        SDL_RenderDrawLine(r_, rr, b - len, rr, b);
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    // Draw four corner brackets so a preview reads as an outline rather than
    // another piece of scenery.
    void bracket(int left, int top, int right, int bottom, SDL_Color color,
                 Uint8 alpha) {
        const int len = 3;
        const int l = std::clamp(left, kL + 1, kR - 1);
        const int rr = std::clamp(right, kL + 1, kR - 1);
        const int t = std::clamp(top, kT + 2, kB - 1);
        const int b = std::clamp(bottom, kT + 2, kB - 1);
        SDL_SetRenderDrawColor(r_, color.r, color.g, color.b, alpha);
        SDL_RenderDrawLine(r_, l, t, l + len, t);
        SDL_RenderDrawLine(r_, l, t, l, t + len);
        SDL_RenderDrawLine(r_, rr - len, t, rr, t);
        SDL_RenderDrawLine(r_, rr, t, rr, t + len);
        SDL_RenderDrawLine(r_, l, b, l + len, b);
        SDL_RenderDrawLine(r_, l, b - len, l, b);
        SDL_RenderDrawLine(r_, rr - len, b, rr, b);
        SDL_RenderDrawLine(r_, rr, b - len, rr, b);
    }

    void drawToolPreview(const Ecosystem& eco) {
        if (previewMode_ == 0) return;
        if (previewX_ < kL || previewX_ > kR || previewY_ < kT || previewY_ > kB) {
            return;
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        if (previewMode_ == 1) {
            // Hovering an individual shows the exact hit the click would take.
            Ecosystem::CreatureInfo info;
            if (eco.creatureAt(static_cast<float>(previewX_),
                               static_cast<float>(previewY_), 16.0f, info)) {
                int cx = static_cast<int>(std::lround(info.x));
                int cy = static_cast<int>(std::lround(info.y));
                if (info.kind == 5) {
                    // Follow the pointer up the frond column so the feedback
                    // sits where the user is aiming, not at the root.
                    cy = std::clamp(previewY_, kT + 14, groundY(cx) - 12);
                }
                if (info.kind == 3) cy = 92;
                const int rx = (info.kind == 1) ? 12 : (info.kind == 5 ? 14 : 8);
                const int ry = (info.kind == 1) ? 8 : (info.kind == 5 ? 10 : 7);
                bracket(cx - rx, cy - ry, cx + rx, cy + ry, art::kCream, 120);
            }
        } else {
            const int cx = std::clamp(previewX_, kL + 6, kR - 6);
            const int gy = groundY(cx) - 1;
            if (previewKind_ == 3) {
                // Eraser: mark the click point with a cross.
                SDL_SetRenderDrawColor(r_, art::kRust.r, art::kRust.g,
                                       art::kRust.b, 200);
                const int cy = std::clamp(previewY_, gy - 16, gy + 2);
                SDL_RenderDrawLine(r_, cx - 3, cy - 3, cx + 3, cy + 3);
                SDL_RenderDrawLine(r_, cx + 3, cy - 3, cx - 3, cy + 3);
                bracket(cx - 6, cy - 6, cx + 6, cy + 6, art::kRust, 150);
            } else {
                const int halfW = (previewKind_ == 1) ? 12
                                : (previewKind_ == 2 ? 5 : 6);
                const int up = (previewKind_ == 1) ? 8
                             : (previewKind_ == 2 ? 6 : 4);
                // Amber keeps the footprint readable over dark plants and mud.
                bracket(cx - halfW, gy - up, cx + halfW, gy, art::kAmber, 210);
                SDL_SetRenderDrawColor(r_, art::kAmber.r, art::kAmber.g,
                                       art::kAmber.b, 210);
                SDL_RenderDrawPoint(r_, cx, gy);
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void rebuildPlantLayer(const Ecosystem& eco) {
        const float t = eco.animTime();
        plantPixels_.assign(static_cast<std::size_t>(art::kScreenW) * art::kScreenH, 0);
        const auto* plants = eco.plants();

        // Fertility is drawn as a very quiet warm/cool gradient in the mud.
        for (int x = kL; x <= kR; ++x) {
            const float fert = std::clamp(eco.sedimentAtX(static_cast<float>(x)) / 100.0f,
                                          0.0f, 1.0f);
            const SDL_Color bed = blend(art::kMudLight, art::kMudDark, fert);
            const int gy = groundY(x);
            for (int y = gy; y <= kB; ++y) {
                if (y < 0 || y >= art::kScreenH) continue;
                const Uint8 a = static_cast<Uint8>(45 + 55 * fert);
                plantPixels_[y * art::kScreenW + x] = pack(bed, a);
            }
        }

        // Dense mature clusters cast a visible, soft shade onto the roots.
        for (int i = 0; i < eco.plantCount(); ++i) {
            const Ecosystem::Plant& p = plants[i];
            if (p.dying || p.stage < 2 || p.health < 0.5f) continue;
            const int cx = static_cast<int>(std::lround(p.x));
            const int gy = groundY(cx);
            const int rx = (p.type == 2) ? 8 : (p.type == 1 ? 7 : 8);
            for (int dx = -rx; dx <= rx; ++dx) {
                if (cx + dx < kL || cx + dx > kR) continue;
                const int h = 2 - std::abs(dx) / 4;
                for (int dy = 0; dy <= h; ++dy) {
                    const int px = cx + dx, py = gy - 1 + dy;
                    if (px < 0 || px >= art::kScreenW || py < 0 || py >= art::kScreenH) continue;
                    plantPixels_[py * art::kScreenW + px] = pack(art::kDeep, 58);
                }
            }
        }

        for (int i = 0; i < eco.plantCount(); ++i) {
            const auto& p = plants[i];
            const int frame = (eco.animFrame() + static_cast<int>(p.phase * 2.0f)) & 1;

            const char* src = nullptr;
            int sw = 16, sh = 16, stride = 17;
            float baseScale = 1.50f;
            if (p.type == 1) {
                src = &art::kTallPlantFrames[frame][0][0];
                sh = 48;
                baseScale = 1.92f;
            } else if (p.type == 2) {
                src = &art::kRosetteFrames[frame][0][0];
                sw = 24; sh = 12; stride = 25;
                baseScale = 1.20f;
            } else {
                src = &art::kAlgaeFrames[frame][0][0];
            }

            float stageMul = Ecosystem::plantVisualScale(p.growth);
            if (p.stage == 3) {
                const float sen = std::clamp(std::max((p.age - 400.0f) / 160.0f,
                                                      (0.45f - p.health) / 0.30f),
                                             0.0f, 1.0f);
                stageMul *= 1.0f - 0.10f * sen;
            }
            if (p.dying) stageMul *= 1.0f - 0.18f * p.wilt;

            const float scaleY = baseScale * stageMul * p.g.height;
            const float widthMul = (p.type == 1) ? 1.00f : (p.type == 0 ? 1.15f : 1.0f);
            const float scaleX = baseScale * stageMul * widthMul
                               * (0.92f + 0.08f * p.g.density);
            const int dh = (p.type == 1)
                         ? std::min(static_cast<int>(std::lround(sh * scaleY)),
                                    groundY(static_cast<int>(std::lround(p.x))) - 10)
                         : std::max(1, static_cast<int>(std::lround(sh * scaleY)));
            const int plantH = dh;
            const int dw = std::max(1, static_cast<int>(std::lround(sw * scaleX)));
            const int x = static_cast<int>(std::lround(p.x));
            // Rosettes float: their leaf disc sits astride the live waterline
            // instead of a fixed row, so they follow the seasonal level.
            const int y = (p.type == 2) ? (surfaceTop_ - dh / 3)
                                        : (groundY(x) - plantH);

            const float nutrientLoss = std::clamp((28.0f - eco.nutrient()) / 28.0f, 0.0f, 1.0f);
            const float grazeLoss = 1.0f - p.health;
            const float senescent = std::clamp(std::max((p.age - 400.0f) / 160.0f,
                                                         (0.45f - p.health) / 0.30f),
                                               0.0f, 1.0f);
            float tintAmount = std::clamp(0.16f * grazeLoss + 0.18f * nutrientLoss
                                        + 0.22f * senescent + 0.60f * p.wilt
                                        + 0.10f * p.epiphyte, 0.0f, 0.88f);
            SDL_Color tint = art::kMudDark;
            tint = blend(tint, art::kRust,
                         std::min(1.0f, senescent * 0.85f + p.wilt * 1.2f));
            tint = blend(tint, art::kMudLight, std::min(1.0f, nutrientLoss * 1.4f));
            tint = blend(tint, art::kMossDark, std::min(1.0f, p.epiphyte * 1.8f));
            const float dayDim = std::max(0.0f, 0.95f - eco.daylight());
            tint = blend(tint, art::kWaterDark, std::min(1.0f, dayDim * 1.8f));
            tintAmount = std::min(0.88f, tintAmount + dayDim * 0.30f);
            const Uint8 alpha = static_cast<Uint8>(255 - 170.0f * p.wilt);

            const int copies = 1;
            for (int c = 0; c < copies; ++c) {
                const int off = 0;
                const float sway = std::sin(t * (0.42f + p.type * 0.08f) + p.phase
                                  + c * 1.7f) * (0.6f + 0.45f * stageMul);
                float lean = sway;
                const float senLean = senescent * 1.2f + p.wilt * 5.0f;
                if (senLean > 0.01f) {
                    lean += (p.flip ? 1.0f : -1.0f) * senLean;
                }
                art::drawSpriteScaledToBuffer(plantPixels_.data(),
                                      art::kScreenW, art::kScreenH,
                                      src, sw, sh, stride,
                                      x + off - dw / 2, y, scaleX,
                                      static_cast<float>(plantH) / static_cast<float>(sh),
                                      p.flip != 0, false, lean,
                                      tint, tintAmount, alpha);
            }
        }

        const int count = art::kScreenW * art::kScreenH;
        if (static_cast<int>(bgPixels_.size()) == count) {
            scenePixels_ = bgPixels_;
        } else {
            scenePixels_.assign(static_cast<std::size_t>(count), pack(art::kWaterDark));
        }
        for (int i = 0; i < count; ++i) {
            const Uint32 src = plantPixels_[i];
            const Uint32 a = src >> 24;
            if (a == 0) continue;
            if (a == 255) {
                scenePixels_[i] = src;
                continue;
            }
            const Uint32 dst = scenePixels_[i];
            const Uint32 ia = 255 - a;
            const Uint32 r = (((src >> 16) & 255) * a + ((dst >> 16) & 255) * ia) / 255;
            const Uint32 g = (((src >> 8) & 255) * a + ((dst >> 8) & 255) * ia) / 255;
            const Uint32 b = ((src & 255) * a + (dst & 255) * ia) / 255;
            scenePixels_[i] = (255u << 24) | (r << 16) | (g << 8) | b;
        }

        if (!scene_) {
            scene_ = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STATIC,
                                       art::kScreenW, art::kScreenH);
            if (scene_) SDL_SetTextureBlendMode(scene_, SDL_BLENDMODE_NONE);
        }
        if (scene_) {
            SDL_UpdateTexture(scene_, nullptr, scenePixels_.data(),
                              art::kScreenW * static_cast<int>(sizeof(Uint32)));
        }
    }

    // Shore shrubs drawn from the live list.  Growth, autumn colour, winter
    // bareness and fresh seedlings all read straight off the Shrub state.
    // They stand on the upper bank, against the sky, and are kept to roughly
    // a quarter to a third of the sky strip so most of the sky stays visible.
    void drawShrubs(const Ecosystem& eco) {
        const int n = eco.shrubCount();
        const float t = eco.animTime();
        for (int i = 0; i < n; ++i) {
            const auto& sh = eco.shrubAt(i);
            const int baseX = std::clamp(
                static_cast<int>(std::lround(sh.x)), kL + 2, kR - 2);
            const int baseY = bankTopAt(eco, baseX) - 1;
            const int band = std::max(8, baseY - kT);
            const int height = std::clamp(
                static_cast<int>(std::lround(
                    static_cast<float>(band) * (0.16f + 0.08f * sh.growth))), 3, 5);
            const int trunk = std::max(1, height * 3 / 5);
            const int rh = std::max(1, height - trunk);
            const float sway = std::sin(t * 0.55f + sh.swaySeed) * 0.8f;
            const int topX = baseX + static_cast<int>(std::lround(sway * sh.growth));

            for (int k = 0; k <= trunk; ++k) {
                const int x = (k >= trunk - 1) ? topX : baseX;
                px(x, baseY - k, art::kMudDark);
            }
            if (sh.leaf < 0.06f) {
                // Winter: bare twigs only, the root is dormant but alive.
                if (sh.growth > 0.30f) {
                    px(topX - 1, baseY - trunk, art::kMudDark);
                    px(topX + 1, baseY - trunk - 1, art::kMudDark);
                    px(topX, baseY - trunk - rh, art::kMudDark);
                }
                continue;
            }

            const int crownY = baseY - trunk;
            const int rw = 2 + static_cast<int>(std::lround(sh.growth * 2.0f));
            const SDL_Color moss = blend(art::kMoss, art::kRust, sh.colorBlend);
            const SDL_Color bright = blend(art::kLeaf, art::kAmber, sh.colorBlend);
            const SDL_Color dark = blend(art::kMossDark, art::kRust,
                                         sh.colorBlend * 0.70f);
            const int cover = static_cast<int>(std::lround(sh.leaf * 100.0f));
            for (int dy = -rh; dy <= rh; ++dy) {
                for (int dx = -rw; dx <= rw; ++dx) {
                    if (dx * dx * rh * rh + dy * dy * rw * rw > rw * rw * rh * rh) continue;
                    // The lower rim of the crown tucks into the bank instead of
                    // floating over it.
                    if (dy == rh) continue;
                    // Thinning crown as the leaves drop in autumn.
                    if (static_cast<int>(art::hash2(baseX + dx * 7,
                                                    baseY + dy * 13 + i) % 100u)
                        > cover) continue;
                    SDL_Color c = dark;
                    if (dy <= 0 && dx < rw / 2) c = moss;
                    if (dy == -rh && dx < 0
                        && (static_cast<int>(sh.seed) + dx) % 3 == 0) c = bright;
                    px(topX + dx, crownY + dy, c);
                }
            }
        }
    }

    void drawPlants(const Ecosystem& eco) {
        if (!scene_ || eco.animTime() - plantLayerTime_ >= 0.50f) {
            rebuildPlantLayer(eco);
            plantLayerTime_ = eco.animTime();
        }
    }

    void drawEggs(const Ecosystem& eco) {
        const auto* eggs = eco.eggs();
        for (int i = 0; i < eco.eggCount(); ++i) {
            const auto& e = eggs[i];
            const int frame = (eco.animFrame() + i) & 1;
            const int x = static_cast<int>(std::lround(e.x)) - 4;
            const int y = static_cast<int>(std::lround(e.y)) - 4;
            SDL_Color tint = art::kMudDark;
            float tintAmount = 0.0f;
            if (e.epiphyte > 0.03f) {
                tint = art::kMoss;
                tintAmount = std::min(0.45f, e.epiphyte * 0.75f);
            }
            art::drawSpriteScaled(r_, &art::kEggFrames[frame][0][0], 6, 6, 7,
                                  x, y, 1.65f, 1.65f, false, false, 0.0f,
                                  tint, tintAmount, 255);
        }
    }

    void drawCorpses(const Ecosystem& eco) {
        const auto* corpses = eco.corpses();
        for (int i = 0; i < eco.corpseCount(); ++i) {
            const auto& c = corpses[i];
            const char* src = nullptr;
            int sw = 16, sh = 16, stride = 17;
            switch (c.kind) {
            case 0: src = &art::kShrimpFrames[0][0][0]; break;
            case 1: src = &art::kFishFrames[0][0][0]; break;
            case 2: src = &art::kDamselFrames[0][0][0]; break;
            case 3: src = &art::kWormFrames[0][0][0]; break;
            default:
                src = &art::kTubifexFrames[0][0][0];
                sw = 8; sh = 8; stride = 9;
                break;
            }
            const float progress = c.age / c.duration;
            const float tintAmount = (c.kind == 4)
                                   ? std::clamp(0.35f + progress * 0.45f, 0.0f, 0.80f)
                                   : std::clamp(0.20f + progress * 0.58f, 0.0f, 0.82f);
            const Uint8 alpha = (progress < 0.74f)
                              ? 255
                              : static_cast<Uint8>(std::max(0.0f, 255.0f
                                * (1.0f - (progress - 0.74f) / 0.26f)));
            const int dw = std::max(1, static_cast<int>(std::lround(sw * c.scale)));
            const int dh = std::max(1, static_cast<int>(std::lround(sh * c.scale)));
            const int x = static_cast<int>(std::lround(c.x)) - dw / 2;
            const int y = static_cast<int>(std::lround(c.y)) - dh / 2;
            art::drawSpriteScaled(r_, src, sw, sh, stride, x, y,
                                  c.scale, c.scale, false, c.flipY,
                                  c.lean * progress,
                                  (c.kind == 4) ? art::kCream : art::kMudDark,
                                  tintAmount, alpha);
        }
    }

    void drawWorms(const Ecosystem& eco) {
        const auto* worms = eco.worms();
        for (int i = 0; i < eco.wormCount(); ++i) {
            const int frame = (eco.animFrame() + i) & 1;
            const float scale = Ecosystem::speciesBase(3)
                              * Ecosystem::visualScale(worms[i].growth) * worms[i].g.size;
            const int dh = std::max(1, static_cast<int>(std::lround(16 * scale)));
            const int x = static_cast<int>(std::lround(worms[i].x)) - dh / 2;
            const int y = groundY(static_cast<int>(std::lround(worms[i].x))) - dh;
            SDL_Color tint = art::kMudDark;
            float tintAmount = 0.0f;
            bodyTint(worms[i].g, 0.0f, tint, tintAmount);
            art::drawSpriteScaled(r_, &art::kWormFrames[frame][0][0], 16, 16, 17,
                                  x, y, scale, scale, false, false, 0.0f,
                                  tint, tintAmount, 255);
        }
    }

    static void bodyTint(const Ecosystem::Genes& g, float territoryBlend,
                         SDL_Color& tint, float& amount) {
        tint = art::kMudDark;
        amount = 0.0f;
        if (g.lineage > 0.10f) {
            tint = art::kAmber;
            amount = 0.10f * std::min(1.0f, std::fabs(g.lineage));
        } else if (g.lineage < -0.10f) {
            tint = art::kTeal;
            amount = 0.10f * std::min(1.0f, std::fabs(g.lineage));
        }
        if (territoryBlend > 0.0f) {
            tint = blend(tint, art::kAmber, std::min(1.0f, territoryBlend * 1.6f));
            amount = std::max(amount, 0.10f * territoryBlend);
        }
    }

    void drawTubifex(const Ecosystem& eco) {
        const auto* worms = eco.tubifex();
        for (int i = 0; i < eco.tubifexCount(); ++i) {
            const auto& t = worms[i];
            const int frame = (eco.animFrame() + i) & 1;
            const int x = static_cast<int>(std::lround(t.x)) - 4;
            const int y = groundY(static_cast<int>(std::lround(t.x))) - 8;
            SDL_Color tint = art::kMudDark;
            float tintAmount = 0.0f;
            bodyTint(t.g, 0.0f, tint, tintAmount);
            art::drawSpriteScaled(r_, &art::kTubifexFrames[frame][0][0], 8, 8, 9,
                                  x - 1, y, 1.15f, 1.15f, t.vx < 0.0f, false, 0.0f,
                                  tint, tintAmount, 255);
        }
    }

    void drawEpiphyte(const Ecosystem& eco) {
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        auto dot = [&](int x, int y, int alpha) {
            if (x < kL || x > kR || y < 7 || y > kB) return;
            SDL_SetRenderDrawColor(r_, art::kLeaf.r, art::kLeaf.g, art::kLeaf.b,
                                   static_cast<Uint8>(std::clamp(alpha, 20, 180)));
            SDL_RenderDrawPoint(r_, x, y);
        };

        const auto* plants = eco.plants();
        for (int i = 0; i < eco.plantCount(); ++i) {
            const auto& p = plants[i];
            if (p.epiphyte < 0.05f || p.dying) continue;
            const int count = static_cast<int>(p.epiphyte * 32.0f);
            const int cx = static_cast<int>(std::lround(p.x));
            const int gy = groundY(cx);
            for (int k = 0; k < count; ++k) {
                const unsigned h = art::hash2(i * 31 + k * 17, 9);
                const int dx = static_cast<int>(h % 17) - 8;
                const int dy = (p.type == 2) ? (4 + static_cast<int>((h >> 4) % 8))
                                             : (gy - 5 - static_cast<int>((h >> 4) % 28));
                dot(cx + dx, dy, 90 + static_cast<int>(p.epiphyte * 100.0f));
            }
        }

        auto animalDots = [&](float ax, float ay, float size, float epi, int seed) {
            if (epi < 0.05f) return;
            const int count = static_cast<int>(epi * 22.0f);
            for (int k = 0; k < count; ++k) {
                const unsigned h = art::hash2(seed * 53 + k * 11, 12);
                const int dx = static_cast<int>(h % 9) - 4;
                const int dy = static_cast<int>((h >> 4) % 8) - 4;
                dot(static_cast<int>(std::lround(ax)) + dx,
                    static_cast<int>(std::lround(ay)) + dy,
                    80 + static_cast<int>(epi * 100.0f));
            }
            (void)size;
        };
        for (int i = 0; i < eco.shrimpCount(); ++i) {
            const auto& s = eco.shrimp()[i];
            animalDots(s.x, s.y, s.g.size, s.epiphyte, i + 3);
        }
        for (int i = 0; i < eco.damselCount(); ++i) {
            const auto& d = eco.damsel()[i];
            animalDots(d.x, d.y, d.g.size, d.epiphyte, i + 19);
        }
        for (int i = 0; i < eco.fishCount(); ++i) {
            const auto& f = eco.fish()[i];
            animalDots(f.x, f.y, f.g.size, f.epiphyte, i + 41);
        }
        const auto* eggs = eco.eggs();
        for (int i = 0; i < eco.eggCount(); ++i) {
            const auto& e = eggs[i];
            if (e.epiphyte < 0.05f) continue;
            const int count = static_cast<int>(e.epiphyte * 16.0f);
            for (int k = 0; k < count; ++k) {
                const unsigned h = art::hash2(i * 71 + k * 13, 7);
                dot(static_cast<int>(std::lround(e.x)) + static_cast<int>(h % 5) - 2,
                    static_cast<int>(std::lround(e.y)) + static_cast<int>((h >> 4) % 5) - 2,
                    80 + static_cast<int>(e.epiphyte * 100.0f));
            }
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void drawWaterLevel(const Ecosystem& eco) {
        const int offset = eco.waterLevelOffset();
        const int level = art::kWaterTop + offset;
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
        // The exposed or flooded margin is already part of the far-bank
        // silhouette, so no flat rectangle is painted here; only the ticks
        // and the surface line.

        // Level ticks on both side walls make the slow seasonal rise and fall
        // readable even when the difference is only a few pixels.
        SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b, 96);
        for (int k = -2; k <= 2; ++k) {
            const int y = art::kWaterTop + k * 5;
            const int len = (k == 0) ? 5 : 3;
            SDL_RenderDrawLine(r_, kL + 1, y, kL + 1 + len, y);
            SDL_RenderDrawLine(r_, kR - 1 - len, y, kR - 1, y);
        }

        // Surface line: darker core plus a bright wet highlight.
        SDL_SetRenderDrawColor(r_, art::kTeal.r, art::kTeal.g, art::kTeal.b, 170);
        for (int x = kL + 1; x < kR; ++x) {
            const int y = level + static_cast<int>(std::lround(
                std::sin(x * 0.11f + eco.animTime() * 0.65f) * 0.6f));
            SDL_RenderDrawPoint(r_, x, y);
            SDL_RenderDrawPoint(r_, x, y + 1);
        }
        SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b, 70);
        for (int x = kL + 1; x < kR; x += 2) {
            const int y = level + static_cast<int>(std::lround(
                std::sin(x * 0.11f + eco.animTime() * 0.65f) * 0.6f));
            SDL_RenderDrawPoint(r_, x, y);
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    // Sprite mirroring follows a clear horizontal heading only.  A shrimp
    // hovering on its goal has vx oscillating around zero, and testing vx<0
    // every frame mirrored the sprite back and forth, which read as whipping
    // its head left and right.  Near standstill the previous heading is kept.
    bool facesLeft(uint32_t id, float vx) {
        const auto it = facing_.find(id);
        int dir = (it == facing_.end()) ? (vx < 0.0f ? -1 : 1) : it->second;
        if (vx < -0.9f) dir = -1;
        else if (vx > 0.9f) dir = 1;
        facing_[id] = dir;
        return dir < 0;
    }

    void drawShrimp(const Ecosystem& eco) {
        const auto* shrimp = eco.shrimp();
        for (int i = 0; i < eco.shrimpCount(); ++i) {
            const auto& s = shrimp[i];
            const int frame = (eco.animFrame() + i) & 1;
            const float scale = Ecosystem::speciesBase(0)
                              * Ecosystem::visualScale(s.growth) * s.g.size;
            const int dw = std::max(1, static_cast<int>(std::lround(16 * scale)));
            const int dh = std::max(1, static_cast<int>(std::lround(16 * scale)));
            const int x = static_cast<int>(std::lround(s.x)) - dw / 2;
            const int y = static_cast<int>(std::lround(s.y)) - dh / 2;
            SDL_Color tint = art::kMudDark;
            float tintAmount = 0.0f;
            bodyTint(s.g, 0.0f, tint, tintAmount);
            if (s.epiphyte > 0.001f) {
                tint = blend(tint, art::kMoss, std::min(1.0f, s.epiphyte * 2.2f));
                tintAmount = std::max(tintAmount, 0.10f * std::min(1.0f, s.epiphyte * 2.0f));
            }
            const float stress = eco.stressAt(s.x, s.y, 0, s.g);
            if (stress > 0.22f) {
                tint = blend(tint, art::kMudLight, std::min(1.0f, (stress - 0.22f) * 1.6f));
                tintAmount = std::max(tintAmount, 0.10f + 0.12f * stress);
            }
            // A failing animal pales before it dies, so decline is visible.
            const float weak = std::clamp((0.30f - s.health) / 0.30f, 0.0f, 1.0f);
            if (weak > 0.01f) {
                tint = blend(tint, art::kCream, 0.55f * weak);
                tintAmount = std::max(tintAmount, 0.45f * weak);
            }
            art::drawSpriteScaled(r_, &art::kShrimpFrames[frame][0][0], 16, 16, 17,
                                  x, y, scale, scale, facesLeft(s.id, s.vx), false, 0.0f,
                                  tint, tintAmount, 255);
        }
    }

    void drawDamsel(const Ecosystem& eco) {
        const auto* damsel = eco.damsel();
        for (int i = 0; i < eco.damselCount(); ++i) {
            const auto& d = damsel[i];
            const int frame = (eco.animFrame() + i) & 1;
            const float scale = Ecosystem::speciesBase(2)
                              * Ecosystem::visualScale(d.growth) * d.g.size;
            const int dw = std::max(1, static_cast<int>(std::lround(16 * scale)));
            const int dh = std::max(1, static_cast<int>(std::lround(16 * scale)));
            const int x = static_cast<int>(std::lround(d.x)) - dw / 2;
            const int y = static_cast<int>(std::lround(d.y)) - dh / 2;
            SDL_Color tint = art::kMudDark;
            float tintAmount = 0.0f;
            bodyTint(d.g, 0.0f, tint, tintAmount);
            if (d.epiphyte > 0.001f) {
                tint = blend(tint, art::kMoss, std::min(1.0f, d.epiphyte * 2.2f));
                tintAmount = std::max(tintAmount, 0.10f * std::min(1.0f, d.epiphyte * 2.0f));
            }
            const float stress = eco.stressAt(d.x, d.y, 2, d.g);
            if (stress > 0.22f) {
                tint = blend(tint, art::kMudLight, std::min(1.0f, (stress - 0.22f) * 1.6f));
                tintAmount = std::max(tintAmount, 0.10f + 0.12f * stress);
            }
            const float weak = std::clamp((0.30f - d.health) / 0.30f, 0.0f, 1.0f);
            if (weak > 0.01f) {
                tint = blend(tint, art::kCream, 0.55f * weak);
                tintAmount = std::max(tintAmount, 0.45f * weak);
            }
            art::drawSpriteScaled(r_, &art::kDamselFrames[frame][0][0], 16, 16, 17,
                                  x, y, scale, scale, facesLeft(d.id, d.vx), false, 0.0f,
                                  tint, tintAmount, 255);
        }
    }

    void drawFish(const Ecosystem& eco) {
        const auto* fish = eco.fish();
        for (int i = 0; i < eco.fishCount(); ++i) {
            const auto& f = fish[i];
            const int frame = (eco.animFrame() + i) & 1;
            const float scale = Ecosystem::speciesBase(1)
                              * Ecosystem::visualScale(f.growth) * f.g.size;
            const int dw = std::max(1, static_cast<int>(std::lround(16 * scale)));
            const int dh = std::max(1, static_cast<int>(std::lround(16 * scale)));
            const int x = static_cast<int>(std::lround(f.x)) - dw / 2;
            const int y = static_cast<int>(std::lround(f.y)) - dh / 2;
            SDL_Color tint = art::kMudDark;
            float tintAmount = 0.0f;
            bodyTint(f.g, f.territoryBlend, tint, tintAmount);
            if (f.epiphyte > 0.001f) {
                tint = blend(tint, art::kMoss, std::min(1.0f, f.epiphyte * 2.2f));
                tintAmount = std::max(tintAmount, 0.10f * std::min(1.0f, f.epiphyte * 2.0f));
            }
            const float stress = eco.stressAt(f.x, f.y, 1, f.g);
            if (stress > 0.22f) {
                tint = blend(tint, art::kMudLight, std::min(1.0f, (stress - 0.22f) * 1.6f));
                tintAmount = std::max(tintAmount, 0.10f + 0.12f * stress);
            }
            const float weak = std::clamp((0.30f - f.health) / 0.30f, 0.0f, 1.0f);
            if (weak > 0.01f) {
                tint = blend(tint, art::kCream, 0.55f * weak);
                tintAmount = std::max(tintAmount, 0.45f * weak);
            }

            art::drawSpriteScaled(r_, &art::kFishFrames[frame][0][0], 16, 16, 17,
                                  x, y, scale, scale, facesLeft(f.id, f.vx), false, 0.0f,
                                  tint, tintAmount, 255);
        }
    }

    void drawGlassFrame(const Ecosystem& eco) {
        // The 3D glass rim is baked into the background.  Here we only add
        // moving reflections and condensation so it feels like glass, not a
        // painted rectangle.
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);

        const int glare = 5 + static_cast<int>(eco.light() / 17.0f);
        for (int y = 10; y < 42; ++y) {
            const int a = std::max(3, glare - (y - 10) / 4);
            SDL_SetRenderDrawColor(r_, art::kTeal.r, art::kTeal.g, art::kTeal.b,
                                   static_cast<Uint8>(a));
            SDL_RenderDrawLine(r_, 7, y, 9, y);
        }

        // Diagonal glint across the top-left glass corner.
        SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b, 16);
        for (int k = 0; k < 24; ++k) {
            SDL_RenderDrawPoint(r_, 8 + k, 5 + k);
        }

        // A few broken specular dashes on the top bevel.
        SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b, 95);
        for (int x = 24; x < 304; x += 37) {
            SDL_RenderDrawPoint(r_, x, 3);
            SDL_RenderDrawPoint(r_, x + 1, 3);
        }
        SDL_SetRenderDrawColor(r_, art::kTeal.r, art::kTeal.g, art::kTeal.b, 100);
        for (int x = 42; x < 300; x += 51) {
            SDL_RenderDrawPoint(r_, x, 4);
        }

        // Condensation beads cling to the air band.
        for (int i = 0; i < 5; ++i) {
            const int dx = 28 + i * 63;
            const int dy = 7 + (i * 3) % 5;
            SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b, 130);
            SDL_RenderDrawPoint(r_, dx, dy);
            SDL_SetRenderDrawColor(r_, art::kTeal.r, art::kTeal.g, art::kTeal.b, 100);
            SDL_RenderDrawPoint(r_, dx, dy + 1);
        }
        SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_NONE);
    }

    void edgePixel(int x, int y, char ink) {
        if (x < 0 || x >= art::kScreenW || y < 0 || y >= art::kScreenH) return;
        if (ink != edgeInk_) {
            art::setInk(r_, ink);
            edgeInk_ = ink;
        }
        SDL_RenderDrawPoint(r_, x, y);
    }

    template <typename Put>
    void emitEdgePlants(float t, Put&& put) {
        auto blade = [&](int baseX, int height, float lean, float phase,
                         char baseInk, char midInk, char tipInk, int leafDir) {
            const int base = groundY(baseX);
            for (int k = 0; k < height; ++k) {
                const float u = static_cast<float>(k) / static_cast<float>(height);
                const int x = baseX + static_cast<int>(std::lround(
                    lean * k + std::sin(static_cast<float>(k) * 0.075f + phase) * 4.0f
                    + std::sin(t * 0.35f + phase) * u * 3.0f));
                const int y = base - k;
                const char ink = (k > height - 7) ? tipInk
                               : (k > height / 2 ? midInk : baseInk);
                put(x, y, ink);
                if (u < 0.30f) put(x + 1, y, baseInk);

                if (k % 7 == 2) {
                    const int dir = ((k / 7 + leafDir) & 1) ? 1 : -1;
                    put(x + dir, y, midInk);
                    put(x + dir * 2, y - 1, tipInk);
                    put(x + dir * 3, y - 1, midInk);
                    put(x + dir * 4, y - 2, tipInk);
                }
                if (k % 11 == 6) {
                    const int dir = -((k / 11 + leafDir) & 1 ? 1 : -1);
                    put(x + dir, y, midInk);
                    put(x + dir * 2, y - 1, tipInk);
                }
            }
        };

        // Left and right border reeds are kept as before: they climb past the
        // glass and frame the bottle at its two edges, leaving the middle of
        // the sky, the weather and the shore shrubs unobstructed.
        blade(7, 112, 0.22f, 0.4f, art::kInkMossDark, art::kInkGreen, art::kInkLeaf, 0);
        blade(12, 96, -0.16f, 1.7f, art::kInkMossDark, art::kInkGreen, art::kInkLeaf, 1);
        blade(20, 82, 0.30f, 3.1f, art::kInkMossDark, art::kInkGreen, art::kInkLeaf, 0);
        blade(4, 70, 0.04f, 5.0f, art::kInkMossDark, art::kInkGreen, art::kInkLeaf, 1);
        blade(312, 110, -0.22f, 2.2f, art::kInkMossDark, art::kInkGreen, art::kInkLeaf, 0);
        blade(306, 94, 0.16f, 0.9f, art::kInkMossDark, art::kInkGreen, art::kInkLeaf, 1);
        blade(298, 78, -0.30f, 4.0f, art::kInkMossDark, art::kInkGreen, art::kInkLeaf, 0);
        blade(316, 66, -0.03f, 5.5f, art::kInkMossDark, art::kInkGreen, art::kInkLeaf, 1);

        for (int i = 0; i < 24; ++i) {
            const int x = 4 + i * 13;
            if (x > 316) break;
            const int h = 1 + static_cast<int>(art::hash2(i, 7) % 4u);
            for (int k = 0; k < h; ++k) {
                const int y = 114 + k;
                put(x + (k & 1), y,
                    (k == h - 1) ? art::kInkLeaf : art::kInkMossDark);
            }
            if (i % 4 == 0) {
                put(x + 1, 117, art::kInkMossDark);
                put(x + 2, 118, art::kInkGreen);
            }
        }
    }

    void drawEdgePlants(const Ecosystem& eco) {
        const int tick = static_cast<int>(eco.animTime() / kEdgeFrameStep);
        const int cycle = tick % (2 * kEdgeFrameCount);
        const int index = (cycle < kEdgeFrameCount)
                        ? cycle : (2 * kEdgeFrameCount - 1 - cycle);
        if (index >= 0 && index < static_cast<int>(edgeFrames_.size()) && edgeFrames_[index]) {
            SDL_RenderCopy(r_, edgeFrames_[index], nullptr, nullptr);
            return;
        }
        emitEdgePlants(eco.animTime(), [&](int x, int y, char ink) {
            edgePixel(x, y, ink);
        });
    }

    void bakeEdgeFrames() {
        edgeFrames_.assign(kEdgeFrameCount, nullptr);
        for (int f = 0; f < kEdgeFrameCount; ++f) {
            std::vector<Uint32> pixels(static_cast<std::size_t>(art::kScreenW) * art::kScreenH, 0);
            emitEdgePlants(static_cast<float>(f) * kEdgeFrameStep,
                           [&](int x, int y, char ink) {
                if (x < 0 || x >= art::kScreenW || y < 0 || y >= art::kScreenH) return;
                pixels[static_cast<std::size_t>(y) * art::kScreenW + x] =
                    pack(art::inkColor(ink), 255);
            });
            SDL_Texture* texture = SDL_CreateTexture(r_, SDL_PIXELFORMAT_ARGB8888,
                                                     SDL_TEXTUREACCESS_STATIC,
                                                     art::kScreenW, art::kScreenH);
            if (!texture) continue;
            if (SDL_UpdateTexture(texture, nullptr, pixels.data(),
                                  art::kScreenW * static_cast<int>(sizeof(Uint32))) != 0) {
                SDL_DestroyTexture(texture);
                continue;
            }
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            edgeFrames_[f] = texture;
        }
    }

    void drawPauseMark() {
        SDL_SetRenderDrawColor(r_, art::kDeep.r, art::kDeep.g, art::kDeep.b, 255);
        const SDL_Rect shadowA{302, 7, 3, 8};
        const SDL_Rect shadowB{307, 7, 3, 8};
        SDL_RenderFillRect(r_, &shadowA);
        SDL_RenderFillRect(r_, &shadowB);

        SDL_SetRenderDrawColor(r_, art::kCream.r, art::kCream.g, art::kCream.b, 255);
        const SDL_Rect barA{301, 6, 2, 8};
        const SDL_Rect barB{306, 6, 2, 8};
        SDL_RenderFillRect(r_, &barA);
        SDL_RenderFillRect(r_, &barB);
    }

    SDL_Renderer* r_ = nullptr;
    SDL_Texture* bg_ = nullptr;
    // Last rendered heading per individual, so mirroring only changes on a
    // real direction change rather than on sub-pixel velocity noise.
    std::unordered_map<uint32_t, int> facing_;
    SDL_Texture* scene_ = nullptr;
    std::vector<Uint32> bgPixels_;
    std::vector<Uint32> plantPixels_;
    std::vector<Uint32> scenePixels_;
    float plantLayerTime_ = -1.0f;
    int surfaceTop_ = art::kWaterTop;
    std::vector<SDL_Texture*> edgeFrames_;
    char edgeInk_ = 0;
    int previewMode_ = 0;    // 0 off, 1 profile, 2 decor
    int previewKind_ = 0;    // decor sub-kind, 3 = erase
    int previewX_ = -1;
    int previewY_ = -1;
};

// =============================================================================
//  5. Win32 window plumbing
// =============================================================================
namespace platform {

// Per-pixel layered windows normally let the mouse fall through pixels whose
// alpha is very low.  Both the bottle and the observation panel must accept
// clicks on their empty glass, so every subclassed window hit-tests as
// HTCLIENT.  Original window procs are kept per HWND because there are two
// layered windows in this process.
std::unordered_map<HWND, WNDPROC> g_oldWndProcs;

LRESULT CALLBACK BottleWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
#ifdef ECO_DEBUG_STATS
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) {
        std::fprintf(stderr, "[wnd] %s\n", msg == WM_LBUTTONDOWN ? "LBUTTONDOWN" : "RBUTTONDOWN");
        std::fflush(stderr);
    }
#endif
    if (msg == WM_NCHITTEST) {
        return HTCLIENT;
    }
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) {
        // A desktop widget may not be the active window.  Make sure the first
        // click (left or right) both activates it and reaches SDL's handler.
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }
    if (msg == WM_MOUSEACTIVATE) {
        return MA_ACTIVATE;
    }
    WNDPROC old = nullptr;
    const auto it = g_oldWndProcs.find(hwnd);
    if (it != g_oldWndProcs.end()) {
        old = it->second;
    }
    return old ? CallWindowProcW(old, hwnd, msg, wParam, lParam)
               : DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool installHitTest(HWND hwnd) {
    if (!hwnd) {
        return false;
    }
    if (g_oldWndProcs.count(hwnd) != 0) {
        return true;
    }
    WNDPROC old = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(BottleWndProc)));
    if (!old) {
        return false;
    }
    g_oldWndProcs[hwnd] = old;
    return true;
}

// Turn the SDL window into a layered, topmost tool window.  Content is
// supplied later with UpdateLayeredWindow (per-pixel alpha).  A WndProc
// subclass below forces the whole rectangle to hit-test, not just opaque art.
bool makeLayeredTopmost(SDL_Window* window, HWND& outHwnd) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) {
        return false;
    }
    if (info.subsystem != SDL_SYSWM_WINDOWS) {
        return false;
    }

    HWND hwnd = info.info.win.window;
    outHwnd = hwnd;

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_LAYERED | WS_EX_TOOLWINDOW;  // no taskbar button
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return true;
}

// Owns a top-down 32-bit DIB section plus a persistent native-size SDL surface
// that SDL draws into.  present() copies (or nearest-neighbour upscales) the
// surface into the DIB and hands it to UpdateLayeredWindow.  Keeping the draw
// surface at art resolution means changing the on-screen scale never has to
// rebuild the SDL renderer or any of the scene objects.
class LayeredCanvas {
public:
    LayeredCanvas(HWND hwnd, int width, int height)
        : hwnd_(hwnd), baseW_(width), baseH_(height) {
        memDC_ = CreateCompatibleDC(nullptr);
        if (!memDC_) {
            return;
        }

        surface_ = SDL_CreateRGBSurfaceWithFormat(
            0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
        if (surface_) {
            SDL_FillRect(surface_, nullptr, 0);  // fully transparent
        }

        setScale(1);
    }

    ~LayeredCanvas() { release(); }

    LayeredCanvas(const LayeredCanvas&) = delete;
    LayeredCanvas& operator=(const LayeredCanvas&) = delete;

    // Rebuilds only the presentation bitmap.  scale_ is 1 or 2.
    bool setScale(int scale) {
        scale = scale >= 2 ? 2 : 1;
        if (dib_ && scale == scale_) {
            return true;
        }
        releaseBitmap();
        scale_ = scale;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = pixelWidth();
        bmi.bmiHeader.biHeight = -pixelHeight();  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dib_ = CreateDIBSection(memDC_, &bmi, DIB_RGB_COLORS, &bits_, nullptr, 0);
        if (!dib_) {
            return false;
        }
        oldBitmap_ = SelectObject(memDC_, dib_);
        return true;
    }

    void release() {
        if (surface_) {
            SDL_FreeSurface(surface_);
            surface_ = nullptr;
        }
        releaseBitmap();
        if (memDC_) {
            DeleteDC(memDC_);
            memDC_ = nullptr;
        }
        bits_ = nullptr;
    }

    bool valid() const { return surface_ != nullptr && dib_ != nullptr; }

    // The surface SDL draws into; always art resolution.
    SDL_Surface* surface() const { return surface_; }

    int scale() const { return scale_; }
    int pixelWidth() const { return baseW_ * scale_; }
    int pixelHeight() const { return baseH_ * scale_; }

    bool present(int alpha = 255) {
        if (!surface_ || !dib_ || !hwnd_ || !memDC_) {
            return false;
        }

        const int sw = baseW_;
        const int sh = baseH_;
        const int dw = pixelWidth();
        const int dh = pixelHeight();
        const Uint32* src = static_cast<const Uint32*>(surface_->pixels);
        Uint32* dst = static_cast<Uint32*>(bits_);
        const int spitch = surface_->pitch / 4;

        if (scale_ == 1) {
            for (int y = 0; y < sh; ++y) {
                std::memcpy(dst + y * dw, src + y * spitch,
                            static_cast<size_t>(sw) * 4);
            }
        } else {
            for (int y = 0; y < dh; ++y) {
                const Uint32* row = src + (y / scale_) * spitch;
                Uint32* out = dst + y * dw;
                for (int x = 0; x < dw; ++x) {
                    out[x] = row[x / scale_];
                }
            }
        }

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = static_cast<BYTE>(std::clamp(alpha, 0, 255));
        blend.AlphaFormat = AC_SRC_ALPHA;

        POINT srcPoint{0, 0};
        SIZE size{dw, dh};
        return UpdateLayeredWindow(hwnd_, nullptr, nullptr, &size, memDC_,
                                   &srcPoint, 0, &blend, ULW_ALPHA) != 0;
    }

private:
    void releaseBitmap() {
        if (memDC_ && oldBitmap_) {
            SelectObject(memDC_, oldBitmap_);
            oldBitmap_ = nullptr;
        }
        if (dib_) {
            DeleteObject(dib_);
            dib_ = nullptr;
        }
        bits_ = nullptr;
    }

    HWND hwnd_ = nullptr;
    int baseW_ = 0;
    int baseH_ = 0;
    int scale_ = 1;
    HDC memDC_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* bits_ = nullptr;
    SDL_Surface* surface_ = nullptr;
};

// The old native popup is gone: the observation panel is drawn entirely in
// software pixels and lives in its own layered window.

void placeAtDesktopCorner(SDL_Window* window) {
    SDL_Rect usable{0, 0, 0, 0};
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0 && usable.w > 0 && usable.h > 0) {
        const int x = usable.x + usable.w - scaledScreenW() - 28;
        const int y = usable.y + usable.h - scaledScreenH() - 24;
        SDL_SetWindowPosition(window, x, y);
    } else {
        SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
}

void keepOnDesktop(SDL_Window* window) {
    SDL_Rect usable{0, 0, 0, 0};
    const int display = SDL_GetWindowDisplayIndex(window);
    if (SDL_GetDisplayUsableBounds(display, &usable) != 0
        || usable.w <= 0 || usable.h <= 0) {
        return;
    }
    int x = 0;
    int y = 0;
    SDL_GetWindowPosition(window, &x, &y);
    const int w = scaledScreenW();
    const int h = scaledScreenH();
    const int maxX = std::max(usable.x, usable.x + usable.w - w);
    const int maxY = std::max(usable.y, usable.y + usable.h - h);
    x = std::clamp(x, usable.x, maxX);
    y = std::clamp(y, usable.y, maxY);
    SDL_SetWindowPosition(window, x, y);
}

void setTopmost(HWND hwnd, bool on) {
    if (!hwnd) return;
    SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void setClickThrough(HWND hwnd, bool on) {
    if (!hwnd) return;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (on) {
        ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
    } else {
        ex &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    }
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                 | SWP_FRAMECHANGED);
}

} // namespace platform

// =============================================================================
//  6. Main loop
// =============================================================================
namespace {

void showFatal(const char* message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Native Swamp Bottle", message, nullptr);
}

// =============================================================================
//  6. Pixel observation panel
// =============================================================================
namespace panelart {

constexpr int kPanelW = 420;
constexpr int kPanelH = 528;

constexpr Uint32 rgba(int r, int g, int b, int a = 255) {
    return (static_cast<Uint32>(a) << 24) | (static_cast<Uint32>(r) << 16)
         | (static_cast<Uint32>(g) << 8) | static_cast<Uint32>(b);
}

constexpr Uint32 kGlassTop  = rgba(22, 55, 47, 250);
constexpr Uint32 kGlassBot  = rgba(9, 28, 24, 250);
constexpr Uint32 kEdgeDark  = rgba(5, 14, 12);
constexpr Uint32 kEdge      = rgba(58, 105, 88);
constexpr Uint32 kInset     = rgba(6, 20, 18);
constexpr Uint32 kLine      = rgba(38, 73, 62);
constexpr Uint32 kCream     = rgba(236, 226, 194);
constexpr Uint32 kDim       = rgba(148, 168, 143);
constexpr Uint32 kFaint     = rgba(96, 120, 104);
constexpr Uint32 kMoss      = rgba(126, 174, 104);
constexpr Uint32 kMossDark  = rgba(74, 115, 66);
constexpr Uint32 kAmber     = rgba(218, 165, 68);
constexpr Uint32 kRust      = rgba(190, 84, 58);
constexpr Uint32 kCyan      = rgba(124, 188, 174);
constexpr Uint32 kIce       = rgba(186, 216, 202);
constexpr Uint32 kShadow    = rgba(2, 8, 7);
constexpr Uint32 kMetal     = rgba(160, 172, 150);

inline Uint32 lerpColor(Uint32 a, Uint32 b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const int ar = static_cast<int>((a >> 16) & 255);
    const int ag = static_cast<int>((a >> 8) & 255);
    const int ab = static_cast<int>(a & 255);
    const int br = static_cast<int>((b >> 16) & 255);
    const int bg = static_cast<int>((b >> 8) & 255);
    const int bb = static_cast<int>(b & 255);
    return rgba(static_cast<int>(ar + (br - ar) * t),
                static_cast<int>(ag + (bg - ag) * t),
                static_cast<int>(ab + (bb - ab) * t));
}

inline void blendOver(Uint32* pixels, int index, Uint32 color, int alpha) {
    if (alpha <= 0) return;
    const Uint32 dst = pixels[index];
    const int dr = static_cast<int>((dst >> 16) & 255);
    const int dg = static_cast<int>((dst >> 8) & 255);
    const int db = static_cast<int>(dst & 255);
    const int sr = static_cast<int>((color >> 16) & 255);
    const int sg = static_cast<int>((color >> 8) & 255);
    const int sb = static_cast<int>(color & 255);
    const int r = (sr * alpha + dr * (255 - alpha)) / 255;
    const int g = (sg * alpha + dg * (255 - alpha)) / 255;
    const int b = (sb * alpha + db * (255 - alpha)) / 255;
    pixels[index] = rgba(r, g, b);
}

inline bool insideRounded(int x, int y, int w, int h, int r) {
    int cx = 0;
    int cy = 0;
    if (x < r) cx = r - x;
    else if (x >= w - r) cx = x - (w - r - 1);
    if (y < r) cy = r - y;
    else if (y >= h - r) cy = y - (h - r - 1);
    return cx * cx + cy * cy <= r * r;
}

inline void fillRounded(Uint32* pixels, int pitch, int x, int y, int w, int h,
                        int r, Uint32 color) {
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            if (!insideRounded(xx, yy, w, h, r)) continue;
            pixels[(y + yy) * pitch + x + xx] = color;
        }
    }
}

inline void fillRect(Uint32* pixels, int pitch, int x, int y, int w, int h,
                     Uint32 color) {
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            pixels[(y + yy) * pitch + x + xx] = color;
        }
    }
}

inline void hLine(Uint32* pixels, int pitch, int x0, int x1, int y, Uint32 color) {
    if (x1 < x0) std::swap(x0, x1);
    for (int x = x0; x <= x1; ++x) pixels[y * pitch + x] = color;
}

inline void vLine(Uint32* pixels, int pitch, int x, int y0, int y1, Uint32 color) {
    if (y1 < y0) std::swap(y0, y1);
    for (int y = y0; y <= y1; ++y) pixels[y * pitch + x] = color;
}

inline void plotLine(Uint32* pixels, int pitch, int x0, int y0, int x1, int y1,
                     Uint32 color) {
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixels[y0 * pitch + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        const int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

inline void frameRect(Uint32* pixels, int pitch, int x, int y, int w, int h,
                      Uint32 color) {
    hLine(pixels, pitch, x, x + w - 1, y, color);
    hLine(pixels, pitch, x, x + w - 1, y + h - 1, color);
    vLine(pixels, pitch, x, y, y + h - 1, color);
    vLine(pixels, pitch, x + w - 1, y, y + h - 1, color);
}

// A 1 Hz ring of values, oldest first when read through at().
struct History {
    std::array<float, 60> light{};
    std::array<float, 60> oxygen{};
    std::array<float, 60> temp{};
    int head = 0;
    int count = 0;

    void push(float l, float o, float t) {
        light[head] = l;
        oxygen[head] = o;
        temp[head] = t;
        head = (head + 1) % 60;
        count = std::min(60, count + 1);
    }
    void clear() {
        head = 0;
        count = 0;
    }
    float at(const std::array<float, 60>& values, int i) const {
        if (count == 0) return 0.0f;
        const int idx = (head - count + i + 120) % 60;
        return values[idx];
    }
};

// Bitmap text generated from the system SimSun font at 12/15 px, then
// thresholded to 1-bit.  SimSun's small sizes are true bitmap glyphs, so the
// result stays crisp and pixel-sized while still supporting Chinese labels.
class PixelText {
public:
    PixelText() {
        HDC screen = GetDC(nullptr);
        mem_ = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        if (!mem_) return;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = kAtlasW;
        bmi.bmiHeader.biHeight = -kAtlasH;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        dib_ = CreateDIBSection(mem_, &bmi, DIB_RGB_COLORS, &bits_, nullptr, 0);
        if (!dib_) return;
        old_ = SelectObject(mem_, dib_);

        font_ = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"SimSun");
        bold_ = CreateFontW(-15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"SimSun");
        if (!font_) font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        if (!bold_) bold_ = font_;
        SelectObject(mem_, font_);
        SetBkMode(mem_, OPAQUE);
        SetBkColor(mem_, RGB(0, 0, 0));
        SetTextColor(mem_, RGB(255, 255, 255));
    }

    ~PixelText() {
        if (mem_ && old_) SelectObject(mem_, old_);
        if (font_) DeleteObject(font_);
        if (bold_ && bold_ != font_) DeleteObject(bold_);
        if (dib_) DeleteObject(dib_);
        if (mem_) DeleteDC(mem_);
    }

    PixelText(const PixelText&) = delete;
    PixelText& operator=(const PixelText&) = delete;

    bool valid() const { return mem_ && dib_ && bits_; }

    int width(const std::wstring& text, bool bold = false) {
        return mask(text, bold).w;
    }

    void draw(Uint32* pixels, int pitch, int clipW, int clipH, int x, int y,
              const std::wstring& text, Uint32 color, bool bold = false,
              int shadow = 0) {
        const Mask& m = mask(text, bold);
        if (m.w <= 0 || m.h <= 0) return;
        if (shadow) {
            for (int yy = 0; yy < m.h; ++yy) {
                const int py = y + 1 + yy;
                if (py < 0 || py >= clipH) continue;
                for (int xx = 0; xx < m.w; ++xx) {
                    if (!m.alpha[static_cast<size_t>(yy) * m.w + xx]) continue;
                    const int px = x + 1 + xx;
                    if (px < 0 || px >= clipW) continue;
                    blendOver(pixels, py * pitch + px, kShadow, 150);
                }
            }
        }
        for (int yy = 0; yy < m.h; ++yy) {
            const int py = y + yy;
            if (py < 0 || py >= clipH) continue;
            for (int xx = 0; xx < m.w; ++xx) {
                if (!m.alpha[static_cast<size_t>(yy) * m.w + xx]) continue;
                const int px = x + xx;
                if (px < 0 || px >= clipW) continue;
                pixels[py * pitch + px] = color;
            }
        }
    }

private:
    struct Mask {
        int w = 0;
        int h = 0;
        std::vector<unsigned char> alpha;
    };

    const Mask& mask(const std::wstring& text, bool bold) {
        static const Mask empty;
        if (!valid() || text.empty()) return empty;

        std::wstring key;
        key.push_back(bold ? L'\1' : L'\2');
        key += text;
        const auto found = cache_.find(key);
        if (found != cache_.end()) return found->second;
        if (cache_.size() > 700) cache_.clear();

        std::memset(bits_, 0, static_cast<size_t>(kAtlasW) * kAtlasH * 4);
        HFONT face = bold ? bold_ : font_;
        SelectObject(mem_, face);
        SetBkMode(mem_, OPAQUE);
        SetBkColor(mem_, RGB(0, 0, 0));
        SetTextColor(mem_, RGB(255, 255, 255));
        TextOutW(mem_, 1, 1, text.c_str(), static_cast<int>(text.size()));

        SIZE sz{};
        GetTextExtentPoint32W(mem_, text.c_str(), static_cast<int>(text.size()), &sz);
        Mask m;
        m.w = std::clamp(static_cast<int>(sz.cx) + 3, 1, kAtlasW);
        m.h = std::clamp(static_cast<int>(sz.cy) + 3, 1, kAtlasH);
        m.alpha.assign(static_cast<size_t>(m.w) * m.h, 0);

        const unsigned char* src = static_cast<const unsigned char*>(bits_);
        for (int yy = 0; yy < m.h; ++yy) {
            for (int xx = 0; xx < m.w; ++xx) {
                const unsigned char* p = src + (static_cast<size_t>(yy) * kAtlasW + xx) * 4;
                const int value = std::max({p[0], p[1], p[2]});
                m.alpha[static_cast<size_t>(yy) * m.w + xx] = value >= 96 ? 255 : 0;
            }
        }
        const auto inserted = cache_.emplace(key, std::move(m));
        return inserted.first->second;
    }

    static constexpr int kAtlasW = 768;
    static constexpr int kAtlasH = 48;

    HDC mem_ = nullptr;
    HBITMAP dib_ = nullptr;
    HGDIOBJ old_ = nullptr;
    void* bits_ = nullptr;
    HFONT font_ = nullptr;
    HFONT bold_ = nullptr;
    std::unordered_map<std::wstring, Mask> cache_;
};

// ---- 10x10 icons -----------------------------------------------------------
// '#' primary, '+' accent, '-' faint, '.' transparent.

const char* const kIcoSun[10] = {
    "..#...#..", "...#.#...", "..#####..", ".##+++##.",
    "#.#+++#.#", "#.#+++#.#", ".##+++##.", "..#####..",
    "...#.#...", "..#...#..",
};
const char* const kIcoO2[10] = {
    "....##....", "..#....#..", ".#..++..#.", "#..++++..#",
    "#..++++..#", "#..++++..#", ".#..++..#.", "..#....#..",
    "....##....", "..........",
};
const char* const kIcoTemp[10] = {
    "....##....", "....##....", "....##....", "....##....",
    "....##....", "...####...", "..######..", "..#++++#..",
    "..######..", "...####...",
};
const char* const kIcoWater[10] = {
    "..........", ".####.....", "#....#.###", "......#...",
    "..........", ".####.....", "#....#.###", "......#...",
    "..........", "..........",
};
const char* const kIcoDrop[10] = {
    "....##....", "....##....", "...#++#...", "...#++#...",
    "..#++++#..", "..#++++#..", ".#++++++#.", ".#++++++#.",
    "..#++++#..", "...####...",
};
const char* const kIcoWarn[10] = {
    "....##....", "...#..#...", "...#..#...", "..#.##.#..",
    "..#.##.#..", ".#..##..#.", ".#..##..#.", "#...##...#",
    "#........#", ".########.",
};
const char* const kIcoLeaf[10] = {
    ".......#..", "......##..", ".....##+..", "....##++..",
    "...##+++..", "..##++++..", ".##+++++..", ".#+++++#..",
    "..#+++#...", "...#.#....",
};
const char* const kIcoPlant[10] = {
    "....#.....", "...#.#....", "..#.#.#...", "..#.#.#...",
    "..#.#.#...", ".#..#.#.#.", ".#..#.#.#.", "#...#.#..#",
    "#...#.#..#", "#...#.#..#",
};
const char* const kIcoBactA[10] = {
    "..........", "..##...##.", ".#++#.#++#", ".#++#.#++#",
    "..##...##.", ".....##...", "....#++#..", "....#++#..",
    ".....##...", "..........",
};
const char* const kIcoBactN[10] = {
    "..........", ".##....##.", "#++#..#++#", "#++#..#++#",
    ".##....##.", "...##.....", "..#++#....", "..#++#....",
    "...##.....", "..........",
};
const char* const kIcoShrimp[10] = {
    "...##.....", "..#++#....", "..#+++#...", "...#+++#..",
    "....#+++#.", "....#++#..", "...#+#....", "..#+#.....",
    ".##.......", "#.........",
};
const char* const kIcoDamsel[10] = {
    ".........#", "........#+", ".......#+.", "..###.#+..",
    ".#+++##...", "#..##.....", ".#+++##...", "..###..#+.",
    ".......#+.", ".........#",
};
const char* const kIcoFish[10] = {
    "..........", "......##..", "..#######.", ".#+++++++#",
    "#.#+++++#.", "..#######.", "......##..", "..........",
    "..........", "..........",
};
const char* const kIcoTubifex[10] = {
    "..........", "..........", ".....###..", "....#..#..",
    "...#....#.", "..#..##..#", ".#..#..#..", "#..#...#..",
    "...##.....", "..........",
};
const char* const kIcoWorm[10] = {
    "..........", "..........", "..##......", ".#..#.....",
    ".#..#..#..", "..#..#..#.", "...##..#..", ".......##.",
    "..........", "..........",
};
const char* const kIcoHeart[10] = {
    ".##...##..", "#++#.#++#.", "#+++++++#.", "#+++++++#.",
    ".#+++++#..", "..#+++#...", "...#+#....", "....#.....",
    "..........", "..........",
};
const char* const kIcoEgg[10] = {
    "..........", "...###....", "..#+++#...", ".#+++++#..",
    ".#+++++#..", ".#+++++#..", "..#+++#...", "...###....",
    "..........", "..........",
};
const char* const kIcoCorpse[10] = {
    "..........", "...####...", "..#....#..", ".#..##..#.",
    "#..####..#", "#..####..#", ".#..##..#.", "..#....#..",
    "...####...", "..........",
};
const char* const kIcoFlag[10] = {
    "..#.......", "..#####...", "..#...#...", "..#...#...",
    "..#####...", "..#.......", "..#.......", "..#.......",
    "..#.......", "..........",
};
const char* const kIcoSediment[10] = {
    "..........", "..........", "..........", ".####.....",
    "#....#.###", "......#...", "..........", ".####.....",
    "#....#.###", "......#...",
};

inline void drawIcon(Uint32* pixels, int pitch, int x, int y,
                     const char* const* rows, Uint32 primary, Uint32 accent) {
    for (int yy = 0; yy < 10; ++yy) {
        for (int xx = 0; xx < 10; ++xx) {
            const char cell = rows[yy][xx];
            if (cell == '.') continue;
            Uint32 color = primary;
            if (cell == '+') color = accent;
            else if (cell == '-') color = kFaint;
            pixels[(y + yy) * pitch + x + xx] = color;
        }
    }
}

} // namespace panelart

using namespace panelart;

// A self-contained topmost layered window that renders the telemetry card in
// software pixels.  The bottle window, renderer and simulation are untouched.
class ObservationPanel {
public:
    enum class Action { none, togglePause, closePanel, exitApp, cycleLayer, applySettings, resetEcosystem };

    ObservationPanel(SDL_Window* window, HWND hwnd)
        : window_(window), windowId_(SDL_GetWindowID(window)), hwnd_(hwnd),
          canvas_(hwnd, kPanelW, kPanelH) {}

    bool valid() const { return canvas_.valid() && text_.valid(); }
    Uint32 windowId() const { return windowId_; }
    bool isOpen() const { return open_; }

    // Tools are strictly panel-scoped: the owner only lets them touch the
    // bottle while this panel is open, so ordinary desktop use is untouched.
    int toolMode() const { return open_ ? toolMode_ : 0; }  // 0 none, 1 profile, 2 decor
    int decorKind() const { return decorKind_; }            // 0 pebble, 1 wood, 2 shell, 3 erase
    void setProfile(const Ecosystem::CreatureInfo& info) {
        profile_ = info;
        hasProfile_ = true;
        profileAlive_ = true;
    }
    void clearProfile() { hasProfile_ = false; profileAlive_ = false; }

    // True once after the user changed a setting from the panel.  The owner
    // applies the system-level parts and persists the file.
    bool consumeSettingsDirty() {
        const bool dirty = settingsDirty_;
        settingsDirty_ = false;
        return dirty;
    }

    // Redraw immediately so steppers and toggles feel responsive even though
    // the telemetry card otherwise refreshes at 20 Hz.
    void refresh(const Ecosystem& eco, bool paused) {
        if (!open_ && !visible_) return;
        if (!visible_) {
            render(eco, paused, 0.0f);
            canvas_.present();
            SDL_ShowWindow(window_);
            visible_ = true;
            return;
        }
        render(eco, paused, 0.0f);
        canvas_.present();
    }

    void toggle(const Ecosystem& eco, bool paused, SDL_Window* bottle) {
        if (open_) close();
        else open(eco, paused, bottle);
    }

    void open(const Ecosystem& eco, bool paused, SDL_Window* bottle) {
        if (open_) return;
#ifdef ECO_DEBUG_STATS
        std::fprintf(stderr, "[panel] open\n");
        std::fflush(stderr);
#endif
        open_ = true;
        openT_ = 0.0f;
        page_ = 0;
        settingsTab_ = 0;
        ecoPage_ = 0;
        toolMode_ = 0;
        decorKind_ = 0;
        hasProfile_ = false;
        profileAlive_ = false;
        confirmExit_ = false;
        confirmReset_ = false;
        if (hist_.count == 0) prime(eco);
        placeNear(bottle);
        applyPosition();
        render(eco, paused, 0.0f);
        canvas_.present();
        SDL_ShowWindow(window_);
        SDL_RaiseWindow(window_);
        visible_ = true;
        topmost();
    }

    void close() {
#ifdef ECO_DEBUG_STATS
        std::fprintf(stderr, "[panel] close\n");
        std::fflush(stderr);
#endif
        open_ = false;
        confirmExit_ = false;
        confirmReset_ = false;
        ecoPage_ = 0;
        toolMode_ = 0;
        hasProfile_ = false;
        profileAlive_ = false;
    }

    // Esc while a confirmation overlay is open dismisses that overlay instead
    // of closing the whole panel.  Returns true if an overlay was dismissed.
    bool cancelExitConfirm() {
        if (confirmReset_) {
            confirmReset_ = false;
            return true;
        }
        if (!confirmExit_) return false;
        confirmExit_ = false;
        return true;
    }

    // Called after the ecosystem is wiped and restarted, so stale readings
    // (trend lines, selected individual) do not describe the old pond.
    void onEcosystemReset() {
        hist_.count = 0;
        hasProfile_ = false;
        profileAlive_ = false;
        profile_ = Ecosystem::CreatureInfo{};
    }

    void sample(const Ecosystem& eco, float dt) {
        if (hist_.count == 0) {
            prime(eco);
            return;
        }
        sampleTimer_ += dt;
        while (sampleTimer_ >= 1.0f) {
            sampleTimer_ -= 1.0f;
            hist_.push(eco.light(), eco.oxygen(), eco.temperature());
        }
    }

    void update(const Ecosystem& eco, bool paused, float dt,
                SDL_Window* bottle, float fps) {
        fps_ = fps;
        if (!open_ && !visible_) return;

        if (open_) openT_ = std::min(1.0f, openT_ + dt * 7.0f);
        else openT_ = std::max(0.0f, openT_ - dt * 7.0f);

        if (bottle) followBottle(bottle);
        applyPosition();

        if (!open_ && openT_ <= 0.0f) {
            hide();
            return;
        }

        // The telemetry card only changes at human speed; 20 Hz keeps the
        // button feedback crisp while leaving headroom for the 30 fps bottle.
        renderTimer_ += dt;
        if (renderTimer_ < 0.05f) return;
        const float renderDt = renderTimer_;
        renderTimer_ = 0.0f;

        if (!visible_) {
            render(eco, paused, 0.0f);
            canvas_.present();
            SDL_ShowWindow(window_);
            visible_ = true;
        }
        render(eco, paused, renderDt);
        canvas_.present();
    }

    Action handleEvent(const SDL_Event& event) {
        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.windowID == windowId_) {
            const int mx = event.button.x;
            const int my = event.button.y;
            if (event.button.button == SDL_BUTTON_RIGHT) {
                return Action::closePanel;
            }
            if (confirmReset_) {
                // The reset overlay owns the panel until the user decides.
                if (event.button.button != SDL_BUTTON_LEFT) return Action::none;
                if (hit(confirmResetOkRect_, mx, my)) {
                    confirmReset_ = false;
                    pressed_ = 24;
                    pressTimer_ = 0.16f;
                    return Action::resetEcosystem;
                }
                if (hit(confirmResetCancelRect_, mx, my)) {
                    confirmReset_ = false;
                    pressed_ = 23;
                    pressTimer_ = 0.16f;
                }
                return Action::none;
            }
            if (confirmExit_) {
                // The confirmation overlay owns the whole panel until the user
                // picks one of its two buttons.
                if (event.button.button != SDL_BUTTON_LEFT) return Action::none;
                if (hit(confirmExitRect_, mx, my)) {
                    pressed_ = 22;
                    pressTimer_ = 0.16f;
                    return Action::exitApp;
                }
                if (hit(confirmCancelRect_, mx, my)) {
                    confirmExit_ = false;
                    pressed_ = 21;
                    pressTimer_ = 0.16f;
                }
                return Action::none;
            }

            if (event.button.button == SDL_BUTTON_LEFT) {
                if (page_ == 1) {
                    return handleSettingsClick(mx, my);
                }
                if (hit(pauseRect_, mx, my)) {
                    pressed_ = 1;
                    pressTimer_ = 0.16f;
                    return Action::togglePause;
                }
                if (hit(settingsRect_, mx, my)) {
                    pressed_ = 2;
                    pressTimer_ = 0.16f;
                    page_ = 1;
                    settingsTab_ = 0;
                    return Action::applySettings;
                }
                if (hit(closeRect_, mx, my)) {
                    pressed_ = 3;
                    pressTimer_ = 0.16f;
                    return Action::closePanel;
                }
                if (hit(exitRect_, mx, my)) {
                    // Ask first; the dialog also tells the user progress is saved.
                    pressed_ = 4;
                    pressTimer_ = 0.16f;
                    confirmExit_ = true;
                    return Action::none;
                }
                if (hit(heatRect_, mx, my)) {
                    layer_ = (layer_ + 1) % 5;
                    return Action::cycleLayer;
                }
            }
        }
        if (event.type == SDL_MOUSEWHEEL && event.wheel.windowID == windowId_
            && page_ == 0) {
            int mx = 0;
            int my = 0;
            SDL_GetMouseState(&mx, &my);
            if (hit(heatRect_, mx, my)) {
                layer_ = (layer_ + (event.wheel.y > 0 ? 1 : 4)) % 5;
                return Action::cycleLayer;
            }
        }
        return Action::none;
    }

    Action handleSettingsClick(int mx, int my) {
        if (hit(tabSystemRect_, mx, my)) {
            settingsTab_ = 0;
            ecoPage_ = 0;
            pressed_ = 5;
            pressTimer_ = 0.12f;
            return Action::applySettings;
        }
        if (hit(tabEcoRect_, mx, my)) {
            settingsTab_ = 1;
            ecoPage_ = 0;
            pressed_ = 6;
            pressTimer_ = 0.12f;
            return Action::applySettings;
        }
        if (hit(tabToolRect_, mx, my)) {
            settingsTab_ = 2;
            ecoPage_ = 0;
            pressed_ = 7;
            pressTimer_ = 0.12f;
            return Action::applySettings;
        }
        if (hit(pauseRect_, mx, my)) {
            pressed_ = 1;
            pressTimer_ = 0.16f;
            page_ = 0;
            return Action::applySettings;
        }
        if (settingsTab_ == 2) {
            return handleToolsClick(mx, my);
        }
        if (settingsTab_ == 1 && ecoPage_ == 0 && hit(ecoResetRect_, mx, my)) {
            // Destructive: everything living in the bottle is replaced.
            pressed_ = 9;
            pressTimer_ = 0.14f;
            confirmReset_ = true;
            return Action::none;
        }
        if (settingsTab_ == 1
            && (ecoPage_ == 0 ? hit(ecoEventsRect_, mx, my)
                              : hit(ecoBackRect_, mx, my))) {
            ecoPage_ = ecoPage_ == 0 ? 1 : 0;
            pressed_ = 8;
            pressTimer_ = 0.14f;
            return Action::applySettings;
        }
        if (hit(settingsRect_, mx, my)) {
            g_settings = Settings{};
            sanitizeSettings();
            platform::setAutoStart(false);
            settingsDirty_ = true;
            pressed_ = 2;
            pressTimer_ = 0.16f;
            return Action::applySettings;
        }
        if (hit(closeRect_, mx, my)) {
            pressed_ = 3;
            pressTimer_ = 0.16f;
            return Action::closePanel;
        }
        if (hit(exitRect_, mx, my)) {
            // Ask first; the dialog also tells the user the progress is saved.
            pressed_ = 4;
            pressTimer_ = 0.16f;
            confirmExit_ = true;
            return Action::none;
        }

        SettingItem items[kMaxSettingRows];
        const int count = settingItems(settingsTab_, items);
        for (int i = 0; i < count; ++i) {
            const SDL_Rect row = settingRowRect(i);
            if (!hit(row, mx, my)) continue;
            if (items[i].kind == Kind::stepper) {
                if (hit(stepMinusRect(row), mx, my)) {
                    adjustSetting(items[i], -items[i].step);
                } else if (hit(stepPlusRect(row), mx, my)) {
                    adjustSetting(items[i], items[i].step);
                } else {
                    continue;
                }
            } else {
                adjustSetting(items[i], 0);
            }
            pressed_ = 10 + i;
            pressTimer_ = 0.14f;
            return Action::applySettings;
        }
        return Action::none;
    }

    Action handleToolsClick(int mx, int my) {
        if (hit(settingsRect_, mx, my)) {   // 收起工具
            toolMode_ = 0;
            decorKind_ = 0;
            hasProfile_ = false;
            profileAlive_ = false;
            pressed_ = 2;
            pressTimer_ = 0.14f;
            return Action::applySettings;
        }
        if (hit(closeRect_, mx, my)) {
            pressed_ = 3;
            pressTimer_ = 0.16f;
            return Action::closePanel;
        }
        if (hit(exitRect_, mx, my)) {
            pressed_ = 4;
            pressTimer_ = 0.16f;
            confirmExit_ = true;
            return Action::none;
        }
        if (hit(toolOffRect_, mx, my)) {
            toolMode_ = 0;
            pressed_ = 12;
            pressTimer_ = 0.14f;
            return Action::applySettings;
        }
        if (hit(toolProfileRect_, mx, my)) {
            toolMode_ = (toolMode_ == 1) ? 0 : 1;
            if (toolMode_ != 1) hasProfile_ = false;
            pressed_ = 13;
            pressTimer_ = 0.14f;
            return Action::applySettings;
        }
        if (hit(toolDecorRect_, mx, my)) {
            toolMode_ = (toolMode_ == 2) ? 0 : 2;
            pressed_ = 14;
            pressTimer_ = 0.14f;
            return Action::applySettings;
        }
        for (int i = 0; i < 4; ++i) {
            if (!hit(decorKindRect_[i], mx, my)) continue;
            decorKind_ = i;
            toolMode_ = 2;
            pressed_ = 30 + i;
            pressTimer_ = 0.14f;
            return Action::applySettings;
        }
        return Action::none;
    }

    void reassertTopmost() {
        if (visible_) topmost();
    }

    void shutdown() {
        if (visible_) {
            SDL_HideWindow(window_);
            visible_ = false;
        }
        canvas_.release();
    }

private:
    struct Snapshot {
        float light = 0.0f;
        float oxygen = 0.0f;
        float temperature = 0.0f;
        int water = 0;
        float tannin = 0.0f;
        float toxin = 0.0f;
        float humus = 0.0f;
        float nutrient = 0.0f;
        float phosphorus = 0.0f;
        int events = 0;
        const wchar_t* eventNames[3] = {nullptr, nullptr, nullptr};
        // Observation-only ecology log (newest first) and richness metrics.
        struct LogRow {
            const wchar_t* name = L"";
            float age = 0.0f;
            float intensity = 1.0f;
            bool ongoing = false;
        };
        static constexpr int kLogRows = 3;
        LogRow logRows[kLogRows]{};
        int logRowCount = 0;
        int logTotal = 0;
        float diversity = 0.0f;
        float evenness = 0.0f;
        int groups = 0;
        float sediment = 0.0f;
        float aerobic = 0.0f;
        float anaerobic = 0.0f;
        float stress = 0.0f;
        int plants = 0;
        int rafts = 0;
        float duckweed = 0.0f;
        int shrimp = 0;
        int damsel = 0;
        int fish = 0;
        int worms = 0;
        int tubifex = 0;
        int corpses = 0;
        int eggs = 0;
        int bank = 0;
        int territory = 0;
        float biomass = 0.0f;
        float health = 1.0f;
        int season = 0;
        int day = 0;
        float gridLight[128]{};
        float gridO2[128]{};
        float gridTannin[128]{};
        float gridToxin[128]{};
        float gridDensity[128]{};
        float sedimentBins[16]{};
    };

    static constexpr int kLeftX = 12;
    static constexpr int kRightX = 212;
    static constexpr int kColW = 196;
    static constexpr int kColY = 42;
    static constexpr int kRowH = 19;
    static constexpr int kRowsY = 62;
    static constexpr int kCycleHeaderY = 182;
    static constexpr int kCycleRowsY = 202;
    static constexpr int kNoteY = 296;
    static constexpr int kLogY = 316;
    static constexpr int kLogH = 64;
    static constexpr int kBottomY = 386;
    static constexpr int kBottomH = 94;
    static constexpr int kButtonY = 488;
    static constexpr int kButtonH = 28;
    static constexpr int kButtonW = 92;
    static constexpr int kSettingsY = 82;
    static constexpr int kSettingsRowH = 32;
    static constexpr int kMaxSettingRows = 12;

    static bool hit(const SDL_Rect& r, int x, int y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

    enum class Kind { stepper, toggle, scale };

    struct SettingItem {
        const wchar_t* label = L"";
        int* value = nullptr;
        int minValue = 0;
        int maxValue = 0;
        int step = 1;
        Kind kind = Kind::stepper;
        const wchar_t* suffix = L"";
    };

    static SDL_Rect settingRowRect(int index) {
        return SDL_Rect{12, kSettingsY + index * kSettingsRowH, 396, kSettingsRowH};
    }

    static SDL_Rect stepMinusRect(const SDL_Rect& row) {
        return SDL_Rect{row.x + row.w - 116, row.y + 4, 24, 20};
    }

    static SDL_Rect stepPlusRect(const SDL_Rect& row) {
        return SDL_Rect{row.x + row.w - 34, row.y + 4, 24, 20};
    }

    static SDL_Rect toggleRect(const SDL_Rect& row) {
        return SDL_Rect{row.x + row.w - 96, row.y + 4, 86, 20};
    }

    int settingItems(int tab, SettingItem* out) {
        if (tab == 0) {
            out[0] = {L"最大帧率", &g_settings.maxFps, 15, 60, 5, Kind::stepper, L""};
            out[1] = {L"整体大小", &g_settings.scale, 100, 200, 100, Kind::scale, L"%"};
            out[2] = {L"始终置顶", &g_settings.topmost, 0, 1, 1, Kind::toggle, L""};
            out[3] = {L"不透明度", &g_settings.opacity, 50, 100, 5, Kind::stepper, L"%"};
            out[4] = {L"鼠标穿透", &g_settings.clickThrough, 0, 1, 1, Kind::toggle, L""};
            out[5] = {L"开机自启", &g_settings.autoStart, 0, 1, 1, Kind::toggle, L""};
            return 6;
        }
        if (ecoPage_ == 1) {
            static const wchar_t* labels[8] = {
                L"事件 · 暴雨", L"事件 · 干旱", L"事件 · 热浪", L"事件 · 寒潮",
                L"事件 · 藻华", L"事件 · 秋季翻底", L"事件 · 落叶输入", L"事件 · 食物丰度"};
            for (int i = 0; i < 8; ++i) {
                out[i] = {labels[i], &g_settings.eventWeight[i], 0, 300, 10,
                          Kind::stepper, L"%"};
            }
            out[8] = {L"事件频率", &g_settings.eventFrequency, 0, 300, 10,
                      Kind::stepper, L"%"};
            return 9;
        }
        out[0] = {L"四季周期", &g_settings.seasonMinutes, 5, 60, 5, Kind::stepper, L" 分"};
        out[1] = {L"水草上限", &g_settings.maxPlants,
                   3, Ecosystem::kMaxPlants, 1, Kind::stepper, L""};
        out[2] = {L"米虾上限", &g_settings.maxShrimp,
                   1, Ecosystem::kMaxShrimp, 1, Kind::stepper, L""};
        out[3] = {L"豆娘上限", &g_settings.maxDamsel,
                   1, Ecosystem::kMaxDamsel, 1, Kind::stepper, L""};
        out[4] = {L"鳑鲏上限", &g_settings.maxFish,
                   1, Ecosystem::kMaxFish, 1, Kind::stepper, L""};
        out[5] = {L"水蚯蚓上限", &g_settings.maxWorms,
                   1, Ecosystem::kMaxWorms, 1, Kind::stepper, L""};
        out[6] = {L"颤蚓上限", &g_settings.maxTubifex,
                   2, Ecosystem::kMaxTubifex, 1, Kind::stepper, L""};
        out[7] = {L"繁殖强度", &g_settings.breeding, 25, 200, 5, Kind::stepper, L"%"};
        out[8] = {L"扰动频率", &g_settings.disturbance, 0, 300, 10, Kind::stepper, L"%"};
        out[9] = {L"休眠卵兜底", &g_settings.refuge, 0, 1, 1, Kind::toggle, L""};
        return 10;
    }

    void adjustSetting(const SettingItem& item, int delta) {
        if (item.kind == Kind::toggle) {
            *item.value = *item.value ? 0 : 1;
        } else if (item.kind == Kind::scale) {
            *item.value = *item.value >= 150 ? 100 : 200;
        } else {
            *item.value = std::clamp(*item.value + delta,
                                     item.minValue, item.maxValue);
        }
        sanitizeSettings();
        if (item.value == &g_settings.autoStart
            && !platform::setAutoStart(g_settings.autoStart != 0)) {
            // Registry write failed (unusual for HKCU); keep the UI truthful.
            g_settings.autoStart = g_settings.autoStart ? 0 : 1;
        }
        settingsDirty_ = true;
    }

    static std::wstring fmt(float value, const wchar_t* format) {
        wchar_t buf[64];
        std::swprintf(buf, 64, format, static_cast<double>(value));
        return std::wstring(buf);
    }

    static std::wstring ratio(int value, int cap) {
        return std::to_wstring(value) + L"/" + std::to_wstring(cap);
    }

    Snapshot makeSnapshot(const Ecosystem& eco) {
        Snapshot s;
        s.light = eco.light();
        s.oxygen = eco.oxygen();
        s.temperature = eco.temperature();
        s.water = eco.waterLevelOffset();
        s.humus = eco.humus();
        s.nutrient = eco.nutrient();
        s.phosphorus = eco.phosphorus();
        s.events = std::min(3, eco.eventCount());
        for (int i = 0; i < s.events; ++i) {
            s.eventNames[i] = Ecosystem::eventName(eco.events()[i].type);
        }
        s.plants = eco.plantCount();
        s.rafts = eco.raftCount();
        s.duckweed = eco.duckweedCover();
        s.shrimp = eco.shrimpCount();
        s.damsel = eco.damselCount();
        s.fish = eco.fishCount();
        s.worms = eco.wormCount();
        s.tubifex = eco.tubifexCount();
        s.corpses = eco.corpseCount();
        s.eggs = eco.eggCount();
        s.bank = eco.fishEggs() + eco.damselEggs() + eco.shrimpEggs()
               + eco.tubifexEggs() + eco.wormCysts() + eco.plantSeeds();
        s.territory = eco.territoryCount();
        // Panel-only biomass.  Ecosystem::plantBiomass_ is clamped at 8.0
        // because it also scales respiration and nutrient uptake, so once the
        // stand is lush that value stops moving and the LIFE row would lie.
        // Recompute the same weighted mass here without the cap; the row is
        // normalised against the configured plant capacity in drawLife().
        float biomassRaw = 0.0f;
        for (int i = 0; i < s.plants; ++i) {
            const auto& p = eco.plants()[i];
            const float typeW = (p.type == 1 ? 1.25f : (p.type == 2 ? 0.95f : 0.70f));
            biomassRaw += typeW * p.growth * p.health * p.g.density
                        * (p.dying ? 0.20f : 1.0f);
        }
        s.biomass = biomassRaw;
        s.season = static_cast<int>(eco.seasonPhase() / 6.2831853f * 4.0f) & 3;
        s.day = static_cast<int>(eco.dayPhase() / 6.2831853f * 4.0f) & 3;

        float healthSum = 0.0f;
        int healthCount = 0;
        for (int i = 0; i < s.plants; ++i) {
            healthSum += eco.plants()[i].health;
            ++healthCount;
        }
        for (int i = 0; i < s.shrimp; ++i) {
            healthSum += eco.shrimp()[i].health;
            ++healthCount;
        }
        for (int i = 0; i < s.damsel; ++i) {
            healthSum += eco.damsel()[i].health;
            ++healthCount;
        }
        for (int i = 0; i < s.fish; ++i) {
            healthSum += eco.fish()[i].health;
            ++healthCount;
        }
        for (int i = 0; i < s.worms; ++i) {
            healthSum += eco.worms()[i].health;
            ++healthCount;
        }
        for (int i = 0; i < s.tubifex; ++i) {
            healthSum += eco.tubifex()[i].health;
            ++healthCount;
        }
        s.health = healthCount > 0 ? healthSum / static_cast<float>(healthCount) : 1.0f;

        // Observation-only log lines: the newest few entries of the ring buffer.
        for (int i = 0; i < Snapshot::kLogRows; ++i) {
            if (i >= eco.eventLogCount()) break;
            const Ecosystem::EventLogEntry& e = eco.eventLogAt(i);
            s.logRows[i].name = Ecosystem::eventName(e.type);
            s.logRows[i].age = e.age;
            s.logRows[i].intensity = e.intensity;
            s.logRows[i].ongoing = e.age < e.duration;
            ++s.logRowCount;
        }
        s.logTotal = eco.eventLogCount();

        // Shannon diversity over 8 groups: three plant forms + five animals.
        int groupCounts[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        for (int i = 0; i < s.plants; ++i) {
            const int t = eco.plants()[i].type;
            if (t >= 0 && t < 3) ++groupCounts[t];
        }
        groupCounts[3] = s.shrimp;
        groupCounts[4] = s.damsel;
        groupCounts[5] = s.fish;
        groupCounts[6] = s.worms;
        groupCounts[7] = s.tubifex;
        int groupTotal = 0;
        for (int i = 0; i < 8; ++i) {
            groupTotal += groupCounts[i];
            if (groupCounts[i] > 0) ++s.groups;
        }
        if (groupTotal > 0) {
            float h = 0.0f;
            for (int i = 0; i < 8; ++i) {
                if (groupCounts[i] <= 0) continue;
                const float p = static_cast<float>(groupCounts[i])
                              / static_cast<float>(groupTotal);
                h -= p * std::log(p);
            }
            s.diversity = h;
            s.evenness = s.groups > 1
                ? h / std::log(static_cast<float>(s.groups)) : 1.0f;
        }

        float gO2 = 0.0f;
        float gTannin = 0.0f;
        float gToxin = 0.0f;
        float gAerobic = 0.0f;
        float gAnaerobic = 0.0f;
        for (int gy = 0; gy < 8; ++gy) {
            for (int gx = 0; gx < 16; ++gx) {
                const auto& cell = eco.gridCell(gx, gy);
                const int i = gy * 16 + gx;
                s.gridLight[i] = cell.light;
                s.gridO2[i] = cell.o2;
                s.gridTannin[i] = cell.tannin;
                s.gridToxin[i] = cell.toxin;
                s.gridDensity[i] = cell.density;
                gO2 += cell.o2;
                gTannin += cell.tannin;
                gToxin += cell.toxin;
                gAerobic += cell.aerobic;
                gAnaerobic += cell.anaerobic;
            }
        }
        constexpr float inv = 1.0f / 128.0f;
        s.tannin = gTannin * inv;
        s.toxin = gToxin * inv;
        s.aerobic = gAerobic * inv;
        s.anaerobic = gAnaerobic * inv;

        float sedimentSum = 0.0f;
        for (int i = 0; i < 16; ++i) {
            s.sedimentBins[i] = eco.sedimentBins()[i];
            sedimentSum += s.sedimentBins[i];
        }
        s.sediment = sedimentSum / 16.0f;

        const float oxygenAvg = gO2 * inv;
        s.stress = std::clamp(
            std::max(0.0f, 34.0f - oxygenAvg) / 34.0f * 0.58f
            + s.tannin / 80.0f * 0.22f
            + s.toxin / 26.0f * 0.55f,
            0.0f, 1.0f);
        return s;
    }

    void prime(const Ecosystem& eco) {
        hist_.clear();
        for (int i = 0; i < 60; ++i) {
            hist_.push(eco.light(), eco.oxygen(), eco.temperature());
        }
    }

    void placeNear(SDL_Window* bottle) {
        int bx = 0;
        int by = 0;
        SDL_GetWindowPosition(bottle, &bx, &by);
        SDL_Rect usable{0, 0, 0, 0};
        SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(bottle), &usable);
        if (usable.w <= 0 || usable.h <= 0) {
            SDL_GetDisplayUsableBounds(0, &usable);
        }

        int x = bx + (scaledScreenW() - kPanelW) / 2;
        int y = by - kPanelH - 8;
        if (y < usable.y + 4) {
            y = by + scaledScreenH() + 8;
        }
        if (y + kPanelH > usable.y + usable.h - 4) {
            y = std::clamp(by + (scaledScreenH() - kPanelH) / 2,
                           usable.y + 4, usable.y + usable.h - kPanelH - 4);
            if (bx - kPanelW - 8 >= usable.x) {
                x = bx - kPanelW - 8;
            } else if (bx + scaledScreenW() + 8 + kPanelW <= usable.x + usable.w) {
                x = bx + scaledScreenW() + 8;
            }
        }
        x = std::clamp(x, usable.x + 4, usable.x + usable.w - kPanelW - 4);
        y = std::clamp(y, usable.y + 4, usable.y + usable.h - kPanelH - 4);
        baseX_ = x;
        baseY_ = y;
        baseSet_ = true;
        lastBottleX_ = bx;
        lastBottleY_ = by;
    }

    void followBottle(SDL_Window* bottle) {
        int bx = 0;
        int by = 0;
        SDL_GetWindowPosition(bottle, &bx, &by);
        if (bx != lastBottleX_ || by != lastBottleY_) {
            placeNear(bottle);
        }
    }

    void applyPosition() {
        if (!baseSet_) return;
        const int offset = static_cast<int>((1.0f - openT_) * 12.0f);
        const int x = baseX_;
        const int y = baseY_ + offset;
        if (x != appliedX_ || y != appliedY_) {
            SDL_SetWindowPosition(window_, x, y);
            appliedX_ = x;
            appliedY_ = y;
        }
    }

    void hide() {
        if (visible_) {
#ifdef ECO_DEBUG_STATS
            std::fprintf(stderr, "[panel] hide\n");
            std::fflush(stderr);
#endif
            SDL_HideWindow(window_);
            visible_ = false;
        }
    }

    void topmost() {
        if (hwnd_ && g_settings.topmost) {
            SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    void drawText(int x, int y, const std::wstring& value, Uint32 color,
                  bool bold = false, int shadow = 1) {
        text_.draw(pixels_, pitch_, kPanelW, kPanelH, x, y, value, color, bold, shadow);
    }

    void drawTextRight(int right, int y, const std::wstring& value, Uint32 color,
                       bool bold = false, int shadow = 1) {
        drawText(right - text_.width(value, bold), y, value, color, bold, shadow);
    }

    void drawSection(int x, int y, int w, const wchar_t* title, const wchar_t* english) {
        drawText(x, y, title, kCream, false, 1);
        const int tw = text_.width(title);
        drawText(x + tw + 8, y + 1, english, kFaint, false, 0);
        hLine(pixels_, pitch_, x, x + w - 1, y + 15, kLine);
    }

    void drawBar(int x, int y, int w, float value, Uint32 color) {
        fillRect(pixels_, pitch_, x, y, w, 2, kLine);
        const int fill = static_cast<int>(std::clamp(value, 0.0f, 1.0f)
                                          * static_cast<float>(w - 1));
        if (fill > 0) {
            fillRect(pixels_, pitch_, x, y, fill, 2, color);
            fillRect(pixels_, pitch_, x, y, fill, 1,
                     lerpColor(color, kIce, 0.28f));
        }
    }

    void drawRow(int x, int y, int w, const char* const* icon,
                 Uint32 iconPrimary, Uint32 iconAccent,
                 const std::wstring& label, const std::wstring& value,
                 float barValue, bool bar, Uint32 valueColor) {
        drawIcon(pixels_, pitch_, x, y + 3, icon, iconPrimary, iconAccent);
        drawText(x + 13, y + 1, label, kDim, false, 0);
        drawTextRight(x + w, y + 1, value, valueColor, false, 0);
        if (bar) drawBar(x + 13, y + 15, w - 13, barValue, valueColor);
    }

    Uint32 heatColor(int layer, float t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        Uint32 c0 = kInset;
        Uint32 c1 = rgba(30, 76, 68);
        Uint32 c2 = rgba(70, 128, 108);
        Uint32 c3 = kCyan;
        switch (layer) {
        case 0:
            break;
        case 1:
            c1 = rgba(74, 70, 45);
            c2 = rgba(148, 128, 70);
            c3 = kAmber;
            break;
        case 2:
            c1 = rgba(68, 47, 30);
            c2 = rgba(128, 80, 38);
            c3 = rgba(198, 132, 62);
            break;
        case 3:
            c1 = rgba(72, 36, 30);
            c2 = rgba(128, 56, 42);
            c3 = kRust;
            break;
        default:
            c1 = rgba(39, 72, 52);
            c2 = rgba(70, 110, 72);
            c3 = kMoss;
            break;
        }
        if (t < 0.35f) return lerpColor(c0, c1, t / 0.35f);
        if (t < 0.72f) return lerpColor(c1, c2, (t - 0.35f) / 0.37f);
        return lerpColor(c2, c3, (t - 0.72f) / 0.28f);
    }

    void drawFrame() {
        fillRounded(pixels_, pitch_, 0, 0, kPanelW, kPanelH, 7, kEdgeDark);
        fillRounded(pixels_, pitch_, 2, 2, kPanelW - 4, kPanelH - 4, 6, kEdge);

        for (int y = 4; y < kPanelH - 4; ++y) {
            const Uint32 color = lerpColor(
                kGlassTop, kGlassBot,
                static_cast<float>(y - 4) / static_cast<float>(kPanelH - 9));
            for (int x = 4; x < kPanelW - 4; ++x) {
                if (!insideRounded(x - 4, y - 4, kPanelW - 8, kPanelH - 8, 4)) continue;
                pixels_[y * pitch_ + x] = color;
            }
        }

        hLine(pixels_, pitch_, 12, kPanelW - 13, 4, rgba(116, 158, 136));
        hLine(pixels_, pitch_, 12, kPanelW - 13, 5, rgba(70, 112, 94));
        hLine(pixels_, pitch_, 12, kPanelW - 13, kPanelH - 5, kEdgeDark);

        // Rivets.
        const int rivets[4][2] = {{8, 8}, {kPanelW - 11, 8},
                                  {8, kPanelH - 11}, {kPanelW - 11, kPanelH - 11}};
        for (const auto& r : rivets) {
            fillRect(pixels_, pitch_, r[0] - 1, r[1] - 1, 3, 3, kEdgeDark);
            pixels_[(r[1]) * pitch_ + r[0]] = kMetal;
            pixels_[(r[1]) * pitch_ + r[0] + 1] = kFaint;
        }

        // A little condensation and moss keep the glass from looking flat.
        const int drops[6][2] = {{9, 120}, {11, 121}, {kPanelW - 12, 168},
                                 {kPanelW - 10, 169}, {14, kPanelH - 26},
                                 {kPanelW - 16, kPanelH - 38}};
        for (const auto& d : drops) {
            blendOver(pixels_, d[1] * pitch_ + d[0], kIce, 46);
            blendOver(pixels_, (d[1] + 1) * pitch_ + d[0], kCyan, 34);
        }
        const int moss[8][2] = {{10, kPanelH - 14}, {11, kPanelH - 13},
                                {13, kPanelH - 13}, {15, kPanelH - 12},
                                {kPanelW - 14, kPanelH - 18},
                                {kPanelW - 12, kPanelH - 17},
                                {kPanelW - 11, kPanelH - 16},
                                {kPanelW - 9, kPanelH - 15}};
        for (const auto& m : moss) {
            pixels_[m[1] * pitch_ + m[0]] = kMossDark;
        }
        hLine(pixels_, pitch_, 12, kPanelW - 13, 34, kLine);
        hLine(pixels_, pitch_, 12, kPanelW - 13, 35, kEdgeDark);
    }

    void drawHeader(const Snapshot& s, bool paused) {
        drawText(14, 5, L"沼泽观测站", kCream, true, 1);
        drawText(100, 9, L"SWAMP TELEMETRY", kFaint, false, 0);

        static const wchar_t* seasons[4] = {L"春", L"夏", L"秋", L"冬"};
        static const wchar_t* phases[4] = {L"晨", L"昼", L"暮", L"夜"};
        const std::wstring stamp = std::wstring(seasons[s.season & 3]) + L" · "
                                 + phases[s.day & 3];
        const std::wstring status = paused ? L"已暂停" : L"运行中";
        const std::wstring fps = fmt(fps_, L"%.0f FPS");
        const Uint32 statusColor = paused ? kAmber : kMoss;

        const int right = kPanelW - 14;
        const int fx = right - text_.width(fps);
        drawText(fx, 9, fps, kFaint, false, 0);
        const int sx = fx - 14 - text_.width(status);
        fillRect(pixels_, pitch_, sx - 8, 12, 3, 3, statusColor);
        drawText(sx, 9, status, statusColor, false, 0);
        const int tx = sx - 12 - text_.width(stamp);
        drawText(tx, 9, stamp, kDim, false, 0);
    }

    void drawEnvironment(const Snapshot& s) {
        drawSection(kLeftX, kColY, kColW, L"环境", L"CLIMATE");
        int y = kRowsY;
        const int x = kLeftX;
        const int w = kColW;

        const Uint32 o2Color = s.oxygen < 30.0f ? kRust
                             : s.oxygen < 50.0f ? kAmber : kCyan;
        const Uint32 tempColor = (s.temperature > 25.0f || s.temperature < 18.0f)
                               ? kRust : kCream;
        const Uint32 tanninColor = s.tannin > 45.0f ? kAmber : kCream;
        const Uint32 toxinColor = s.toxin > 8.0f ? kRust : kCream;

        drawRow(x, y, w, kIcoSun, kAmber, kCream,
                L"光照", fmt(s.light, L"%.0f"), s.light / 100.0f, true,
                s.light < 25.0f ? kAmber : kCream); y += kRowH;
        drawRow(x, y, w, kIcoO2, kCyan, kIce,
                L"溶氧", fmt(s.oxygen, L"%.0f"), s.oxygen / 100.0f, true, o2Color); y += kRowH;
        drawRow(x, y, w, kIcoTemp, kRust, kAmber,
                L"水温", fmt(s.temperature, L"%.1f C"), (s.temperature - 15.0f) / 12.0f,
                true, tempColor); y += kRowH;
        const int waterDepth = -s.water;
        drawRow(x, y, w, kIcoWater, kCyan, kIce,
                L"水位", std::wstring(waterDepth >= 0 ? L"+" : L"")
                + std::to_wstring(waterDepth) + L" px",
                std::clamp((static_cast<float>(waterDepth) + 26.0f) / 32.0f,
                           0.0f, 1.0f), true, kCyan); y += kRowH;
        drawRow(x, y, w, kIcoDrop, kAmber, kCream,
                L"单宁", fmt(s.tannin, L"%.0f"), s.tannin / 80.0f, true, tanninColor); y += kRowH;
        drawRow(x, y, w, kIcoWarn, kRust, kAmber,
                L"毒素", fmt(s.toxin, L"%.0f"), s.toxin / 26.0f, true, toxinColor);
    }

    void drawCycle(const Snapshot& s) {
        drawSection(kLeftX, kCycleHeaderY, kColW, L"循环", L"CYCLE");
        int y = kCycleRowsY;
        const int x = kLeftX;
        const int w = kColW;

        drawRow(x, y, w, kIcoLeaf, kAmber, kMoss,
                L"腐殖质", fmt(s.humus, L"%.0f"), s.humus / 100.0f, true, kCream); y += kRowH;
        drawRow(x, y, w, kIcoPlant, kMoss, kIce,
                L"氮/磷", fmt(s.nutrient, L"%.0f") + L" / " + fmt(s.phosphorus, L"%.0f"),
                std::min(s.nutrient, s.phosphorus * 1.25f) / 100.0f, true,
                (s.nutrient < 22.0f || s.phosphorus < 18.0f) ? kAmber : kCream); y += kRowH;

        drawIcon(pixels_, pitch_, x, y + 3, kIcoSediment, kAmber, kMoss);
        drawText(x + 13, y + 1, L"底泥", kDim, false, 0);
        drawTextRight(x + w, y + 0, fmt(s.sediment, L"%.0f"), kCream, false, 0);
        for (int i = 0; i < 16; ++i) {
            const float v = std::clamp(s.sedimentBins[i] / 100.0f, 0.0f, 1.0f);
            const int bh = 1 + static_cast<int>(v * 3.0f);
            fillRect(pixels_, pitch_, x + 13 + i * 11, y + 13 + (4 - bh), 8, bh,
                     lerpColor(kMossDark, kMoss, v));
        }
        y += kRowH;

        drawRow(x, y, w, kIcoBactA, kMoss, kIce,
                L"好氧菌", fmt(s.aerobic, L"%.2f"), s.aerobic, true, kMoss); y += kRowH;
        drawRow(x, y, w, kIcoBactN, kRust, kAmber,
                L"厌氧菌", fmt(s.anaerobic, L"%.2f"), s.anaerobic, true, kRust); y += kRowH;
        drawRow(x, y, w, kIcoWarn, kAmber, kRust,
                L"环境压力", fmt(s.stress * 100.0f, L"%.0f%%"), s.stress, true,
                s.stress > 0.55f ? kRust : kAmber);
    }

    void drawLife(const Snapshot& s) {
        drawSection(kRightX, kColY, kColW, L"生命", L"LIFE");
        // Duckweed rides in the section header so the twelve life rows and
        // the whole 420x528 panel layout stay exactly as they were.
        const std::wstring duck = L"浮萍 " + std::to_wstring(s.rafts) + L" · "
                                + fmt(s.duckweed * 100.0f, L"%.0f%%");
        drawTextRight(kRightX + kColW, kColY, duck, kMoss, false, 0);
        int y = kRowsY;
        const int x = kRightX;
        const int w = kColW;
        // Dormant bank ceiling: four 6-slot animal egg banks + 4 worm cysts
        // + 5 loose plant seeds + three types x 3 typed seeds.
        constexpr float kBankFull = 4.0f * 6.0f + 4.0f + 5.0f + 3.0f * 3.0f;

        drawRow(x, y, w, kIcoPlant, kMoss, kIce,
                L"水草", ratio(s.plants, g_settings.maxPlants),
                static_cast<float>(s.plants) / g_settings.maxPlants, true,
                s.plants > 0 ? kMoss : kRust); y += kRowH;
        // Full scale is every allowed plant at the tallest form's oxygen
        // weight (1.25) and the top density gene (1.1), i.e. 1.375 each, so
        // the bar tracks the stand instead of pinning at a fixed 8.0.
        const float biomassFull = std::max(1, g_settings.maxPlants) * 1.375f;
        drawRow(x, y, w, kIcoPlant, kMossDark, kMoss,
                L"生物量", fmt(s.biomass, L"%.1f"), s.biomass / biomassFull, true,
                kMoss); y += kRowH;
        drawRow(x, y, w, kIcoShrimp, kAmber, kCream,
                L"米虾", ratio(s.shrimp, g_settings.maxShrimp),
                static_cast<float>(s.shrimp) / g_settings.maxShrimp, true,
                s.shrimp > 0 ? kCream : kRust); y += kRowH;
        drawRow(x, y, w, kIcoDamsel, kMoss, kIce,
                L"豆娘", ratio(s.damsel, g_settings.maxDamsel),
                static_cast<float>(s.damsel) / g_settings.maxDamsel, true,
                s.damsel > 0 ? kCream : kRust); y += kRowH;
        drawRow(x, y, w, kIcoFish, kCyan, kAmber,
                L"鳑鲏", ratio(s.fish, g_settings.maxFish),
                static_cast<float>(s.fish) / g_settings.maxFish, true,
                s.fish > 0 ? kCream : kRust); y += kRowH;
        drawRow(x, y, w, kIcoFlag, kAmber, kRust,
                L"领地", std::to_wstring(s.territory),
                static_cast<float>(s.territory) / g_settings.maxFish, false,
                s.territory > 0 ? kAmber : kDim); y += kRowH;
        drawRow(x, y, w, kIcoTubifex, kRust, kAmber,
                L"颤蚓", ratio(s.tubifex, g_settings.maxTubifex),
                static_cast<float>(s.tubifex) / g_settings.maxTubifex, true,
                s.tubifex > 0 ? kCream : kRust); y += kRowH;
        drawRow(x, y, w, kIcoWorm, kAmber, kCream,
                L"水蚯蚓", ratio(s.worms, g_settings.maxWorms),
                static_cast<float>(s.worms) / g_settings.maxWorms, true,
                s.worms > 0 ? kCream : kRust); y += kRowH;
        drawRow(x, y, w, kIcoHeart, kMoss, kRust,
                L"平均健康", fmt(s.health * 100.0f, L"%.0f%%"), s.health, true,
                s.health > 0.7f ? kMoss : (s.health > 0.45f ? kAmber : kRust)); y += kRowH;
        drawRow(x, y, w, kIcoEgg, kCream, kAmber,
                L"卵", std::to_wstring(s.eggs),
                static_cast<float>(s.eggs) / 12.0f, false, kCream); y += kRowH;
        drawRow(x, y, w, kIcoEgg, kFaint, kMoss,
                L"休眠库", std::to_wstring(s.bank),
                static_cast<float>(s.bank) / kBankFull, false, kDim); y += kRowH;
        drawRow(x, y, w, kIcoCorpse, kFaint, kRust,
                L"尸体", std::to_wstring(s.corpses),
                static_cast<float>(s.corpses) / 10.0f, false, kDim);

        Uint32 noteColor = kDim;
        const wchar_t* note = L"物质循环平稳";
        std::wstring eventNote;
        if (s.oxygen < 30.0f) {
            note = L"溶氧偏低，底栖受压";
            noteColor = kRust;
        } else if (s.toxin > 12.0f) {
            note = L"毒素累积，注意厌氧层";
            noteColor = kRust;
        } else if (s.tannin > 50.0f) {
            note = L"单宁偏高，光照受阻";
            noteColor = kAmber;
        } else if (s.events > 0) {
            eventNote = std::wstring(L"事件 · ") + s.eventNames[0];
            if (s.events > 1) eventNote += L" 等";
            note = eventNote.c_str();
            noteColor = kAmber;
        } else if (s.health < 0.62f) {
            note = L"部分生物健康下滑";
            noteColor = kAmber;
        } else if (s.shrimp == 0) {
            note = L"虾群短暂空缺，正在补入";
            noteColor = kAmber;
        } else if (s.fish == 0) {
            note = L"鱼群短暂空缺，正在补入";
            noteColor = kAmber;
        }
        drawText(kRightX, kNoteY, L"状态 · ", kFaint, false, 0);
        drawText(kRightX + text_.width(L"状态 · "), kNoteY, note, noteColor, false, 0);
    }

    // Human-readable age for one log line.  "进行中" is used while the event is
    // still within its planned duration, then the elapsed time is shown.
    static std::wstring logAgeText(float age, bool ongoing) {
        if (ongoing) return L"进行中";
        if (age < 1.5f) return L"刚刚";
        if (age < 60.0f) {
            return std::to_wstring(static_cast<int>(age)) + L"s 前";
        }
        return std::to_wstring(static_cast<int>(age / 60.0f)) + L"m 前";
    }

    // Read-only strip: newest three events plus Shannon diversity.  It draws
    // straight from the snapshot and never writes back into the ecosystem.
    void drawLog(const Snapshot& s) {
        fillRounded(pixels_, pitch_, 12, kLogY, 396, kLogH, 3, kEdgeDark);
        fillRounded(pixels_, pitch_, 13, kLogY + 1, 394, kLogH - 2, 2, kInset);
        hLine(pixels_, pitch_, 13, 406, kLogY + 1, kLine);

        drawText(20, kLogY + 4, L"生态日志", kCream, false, 1);
        drawText(20 + text_.width(L"生态日志") + 6, kLogY + 5, L"EVENT LOG",
                 kFaint, false, 0);

        std::wstring div = L"多样性 H " + fmt(s.diversity, L"%.2f")
                         + L" · 均匀 " + fmt(s.evenness, L"%.2f");
        if (s.groups < 8) {
            div += L" · " + std::to_wstring(s.groups) + L"/8 类";
        }
        const Uint32 divColor = s.groups < 6 ? kRust
                              : (s.evenness > 0.72f ? kMoss
                              : (s.evenness > 0.45f ? kAmber : kRust));
        drawTextRight(12 + 396 - 8, kLogY + 4, div, divColor, false, 0);

        if (s.logRowCount == 0) {
            drawText(24, kLogY + 26, L"暂无事件记录 · 生态保持平稳",
                     kDim, false, 0);
            return;
        }
        for (int i = 0; i < s.logRowCount; ++i) {
            const int y = kLogY + 22 + i * 13;
            const Snapshot::LogRow& row = s.logRows[i];
            fillRect(pixels_, pitch_, 24, y + 4, 3, 3, row.ongoing ? kAmber : kLine);
            drawText(32, y, row.name, row.ongoing ? kAmber : kDim, false, 0);
            drawText(96, y, logAgeText(row.age, row.ongoing),
                     row.ongoing ? kAmber : kFaint, false, 0);
            drawTextRight(12 + 396 - 8, y,
                          L"强度 "
                          + std::to_wstring(static_cast<int>(row.intensity * 100.0f))
                          + L"%",
                          row.ongoing ? kCream : kDim, false, 0);
        }
    }

    void drawHeatmap(const Snapshot& s) {
        fillRounded(pixels_, pitch_, 12, kBottomY, kColW, kBottomH, 3, kEdgeDark);
        fillRounded(pixels_, pitch_, 13, kBottomY + 1, kColW - 2, kBottomH - 2, 2, kInset);
        hLine(pixels_, pitch_, 13, 12 + kColW - 2, kBottomY + 1, kLine);

        static const wchar_t* layerNames[5] = {L"溶氧", L"光照", L"单宁", L"毒素", L"密度"};
        const Uint32 layerColor = layer_ == 0 ? kCyan
                                : layer_ == 1 ? kAmber
                                : layer_ == 2 ? kAmber
                                : layer_ == 3 ? kRust : kMoss;
        drawText(20, kBottomY + 5, L"微生境", kCream, false, 1);
        drawText(20 + text_.width(L"微生境") + 6, kBottomY + 6, L"16×8", kFaint, false, 0);
        drawTextRight(12 + kColW - 8, kBottomY + 5, layerNames[layer_], layerColor,
                      false, 0);

        auto rawAt = [&](int i) {
            switch (layer_) {
            case 0: return s.gridO2[i];
            case 1: return s.gridLight[i];
            case 2: return s.gridTannin[i];
            case 3: return s.gridToxin[i];
            default: return s.gridDensity[i];
            }
        };
        // Center every layer on its grid average and scale by the grid's own
        // standard deviation, so the map always shows local structure while a
        // single extreme cell cannot wash the whole field out.
        float floorHalf = 2.0f;
        switch (layer_) {
        case 0: floorHalf = 3.0f; break;
        case 1: floorHalf = 4.0f; break;
        case 2: floorHalf = 1.5f; break;
        case 3: floorHalf = 0.8f; break;
        default: floorHalf = 0.25f; break;
        }
        float sum = 0.0f;
        float sumSq = 0.0f;
        for (int i = 0; i < 128; ++i) {
            const float raw = rawAt(i);
            sum += raw;
            sumSq += raw * raw;
        }
        const float center = sum / 128.0f;
        const float variance = std::max(0.0f, sumSq / 128.0f - center * center);
        const float half = std::max(floorHalf, std::sqrt(variance) * 2.0f);

        const int gx0 = 20;
        const int gy0 = kBottomY + 22;
        for (int gy = 0; gy < 8; ++gy) {
            for (int gx = 0; gx < 16; ++gx) {
                const int i = gy * 16 + gx;
                const float t = std::clamp(
                    0.5f + (rawAt(i) - center) / (2.0f * half), 0.0f, 1.0f);
                // Smoothstep keeps the middle of the range readable, which is
                // where neighbouring cells actually differ.
                const float shaped = t * t * (3.0f - 2.0f * t);
                fillRect(pixels_, pitch_, gx0 + gx * 10, gy0 + gy * 6, 9, 5,
                         heatColor(layer_, shaped));
            }
        }
        frameRect(pixels_, pitch_, gx0 - 1, gy0 - 1, 161, 49, kLine);

        drawText(20, kBottomY + 72, L"低", kFaint, false, 0);
        for (int i = 0; i < 5; ++i) {
            fillRect(pixels_, pitch_, 34 + i * 6, kBottomY + 72, 5, 5,
                     heatColor(layer_, static_cast<float>(i) / 4.0f));
        }
        drawText(64, kBottomY + 72, L"高", kFaint, false, 0);
        drawTextRight(12 + kColW - 8, kBottomY + 72, L"点击切换", kFaint, false, 0);
    }

    void drawTrend() {
        const int x = kRightX;
        fillRounded(pixels_, pitch_, x, kBottomY, kColW, kBottomH, 3, kEdgeDark);
        fillRounded(pixels_, pitch_, x + 1, kBottomY + 1, kColW - 2, kBottomH - 2, 2, kInset);
        hLine(pixels_, pitch_, x + 1, x + kColW - 2, kBottomY + 1, kLine);

        drawText(x + 8, kBottomY + 5, L"60 秒趋势", kCream, false, 1);
        const int legendX = x + 108;
        drawText(legendX, kBottomY + 6, L"光", kAmber, false, 0);
        fillRect(pixels_, pitch_, legendX + 13, kBottomY + 8, 3, 3, kAmber);
        drawText(legendX + 19, kBottomY + 6, L"氧", kCyan, false, 0);
        fillRect(pixels_, pitch_, legendX + 32, kBottomY + 8, 3, 3, kCyan);
        drawText(legendX + 38, kBottomY + 6, L"温", kRust, false, 0);
        fillRect(pixels_, pitch_, legendX + 51, kBottomY + 8, 3, 3, kRust);

        const int cx = x + 8;
        const int cy = kBottomY + 22;
        const int cw = kColW - 16;
        const int ch = 56;
        fillRect(pixels_, pitch_, cx, cy, cw, ch, rgba(4, 13, 12));
        for (int i = 1; i < 4; ++i) {
            hLine(pixels_, pitch_, cx, cx + cw - 1, cy + ch * i / 4, kLine);
        }
        frameRect(pixels_, pitch_, cx, cy, cw, ch, kLine);

        if (hist_.count >= 2) {
            auto yFor = [&](float value, float lo, float hi) {
                const float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
                return cy + ch - 1 - static_cast<int>(t * static_cast<float>(ch - 2));
            };
            for (int i = 1; i < hist_.count; ++i) {
                const int x0 = cx + (i - 1) * (cw - 1) / 59;
                const int x1 = cx + i * (cw - 1) / 59;
                const int ly0 = yFor(hist_.at(hist_.light, i - 1), 0.0f, 100.0f);
                const int ly1 = yFor(hist_.at(hist_.light, i), 0.0f, 100.0f);
                const int oy0 = yFor(hist_.at(hist_.oxygen, i - 1), 0.0f, 100.0f);
                const int oy1 = yFor(hist_.at(hist_.oxygen, i), 0.0f, 100.0f);
                const int ty0 = yFor(hist_.at(hist_.temp, i - 1), 15.0f, 27.0f);
                const int ty1 = yFor(hist_.at(hist_.temp, i), 15.0f, 27.0f);
                plotLine(pixels_, pitch_, x0, ly0, x1, ly1, kAmber);
                plotLine(pixels_, pitch_, x0, oy0, x1, oy1, kCyan);
                plotLine(pixels_, pitch_, x0, ty0, x1, ty1, kRust);
            }
        }
    }

    void drawButton(const SDL_Rect& r, const std::wstring& label,
                    Uint32 accent, int id, bool hot) {
        const bool down = (pressed_ == id && pressTimer_ > 0.0f);
        fillRect(pixels_, pitch_, r.x, r.y, r.w, r.h, down ? rgba(9, 26, 22) : rgba(15, 38, 32));
        frameRect(pixels_, pitch_, r.x, r.y, r.w, r.h, hot ? accent : kLine);
        hLine(pixels_, pitch_, r.x + 1, r.x + r.w - 2, r.y + 1,
              down ? kEdgeDark : lerpColor(kLine, accent, 0.35f));
        const int tw = text_.width(label);
        drawText(r.x + (r.w - tw) / 2, r.y + (r.h - 12) / 2 + (down ? 1 : 0),
                 label, down ? kDim : kCream, false, 0);
    }

    void drawButtons(bool paused) {
        drawButton(pauseRect_, paused ? L"继续" : L"暂停", kMoss, 1,
                   hit(pauseRect_, mouseX_, mouseY_));
        drawButton(settingsRect_, L"设置", kCyan, 2,
                   hit(settingsRect_, mouseX_, mouseY_));
        drawButton(closeRect_, L"关闭", kCyan, 3,
                   hit(closeRect_, mouseX_, mouseY_));
        drawButton(exitRect_, L"退出", kRust, 4,
                   hit(exitRect_, mouseX_, mouseY_));
    }

    void drawSettingsButtons(bool tools) {
        drawButton(pauseRect_, L"返回", kCyan, 1,
                   hit(pauseRect_, mouseX_, mouseY_));
        drawButton(settingsRect_, tools ? L"收起工具" : L"恢复默认", kAmber, 2,
                   hit(settingsRect_, mouseX_, mouseY_));
        drawButton(closeRect_, L"关闭", kCyan, 3,
                   hit(closeRect_, mouseX_, mouseY_));
        drawButton(exitRect_, L"退出", kRust, 4,
                   hit(exitRect_, mouseX_, mouseY_));
    }

    void drawTab(const SDL_Rect& r, const wchar_t* label, const wchar_t* english,
                 bool active) {
        const bool hot = hit(r, mouseX_, mouseY_);
        const Uint32 accent = active ? kMoss : (hot ? kDim : kLine);
        fillRect(pixels_, pitch_, r.x, r.y, r.w, r.h,
                 active ? rgba(28, 58, 48) : rgba(12, 28, 24));
        frameRect(pixels_, pitch_, r.x, r.y, r.w, r.h, accent);
        if (active) {
            hLine(pixels_, pitch_, r.x + 1, r.x + r.w - 2, r.y + 1,
                  lerpColor(kMoss, kIce, 0.35f));
        }
        const int lw = text_.width(label);
        const int ew = text_.width(english);
        const int x = r.x + (r.w - (lw + 6 + ew)) / 2;
        drawText(x, r.y + 6, label, active ? kCream : kDim, false, 0);
        drawText(x + lw + 6, r.y + 7, english, kFaint, false, 0);
    }

    void drawStepButton(const SDL_Rect& r, bool plus, bool enabled, int id) {
        const bool down = (pressed_ == id && pressTimer_ > 0.0f);
        const bool hot = enabled && hit(r, mouseX_, mouseY_);
        const Uint32 border = enabled ? (hot ? kMoss : kLine) : rgba(34, 52, 46);
        fillRect(pixels_, pitch_, r.x, r.y, r.w, r.h,
                 down ? rgba(9, 26, 22) : rgba(15, 38, 32));
        frameRect(pixels_, pitch_, r.x, r.y, r.w, r.h, border);
        const int cx = r.x + r.w / 2;
        const int cy = r.y + r.h / 2;
        const Uint32 glyph = enabled ? (hot ? kCream : kDim) : rgba(50, 70, 60);
        hLine(pixels_, pitch_, cx - 4, cx + 4, cy, glyph);
        if (plus) vLine(pixels_, pitch_, cx, cy - 4, cy + 4, glyph);
    }

    void drawTogglePill(const SDL_Rect& r, const SettingItem& item) {
        const bool on = item.kind == Kind::scale ? *item.value >= 150
                                                : *item.value != 0;
        const std::wstring text = item.kind == Kind::scale
            ? settingValueText(item) : std::wstring(on ? L"开" : L"关");
        const bool hot = hit(r, mouseX_, mouseY_);
        const Uint32 accent = item.kind == Kind::scale ? (on ? kCyan : kLine)
                             : (on ? kMoss : kLine);
        fillRect(pixels_, pitch_, r.x, r.y, r.w, r.h,
                 on ? rgba(24, 52, 44) : rgba(16, 30, 26));
        frameRect(pixels_, pitch_, r.x, r.y, r.w, r.h,
                  hot ? lerpColor(accent, kIce, 0.3f) : accent);
        const int tw = text_.width(text);
        drawText(r.x + (r.w - tw) / 2, r.y + 4, text, on ? kCream : kDim, false, 0);
    }

    std::wstring settingValueText(const SettingItem& item) const {
        return std::to_wstring(*item.value) + item.suffix;
    }

    void drawSettingRow(const SettingItem& item, int index) {
        const SDL_Rect row = settingRowRect(index);
        if (index % 2 == 1) {
            fillRect(pixels_, pitch_, row.x, row.y, row.w, row.h - 2,
                     rgba(10, 24, 20));
        }
        drawText(row.x + 6, row.y + 8, item.label, kDim, false, 0);
        if (item.kind == Kind::stepper) {
            drawStepButton(stepMinusRect(row), false,
                           *item.value > item.minValue, 10 + index);
            drawStepButton(stepPlusRect(row), true,
                           *item.value < item.maxValue, 10 + index);
            drawTextRight(stepPlusRect(row).x - 10, row.y + 8,
                          settingValueText(item), kCream, false, 0);
        } else {
            drawTogglePill(toggleRect(row), item);
        }
    }

    void drawSettingsTips() {
        const int y = 330;
        const int h = 150;
        fillRounded(pixels_, pitch_, 12, y, 396, h, 3, kEdgeDark);
        fillRounded(pixels_, pitch_, 13, y + 1, 394, h - 2, 2, kInset);
        hLine(pixels_, pitch_, 13, 406, y + 1, kLine);
        drawText(20, y + 7, L"提示", kCream, false, 1);
        drawText(20 + text_.width(L"提示") + 6, y + 8, L"TIPS", kFaint, false, 0);
        static const wchar_t* lines[6] = {
            L"左键拖动挂件，右键打开或关闭面板",
            L"开启鼠标穿透后，左键直接落到桌面",
            L"在挂件上右键一次即可恢复操作",
            L"设置自动保存到 ecobottle.cfg",
            L"面板底部的生态日志记录最近八次事件",
            L"演化状态每 5 秒写入 ecobottle.state",
        };
        for (int i = 0; i < 6; ++i) {
            drawText(24, y + 28 + i * 19, lines[i], kDim, false, 0);
        }
    }

    static const wchar_t* creatureKindName(int kind) {
        switch (kind) {
        case 0: return L"米虾";
        case 1: return L"鳑鲏";
        case 2: return L"豆娘稚虫";
        case 3: return L"水蚯蚓";
        case 4: return L"颤蚓";
        default: return L"水草";
        }
    }

    static const wchar_t* creatureStageName(int kind, int stage) {
        if (kind == 5) {
            static const wchar_t* names[4] = {L"幼苗", L"成型", L"成熟", L"衰亡"};
            return names[std::clamp(stage, 0, 3)];
        }
        static const wchar_t* names[3] = {L"幼体", L"亚成", L"成体"};
        return names[std::clamp(stage, 0, 2)];
    }

    void drawChoice(const SDL_Rect& r, const std::wstring& label, bool active,
                    bool enabled) {
        const bool hot = enabled && hit(r, mouseX_, mouseY_);
        fillRect(pixels_, pitch_, r.x, r.y, r.w, r.h,
                 active ? rgba(30, 62, 52) : rgba(16, 30, 26));
        frameRect(pixels_, pitch_, r.x, r.y, r.w, r.h,
                  active ? kMoss : (hot ? kDim : kLine));
        if (active) {
            hLine(pixels_, pitch_, r.x + 1, r.x + r.w - 2, r.y + 1,
                  lerpColor(kMoss, kIce, 0.35f));
        }
        const int tw = text_.width(label);
        drawText(r.x + (r.w - tw) / 2, r.y + 7, label,
                 active ? kCream : (enabled ? kDim : kFaint), false, 0);
    }

    void drawProfileCard(const Ecosystem& eco) {
        const int y = 162;
        const int h = 300;
        fillRounded(pixels_, pitch_, 12, y, 396, h, 3, kEdgeDark);
        fillRounded(pixels_, pitch_, 13, y + 1, 394, h - 2, 2, kInset);
        hLine(pixels_, pitch_, 13, 406, y + 1, kLine);
        drawText(20, y + 7, L"个体档案", kCream, false, 1);
        drawText(20 + text_.width(L"个体档案") + 6, y + 8, L"PROFILE", kFaint, false, 0);

        if (!hasProfile_) {
            drawText(24, y + 40, L"选择「个体档案」后，在瓶内", kDim, false, 0);
            drawText(24, y + 60, L"左键点选一只生物即可读取。", kDim, false, 0);
            drawText(24, y + 90, L"档案只读取状态，不会惊扰个体。", kFaint, false, 0);
            drawText(24, y + 112, L"点空白处可取消当前选择。", kFaint, false, 0);
            return;
        }

        Ecosystem::CreatureInfo info = profile_;
        profileAlive_ = eco.creatureById(info.id, info.kind, info);
        if (profileAlive_) profile_ = info;

        auto pct = [](float v) {
            return std::to_wstring(static_cast<int>(
                std::lround(std::clamp(v, 0.0f, 1.0f) * 100.0f))) + L"%";
        };
        int ty = y + 32;
        const std::wstring head = std::wstring(creatureKindName(info.kind))
                                + L"  #" + std::to_wstring(info.id);
        drawText(20, ty, head, kIce, false, 1);
        drawTextRight(396, ty, creatureStageName(info.kind, info.stage), kMoss, false, 0);
        ty += 22;

        auto line = [&](const wchar_t* label, const std::wstring& value, Uint32 color) {
            drawText(24, ty, label, kDim, false, 0);
            drawTextRight(396, ty, value, color, false, 0);
            ty += 17;
        };
        line(L"年龄", fmt(info.age, L"%.0f s"), kCream);
        line(L"能量", fmt(info.energy, L"%.0f"),
             info.energy > 55.0f ? kMoss : (info.energy > 25.0f ? kAmber : kRust));
        line(L"健康", pct(info.health),
             info.health > 0.7f ? kMoss : (info.health > 0.4f ? kAmber : kRust));
        line(L"生长", pct(info.growth), kCream);
        line(L"附生藻", pct(info.epiphyte),
             info.epiphyte > 0.40f ? kAmber : kDim);
        if (info.kind == 0 || info.kind == 1 || info.kind == 2) {
            line(L"繁殖冷却", fmt(info.breedCooldown, L"%.0f s"), kDim);
        }
        if (info.kind == 1) {
            line(L"领地", info.territory ? L"已建立" : L"巡游中",
                 info.territory ? kAmber : kDim);
        }

        ty += 4;
        hLine(pixels_, pitch_, 20, 400, ty, kLine);
        ty += 8;
        if (info.kind == 5) {
            line(L"基因 · 高度", fmt(info.pg.height, L"%.2f"), kCream);
            line(L"基因 · 密度", fmt(info.pg.density, L"%.2f"), kCream);
            line(L"基因 · 遮光", fmt(info.pg.shade, L"%.2f"), kCream);
            line(L"基因 · 啃食", fmt(info.pg.graze, L"%.2f"), kCream);
        } else {
            line(L"基因 · 体型", fmt(info.g.size, L"%.2f"), kCream);
            line(L"基因 · 速度", fmt(info.g.speed, L"%.2f"), kCream);
            line(L"基因 · 耐氧", fmt(info.g.oxygen, L"%.2f"), kCream);
            line(L"基因 · 温度", fmt(info.g.temp, L"%.2f"), kCream);
            drawText(24, ty, L"谱系", kDim, false, 0);
            drawTextRight(396, ty,
                          (info.g.lineage >= 0.0f ? L"+" : L"")
                          + fmt(info.g.lineage, L"%.2f"),
                          info.g.lineage >= 0.0f ? kMoss : kRust, false, 0);
        }
        drawText(24, y + h - 22,
                 profileAlive_ ? L"档案随个体实时刷新"
                               : L"该个体已凋亡，档案保留最后读数",
                 profileAlive_ ? kFaint : kRust, false, 0);
    }

    void drawToolsPage(const Ecosystem& eco) {
        drawChoice(toolOffRect_, L"关闭工具", toolMode_ == 0, true);
        drawChoice(toolProfileRect_, L"个体档案", toolMode_ == 1, true);
        drawChoice(toolDecorRect_, L"放置造景", toolMode_ == 2, true);

        static const wchar_t* decorNames[4] = {L"卵石", L"沉木", L"贝壳", L"清除"};
        const bool decorOn = toolMode_ == 2;
        for (int i = 0; i < 4; ++i) {
            drawChoice(decorKindRect_[i], decorNames[i],
                       decorOn && decorKind_ == i, decorOn);
        }
        drawText(16, 146, L"在瓶内左键使用当前工具；关闭面板即恢复拖拽",
                 kFaint, false, 0);
        drawProfileCard(eco);
    }

    void drawSettingsPage(const Ecosystem& eco) {
        drawTab(tabSystemRect_, L"系统", L"SYSTEM", settingsTab_ == 0);
        drawTab(tabEcoRect_, L"生态", L"ECO", settingsTab_ == 1);
        drawTab(tabToolRect_, L"工具", L"TOOL", settingsTab_ == 2);

        if (settingsTab_ == 2) {
            drawToolsPage(eco);
            return;
        }

        SettingItem items[kMaxSettingRows];
        const int count = settingItems(settingsTab_, items);
        for (int i = 0; i < count; ++i) {
            drawSettingRow(items[i], i);
        }
        if (settingsTab_ == 0) {
            drawSettingsTips();
        } else if (ecoPage_ == 1) {
            drawText(18, kSettingsY + count * kSettingsRowH + 4,
                     L"0% 关闭该类事件 · 100% 为季节基准权重", kFaint, false, 0);
            drawButton(ecoBackRect_, L"← 返回生态设置", kCyan, 8,
                       hit(ecoBackRect_, mouseX_, mouseY_));
        } else {
            drawButton(ecoEventsRect_, L"事件概率 · 八类独立权重  >", kMoss, 8,
                       hit(ecoEventsRect_, mouseX_, mouseY_));
            drawButton(ecoResetRect_, L"重置生态系统", kRust, 9,
                       hit(ecoResetRect_, mouseX_, mouseY_));
        }
    }

    void drawExitConfirm() {
        const SDL_Rect card = confirmCardRect_;
        fillRounded(pixels_, pitch_, card.x, card.y, card.w, card.h, 4, kEdgeDark);
        fillRounded(pixels_, pitch_, card.x + 1, card.y + 1, card.w - 2, card.h - 2, 3, kInset);
        hLine(pixels_, pitch_, card.x + 1, card.x + card.w - 2, card.y + 1, kLine);
        drawText(card.x + 16, card.y + 14, L"确认退出", kCream, true, 1);
        drawText(card.x + 16 + text_.width(L"确认退出", true) + 8, card.y + 17,
                 L"EXIT", kFaint, false, 0);
        drawText(card.x + 18, card.y + 46, L"退出前会自动保存演化进度，", kDim, false, 0);
        drawText(card.x + 18, card.y + 66, L"下次启动从当前状态继续。", kDim, false, 0);
        drawButton(confirmCancelRect_, L"取消", kCyan, 21,
                   hit(confirmCancelRect_, mouseX_, mouseY_));
        drawButton(confirmExitRect_, L"确认退出", kRust, 22,
                   hit(confirmExitRect_, mouseX_, mouseY_));
    }

    void drawResetConfirm() {
        const SDL_Rect card = confirmResetCardRect_;
        fillRounded(pixels_, pitch_, card.x, card.y, card.w, card.h, 4, kEdgeDark);
        fillRounded(pixels_, pitch_, card.x + 1, card.y + 1, card.w - 2, card.h - 2, 3, kInset);
        hLine(pixels_, pitch_, card.x + 1, card.x + card.w - 2, card.y + 1, kLine);
        drawText(card.x + 16, card.y + 14, L"重置生态系统", kCream, true, 1);
        drawText(card.x + 16 + text_.width(L"重置生态系统", true) + 8, card.y + 17,
                 L"RESET", kFaint, false, 0);
        drawText(card.x + 18, card.y + 46, L"将清空瓶内全部动植物、卵库与演化", kDim, false, 0);
        drawText(card.x + 18, card.y + 66, L"进度，重新开始一缸全新的生态。", kDim, false, 0);
        drawText(card.x + 18, card.y + 86, L"设置项与 ecobottle.cfg 不会被修改。", kFaint, false, 0);
        drawButton(confirmResetCancelRect_, L"取消", kCyan, 23,
                   hit(confirmResetCancelRect_, mouseX_, mouseY_));
        drawButton(confirmResetOkRect_, L"确认重置", kRust, 24,
                   hit(confirmResetOkRect_, mouseX_, mouseY_));
    }

    void render(const Ecosystem& eco, bool paused, float dt) {
        SDL_Surface* surface = canvas_.surface();
        if (!surface) return;
        pixels_ = static_cast<Uint32*>(surface->pixels);
        pitch_ = surface->pitch / 4;
        std::memset(pixels_, 0, static_cast<size_t>(kPanelW) * kPanelH * 4);

        if (pressTimer_ > 0.0f) {
            pressTimer_ = std::max(0.0f, pressTimer_ - dt);
            if (pressTimer_ <= 0.0f) pressed_ = 0;
        }

        int wx = 0;
        int wy = 0;
        int gx = 0;
        int gy = 0;
        SDL_GetWindowPosition(window_, &wx, &wy);
        SDL_GetGlobalMouseState(&gx, &gy);
        mouseX_ = gx - wx;
        mouseY_ = gy - wy;

        const Snapshot s = makeSnapshot(eco);
        drawFrame();
        drawHeader(s, paused);
        if (page_ == 1) {
            drawSettingsPage(eco);
            drawSettingsButtons(settingsTab_ == 2);
        } else {
            drawEnvironment(s);
            drawCycle(s);
            drawLife(s);
            drawLog(s);
            drawHeatmap(s);
            drawTrend();
            drawButtons(paused);
        }
        if (confirmExit_) {
            drawExitConfirm();
        }
        if (confirmReset_) {
            drawResetConfirm();
        }
    }

    SDL_Window* window_ = nullptr;
    Uint32 windowId_ = 0;
    HWND hwnd_ = nullptr;
    platform::LayeredCanvas canvas_;
    PixelText text_;

    bool open_ = false;
    bool visible_ = false;
    float openT_ = 0.0f;
    float fps_ = 30.0f;
    int layer_ = 0;
    float renderTimer_ = 0.0f;
    int page_ = 0;          // 0 telemetry, 1 settings
    int settingsTab_ = 0;   // 0 system, 1 ecology, 2 tools
    int ecoPage_ = 0;       // 0 ecology basics, 1 per-event weights
    int toolMode_ = 0;      // 0 off, 1 profile, 2 decor (panel-scoped)
    int decorKind_ = 0;     // 0 pebble, 1 driftwood, 2 shell, 3 erase
    Ecosystem::CreatureInfo profile_{};
    bool hasProfile_ = false;
    bool profileAlive_ = false;
    bool settingsDirty_ = false;
    bool confirmExit_ = false;
    bool confirmReset_ = false;

    History hist_;
    float sampleTimer_ = 0.0f;

    int baseX_ = 0;
    int baseY_ = 0;
    bool baseSet_ = false;
    int appliedX_ = -10000;
    int appliedY_ = -10000;
    int lastBottleX_ = -10000;
    int lastBottleY_ = -10000;
    int mouseX_ = -1000;
    int mouseY_ = -1000;
    int pressed_ = 0;
    float pressTimer_ = 0.0f;

    SDL_Rect pauseRect_{16, kButtonY, kButtonW, kButtonH};
    SDL_Rect settingsRect_{117, kButtonY, kButtonW, kButtonH};
    SDL_Rect closeRect_{218, kButtonY, kButtonW, kButtonH};
    SDL_Rect exitRect_{319, kButtonY, kButtonW, kButtonH};
    SDL_Rect confirmCardRect_{60, 150, 300, 150};
    SDL_Rect confirmCancelRect_{76, 250, 120, 28};
    SDL_Rect confirmExitRect_{224, 250, 120, 28};
    SDL_Rect confirmResetCardRect_{50, 138, 320, 172};
    SDL_Rect confirmResetCancelRect_{64, 260, 130, 28};
    SDL_Rect confirmResetOkRect_{226, 260, 130, 28};
    SDL_Rect heatRect_{12, kBottomY, kColW, kBottomH};
    SDL_Rect tabSystemRect_{12, 44, 130, 24};
    SDL_Rect tabEcoRect_{146, 44, 130, 24};
    SDL_Rect tabToolRect_{280, 44, 128, 24};
    SDL_Rect ecoEventsRect_{12, 438, 396, 24};
    SDL_Rect ecoBackRect_{12, 438, 396, 24};
    SDL_Rect ecoResetRect_{12, 464, 396, 22};
    SDL_Rect toolOffRect_{12, 82, 124, 26};
    SDL_Rect toolProfileRect_{142, 82, 124, 26};
    SDL_Rect toolDecorRect_{272, 82, 136, 26};
    SDL_Rect decorKindRect_[4] = {
        {12, 114, 95, 24}, {113, 114, 95, 24},
        {214, 114, 95, 24}, {315, 114, 93, 24}
    };

    Uint32* pixels_ = nullptr;
    int pitch_ = 0;
};

} // namespace

int main(int /*argc*/, char** /*argv*/) {
    // Only one bottle may run: a second copy would fight over ecobottle.state.
    // If the mutex already exists, bring the running window forward and exit.
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\EcoBottle.SingleInstance");
    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(L"SDL_app", L"Native Swamp Bottle");
        if (!existing) existing = FindWindowW(nullptr, L"Native Swamp Bottle");
        if (existing) {
            ShowWindow(existing, SW_SHOWNORMAL);
            // Raise inside the topmost band even when Windows refuses the
            // foreground activation requested below.
            BringWindowToTop(existing);
            SetForegroundWindow(existing);
        }
        CloseHandle(instanceMutex);
        return 0;
    }
#ifdef ECO_DEBUG_STATS
    std::freopen("eco_debug.log", "w", stderr);
#endif
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
    // These windows are usually not the active window.  Without this hint SDL
    // drops the mouse press that activates them, so the first click on the
    // bottle or the panel would be swallowed.
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        showFatal(SDL_GetError());
        return 1;
    }

    loadSettings();
    // Keep the Run entry pointing at the current executable (it may have been
    // moved since the toggle was enabled).
    if (g_settings.autoStart) platform::setAutoStart(true);

    // Topmost is applied explicitly through SetWindowPos below and reasserted
    // once per second, so the SDL topmost flag is not required.
    Uint32 windowFlags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN;

    SDL_Window* window = SDL_CreateWindow(
        "Native Swamp Bottle",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        scaledScreenW(), scaledScreenH(),
        windowFlags);
    if (!window) {
        showFatal(SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = nullptr;
    HWND hwnd = nullptr;
    const bool layered = platform::makeLayeredTopmost(window, hwnd);
    if (!layered) {
        showFatal("Could not make the window layered.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    if (!platform::installHitTest(hwnd)) {
        showFatal("Could not install the window hit-test handler.");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    platform::LayeredCanvas canvas(hwnd, art::kScreenW, art::kScreenH);
    canvas.setScale(viewScale());
    if (!canvas.valid()) {
        showFatal("Could not create the 32-bit layered canvas.");
        canvas.release();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateSoftwareRenderer(canvas.surface());
    if (!renderer) {
        showFatal(SDL_GetError());
        canvas.release();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    platform::placeAtDesktopCorner(window);
    const Uint32 bottleWindowId = SDL_GetWindowID(window);

    Ecosystem ecosystem;
    // Resume from the last auto-saved snapshot.  A missing, truncated or
    // version-mismatched file falls back to a fresh bottle.
    if (!loadEcosystemState(ecosystem)) {
        ecosystem.reset();
    }
    BottleRenderer bottle(renderer);
    AmbientEnvironment ambient;

    SDL_Window* panelWindow = SDL_CreateWindow(
        "Swamp Observation Panel", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        panelart::kPanelW, panelart::kPanelH,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);
    HWND panelHwnd = nullptr;
    if (!panelWindow || !platform::makeLayeredTopmost(panelWindow, panelHwnd)
        || !platform::installHitTest(panelHwnd)) {
        showFatal("Could not create the observation panel window.");
        if (panelWindow) SDL_DestroyWindow(panelWindow);
        bottle.release();
        SDL_DestroyRenderer(renderer);
        canvas.release();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    ObservationPanel panel(panelWindow, panelHwnd);
    if (!panel.valid()) {
        showFatal("Could not create the observation panel canvas.");
        panel.shutdown();
        SDL_DestroyWindow(panelWindow);
        bottle.release();
        SDL_DestroyRenderer(renderer);
        canvas.release();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    panel.sample(ecosystem, 1.0f);
    platform::setTopmost(hwnd, g_settings.topmost != 0);
    platform::setTopmost(panelHwnd, g_settings.topmost != 0);

    // Draw and install the first layered frame before showing the window, so
    // no opaque flash is ever visible.
    bottle.draw(ecosystem, false);
    SDL_RenderPresent(renderer);
    if (!canvas.present(g_settings.opacity * 255 / 100)) {
        showFatal("UpdateLayeredWindow failed.");
        panel.shutdown();
        SDL_DestroyWindow(panelWindow);
        bottle.release();
        SDL_DestroyRenderer(renderer);
        canvas.release();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    if (g_settings.clickThrough) {
        platform::setClickThrough(hwnd, true);
    }

    bool running = true;
    bool paused = false;
    bool dragging = false;
    bool pressPending = false;
    bool pressShift = false;
    int pressClientX = 0;
    int pressClientY = 0;
    int pressGlobalX = 0;
    int pressGlobalY = 0;
    int dragOffsetX = 0;
    int dragOffsetY = 0;
    float topmostTimer = 0.0f;
    float stateSaveTimer = 0.0f;
    // Click-through is only paused while the panel is open; the user's
    // setting itself is never rewritten by interaction.
    bool clickThroughSuspended = false;

    auto closePanelNow = [&]() {
        panel.close();
        ecosystem.clearSelection();
        clickThroughSuspended = false;
    };

    auto beginPress = [&](int cx, int cy) {
        int mouseX = 0, mouseY = 0;
        int windowX = 0, windowY = 0;
        SDL_GetGlobalMouseState(&mouseX, &mouseY);
        SDL_GetWindowPosition(window, &windowX, &windowY);
        pressClientX = cx;
        pressClientY = cy;
        pressGlobalX = mouseX;
        pressGlobalY = mouseY;
        dragOffsetX = mouseX - windowX;
        dragOffsetY = mouseY - windowY;
        pressShift = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
                  || ((SDL_GetModState() & KMOD_SHIFT) != 0);
        pressPending = true;
        dragging = false;
        SDL_CaptureMouse(SDL_TRUE);
    };

    auto beginDrag = [&]() {
        pressPending = false;
        dragging = true;
        SDL_CaptureMouse(SDL_TRUE);
    };


    auto moveDrag = [&]() {
        if (!dragging) {
            return;
        }
        int mouseX = 0, mouseY = 0;
        SDL_GetGlobalMouseState(&mouseX, &mouseY);
        SDL_SetWindowPosition(window, mouseX - dragOffsetX, mouseY - dragOffsetY);
#ifdef ECO_DEBUG_STATS
        static int debugMoveCount = 0;
        if (debugMoveCount < 5) {
            int wx = 0, wy = 0;
            SDL_GetWindowPosition(window, &wx, &wy);
            std::fprintf(stderr, "[drag] move mouse=%d,%d -> win=%d,%d\n",
                         mouseX, mouseY, wx, wy);
            std::fflush(stderr);
            ++debugMoveCount;
        }
#endif
    };

    auto endDrag = [&]() {
        if (dragging || pressPending) {
            dragging = false;
            pressPending = false;
            SDL_CaptureMouse(SDL_FALSE);
        }
    };

    int appliedScale = g_settings.scale;
    int appliedTopmost = g_settings.topmost;
    int appliedClickThrough = g_settings.clickThrough;
    bool bottleDirty = true;
    bool rightWasDown = false;

    // A tool (profile / scenery) needs the bottle to receive left clicks, so
    // pass-through is suspended while one is armed.  The click_through setting
    // itself is untouched: it comes back the moment the panel closes.
    auto syncClickThrough = [&]() {
        const bool want = g_settings.clickThrough != 0 && !clickThroughSuspended
                       && panel.toolMode() == 0;
        if (static_cast<int>(want) != appliedClickThrough) {
            appliedClickThrough = want ? 1 : 0;
            platform::setClickThrough(hwnd, want);
            if (want) {
                endDrag();
            }
            bottleDirty = true;
        }
    };

    auto applySystemSettings = [&]() {
        if (g_settings.scale != appliedScale) {
            if (canvas.setScale(viewScale())) {
                appliedScale = g_settings.scale;
                SDL_SetWindowSize(window, scaledScreenW(), scaledScreenH());
                platform::keepOnDesktop(window);
            }
        }
        if (g_settings.topmost != appliedTopmost) {
            appliedTopmost = g_settings.topmost;
            platform::setTopmost(hwnd, g_settings.topmost != 0);
            platform::setTopmost(panelHwnd, g_settings.topmost != 0);
        }
        syncClickThrough();
        bottleDirty = true;
    };

    float fpsSmooth = static_cast<float>(g_settings.maxFps);
    const Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 lastCounter = SDL_GetPerformanceCounter();
    Uint64 nextFrame = lastCounter;
#ifdef ECO_DEBUG_STATS
    Uint64 debugFpsStart = lastCounter;
    int debugFrames = 0;
#endif

    while (running) {
        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(
            static_cast<double>(nowCounter - lastCounter) / static_cast<double>(perfFreq));
        lastCounter = nowCounter;
        dt = std::clamp(dt, 0.0f, 0.1f);

        // Read the left button before SDL pumps events: SDL's own Windows
        // mouse tracking consumes the "pressed since last call" bit, so a
        // short outside click could otherwise go unseen while the panel is up.
        const bool leftTap = (GetAsyncKeyState(VK_LBUTTON) & 0x8001) != 0;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;

            case SDL_MOUSEBUTTONDOWN:
#ifdef ECO_DEBUG_STATS
                std::fprintf(stderr, "[sdl] button down %u at %d,%d\n",
                             static_cast<unsigned>(event.button.button),
                             event.button.x, event.button.y);
                std::fflush(stderr);
#endif
                if (event.button.windowID != bottleWindowId) {
                    const ObservationPanel::Action action = panel.handleEvent(event);
                    if (action == ObservationPanel::Action::togglePause) {
                        paused = !paused;
                        bottleDirty = true;
                    } else if (action == ObservationPanel::Action::closePanel) {
                        closePanelNow();
                        bottleDirty = true;
                    } else if (action == ObservationPanel::Action::exitApp) {
                        running = false;
                    } else if (action == ObservationPanel::Action::applySettings) {
                        bottleDirty = true;
                    } else if (action == ObservationPanel::Action::resetEcosystem) {
                        // Wipe the pond and write the fresh snapshot right away,
                        // so a crash right after reset cannot restore the old one.
                        ecosystem.reset();
                        saveEcosystemState(ecosystem);
                        panel.onEcosystemReset();
                        bottleDirty = true;
                    }
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
#ifdef ECO_DEBUG_STATS
                    std::fprintf(stderr, "[tool] enter mode=%d x=%d y=%d scale=%d\n",
                                 panel.toolMode(), event.button.x, event.button.y,
                                 viewScale());
                    std::fflush(stderr);
#endif
                    if (panel.isOpen() && panel.toolMode() == 0) {
                        // A left click outside the panel dismisses it and is
                        // not forwarded to the pond.
                        closePanelNow();
                        bottleDirty = true;
                        break;
                    }
                    if (panel.toolMode() != 0) {
                        // Tools only exist while the panel is open.  A left
                        // click reads a profile or places/removes scenery
                        // instead of starting a drag or startling the pond.
                        const int cx = event.button.x / viewScale();
                        const int cy = event.button.y / viewScale();
                        if (cx >= 0 && cx < art::kScreenW
                            && cy >= 0 && cy < art::kScreenH) {
                            if (panel.toolMode() == 1) {
                                Ecosystem::CreatureInfo info;
                                if (ecosystem.creatureAt(
                                        static_cast<float>(cx),
                                        static_cast<float>(cy), 16.0f, info)) {
                                    ecosystem.setSelection(info.id, info.kind);
                                    panel.setProfile(info);
#ifdef ECO_DEBUG_STATS
                                    std::fprintf(stderr, "[tool] profile hit id=%u kind=%d\n",
                                                 static_cast<unsigned>(info.id), info.kind);
                                    std::fflush(stderr);
#endif
                                } else {
                                    ecosystem.clearSelection();
                                    panel.clearProfile();
#ifdef ECO_DEBUG_STATS
                                    std::fprintf(stderr, "[tool] profile miss\n");
                                    std::fflush(stderr);
#endif
                                }
                            } else if (panel.decorKind() == 3) {
                                const bool removed = ecosystem.removeDecorAt(
                                    static_cast<float>(cx), static_cast<float>(cy));
                                (void)removed;
#ifdef ECO_DEBUG_STATS
                                std::fprintf(stderr, "[tool] decor erase %d at %d,%d\n",
                                             removed ? 1 : 0, cx, cy);
                                std::fflush(stderr);
#endif
                            } else {
                                ecosystem.placeDecor(panel.decorKind(),
                                                     static_cast<float>(cx),
                                                     static_cast<float>(cy));
#ifdef ECO_DEBUG_STATS
                                std::fprintf(stderr, "[tool] decor place kind=%d at %d,%d\n",
                                             panel.decorKind(), cx, cy);
                                std::fflush(stderr);
#endif
                            }
                            bottleDirty = true;
                        }
                        break;
                    }
                    beginPress(event.button.x, event.button.y);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    endDrag();
                    panel.toggle(ecosystem, paused, window);
                    if (!panel.isOpen()) {
                        ecosystem.clearSelection();
                        clickThroughSuspended = false;
                    }
                }
                break;

            case SDL_MOUSEBUTTONUP:
                if (event.button.windowID != bottleWindowId) {
                    panel.handleEvent(event);
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (dragging) {
                        endDrag();
                    } else if (pressPending) {
                        const int cx = event.button.x / viewScale();
                        const int cy = event.button.y / viewScale();
                        const bool shift = pressShift
                            || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0)
                            || ((SDL_GetModState() & KMOD_SHIFT) != 0);
                        pressPending = false;
                        SDL_CaptureMouse(SDL_FALSE);
                        if (!paused && cx >= 0 && cx < art::kScreenW
                            && cy >= 0 && cy < art::kScreenH) {
                            if (shift) {
                                ecosystem.feedAt(static_cast<float>(cx),
                                                 static_cast<float>(cy));
                            } else {
                                ecosystem.startleAt(static_cast<float>(cx),
                                                    static_cast<float>(cy));
                            }
                        }
                    }
                }
                break;

            case SDL_MOUSEMOTION:
                if (event.motion.windowID != bottleWindowId) {
                    break;
                }
                if (panel.toolMode() != 0) {
                    // The ghost cursor has to follow the pointer even while the
                    // simulation is paused.
                    bottleDirty = true;
                }
                if (pressPending && !dragging) {
                    int gx = 0, gy = 0;
                    SDL_GetGlobalMouseState(&gx, &gy);
                    const int dx = gx - pressGlobalX;
                    const int dy = gy - pressGlobalY;
                    if (dx * dx + dy * dy >= 25) {
                        beginDrag();
                        moveDrag();
                    }
                } else if (dragging) {
                    moveDrag();
                }
                break;

            case SDL_MOUSEWHEEL:
                if (event.wheel.windowID == panel.windowId()) {
                    panel.handleEvent(event);
                    break;
                }
                if (SDL_GetMouseFocus() == window) {
                    int mx = 0, my = 0;
                    SDL_GetMouseState(&mx, &my);
                    mx /= viewScale();
                    my /= viewScale();
                    if (!paused && mx >= 0 && mx < art::kScreenW
                        && my >= 0 && my < art::kScreenH) {
                        ecosystem.adjustLightAt(static_cast<float>(mx),
                                                static_cast<float>(my),
                                                static_cast<float>(event.wheel.y) * 6.0f);
                    }
                }
                break;

            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    if (!panel.cancelExitConfirm() && panel.isOpen()) {
                        closePanelNow();
                    }
                }
                break;

            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    // The panel simply hides; closing the bottle (Alt+F4 /
                    // WM_CLOSE) exits through the normal path so settings and
                    // the simulation snapshot are written to disk.
                    if (event.window.windowID == panel.windowId()) {
                        closePanelNow();
                    } else if (event.window.windowID == bottleWindowId) {
                        running = false;
                    }
                }
                break;

            default:
                break;
            }
        }

        if (!running) {
            break;
        }

        // A setting changed from the panel: persist it, apply the system-level
        // parts (scale / topmost / click-through) and redraw immediately.
        if (panel.consumeSettingsDirty()) {
            saveSettings();
            applySystemSettings();
            panel.refresh(ecosystem, paused);
        }

        // Arming / releasing a tool changes whether the bottle must accept
        // clicks, so re-evaluate the pass-through state every frame.  It is a
        // couple of comparisons unless the style actually flips.
        syncClickThrough();

        // A click anywhere outside the panel dismisses it.  SDL only reports
        // mouse buttons for its own windows, so clicks that land on the
        // desktop or another application have to be polled.  A click on the
        // panel itself is excluded; so is a click inside the bottle while a
        // tool is armed (that click is a genuine edit of the pond).  If a
        // confirmation overlay is up, the click only dismisses the overlay.
        {
            if (leftTap && panel.isOpen() && panel.toolMode() == 0) {
                POINT cursor{};
                RECT bounds{};
                if (GetCursorPos(&cursor) && panelHwnd
                    && GetWindowRect(panelHwnd, &bounds)
                    && !PtInRect(&bounds, cursor)) {
                    if (!panel.cancelExitConfirm()) {
                        closePanelNow();
                    }
                    bottleDirty = true;
                }
            }
        }

        // Click-through hides the bottle from the mouse, so a global right
        // click over it is the only way back to it.  One press opens the
        // panel and suspends pass-through for as long as the panel stays
        // open; the saved click_through setting is never rewritten, so the
        // user's choice survives a restart.  Only poll while the style is
        // really applied: with a tool armed the bottle already receives real
        // clicks and a right click there should just close the panel.
        if (appliedClickThrough) {
            const bool rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
#ifdef ECO_DEBUG_STATS
            static int dbgPoll = 0;
            if (++dbgPoll % 15 == 0 || (rightDown && !rightWasDown)) {
                std::fprintf(stderr, "[ct] down=%d was=%d\n", rightDown ? 1 : 0,
                             rightWasDown ? 1 : 0);
                std::fflush(stderr);
            }
#endif
            if (rightDown && !rightWasDown) {
                POINT cursor{};
                RECT bounds{};
                if (GetCursorPos(&cursor) && GetWindowRect(hwnd, &bounds)
                    && PtInRect(&bounds, cursor)) {
#ifdef ECO_DEBUG_STATS
                    std::fprintf(stderr, "[ct] restore at %ld,%ld\n",
                                 static_cast<long>(cursor.x),
                                 static_cast<long>(cursor.y));
                    std::fflush(stderr);
#endif
                    clickThroughSuspended = true;
                    syncClickThrough();
                    if (!panel.isOpen()) {
                        panel.toggle(ecosystem, paused, window);
                    }
                    panel.refresh(ecosystem, paused);
                    bottleDirty = true;
                }
            }
            rightWasDown = rightDown;
        } else {
            rightWasDown = false;
        }

        // Keep tracking the cursor even if a motion event was coalesced.
        moveDrag();

        // Re-assert topmost once a second.  Other topmost desktop widgets can
        // otherwise be inserted above this window when they appear.
        topmostTimer += dt;
        if (topmostTimer >= 1.0f && hwnd) {
            topmostTimer = 0.0f;
            if (g_settings.topmost) {
                platform::setTopmost(hwnd, true);
                panel.reassertTopmost();
            }
        }

        ambient.update(dt);
        if (!paused) {
            ecosystem.update(dt, ambient.factors());
        }
        panel.sample(ecosystem, dt);

        // Auto-save the simulation every five seconds so a hard power loss or
        // killed process resumes from (at worst) five seconds behind.  Writes
        // are atomic: temp file + MoveFileEx replace.
        stateSaveTimer += dt;
        if (stateSaveTimer >= 5.0f) {
            stateSaveTimer = 0.0f;
            saveEcosystemState(ecosystem);
        }

        // While a tool is armed the bottle shows a ghost outline: a hover
        // bracket over the individual a click would read, or the footprint the
        // next piece of scenery would occupy.  Purely visual.
        int previewMode = 0;
        int previewX = -1;
        int previewY = -1;
        if (panel.toolMode() != 0) {
            POINT cursor{};
            if (GetCursorPos(&cursor) && ScreenToClient(hwnd, &cursor)) {
                previewMode = panel.toolMode();
                previewX = cursor.x / viewScale();
                previewY = cursor.y / viewScale();
            }
        }
        bottle.setToolPreview(previewMode, panel.decorKind(), previewX, previewY);

        // While paused nothing in the bottle changes, so skip the whole
        // draw/present path until the user does something.
        if (!paused || bottleDirty) {
            bottle.draw(ecosystem, paused);
            SDL_RenderPresent(renderer);
            if (!canvas.present(g_settings.opacity * 255 / 100)) {
                running = false;
            }
            bottleDirty = false;
        }

        const float instantFps = dt > 0.0001f ? 1.0f / dt : fpsSmooth;
        fpsSmooth += (instantFps - fpsSmooth) * std::min(1.0f, dt * 4.0f);
        panel.update(ecosystem, paused, dt, window, fpsSmooth);

#ifdef ECO_DEBUG_STATS
        ++debugFrames;
        if (nowCounter - debugFpsStart >= perfFreq * 5ull) {
            const double seconds = static_cast<double>(nowCounter - debugFpsStart)
                                 / static_cast<double>(perfFreq);
            std::fprintf(stderr, "[fps] %.2f\n", static_cast<double>(debugFrames) / seconds);
            std::fflush(stderr);
            debugFrames = 0;
            debugFpsStart = nowCounter;
        }
#endif

        // Sleep until the next slot at the user-selected frame rate.  If we are
        // late, re-base the clock instead of catching up with a burst.
        const int fpsCap = std::max(10, g_settings.maxFps);
        nextFrame += static_cast<Uint64>(
            static_cast<double>(perfFreq) / static_cast<double>(fpsCap));
        const Uint64 afterPresent = SDL_GetPerformanceCounter();
        if (nextFrame > afterPresent) {
            const Uint64 remaining = nextFrame - afterPresent;
            const Uint32 sleepMs = static_cast<Uint32>(
                remaining * 1000ull / static_cast<Uint64>(perfFreq));
            if (sleepMs > 0) {
                SDL_Delay(sleepMs);
            }
        } else {
            nextFrame = afterPresent;
        }
    }

    endDrag();
    saveSettings();
    saveEcosystemState(ecosystem);
    // Local renderers/Panel are destroyed when this function returns, which is
    // after SDL_Quit().  Free everything that depends on SDL or on the layered
    // window while those systems are still alive.
    bottle.release();
    panel.shutdown();
    SDL_DestroyWindow(panelWindow);
    SDL_DestroyRenderer(renderer);
    canvas.release();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

// =============================================================================
//  Build / dependency notes
// =============================================================================
//  Dependency: SDL2 development package, version 2.0.16 or newer.
//              Tested with SDL2 2.32.10, Windows 10/11, MinGW-w64.
//              No SDL2_image / SDL2_ttf / SDL2_mixer and no asset files.
//
//  MinGW-w64 (from an SDL2-devel-*-mingw package):
//      set SDL2=C:\SDL2\x86_64-w64-mingw32
//      g++ -std=c++17 -O2 -Wall -Wextra ecobottle.cpp -o EcoBottle.exe ^
//          -I "%SDL2%\include\SDL2" -L "%SDL2%\lib" ^
//          -lmingw32 -lSDL2main -lSDL2 -lgdi32 -luser32 -lole32 -luuid -mwindows ^
//          -static-libgcc -static-libstdc++
//      copy "%SDL2%\bin\SDL2.dll" .
//      copy "C:\mingw64\bin\libwinpthread-1.dll" .
//      EcoBottle.exe
//
//  MSVC 2019/2022 (x64 Native Tools Command Prompt, SDL2-devel-*-VC package):
//      cl /nologo /std:c++17 /O2 /EHsc /I SDL2\include\SDL2 ecobottle.cpp ^
//         /link /LIBPATH:SDL2\lib\x64 SDL2main.lib SDL2.lib ole32.lib uuid.lib ^
//         /SUBSYSTEM:WINDOWS
//      copy SDL2\lib\x64\SDL2.dll .
//      EcoBottle.exe
//
//  Controls:
//      Left button drag : move the bottle
//      Right click      : open / close the pixel observation panel
//      Panel buttons    : pause / close / exit
//      Panel heatmap    : click or wheel to cycle the 16x8 micro-habitat layer
//      Esc              : close the observation panel
//
//  Transparency uses UpdateLayeredWindow with a 32-bit DIB and per-pixel
//  alpha; empty pixels use alpha 1 so the result is visually transparent.
//  A WM_NCHITTEST subclass keeps the whole rectangle draggable.  SDL's
//  software renderer draws straight into the DIB, so no D3D hook is needed.
// =============================================================================


