#include "render.h"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>

#include "factions.h"
#include "settlements.h"
#include "tree_rules.h"
#include "util.h"

namespace {
constexpr int kTilePx = 32;
constexpr int kTilesAtlasCols = 8;
constexpr int kTilesAtlasRows = 8;
constexpr int kFoamCols = 4;
constexpr int kFoamRows = 4;
constexpr int kObjectCols = 2;
constexpr int kObjectRows = 2;
constexpr int kShadowTexPx = 32;
constexpr int kFireTexPx = 16;
constexpr int kAOCornerTexPx = 16;
constexpr int kVignetteTexPx = 256;

constexpr uint32_t kDeepSeed = 0x3C6EF372u;
constexpr uint32_t kMidSeed = 0x9E3779B9u;
constexpr uint32_t kShallowSeed = 0xBB67AE85u;
constexpr uint32_t kGrassSeed = 0xA54FF53Au;
constexpr uint32_t kSandSeed = 0x510E527Fu;
constexpr uint32_t kFoodSeed = 0x5BE0CD19u;
constexpr uint32_t kFireSeed = 0xC1059ED8u;

constexpr int kTreeTrunkHeightFromBottomPx = 73;
constexpr int kTree1TrunkHeightFromBottomPx = 53;
constexpr int kTreeSeamOverlapPx = 1;

struct AtlasCoord {
  int col = 0;
  int row = 0;
};

SDL_Rect TilesRect(AtlasCoord coord) {
  assert(coord.col >= 0 && coord.col < kTilesAtlasCols);
  assert(coord.row >= 0 && coord.row < kTilesAtlasRows);
  return SDL_Rect{coord.col * kTilePx, coord.row * kTilePx, kTilePx, kTilePx};
}

SDL_Rect ObjectRect(AtlasCoord coord) {
  assert(coord.col >= 0 && coord.col < kObjectCols);
  assert(coord.row >= 0 && coord.row < kObjectRows);
  return SDL_Rect{coord.col * kTilePx, coord.row * kTilePx, kTilePx, kTilePx};
}

SDL_Rect FoamRect(uint8_t mask) {
  assert(mask < 16);
  int col = mask % kFoamCols;
  int row = mask / kFoamCols;
  assert(col >= 0 && col < kFoamCols);
  assert(row >= 0 && row < kFoamRows);
  return SDL_Rect{col * kTilePx, row * kTilePx, kTilePx, kTilePx};
}

const std::array<AtlasCoord, 4> kDeepWaterCoords = {
    AtlasCoord{0, 0}, AtlasCoord{1, 0}, AtlasCoord{2, 0}, AtlasCoord{3, 0}};
const std::array<AtlasCoord, 4> kMidWaterCoords = {
    AtlasCoord{4, 0}, AtlasCoord{5, 0}, AtlasCoord{6, 0}, AtlasCoord{7, 0}};
const std::array<AtlasCoord, 4> kShallowWaterCoords = {
    AtlasCoord{0, 1}, AtlasCoord{1, 1}, AtlasCoord{2, 1}, AtlasCoord{3, 1}};
const std::array<AtlasCoord, 8> kGrassCoords = {
    AtlasCoord{0, 2}, AtlasCoord{1, 2}, AtlasCoord{2, 2}, AtlasCoord{3, 2},
    AtlasCoord{0, 7}, AtlasCoord{1, 7}, AtlasCoord{2, 7}, AtlasCoord{3, 7}};
const std::array<AtlasCoord, 8> kSandCoords = {
    AtlasCoord{4, 2}, AtlasCoord{5, 2}, AtlasCoord{6, 2}, AtlasCoord{7, 2},
    AtlasCoord{4, 7}, AtlasCoord{5, 7}, AtlasCoord{6, 7}, AtlasCoord{7, 7}};

const std::array<AtlasCoord, 2> kTreeCoords = {AtlasCoord{0, 0}, AtlasCoord{1, 0}};
const std::array<AtlasCoord, 2> kFoodCoords = {AtlasCoord{0, 1}, AtlasCoord{1, 1}};

int AutoDetectCanopyOccludeSrcY(const std::string& path, int canopySrcH) {
  if (canopySrcH <= 0) return 0;
  SDL_Surface* loaded = IMG_Load(path.c_str());
  if (!loaded) return canopySrcH;
  SDL_Surface* surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
  SDL_FreeSurface(loaded);
  if (!surface) return canopySrcH;

  const int w = surface->w;
  const int h = surface->h;
  const int scanH = std::min(canopySrcH, h);
  if (w <= 0 || scanH <= 0) {
    SDL_FreeSurface(surface);
    return canopySrcH;
  }

  const int x0 = std::max(0, std::min(w, static_cast<int>(std::floor(w * 0.35f))));
  const int x1 = std::max(0, std::min(w, static_cast<int>(std::ceil(w * 0.65f))));
  if (x1 <= x0) {
    SDL_FreeSurface(surface);
    return canopySrcH;
  }

  constexpr uint8_t kAlphaThreshold = 32;
  constexpr int kMinOpaqueCount = 10;

  if (SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
  const int pitchPixels = surface->pitch / static_cast<int>(sizeof(uint32_t));
  const uint32_t* pixels = static_cast<const uint32_t*>(surface->pixels);

  int foundRowY = -1;
  for (int y = scanH - 1; y >= 0; --y) {
    int count = 0;
    const uint32_t* row = pixels + y * pitchPixels;
    for (int x = x0; x < x1; ++x) {
      uint8_t r = 0, g = 0, b = 0, a = 0;
      SDL_GetRGBA(row[x], surface->format, &r, &g, &b, &a);
      if (a > kAlphaThreshold) {
        ++count;
        if (count >= kMinOpaqueCount) break;
      }
    }
    if (count >= kMinOpaqueCount) {
      foundRowY = y;
      break;
    }
  }

  if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
  SDL_FreeSurface(surface);

  if (foundRowY < 0) return canopySrcH;
  return std::max(0, std::min(canopySrcH, foundRowY + 1));
}

SDL_Rect ShadowSrc() { return SDL_Rect{0, 0, kShadowTexPx, kShadowTexPx}; }
SDL_Rect FireSrc() { return SDL_Rect{0, 0, kFireTexPx, kFireTexPx}; }

template <size_t N>
SDL_Rect PickTilesVariant(const std::array<AtlasCoord, N>& coords, uint32_t h) {
  return TilesRect(coords[h % N]);
}

template <size_t N>
SDL_Rect PickObjectVariant(const std::array<AtlasCoord, N>& coords, uint32_t h) {
  return ObjectRect(coords[h % N]);
}

float Hash01(uint32_t x, uint32_t y, uint32_t seed) {
  uint32_t h = Hash2D(x, y, seed);
  return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }
float Smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

float ValueNoise2D(float x, float y, uint32_t seed) {
  int xi = static_cast<int>(std::floor(x));
  int yi = static_cast<int>(std::floor(y));
  float xf = x - static_cast<float>(xi);
  float yf = y - static_cast<float>(yi);
  float u = Smoothstep(xf);
  float v = Smoothstep(yf);

  auto v00 = Hash01(static_cast<uint32_t>(xi), static_cast<uint32_t>(yi), seed) * 2.0f - 1.0f;
  auto v10 = Hash01(static_cast<uint32_t>(xi + 1), static_cast<uint32_t>(yi), seed) * 2.0f - 1.0f;
  auto v01 = Hash01(static_cast<uint32_t>(xi), static_cast<uint32_t>(yi + 1), seed) * 2.0f - 1.0f;
  auto v11 = Hash01(static_cast<uint32_t>(xi + 1), static_cast<uint32_t>(yi + 1), seed) * 2.0f - 1.0f;

  float a = Lerp(v00, v10, u);
  float b = Lerp(v01, v11, u);
  return Lerp(a, b, v);
}

float Fbm2D(float x, float y, uint32_t seed, int octaves) {
  float sum = 0.0f;
  float amp = 0.5f;
  float freq = 1.0f;
  float norm = 0.0f;
  for (int i = 0; i < octaves; ++i) {
    sum += ValueNoise2D(x * freq, y * freq, seed + static_cast<uint32_t>(i) * 0x9E3779B9u) * amp;
    norm += amp;
    amp *= 0.5f;
    freq *= 2.0f;
  }
  if (!(norm > 0.0f)) return 0.0f;
  return sum / norm;
}

void Rotate(float x, float y, float radians, float& outX, float& outY) {
  float c = std::cos(radians);
  float s = std::sin(radians);
  outX = x * c - y * s;
  outY = x * s + y * c;
}

struct TintMod {
  Uint8 modR = 255;
  Uint8 modG = 255;
  Uint8 modB = 255;
  Uint8 addR = 0;
  Uint8 addG = 0;
  Uint8 addB = 0;
  Uint8 addA = 0;
};

TintMod MakeTintMods(float rMult, float gMult, float bMult, float expectedMaxExcess, Uint8 maxAddAlpha) {
  auto toU8Mod01 = [](float mult01) -> Uint8 {
    int v = static_cast<int>(std::lround(std::clamp(mult01, 0.0f, 1.0f) * 255.0f));
    v = std::clamp(v, 0, 255);
    return static_cast<Uint8>(v);
  };

  TintMod out;
  out.modR = toU8Mod01(rMult);
  out.modG = toU8Mod01(gMult);
  out.modB = toU8Mod01(bMult);

  float exR = std::max(0.0f, rMult - 1.0f);
  float exG = std::max(0.0f, gMult - 1.0f);
  float exB = std::max(0.0f, bMult - 1.0f);
  float maxEx = std::max({exR, exG, exB});
  if (maxEx > 0.0001f && expectedMaxExcess > 0.0001f && maxAddAlpha > 0u) {
    float t = std::clamp(maxEx / expectedMaxExcess, 0.0f, 1.0f);
    out.addA = static_cast<Uint8>(std::lround(t * static_cast<float>(maxAddAlpha)));
    if (out.addA > 0u) {
      out.addR = static_cast<Uint8>(std::lround((exR / maxEx) * 255.0f));
      out.addG = static_cast<Uint8>(std::lround((exG / maxEx) * 255.0f));
      out.addB = static_cast<Uint8>(std::lround((exB / maxEx) * 255.0f));
    }
  }
  return out;
}

TintMod GrassTint(int tx, int ty) {
  // Keep base calm: very small per-tile variation, rely on macro patches for interest.
  uint32_t ux = static_cast<uint32_t>(tx);
  uint32_t uy = static_cast<uint32_t>(ty);
  float tileV = (Hash01(ux, uy, 0xA11CE5EDu) * 2.0f - 1.0f) * 0.012f;  // ±1.2%
  float tileS = (Hash01(ux, uy, 0x7D3A2D11u) * 2.0f - 1.0f) * 0.010f;  // ±1.0%

  // Large-scale smooth tint (macro patches), rotated + domain-warped + fBm to avoid "grid cells".
  float baseX = static_cast<float>(tx);
  float baseY = static_cast<float>(ty);
  float rx = 0.0f, ry = 0.0f;
  Rotate(baseX / 55.0f, baseY / 55.0f, 0.72f, rx, ry);
  float warp = Fbm2D(rx * 0.35f, ry * 0.35f, 0xCB22A1D3u, 2) * 0.85f;
  float macro = Fbm2D(rx + warp, ry - warp * 0.6f, 0xE3B0C442u, 4);
  float macro2 = Fbm2D(rx - warp * 0.4f + 4.1f, ry + warp * 0.3f - 3.2f, 0x2F6E2B1Bu, 4);

  float macroV = macro * 0.020f;  // ~1–3% feel
  float macroS = macro2 * 0.012f;

  float value = std::clamp(1.0f + tileV + macroV, 0.92f, 1.06f);
  float sat = std::clamp(tileS + macroS, -0.06f, 0.06f);

  float r = value * (1.0f - sat * 0.18f);
  float g = value * (1.0f + sat * 0.22f);
  float b = value * (1.0f - sat * 0.16f);

  r = std::clamp(r, 0.85f, 1.08f);
  g = std::clamp(g, 0.85f, 1.08f);
  b = std::clamp(b, 0.85f, 1.08f);

  // Reduce additive "sparkle" to keep grass calm.
  return MakeTintMods(r, g, b, 0.06f, 14);
}

TintMod WaterTint(int tx, int ty, int coastDist) {
  // Keep water calm: only macro variation, lower contrast than land.
  float baseX = static_cast<float>(tx);
  float baseY = static_cast<float>(ty);
  float mx = 0.0f, my = 0.0f;
  Rotate(baseX / 55.0f, baseY / 55.0f, 0.58f, mx, my);
  float n = Fbm2D(mx, my, 0xBADC0FFEu, 3);
  float n2 = Fbm2D(mx + 1.3f, my - 4.1f, 0xC0FFEE11u, 3);

  float amp = 0.010f;
  if (coastDist <= 4) amp = 0.015f;
  if (coastDist <= 1) amp = 0.018f;
  float value = 1.0f + n * amp;

  // Thin shoreline rim band (subtle brightening right at the coast).
  if (coastDist <= 1) value *= 1.02f;

  float cool = n2 * 0.010f;
  float r = value * (1.0f - 0.02f + cool * -0.2f);
  float g = value * (1.0f - 0.01f + cool * 0.1f);
  float b = value * (1.0f + 0.02f + cool * 0.4f);

  r = std::clamp(r, 0.88f, 1.06f);
  g = std::clamp(g, 0.88f, 1.06f);
  b = std::clamp(b, 0.88f, 1.06f);

  return MakeTintMods(r, g, b, 0.06f, 24);
}

SDL_Rect MakeDstRect(float worldX, float worldY, float width, float height, const Camera& camera) {
  const float sx = (worldX - camera.x) * camera.zoom;
  const float sy = (worldY - camera.y) * camera.zoom;
  const float sw = width * camera.zoom;
  const float sh = height * camera.zoom;

  const int x = static_cast<int>(std::floor(sx + 0.5f));
  const int y = static_cast<int>(std::floor(sy + 0.5f));
  const int w = static_cast<int>(std::floor(sw + 0.5f));
  const int h = static_cast<int>(std::floor(sh + 0.5f));
  return SDL_Rect{x, y, w, h};
}

SDL_Texture* CreateShadowTexture(SDL_Renderer* renderer) {
  SDL_Surface* surface =
      SDL_CreateRGBSurfaceWithFormat(0, kShadowTexPx, kShadowTexPx, 32, SDL_PIXELFORMAT_RGBA32);
  if (!surface) {
    SDL_Log("Failed to create shadow surface: %s", SDL_GetError());
    return nullptr;
  }

  Uint32* pixels = static_cast<Uint32*>(surface->pixels);
  SDL_PixelFormat* fmt = surface->format;

  const float cx = (kShadowTexPx - 1) * 0.5f;
  const float cy = (kShadowTexPx - 1) * 0.6f;
  const float rx = kShadowTexPx * 0.4f;
  const float ry = kShadowTexPx * 0.22f;
  const float maxAlpha = 110.0f;

  for (int y = 0; y < kShadowTexPx; ++y) {
    for (int x = 0; x < kShadowTexPx; ++x) {
      float dx = (x - cx) / rx;
      float dy = (y - cy) / ry;
      float dist = dx * dx + dy * dy;
      Uint8 alpha = 0;
      if (dist < 1.0f) {
        float t = 1.0f - dist;
        alpha = static_cast<Uint8>(maxAlpha * t);
      }
      // White alpha mask so we can color-mod it for both black shadows and earthy ground AO.
      pixels[y * kShadowTexPx + x] = SDL_MapRGBA(fmt, 255, 255, 255, alpha);
    }
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (!texture) {
    SDL_Log("Failed to create shadow texture: %s", SDL_GetError());
    return nullptr;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
  return texture;
}

SDL_Texture* CreateAOCornerTexture(SDL_Renderer* renderer) {
  SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, kAOCornerTexPx, kAOCornerTexPx, 32,
                                                        SDL_PIXELFORMAT_RGBA32);
  if (!surface) {
    SDL_Log("Failed to create AO corner surface: %s", SDL_GetError());
    return nullptr;
  }

  Uint32* pixels = static_cast<Uint32*>(surface->pixels);
  SDL_PixelFormat* fmt = surface->format;

  constexpr float kMaxAlpha = 220.0f;
  const float denom = static_cast<float>(kAOCornerTexPx - 1);
  for (int y = 0; y < kAOCornerTexPx; ++y) {
    for (int x = 0; x < kAOCornerTexPx; ++x) {
      float fx = static_cast<float>(x) / denom;
      float fy = static_cast<float>(y) / denom;
      float d = std::sqrt(fx * fx + fy * fy);
      Uint8 a = 0;
      if (d < 1.0f) {
        float t = 1.0f - d;
        t = t * t;
        a = static_cast<Uint8>(std::lround(kMaxAlpha * t));
      }
      pixels[y * kAOCornerTexPx + x] = SDL_MapRGBA(fmt, 255, 255, 255, a);
    }
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (!texture) {
    SDL_Log("Failed to create AO corner texture: %s", SDL_GetError());
    return nullptr;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
  return texture;
}

SDL_Texture* CreateVignetteTexture(SDL_Renderer* renderer) {
  SDL_Surface* surface =
      SDL_CreateRGBSurfaceWithFormat(0, kVignetteTexPx, kVignetteTexPx, 32, SDL_PIXELFORMAT_RGBA32);
  if (!surface) {
    SDL_Log("Failed to create vignette surface: %s", SDL_GetError());
    return nullptr;
  }

  Uint32* pixels = static_cast<Uint32*>(surface->pixels);
  SDL_PixelFormat* fmt = surface->format;

  const float cx = (kVignetteTexPx - 1) * 0.5f;
  const float cy = (kVignetteTexPx - 1) * 0.5f;
  const float maxR = std::sqrt(cx * cx + cy * cy);

  // Keep center clean; only darken near edges.
  constexpr float kInner = 0.62f;
  constexpr float kOuter = 1.00f;
  for (int y = 0; y < kVignetteTexPx; ++y) {
    for (int x = 0; x < kVignetteTexPx; ++x) {
      float dx = static_cast<float>(x) - cx;
      float dy = static_cast<float>(y) - cy;
      float r = std::sqrt(dx * dx + dy * dy) / maxR;
      float t = std::clamp((r - kInner) / (kOuter - kInner), 0.0f, 1.0f);
      t = t * t;
      Uint8 a = static_cast<Uint8>(std::lround(255.0f * t));
      pixels[y * kVignetteTexPx + x] = SDL_MapRGBA(fmt, 255, 255, 255, a);
    }
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (!texture) {
    SDL_Log("Failed to create vignette texture: %s", SDL_GetError());
    return nullptr;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
  return texture;
}

SDL_Texture* CreateFireTexture(SDL_Renderer* renderer) {
  SDL_Surface* surface =
      SDL_CreateRGBSurfaceWithFormat(0, kFireTexPx, kFireTexPx, 32, SDL_PIXELFORMAT_RGBA32);
  if (!surface) {
    SDL_Log("Failed to create fire surface: %s", SDL_GetError());
    return nullptr;
  }

  Uint32* pixels = static_cast<Uint32*>(surface->pixels);
  SDL_PixelFormat* fmt = surface->format;

  const float cx = (kFireTexPx - 1) * 0.5f;
  const float cy = (kFireTexPx - 1) * 0.55f;
  const float radius = kFireTexPx * 0.45f;

  for (int y = 0; y < kFireTexPx; ++y) {
    for (int x = 0; x < kFireTexPx; ++x) {
      float dx = x - cx;
      float dy = y - cy;
      float dist = std::sqrt(dx * dx + dy * dy) / radius;
      Uint8 alpha = 0;
      Uint8 r = 0;
      Uint8 g = 0;
      Uint8 b = 0;
      if (dist < 1.0f) {
        float t = 1.0f - dist;
        alpha = static_cast<Uint8>(180.0f * t);
        r = 255;
        g = static_cast<Uint8>(120 + 80 * t);
        b = static_cast<Uint8>(30 + 40 * t);
      }
      pixels[y * kFireTexPx + x] = SDL_MapRGBA(fmt, r, g, b, alpha);
    }
  }

  SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
  SDL_FreeSurface(surface);
  if (!texture) {
    SDL_Log("Failed to create fire texture: %s", SDL_GetError());
    return nullptr;
  }
  SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
  return texture;
}

bool SameColor(const SDL_Color& a, const SDL_Color& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

float Clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

SDL_Color HeatColor(float t) {
  t = Clamp01(t);
  Uint8 r = static_cast<Uint8>(50 + 205 * t);
  float mid = 1.0f - std::abs(t - 0.5f) * 2.0f;
  Uint8 g = static_cast<Uint8>(60 + 140 * Clamp01(mid));
  Uint8 b = static_cast<Uint8>(200 - 170 * t);
  return SDL_Color{r, g, b, 180};
}

}  // namespace

bool Renderer::Load(SDL_Renderer* renderer, const std::string& humanSpritesPath,
                    const std::string& tilesPath, const std::string& terrainOverlayPath,
                    const std::string& objectsPath, const std::string& buildingsPath,
                    const std::string& labelFontPath, int labelFontSize) {
  CrashContextSetStage("Renderer::Load:Shutdown");
  Shutdown();

  CrashContextSetStage("Renderer::Load");

  auto endsWithIgnoreCase = [](const std::string& s, const char* suffix) -> bool {
    if (!suffix) return false;
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) {
      char a = s[s.size() - n + i];
      char b = suffix[i];
      if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
      if (a != b) return false;
    }
    return true;
  };

  // NOTE: Some SDL2_ttf builds (or mixed DLL setups) crash inside TTF_OpenFont on .ttc collections.
  // Inter-Regular.ttc is a font collection, so skip label font loading for stability.
  if (!labelFontPath.empty() && endsWithIgnoreCase(labelFontPath, ".ttc")) {
    SDL_Log("Skipping label font load (%s): .ttc collections can crash SDL2_ttf on some setups",
            labelFontPath.c_str());
    ttfReady_ = false;
    ttfOwned_ = false;
    labelFont_ = nullptr;
  } else {
    if (TTF_WasInit() == 0) {
      if (TTF_Init() != 0) {
        SDL_Log("TTF_Init failed: %s", TTF_GetError());
      } else {
        ttfReady_ = true;
        ttfOwned_ = true;
      }
    } else {
      ttfReady_ = true;
    }

    if (ttfReady_) {
      CrashContextSetStage("Renderer::Load:TTF_OpenFont");
      labelFont_ = TTF_OpenFont(labelFontPath.c_str(), labelFontSize);
      if (!labelFont_) {
        SDL_Log("Failed to load label font (%s): %s", labelFontPath.c_str(), TTF_GetError());
      }
    }
  }

  auto loadTexture = [&](const std::string& path, SDL_Texture*& texture, const char* label) {
    // Keep crash logs actionable if a specific texture load is the culprit.
    if (label) {
      if (std::strcmp(label, "humans") == 0) CrashContextSetStage("Renderer::Load IMG humans");
      else if (std::strcmp(label, "tiles") == 0) CrashContextSetStage("Renderer::Load IMG tiles");
      else if (std::strcmp(label, "terrain overlays") == 0) CrashContextSetStage("Renderer::Load IMG overlays");
      else if (std::strcmp(label, "objects") == 0) CrashContextSetStage("Renderer::Load IMG objects");
      else if (std::strcmp(label, "buildings") == 0) CrashContextSetStage("Renderer::Load IMG buildings");
      else CrashContextSetStage("Renderer::Load IMG texture");
    }
    texture = IMG_LoadTexture(renderer, path.c_str());
    if (!texture) {
      SDL_Log("Failed to load %s texture (%s): %s", label, path.c_str(), IMG_GetError());
      return false;
    }
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
    return true;
  };

  if (!loadTexture(humanSpritesPath, humansTexture_, "humans")) {
    return false;
  }

  // Optional soldier sprites (4 frames in a row: Front, Back, Left, Right).
  {
    CrashContextSetStage("Renderer::Load IMG soldier");
    const std::string soldierPath = "assets/sprites/soldier_sprite.png";
    soldierTexture_ = IMG_LoadTexture(renderer, soldierPath.c_str());
    if (!soldierTexture_) {
      SDL_Log("Failed to load soldier texture (%s): %s", soldierPath.c_str(), IMG_GetError());
      soldierSpriteWidth_ = 16;
      soldierSpriteHeight_ = 16;
    } else {
      SDL_SetTextureScaleMode(soldierTexture_, SDL_ScaleModeNearest);
      SDL_SetTextureBlendMode(soldierTexture_, SDL_BLENDMODE_BLEND);
      int texW = 0;
      int texH = 0;
      if (SDL_QueryTexture(soldierTexture_, nullptr, nullptr, &texW, &texH) != 0) {
        SDL_Log("Failed to query soldier texture: %s", SDL_GetError());
        soldierSpriteWidth_ = 16;
        soldierSpriteHeight_ = 16;
      } else if (texW >= 4 && texH >= 1) {
        soldierSpriteWidth_ = std::max(1, texW / 4);
        soldierSpriteHeight_ = texH;
        if (texW % 4 != 0) {
          SDL_Log("Soldier spritesheet size (%dx%d) is not divisible by 4; using %dx%d sprites",
                  texW, texH, soldierSpriteWidth_, soldierSpriteHeight_);
        }
      } else {
        SDL_Log("Soldier spritesheet size (%dx%d) too small; defaulting to 16x16 sprites", texW, texH);
        soldierSpriteWidth_ = 16;
        soldierSpriteHeight_ = 16;
      }
    }
  }

  // Optional farmer sprites (4 columns: Front, Back, Left, Right; rows: male, female).
  {
    CrashContextSetStage("Renderer::Load IMG farmer");
    const std::string farmerPath = "assets/sprites/farmers.png";
    farmerTexture_ = IMG_LoadTexture(renderer, farmerPath.c_str());
    if (!farmerTexture_) {
      SDL_Log("Failed to load farmer texture (%s): %s", farmerPath.c_str(), IMG_GetError());
      farmerSpriteWidth_ = 16;
      farmerSpriteHeight_ = 16;
    } else {
      SDL_SetTextureScaleMode(farmerTexture_, SDL_ScaleModeNearest);
      SDL_SetTextureBlendMode(farmerTexture_, SDL_BLENDMODE_BLEND);
      int texW = 0;
      int texH = 0;
      if (SDL_QueryTexture(farmerTexture_, nullptr, nullptr, &texW, &texH) != 0) {
        SDL_Log("Failed to query farmer texture: %s", SDL_GetError());
        farmerSpriteWidth_ = 16;
        farmerSpriteHeight_ = 16;
      } else if (texW >= 4 && texH >= 2) {
        farmerSpriteWidth_ = std::max(1, texW / 4);
        farmerSpriteHeight_ = std::max(1, texH / 2);
        if (texW % 4 != 0 || texH % 2 != 0) {
          SDL_Log("Farmer spritesheet size (%dx%d) is not divisible by 4x2; using %dx%d sprites",
                  texW, texH, farmerSpriteWidth_, farmerSpriteHeight_);
        }
      } else {
        SDL_Log("Farmer spritesheet size (%dx%d) too small; defaulting to 16x16 sprites", texW, texH);
        farmerSpriteWidth_ = 16;
        farmerSpriteHeight_ = 16;
      }
    }
  }

  // Optional trade caravan sprite.
  {
    CrashContextSetStage("Renderer::Load IMG caravan");
    const std::string caravanPath = "assets/sprites/caravan.png";
    caravanTexture_ = IMG_LoadTexture(renderer, caravanPath.c_str());
    if (!caravanTexture_) {
      SDL_Log("Failed to load caravan texture (%s): %s", caravanPath.c_str(), IMG_GetError());
      caravanTexW_ = 0;
      caravanTexH_ = 0;
    } else {
      SDL_SetTextureScaleMode(caravanTexture_, SDL_ScaleModeNearest);
      SDL_SetTextureBlendMode(caravanTexture_, SDL_BLENDMODE_BLEND);
      if (SDL_QueryTexture(caravanTexture_, nullptr, nullptr, &caravanTexW_, &caravanTexH_) != 0) {
        SDL_Log("Failed to query caravan texture: %s", SDL_GetError());
        caravanTexW_ = 0;
        caravanTexH_ = 0;
      }
    }
  }

  if (!loadTexture(tilesPath, tilesTexture_, "tiles")) {
    Shutdown();
    return false;
  }
  if (!loadTexture(terrainOverlayPath, terrainOverlayTexture_, "terrain overlays")) {
    Shutdown();
    return false;
  }
  if (!loadTexture(objectsPath, objectsTexture_, "objects")) {
    Shutdown();
    return false;
  }
  if (!loadTexture(buildingsPath, buildingsTexture_, "buildings")) {
    Shutdown();
    return false;
  }

  // Optional large town hall sprite (keeps buildings atlas for other buildings).
  {
    CrashContextSetStage("Renderer::Load IMG townhall");
    const std::string townHallPath = "assets/sprites/TH.png";
    townHallTexture_ = IMG_LoadTexture(renderer, townHallPath.c_str());
    if (!townHallTexture_) {
      SDL_Log("Failed to load town hall texture (%s): %s", townHallPath.c_str(), IMG_GetError());
      townHallTexW_ = 0;
      townHallTexH_ = 0;
    } else {
      SDL_SetTextureScaleMode(townHallTexture_, SDL_ScaleModeNearest);
      SDL_SetTextureBlendMode(townHallTexture_, SDL_BLENDMODE_BLEND);
      if (SDL_QueryTexture(townHallTexture_, nullptr, nullptr, &townHallTexW_, &townHallTexH_) != 0) {
        SDL_Log("Failed to query town hall texture: %s", SDL_GetError());
        townHallTexW_ = 0;
        townHallTexH_ = 0;
      }
    }
  }

  // Optional capital sprite (used for the first settlement of a faction).
  {
    CrashContextSetStage("Renderer::Load IMG capital");
    const std::string capitalPath = "assets/sprites/Capital.png";
    capitalTexture_ = IMG_LoadTexture(renderer, capitalPath.c_str());
    if (!capitalTexture_) {
      SDL_Log("Failed to load capital texture (%s): %s", capitalPath.c_str(), IMG_GetError());
      capitalTexW_ = 0;
      capitalTexH_ = 0;
    } else {
      SDL_SetTextureScaleMode(capitalTexture_, SDL_ScaleModeNearest);
      SDL_SetTextureBlendMode(capitalTexture_, SDL_BLENDMODE_BLEND);
      if (SDL_QueryTexture(capitalTexture_, nullptr, nullptr, &capitalTexW_, &capitalTexH_) != 0) {
        SDL_Log("Failed to query capital texture: %s", SDL_GetError());
        capitalTexW_ = 0;
        capitalTexH_ = 0;
      }
    }
  }

  {
    CrashContextSetStage("Renderer::Load IMG market");
    const std::string marketPath = "assets/sprites/market.png";
    marketTexture_ = IMG_LoadTexture(renderer, marketPath.c_str());
    if (!marketTexture_) {
      SDL_Log("Failed to load market texture (%s): %s", marketPath.c_str(), IMG_GetError());
      marketTexW_ = 0;
      marketTexH_ = 0;
    } else {
      SDL_SetTextureScaleMode(marketTexture_, SDL_ScaleModeNearest);
      SDL_SetTextureBlendMode(marketTexture_, SDL_BLENDMODE_BLEND);
      if (SDL_QueryTexture(marketTexture_, nullptr, nullptr, &marketTexW_, &marketTexH_) != 0) {
        SDL_Log("Failed to query market texture: %s", SDL_GetError());
        marketTexW_ = 0;
        marketTexH_ = 0;
      }
    }
  }

  {
    CrashContextSetStage("Renderer::Load IMG forge");
    const std::string forgePath = "assets/sprites/forge.png";
    forgeTexture_ = IMG_LoadTexture(renderer, forgePath.c_str());
    if (!forgeTexture_) {
      SDL_Log("Failed to load forge texture (%s): %s", forgePath.c_str(), IMG_GetError());
      forgeTexW_ = 0;
      forgeTexH_ = 0;
    } else {
      SDL_SetTextureScaleMode(forgeTexture_, SDL_ScaleModeNearest);
      SDL_SetTextureBlendMode(forgeTexture_, SDL_BLENDMODE_BLEND);
      if (SDL_QueryTexture(forgeTexture_, nullptr, nullptr, &forgeTexW_, &forgeTexH_) != 0) {
        SDL_Log("Failed to query forge texture: %s", SDL_GetError());
        forgeTexW_ = 0;
        forgeTexH_ = 0;
      }
    }
  }

  // Optional large tree sprite (replaces the atlas tree).
  {
    CrashContextSetStage("Renderer::Load IMG tree");
    const std::string treePath = "assets/sprites/Tree.png";
    treeTexture_ = IMG_LoadTexture(renderer, treePath.c_str());
    if (!treeTexture_) {
      SDL_Log("Failed to load tree texture (%s): %s", treePath.c_str(), IMG_GetError());
      treeTexW_ = 0;
      treeTexH_ = 0;
      treeTrunkSrc_ = SDL_Rect{0, 0, 0, 0};
      treeCanopySrc_ = SDL_Rect{0, 0, 0, 0};
      treeCanopyOccludeSrcY_ = 0;
    } else {
      SDL_SetTextureScaleMode(treeTexture_, SDL_ScaleModeNearest);
      SDL_SetTextureBlendMode(treeTexture_, SDL_BLENDMODE_BLEND);
      if (SDL_QueryTexture(treeTexture_, nullptr, nullptr, &treeTexW_, &treeTexH_) != 0) {
        SDL_Log("Failed to query tree texture: %s", SDL_GetError());
        treeTexW_ = 0;
        treeTexH_ = 0;
        treeTrunkSrc_ = SDL_Rect{0, 0, 0, 0};
        treeCanopySrc_ = SDL_Rect{0, 0, 0, 0};
        treeCanopyOccludeSrcY_ = 0;
      } else {
        const int trunkSrcYBase = treeTexH_ - kTreeTrunkHeightFromBottomPx;  // no overlap
        if (treeTexW_ <= 0 || treeTexH_ <= 0 || trunkSrcYBase <= 0 || trunkSrcYBase >= treeTexH_) {
          treeTrunkSrc_ = SDL_Rect{0, 0, 0, 0};
          treeCanopySrc_ = SDL_Rect{0, 0, 0, 0};
          treeCanopyOccludeSrcY_ = 0;
        } else {
          const int trunkSrcY = std::max(0, trunkSrcYBase - kTreeSeamOverlapPx);
          const int canopySrcH = std::min(treeTexH_, trunkSrcYBase + kTreeSeamOverlapPx);
          const int trunkSrcH = std::max(0, treeTexH_ - trunkSrcY);
          treeTrunkSrc_ = SDL_Rect{0, trunkSrcY, treeTexW_, trunkSrcH};
          treeCanopySrc_ = SDL_Rect{0, 0, treeTexW_, canopySrcH};
          treeCanopyOccludeSrcY_ = AutoDetectCanopyOccludeSrcY(treePath, canopySrcH);
          if (treeCanopyOccludeSrcY_ <= 0) treeCanopyOccludeSrcY_ = canopySrcH;
        }
      }
    }
  }

  // Optional large tree sprite variant (used alongside Tree.png).
  {
    CrashContextSetStage("Renderer::Load IMG tree1");
    const std::string tree1Path = "assets/sprites/Tree1.png";
    tree1Texture_ = IMG_LoadTexture(renderer, tree1Path.c_str());
    if (!tree1Texture_) {
      SDL_Log("Failed to load tree1 texture (%s): %s", tree1Path.c_str(), IMG_GetError());
      tree1TexW_ = 0;
      tree1TexH_ = 0;
      tree1TrunkSrc_ = SDL_Rect{0, 0, 0, 0};
      tree1CanopySrc_ = SDL_Rect{0, 0, 0, 0};
      tree1CanopyOccludeSrcY_ = 0;
    } else {
      SDL_SetTextureScaleMode(tree1Texture_, SDL_ScaleModeNearest);
      SDL_SetTextureBlendMode(tree1Texture_, SDL_BLENDMODE_BLEND);
      if (SDL_QueryTexture(tree1Texture_, nullptr, nullptr, &tree1TexW_, &tree1TexH_) != 0) {
        SDL_Log("Failed to query tree1 texture: %s", SDL_GetError());
        tree1TexW_ = 0;
        tree1TexH_ = 0;
        tree1TrunkSrc_ = SDL_Rect{0, 0, 0, 0};
        tree1CanopySrc_ = SDL_Rect{0, 0, 0, 0};
        tree1CanopyOccludeSrcY_ = 0;
      } else {
        const int trunkSrcYBase = tree1TexH_ - kTree1TrunkHeightFromBottomPx;  // no overlap
        if (tree1TexW_ <= 0 || tree1TexH_ <= 0 || trunkSrcYBase <= 0 || trunkSrcYBase >= tree1TexH_) {
          tree1TrunkSrc_ = SDL_Rect{0, 0, 0, 0};
          tree1CanopySrc_ = SDL_Rect{0, 0, 0, 0};
          tree1CanopyOccludeSrcY_ = 0;
        } else {
          const int trunkSrcY = std::max(0, trunkSrcYBase - kTreeSeamOverlapPx);
          const int canopySrcH = std::min(tree1TexH_, trunkSrcYBase + kTreeSeamOverlapPx);
          const int trunkSrcH = std::max(0, tree1TexH_ - trunkSrcY);
          tree1TrunkSrc_ = SDL_Rect{0, trunkSrcY, tree1TexW_, trunkSrcH};
          tree1CanopySrc_ = SDL_Rect{0, 0, tree1TexW_, canopySrcH};
          tree1CanopyOccludeSrcY_ = AutoDetectCanopyOccludeSrcY(tree1Path, canopySrcH);
          if (tree1CanopyOccludeSrcY_ <= 0) tree1CanopyOccludeSrcY_ = canopySrcH;
        }
      }
    }
  }

  SDL_SetTextureBlendMode(humansTexture_, SDL_BLENDMODE_BLEND);
  if (soldierTexture_) SDL_SetTextureBlendMode(soldierTexture_, SDL_BLENDMODE_BLEND);
  if (farmerTexture_) SDL_SetTextureBlendMode(farmerTexture_, SDL_BLENDMODE_BLEND);
  if (caravanTexture_) SDL_SetTextureBlendMode(caravanTexture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(tilesTexture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(terrainOverlayTexture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(objectsTexture_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureBlendMode(buildingsTexture_, SDL_BLENDMODE_BLEND);

  shadowTexture_ = CreateShadowTexture(renderer);
  if (!shadowTexture_) {
    Shutdown();
    return false;
  }
  aoCornerTexture_ = CreateAOCornerTexture(renderer);
  if (!aoCornerTexture_) {
    Shutdown();
    return false;
  }
  vignetteTexW_ = kVignetteTexPx;
  vignetteTexH_ = kVignetteTexPx;
  vignetteTexture_ = CreateVignetteTexture(renderer);
  if (!vignetteTexture_) {
    Shutdown();
    return false;
  }
  fireTexture_ = CreateFireTexture(renderer);
  if (!fireTexture_) {
    Shutdown();
    return false;
  }

  // Ultra-subtle fullscreen grain/dither (no new assets).
  {
    CrashContextSetStage("Renderer::Load grain");
    constexpr int kGrainSize = 64;
    grainTexW_ = kGrainSize;
    grainTexH_ = kGrainSize;
    grainTexture_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC,
                                      grainTexW_, grainTexH_);
    if (!grainTexture_) {
      SDL_Log("Failed to create grain texture: %s", SDL_GetError());
    } else {
      SDL_SetTextureBlendMode(grainTexture_, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(grainTexture_, SDL_ScaleModeNearest);
      std::vector<uint32_t> pixels(static_cast<size_t>(grainTexW_ * grainTexH_), 0u);
      for (int y = 0; y < grainTexH_; ++y) {
        for (int x = 0; x < grainTexW_; ++x) {
          uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0x6A09E667u);
          uint8_t v = static_cast<uint8_t>(h & 0xFFu);
          pixels[static_cast<size_t>(y * grainTexW_ + x)] =
              (0xFFu << 24) | (static_cast<uint32_t>(v) << 16) |
              (static_cast<uint32_t>(v) << 8) | static_cast<uint32_t>(v);
        }
      }
      SDL_UpdateTexture(grainTexture_, nullptr, pixels.data(), grainTexW_ * static_cast<int>(sizeof(uint32_t)));
    }
  }

  auto validateAtlas = [&](SDL_Texture* texture, int expectedW, int expectedH, const char* label) {
    int texW = 0;
    int texH = 0;
    if (SDL_QueryTexture(texture, nullptr, nullptr, &texW, &texH) != 0) {
      SDL_Log("Failed to query %s texture: %s", label, SDL_GetError());
      return;
    }
    if (texW != expectedW || texH != expectedH) {
      SDL_Log("%s atlas size %dx%d does not match expected %dx%d", label, texW, texH, expectedW,
              expectedH);
    }
  };

  validateAtlas(tilesTexture_, kTilesAtlasCols * kTilePx, kTilesAtlasRows * kTilePx, "tiles");
  validateAtlas(terrainOverlayTexture_, kFoamCols * kTilePx, kFoamRows * kTilePx, "terrain overlays");
  validateAtlas(objectsTexture_, kObjectCols * kTilePx, kObjectRows * kTilePx, "objects");
  {
    int texW = 0;
    int texH = 0;
    if (SDL_QueryTexture(buildingsTexture_, nullptr, nullptr, &texW, &texH) == 0) {
      if (texW % kTilePx != 0 || texH % kTilePx != 0) {
        SDL_Log("buildings atlas size %dx%d is not divisible by tile size %d", texW, texH, kTilePx);
      }
    }
  }

  int texW = 0;
  int texH = 0;
  if (SDL_QueryTexture(humansTexture_, nullptr, nullptr, &texW, &texH) == 0) {
    if (texW >= 4 && texH >= 2) {
      spriteWidth_ = texW / 4;
      spriteHeight_ = texH / 2;
      if (texW % 4 != 0 || texH % 2 != 0) {
        SDL_Log("Humans spritesheet size (%dx%d) is not divisible by 4x2; using %dx%d sprites",
                texW, texH, spriteWidth_, spriteHeight_);
      }
    } else {
      SDL_Log("Humans spritesheet size (%dx%d) too small; defaulting to 32x32 sprites", texW,
              texH);
      spriteWidth_ = 32;
      spriteHeight_ = 32;
    }
  }

  terrainDirty_ = true;
  return true;
}

void Renderer::Shutdown() {
  CrashContextSetStage("Renderer::Shutdown");
  DestroyTerrainCache();
  ClearLabelCache();

  if (labelFont_) {
    TTF_CloseFont(labelFont_);
    labelFont_ = nullptr;
  }
  if (ttfOwned_) {
    TTF_Quit();
    ttfOwned_ = false;
    ttfReady_ = false;
  }

  if (humansTexture_) {
    SDL_DestroyTexture(humansTexture_);
    humansTexture_ = nullptr;
  }
  if (soldierTexture_) {
    SDL_DestroyTexture(soldierTexture_);
    soldierTexture_ = nullptr;
  }
  if (farmerTexture_) {
    SDL_DestroyTexture(farmerTexture_);
    farmerTexture_ = nullptr;
  }
  if (caravanTexture_) {
    SDL_DestroyTexture(caravanTexture_);
    caravanTexture_ = nullptr;
  }
  if (tilesTexture_) {
    SDL_DestroyTexture(tilesTexture_);
    tilesTexture_ = nullptr;
  }
  if (terrainOverlayTexture_) {
    SDL_DestroyTexture(terrainOverlayTexture_);
    terrainOverlayTexture_ = nullptr;
  }
  if (objectsTexture_) {
    SDL_DestroyTexture(objectsTexture_);
    objectsTexture_ = nullptr;
  }
  if (buildingsTexture_) {
    SDL_DestroyTexture(buildingsTexture_);
    buildingsTexture_ = nullptr;
  }
  if (townHallTexture_) {
    SDL_DestroyTexture(townHallTexture_);
    townHallTexture_ = nullptr;
  }
  if (capitalTexture_) {
    SDL_DestroyTexture(capitalTexture_);
    capitalTexture_ = nullptr;
  }
  if (marketTexture_) {
    SDL_DestroyTexture(marketTexture_);
    marketTexture_ = nullptr;
  }
  if (forgeTexture_) {
    SDL_DestroyTexture(forgeTexture_);
    forgeTexture_ = nullptr;
  }
  if (treeTexture_) {
    SDL_DestroyTexture(treeTexture_);
    treeTexture_ = nullptr;
  }
  if (tree1Texture_) {
    SDL_DestroyTexture(tree1Texture_);
    tree1Texture_ = nullptr;
  }
  if (shadowTexture_) {
    SDL_DestroyTexture(shadowTexture_);
    shadowTexture_ = nullptr;
  }
  if (aoCornerTexture_) {
    SDL_DestroyTexture(aoCornerTexture_);
    aoCornerTexture_ = nullptr;
  }
  if (fireTexture_) {
    SDL_DestroyTexture(fireTexture_);
    fireTexture_ = nullptr;
  }
  if (grainTexture_) {
    SDL_DestroyTexture(grainTexture_);
    grainTexture_ = nullptr;
  }
  if (vignetteTexture_) {
    SDL_DestroyTexture(vignetteTexture_);
    vignetteTexture_ = nullptr;
  }
  vignetteTexW_ = 0;
  vignetteTexH_ = 0;
  townHallTexW_ = 0;
  townHallTexH_ = 0;
  capitalTexW_ = 0;
  capitalTexH_ = 0;
  treeTexW_ = 0;
  treeTexH_ = 0;
  treeTrunkSrc_ = SDL_Rect{0, 0, 0, 0};
  treeCanopySrc_ = SDL_Rect{0, 0, 0, 0};
  treeCanopyOccludeSrcY_ = 0;
  tree1TexW_ = 0;
  tree1TexH_ = 0;
  tree1TrunkSrc_ = SDL_Rect{0, 0, 0, 0};
  tree1CanopySrc_ = SDL_Rect{0, 0, 0, 0};
  tree1CanopyOccludeSrcY_ = 0;
}

void Renderer::OnRenderTargetsReset() {
  for (auto& chunk : chunks_) {
    if (chunk.texture) {
      SDL_DestroyTexture(chunk.texture);
      chunk.texture = nullptr;
    }
    chunk.dirty = true;
    chunk.lastUsedFrame = 0;
  }
  terrainTextureIndices_.clear();
  terrainDirty_ = true;
  frameCounter_ = 0;
}

void Renderer::DestroyTerrainCache() {
  for (auto& chunk : chunks_) {
    if (chunk.texture) {
      SDL_DestroyTexture(chunk.texture);
      chunk.texture = nullptr;
    }
  }
  chunks_.clear();
  terrainTextureIndices_.clear();
  worldWidth_ = 0;
  worldHeight_ = 0;
  chunksX_ = 0;
  chunksY_ = 0;
  terrainDirty_ = true;
  frameCounter_ = 0;
}

void Renderer::ClearLabelCache() {
  for (auto& entry : labelCache_) {
    if (entry.texture) {
      SDL_DestroyTexture(entry.texture);
      entry.texture = nullptr;
    }
  }
  labelCache_.clear();

  for (auto& entry : textCache_) {
    if (entry.texture) {
      SDL_DestroyTexture(entry.texture);
      entry.texture = nullptr;
    }
  }
  textCache_.clear();
}

void Renderer::UpdateLabelCache(SDL_Renderer* renderer, const SettlementManager& settlements,
                                const FactionManager& factions) {
  if (!labelFont_) {
    ClearLabelCache();
    return;
  }

  const auto& list = settlements.Settlements();
  std::vector<int> factionCounts(factions.Count() + 1, 0);
  for (const auto& settlement : list) {
    int factionId = settlement.factionId;
    if (factionId > 0 && factionId < static_cast<int>(factionCounts.size())) {
      factionCounts[factionId]++;
    }
  }

  for (auto& entry : labelCache_) {
    entry.used = false;
  }

  for (const auto& settlement : list) {
    std::string label;
    SDL_Color color{255, 255, 255, 255};
    const Faction* faction = factions.Get(settlement.factionId);
    if (faction) {
      label = faction->name;
      color.r = faction->color.r;
      color.g = faction->color.g;
      color.b = faction->color.b;
      int factionId = settlement.factionId;
      if (factionId > 0 && factionId < static_cast<int>(factionCounts.size()) &&
          factionCounts[factionId] > 1) {
        label += " #";
        label += std::to_string(settlement.id);
      }
    } else {
      label = "Settlement ";
      label += std::to_string(settlement.id);
    }

    LabelCacheEntry* entry = nullptr;
    for (auto& existing : labelCache_) {
      if (existing.settlementId == settlement.id) {
        entry = &existing;
        break;
      }
    }
    if (!entry) {
      labelCache_.push_back(LabelCacheEntry{});
      entry = &labelCache_.back();
      entry->settlementId = settlement.id;
    }

    entry->used = true;
    if (entry->text == label && SameColor(entry->color, color)) {
      continue;
    }

    if (entry->texture) {
      SDL_DestroyTexture(entry->texture);
      entry->texture = nullptr;
    }

    SDL_Surface* surface = TTF_RenderUTF8_Blended(labelFont_, label.c_str(), color);
    if (!surface) {
      SDL_Log("Failed to render label text: %s", TTF_GetError());
      entry->text = label;
      entry->color = color;
      entry->width = 0;
      entry->height = 0;
      continue;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture) {
      SDL_Log("Failed to create label texture: %s", SDL_GetError());
      SDL_FreeSurface(surface);
      entry->text = label;
      entry->color = color;
      entry->width = 0;
      entry->height = 0;
      continue;
    }

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    entry->texture = texture;
    entry->width = surface->w;
    entry->height = surface->h;
    entry->text = label;
    entry->color = color;
    SDL_FreeSurface(surface);
  }

  for (auto it = labelCache_.begin(); it != labelCache_.end();) {
    if (!it->used) {
      if (it->texture) {
        SDL_DestroyTexture(it->texture);
        it->texture = nullptr;
      }
      it = labelCache_.erase(it);
    } else {
      ++it;
    }
  }
}

void Renderer::BuildChunks(SDL_Renderer* renderer, int worldWidth, int worldHeight) {
  (void)renderer;
  chunksX_ = (worldWidth + chunkTiles_ - 1) / chunkTiles_;
  chunksY_ = (worldHeight + chunkTiles_ - 1) / chunkTiles_;
  chunks_.assign(chunksX_ * chunksY_, TerrainChunk{});
  terrainTextureIndices_.clear();

  for (int cy = 0; cy < chunksY_; ++cy) {
    for (int cx = 0; cx < chunksX_; ++cx) {
      TerrainChunk& chunk = chunks_[cy * chunksX_ + cx];
      chunk.originX = cx * chunkTiles_;
      chunk.originY = cy * chunkTiles_;
      chunk.tilesWide = std::min(chunkTiles_, worldWidth - chunk.originX);
      chunk.tilesHigh = std::min(chunkTiles_, worldHeight - chunk.originY);
      chunk.dirty = true;
      chunk.texture = nullptr;
      chunk.lastUsedFrame = 0;
    }
  }
}

void Renderer::EnsureTerrainCache(SDL_Renderer* renderer, World& world) {
  bool fullRebuild = false;
  if (world.width() != worldWidth_ || world.height() != worldHeight_) {
    DestroyTerrainCache();
    worldWidth_ = world.width();
    worldHeight_ = world.height();
    BuildChunks(renderer, worldWidth_, worldHeight_);
    fullRebuild = true;
    terrainDirty_ = true;
  }

  int dirtyMinX = 0;
  int dirtyMinY = 0;
  int dirtyMaxX = worldWidth_ > 0 ? worldWidth_ - 1 : 0;
  int dirtyMaxY = worldHeight_ > 0 ? worldHeight_ - 1 : 0;
  bool hasDirty = false;
  if (terrainDirty_ || fullRebuild) {
    terrainDirty_ = false;
    int clearMinX = 0;
    int clearMinY = 0;
    int clearMaxX = 0;
    int clearMaxY = 0;
    world.ConsumeTerrainDirty(clearMinX, clearMinY, clearMaxX, clearMaxY);
    hasDirty = true;
    dirtyMinX = 0;
    dirtyMinY = 0;
    dirtyMaxX = worldWidth_ > 0 ? worldWidth_ - 1 : 0;
    dirtyMaxY = worldHeight_ > 0 ? worldHeight_ - 1 : 0;
  } else {
    hasDirty = world.ConsumeTerrainDirty(dirtyMinX, dirtyMinY, dirtyMaxX, dirtyMaxY);
  }
  if (!hasDirty) return;

  dirtyMinX = std::max(0, dirtyMinX);
  dirtyMinY = std::max(0, dirtyMinY);
  dirtyMaxX = std::min(worldWidth_ - 1, dirtyMaxX);
  dirtyMaxY = std::min(worldHeight_ - 1, dirtyMaxY);

  if (dirtyMinX > dirtyMaxX || dirtyMinY > dirtyMaxY) return;

  constexpr int kTerrainPadding = 6;
  int paddedMinX = std::max(0, dirtyMinX - kTerrainPadding);
  int paddedMinY = std::max(0, dirtyMinY - kTerrainPadding);
  int paddedMaxX = std::min(worldWidth_ - 1, dirtyMaxX + kTerrainPadding);
  int paddedMaxY = std::min(worldHeight_ - 1, dirtyMaxY + kTerrainPadding);
  int minChunkX = paddedMinX / chunkTiles_;
  int maxChunkX = paddedMaxX / chunkTiles_;
  int minChunkY = paddedMinY / chunkTiles_;
  int maxChunkY = paddedMaxY / chunkTiles_;
  for (int cy = minChunkY; cy <= maxChunkY; ++cy) {
    for (int cx = minChunkX; cx <= maxChunkX; ++cx) {
      int idx = cy * chunksX_ + cx;
      if (idx >= 0 && idx < static_cast<int>(chunks_.size())) {
        chunks_[idx].dirty = true;
      }
    }
  }
}

void Renderer::RebuildTerrainCache(SDL_Renderer* renderer, const World& world, int minX, int minY,
                                  int maxX, int maxY) {
  if (chunks_.empty()) {
    BuildChunks(renderer, worldWidth_, worldHeight_);
  }
  assert(world.width() == worldWidth_);
  assert(world.height() == worldHeight_);

  frameCounter_++;
  const int minChunkX = std::max(0, minX / chunkTiles_);
  const int minChunkY = std::max(0, minY / chunkTiles_);
  const int maxChunkX = std::min(chunksX_ - 1, maxX / chunkTiles_);
  const int maxChunkY = std::min(chunksY_ - 1, maxY / chunkTiles_);

  auto isLand = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= worldWidth_ || y >= worldHeight_) return false;
    const TileType type = world.At(x, y).type;
    return type == TileType::Land || type == TileType::Sand;
  };

  auto coastDistance = [&](int x, int y) -> int {
    constexpr int kMaxLandDist = 5;
    for (int dist = 1; dist <= kMaxLandDist; ++dist) {
      for (int dy = -dist; dy <= dist; ++dy) {
        int yPos = y + dy;
        if (yPos < 0 || yPos >= worldHeight_) continue;
        int dx = dist - std::abs(dy);
        int xLeft = x - dx;
        int xRight = x + dx;
        if (xLeft >= 0 && xLeft < worldWidth_ && isLand(xLeft, yPos)) {
          return dist;
        }
        if (dx != 0 && xRight >= 0 && xRight < worldWidth_ && isLand(xRight, yPos)) {
          return dist;
        }
      }
    }
    return kMaxLandDist + 1;
  };

  auto jitteredCoastDist = [&](int x, int y, int coastDist) -> int {
    if (coastDist < 2) return coastDist;
    float n = Fbm2D(static_cast<float>(x) / 23.0f, static_cast<float>(y) / 23.0f, 0x4C11DB7Du, 3);
    int j = 0;
    if (n > 0.35f) j = 1;
    else if (n < -0.35f) j = -1;
    return std::max(0, coastDist + j);
  };

  auto waterBandFromCoastDist = [&](int coastDist) -> int {
    // 0 = shallow, 1 = mid, 2 = deep
    if (coastDist <= 1) return 0;
    if (coastDist <= 4) return 1;
    return 2;
  };

  auto ensureChunkTexture = [&](int chunkIndex, TerrainChunk& chunk) {
    if (chunk.texture) return true;
    int texW = chunk.tilesWide * kTilePx;
    int texH = chunk.tilesHigh * kTilePx;
    chunk.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
                                      texW, texH);
    if (!chunk.texture) {
      SDL_Log("Failed to create chunk texture: %s", SDL_GetError());
      return false;
    }
    SDL_SetTextureBlendMode(chunk.texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(chunk.texture, SDL_ScaleModeNearest);
    chunk.dirty = true;
    terrainTextureIndices_.push_back(chunkIndex);
    return true;
  };

  SDL_Texture* previousTarget = SDL_GetRenderTarget(renderer);

  if (minChunkX <= maxChunkX && minChunkY <= maxChunkY) {
    for (int cy = minChunkY; cy <= maxChunkY; ++cy) {
      for (int cx = minChunkX; cx <= maxChunkX; ++cx) {
        const int chunkIndex = cy * chunksX_ + cx;
        TerrainChunk& chunk = chunks_[static_cast<size_t>(chunkIndex)];

        chunk.lastUsedFrame = frameCounter_;
        if (!ensureChunkTexture(chunkIndex, chunk)) continue;
        if (!chunk.dirty) continue;
        chunk.dirty = false;

        SDL_SetRenderTarget(renderer, chunk.texture);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderClear(renderer);

	        auto applyAdd = [&](const SDL_Rect& dst, const TintMod& tint) {
	          if (tint.addA == 0u) return;
	          SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_ADD);
	          SDL_SetRenderDrawColor(renderer, tint.addR, tint.addG, tint.addB, tint.addA);
	          SDL_RenderFillRect(renderer, &dst);
	          SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	        };
	
	        for (int y = chunk.originY; y < chunk.originY + chunk.tilesHigh; ++y) {
	          for (int x = chunk.originX; x < chunk.originX + chunk.tilesWide; ++x) {
	            if (isLand(x, y)) continue;
	
	            int distToLand = coastDistance(x, y);
	            int coastDist = std::max(0, distToLand - 1);
	            int coastDistJ = jitteredCoastDist(x, y, coastDist);
	            SDL_Rect src;
	            uint32_t wh = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0x13579BDFu);
	            if (coastDistJ <= 1) {
	              src = PickTilesVariant(kShallowWaterCoords, wh);
	            } else if (coastDistJ <= 4) {
	              src = PickTilesVariant(kMidWaterCoords, wh);
	            } else {
	              // Deep water: only mild pattern variation.
	              src = TilesRect(kDeepWaterCoords[wh & 1u]);
	            }
	
	            SDL_Rect dst{(x - chunk.originX) * kTilePx, (y - chunk.originY) * kTilePx, kTilePx,
	                         kTilePx};
	            TintMod tint = WaterTint(x, y, coastDistJ);
	            SDL_SetTextureColorMod(tilesTexture_, tint.modR, tint.modG, tint.modB);
	            SDL_RenderCopy(renderer, tilesTexture_, &src, &dst);
	            applyAdd(dst, tint);
	
	            uint8_t mask = 0;
	            if (isLand(x, y - 1)) mask |= 1u;
	            if (isLand(x + 1, y)) mask |= 2u;
	            if (isLand(x, y + 1)) mask |= 4u;
	            if (isLand(x - 1, y)) mask |= 8u;
	            if (mask != 0u) {
	              SDL_Rect foam = FoamRect(mask);
	              SDL_RenderCopy(renderer, terrainOverlayTexture_, &foam, &dst);
	            }
	
	            // Depth band contour: subtle dark strip on the deeper side of band boundaries.
	            int band = waterBandFromCoastDist(coastDistJ);
	            if (band > 0 && (coastDistJ == 2 || coastDistJ == 5)) {
	              SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	              SDL_SetRenderDrawColor(renderer, 0, 0, 10, 14);
	              auto neighborBand = [&](int nx, int ny) -> int {
	                if (nx < 0 || ny < 0 || nx >= worldWidth_ || ny >= worldHeight_) return band;
	                if (isLand(nx, ny)) return band;
	                int nd = std::max(0, coastDistance(nx, ny) - 1);
	                nd = jitteredCoastDist(nx, ny, nd);
	                return waterBandFromCoastDist(nd);
	              };
	              int up = neighborBand(x, y - 1);
	              int right = neighborBand(x + 1, y);
	              int down = neighborBand(x, y + 1);
	              int left = neighborBand(x - 1, y);
	              constexpr int kStrip = 2;
	              if (up < band) {
	                SDL_Rect strip{dst.x, dst.y, dst.w, kStrip};
	                SDL_RenderFillRect(renderer, &strip);
	              }
	              if (down < band) {
	                SDL_Rect strip{dst.x, dst.y + dst.h - kStrip, dst.w, kStrip};
	                SDL_RenderFillRect(renderer, &strip);
	              }
	              if (left < band) {
	                SDL_Rect strip{dst.x, dst.y, kStrip, dst.h};
	                SDL_RenderFillRect(renderer, &strip);
	              }
	              if (right < band) {
	                SDL_Rect strip{dst.x + dst.w - kStrip, dst.y, kStrip, dst.h};
	                SDL_RenderFillRect(renderer, &strip);
	              }
	            }
	          }
	        }
	
	        auto applyTerrainAO = [&](int tx, int ty, const SDL_Rect& dst) {
	          if (!aoCornerTexture_) return;
	          bool upWater = !isLand(tx, ty - 1);
	          bool rightWater = !isLand(tx + 1, ty);
	          bool downWater = !isLand(tx, ty + 1);
	          bool leftWater = !isLand(tx - 1, ty);
	          if (!(upWater || rightWater || downWater || leftWater)) return;

	          SDL_SetTextureColorMod(aoCornerTexture_, 0, 0, 0);
	          SDL_SetTextureAlphaMod(aoCornerTexture_, 34);
	          SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	
	          SDL_Rect src{0, 0, kAOCornerTexPx, kAOCornerTexPx};
	          constexpr int kCornerPx = 12;
	          if (upWater || leftWater) {
	            SDL_Rect tl{dst.x, dst.y, kCornerPx, kCornerPx};
	            SDL_RenderCopyEx(renderer, aoCornerTexture_, &src, &tl, 0.0, nullptr, SDL_FLIP_NONE);
	          }
	          if (upWater || rightWater) {
	            SDL_Rect tr{dst.x + dst.w - kCornerPx, dst.y, kCornerPx, kCornerPx};
	            SDL_RenderCopyEx(renderer, aoCornerTexture_, &src, &tr, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
	          }
	          if (downWater || leftWater) {
	            SDL_Rect bl{dst.x, dst.y + dst.h - kCornerPx, kCornerPx, kCornerPx};
	            SDL_RenderCopyEx(renderer, aoCornerTexture_, &src, &bl, 0.0, nullptr, SDL_FLIP_VERTICAL);
	          }
	          if (downWater || rightWater) {
	            SDL_Rect br{dst.x + dst.w - kCornerPx, dst.y + dst.h - kCornerPx, kCornerPx, kCornerPx};
	            SDL_RenderCopyEx(renderer, aoCornerTexture_, &src, &br, 0.0, nullptr,
	                             static_cast<SDL_RendererFlip>(SDL_FLIP_HORIZONTAL | SDL_FLIP_VERTICAL));
	          }

	          // Extra tiny edge darkening near water to soften square boundaries.
	          SDL_SetRenderDrawColor(renderer, 0, 0, 0, 18);
	          constexpr int kEdge = 2;
	          if (upWater) {
	            SDL_Rect edge{dst.x, dst.y, dst.w, kEdge};
	            SDL_RenderFillRect(renderer, &edge);
	          }
	          if (downWater) {
	            SDL_Rect edge{dst.x, dst.y + dst.h - kEdge, dst.w, kEdge};
	            SDL_RenderFillRect(renderer, &edge);
	          }
	          if (leftWater) {
	            SDL_Rect edge{dst.x, dst.y, kEdge, dst.h};
	            SDL_RenderFillRect(renderer, &edge);
	          }
	          if (rightWater) {
	            SDL_Rect edge{dst.x + dst.w - kEdge, dst.y, kEdge, dst.h};
	            SDL_RenderFillRect(renderer, &edge);
	          }
	        };

	        for (int y = chunk.originY; y < chunk.originY + chunk.tilesHigh; ++y) {
	          for (int x = chunk.originX; x < chunk.originX + chunk.tilesWide; ++x) {
	            if (!isLand(x, y)) continue;

            const bool paintedSand = world.At(x, y).type == TileType::Sand;
            bool beach =
                !isLand(x, y - 1) || !isLand(x + 1, y) || !isLand(x, y + 1) || !isLand(x - 1, y);
            SDL_Rect src{};
            if (paintedSand || beach) {
              uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), kSandSeed);
              src = PickTilesVariant(kSandCoords, h);
              SDL_SetTextureColorMod(tilesTexture_, 255, 255, 255);
            } else {
              // Blue-noise-ish sparse grass alts (no clumps, no grids).
              constexpr int kAltChancePct = 12;
              auto candidateAlt = [&](int gx, int gy) -> bool {
                uint32_t h = Hash2D(static_cast<uint32_t>(gx), static_cast<uint32_t>(gy), 0xA54FF53Au);
                return static_cast<int>(h % 100u) < kAltChancePct;
              };
              bool alt = candidateAlt(x, y);
              if (alt) {
                for (int dy = -1; dy <= 1 && alt; ++dy) {
                  for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    if (candidateAlt(x + dx, y + dy)) {
                      alt = false;
                      break;
                    }
                  }
                }
              }
              int variantIndex = 0;
              if (alt) {
                uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0x1F83D9ABu);
                variantIndex = 4 + static_cast<int>((h >> 8) % 4u);
              } else {
                uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0xD1B54A35u);
                variantIndex = static_cast<int>((h >> 8) % 4u);
              }
              variantIndex = std::clamp(variantIndex, 0, static_cast<int>(kGrassCoords.size()) - 1);
              src = TilesRect(kGrassCoords[static_cast<size_t>(variantIndex)]);
              TintMod tint = GrassTint(x, y);
              SDL_SetTextureColorMod(tilesTexture_, tint.modR, tint.modG, tint.modB);
              // Note: additive brightening happens after the tile is drawn.
	              SDL_Rect dst{(x - chunk.originX) * kTilePx, (y - chunk.originY) * kTilePx, kTilePx,
	                           kTilePx};
	              SDL_RenderCopy(renderer, tilesTexture_, &src, &dst);
	              applyAdd(dst, tint);
	              // Calming color wash to compress contrast (WorldBox-style "flat first").
	              float washN =
	                  Fbm2D(static_cast<float>(x) / 60.0f, static_cast<float>(y) / 60.0f, 0x9B05688Cu, 3);
	              float washA = std::clamp(0.08f + washN * 0.03f, 0.05f, 0.12f);
	              Uint8 alpha = static_cast<Uint8>(std::lround(washA * 255.0f));
	              SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	              SDL_SetRenderDrawColor(renderer, 85, 150, 85, alpha);
	              SDL_RenderFillRect(renderer, &dst);

	              applyTerrainAO(x, y, dst);
	              continue;
	            }
	            SDL_Rect dst{(x - chunk.originX) * kTilePx, (y - chunk.originY) * kTilePx, kTilePx,
	                         kTilePx};
	            SDL_RenderCopy(renderer, tilesTexture_, &src, &dst);
	            // Subtle shoreline land darkening (keeps foam, adds depth).
	            if (beach) {
	              SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	              SDL_SetRenderDrawColor(renderer, 0, 0, 0, 10);
	              SDL_RenderFillRect(renderer, &dst);
	            }
	            applyTerrainAO(x, y, dst);
	          }
	        }

        SDL_SetTextureColorMod(tilesTexture_, 255, 255, 255);
      }
    }
  }

  SDL_SetRenderTarget(renderer, previousTarget);

  // Evict old textures (world can be enormous; keep cache bounded).
  while (static_cast<int>(terrainTextureIndices_.size()) > maxTerrainTextures_) {
    uint64_t oldest = std::numeric_limits<uint64_t>::max();
    int oldestChunkIndex = -1;
    int oldestListIndex = -1;
    for (int i = 0; i < static_cast<int>(terrainTextureIndices_.size()); ++i) {
      int chunkIndex = terrainTextureIndices_[i];
      if (chunkIndex < 0 || chunkIndex >= static_cast<int>(chunks_.size())) continue;
      const auto& chunk = chunks_[static_cast<size_t>(chunkIndex)];
      if (!chunk.texture) continue;
      if (chunk.lastUsedFrame == frameCounter_) continue;
      if (chunk.lastUsedFrame < oldest) {
        oldest = chunk.lastUsedFrame;
        oldestChunkIndex = chunkIndex;
        oldestListIndex = i;
      }
    }
    if (oldestChunkIndex < 0 || oldestListIndex < 0) break;
    SDL_DestroyTexture(chunks_[static_cast<size_t>(oldestChunkIndex)].texture);
    chunks_[static_cast<size_t>(oldestChunkIndex)].texture = nullptr;
    terrainTextureIndices_[static_cast<size_t>(oldestListIndex)] = terrainTextureIndices_.back();
    terrainTextureIndices_.pop_back();
  }
}

void Renderer::Render(SDL_Renderer* renderer, World& world, const HumanManager& humans,
                      const SettlementManager& settlements, const FactionManager& factions,
                      const Camera& camera, int windowWidth, int windowHeight,
                      const std::vector<VillageMarker>& villageMarkers, int hoverTileX,
                      int hoverTileY, bool hoverValid, int brushSize, OverlayMode overlayMode) {
  Render(renderer, world, humans, settlements, factions, camera, windowWidth, windowHeight, villageMarkers,
         hoverTileX, hoverTileY, hoverValid, brushSize, overlayMode, RenderOverlayConfig{});
}

namespace {
void WorldToScreen(const Camera& camera, float worldX, float worldY, float& outX, float& outY) {
  outX = (worldX - camera.x) * camera.zoom;
  outY = (worldY - camera.y) * camera.zoom;
}

SDL_Color DarkenColor(SDL_Color in, float darken, Uint8 alpha) {
  darken = std::max(0.0f, std::min(1.0f, darken));
  SDL_Color out;
  out.r = static_cast<Uint8>(std::max(0, std::min(255, static_cast<int>(std::round(in.r * darken)))));
  out.g = static_cast<Uint8>(std::max(0, std::min(255, static_cast<int>(std::round(in.g * darken)))));
  out.b = static_cast<Uint8>(std::max(0, std::min(255, static_cast<int>(std::round(in.b * darken)))));
  out.a = alpha;
  return out;
}

}  // namespace

void Renderer::Render(SDL_Renderer* renderer, World& world, const HumanManager& humans,
                      const SettlementManager& settlements, const FactionManager& factions,
                      const Camera& camera, int windowWidth, int windowHeight,
                      const std::vector<VillageMarker>& villageMarkers, int hoverTileX,
                      int hoverTileY, bool hoverValid, int brushSize, OverlayMode overlayMode,
                      const RenderOverlayConfig& config) {
  CrashContextSetStage("Render::Begin");
  const float tileSize = static_cast<float>(kTilePx);
  const float invZoom = 1.0f / camera.zoom;

  const float worldLeft = camera.x;
  const float worldTop = camera.y;
  const float worldRight = camera.x + static_cast<float>(windowWidth) * invZoom;
  const float worldBottom = camera.y + static_cast<float>(windowHeight) * invZoom;

  int minX = std::max(0, static_cast<int>(worldLeft / tileSize) - 1);
  int minY = std::max(0, static_cast<int>(worldTop / tileSize) - 1);
  int maxX = std::min(world.width() - 1, static_cast<int>(worldRight / tileSize) + 1);
  int maxY = std::min(world.height() - 1, static_cast<int>(worldBottom / tileSize) + 1);

  SDL_Rect shadowSrc = ShadowSrc();

  CrashContextSetStage("Render::TerrainCache");
  EnsureTerrainCache(renderer, world);
  RebuildTerrainCache(renderer, world, minX, minY, maxX, maxY);

  CrashContextSetStage("Render::TerrainDraw");
  const int minChunkX = std::max(0, minX / chunkTiles_);
  const int minChunkY = std::max(0, minY / chunkTiles_);
  const int maxChunkX = std::min(chunksX_ - 1, maxX / chunkTiles_);
  const int maxChunkY = std::min(chunksY_ - 1, maxY / chunkTiles_);
  if (minChunkX <= maxChunkX && minChunkY <= maxChunkY) {
    for (int cy = minChunkY; cy <= maxChunkY; ++cy) {
      for (int cx = minChunkX; cx <= maxChunkX; ++cx) {
        const int chunkIndex = cy * chunksX_ + cx;
        const TerrainChunk& chunk = chunks_[static_cast<size_t>(chunkIndex)];
        if (!chunk.texture) continue;
        float worldX = static_cast<float>(chunk.originX) * tileSize;
        float worldY = static_cast<float>(chunk.originY) * tileSize;
        float width = static_cast<float>(chunk.tilesWide) * tileSize;
        float height = static_cast<float>(chunk.tilesHigh) * tileSize;
        SDL_Rect dst = MakeDstRect(worldX, worldY, width, height, camera);
        SDL_RenderCopy(renderer, chunk.texture, nullptr, &dst);

        if (config.showChunkBoundaries) {
          SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(renderer, 255, 0, 255, 80);
          SDL_RenderDrawRect(renderer, &dst);
        }
      }
    }
  }

  int zoneSize = settlements.ZoneSize();
  int zonesX = settlements.ZonesX();
  int zonesY = settlements.ZonesY();
  int minZoneX = 0;
  int minZoneY = 0;
  int maxZoneX = -1;
  int maxZoneY = -1;
  if (zoneSize > 0 && zonesX > 0 && zonesY > 0) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    int maxPop = 1;
    if (overlayMode == OverlayMode::PopulationHeat) {
      minZoneX = std::max(0, minX / zoneSize);
      minZoneY = std::max(0, minY / zoneSize);
      maxZoneX = std::min(zonesX - 1, maxX / zoneSize);
      maxZoneY = std::min(zonesY - 1, maxY / zoneSize);
      for (int zy = minZoneY; zy <= maxZoneY; ++zy) {
        for (int zx = minZoneX; zx <= maxZoneX; ++zx) {
          maxPop = std::max(maxPop, settlements.ZonePopAt(zx, zy));
        }
      }
    }

    minZoneX = std::max(0, minX / zoneSize);
    minZoneY = std::max(0, minY / zoneSize);
    maxZoneX = std::min(zonesX - 1, maxX / zoneSize);
    maxZoneY = std::min(zonesY - 1, maxY / zoneSize);

    for (int zy = minZoneY; zy <= maxZoneY; ++zy) {
      for (int zx = minZoneX; zx <= maxZoneX; ++zx) {
        int ownerId = settlements.ZoneOwnerAt(zx, zy);
        const Settlement* settlement = (ownerId > 0) ? settlements.Get(ownerId) : nullptr;
        const Faction* faction = (settlement && settlement->factionId > 0)
                                     ? factions.Get(settlement->factionId)
                                     : nullptr;

        int tilesWide = std::min(zoneSize, world.width() - zx * zoneSize);
        int tilesHigh = std::min(zoneSize, world.height() - zy * zoneSize);
        float worldX = static_cast<float>(zx * zoneSize) * tileSize;
        float worldY = static_cast<float>(zy * zoneSize) * tileSize;
        float width = static_cast<float>(tilesWide) * tileSize;
        float height = static_cast<float>(tilesHigh) * tileSize;
        SDL_Rect dst = MakeDstRect(worldX, worldY, width, height, camera);

        if (overlayMode == OverlayMode::FactionTerritory) {
          if (!faction) continue;
          SDL_Color color = DarkenColor(SDL_Color{faction->color.r, faction->color.g, faction->color.b, 255},
                                        config.territoryDarken,
                                        static_cast<Uint8>(std::max(0, std::min(255, config.territoryAlpha))));
          SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
          SDL_RenderFillRect(renderer, &dst);
        } else if (overlayMode == OverlayMode::SettlementInfluence) {
          if (!faction || !settlement) continue;
          SDL_Color color = DarkenColor(SDL_Color{faction->color.r, faction->color.g, faction->color.b, 255},
                                        config.territoryDarken,
                                        static_cast<Uint8>(std::max(0, std::min(255, config.territoryAlpha))));
          SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
          SDL_RenderFillRect(renderer, &dst);
        } else if (overlayMode == OverlayMode::PopulationHeat) {
          int pop = settlements.ZonePopAt(zx, zy);
          float t = (maxPop > 0) ? static_cast<float>(pop) / static_cast<float>(maxPop) : 0.0f;
          SDL_Color color = HeatColor(t);
          SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
          SDL_RenderFillRect(renderer, &dst);
        } else if (overlayMode == OverlayMode::Conflict) {
          int intensity = settlements.ZoneConflictAt(zx, zy);
          if (intensity <= 0) continue;
          Uint8 alpha = static_cast<Uint8>(std::min(200, intensity));
          SDL_SetRenderDrawColor(renderer, 220, 70, 60, alpha);
          SDL_RenderFillRect(renderer, &dst);
        }

        if (config.showWarZones) {
          int intensity = settlements.ZoneConflictAt(zx, zy);
          if (intensity > 0) {
            float pulse = 0.65f + 0.35f * std::sin(static_cast<float>(frameCounter_) * 0.06f);
            Uint8 alpha = static_cast<Uint8>(std::min(200, static_cast<int>(std::round(intensity * pulse))));
            SDL_SetRenderDrawColor(renderer, 255, 60, 60, alpha);
            SDL_RenderFillRect(renderer, &dst);
          }
        }

        if (overlayMode == OverlayMode::FactionTerritory ||
            overlayMode == OverlayMode::SettlementInfluence) {
          int rightOwner = (zx + 1 <= maxZoneX) ? settlements.ZoneOwnerAt(zx + 1, zy) : ownerId;
          int downOwner = (zy + 1 <= maxZoneY) ? settlements.ZoneOwnerAt(zx, zy + 1) : ownerId;
          int ownerKey = ownerId;
          int rightKey = rightOwner;
          int downKey = downOwner;
          if (overlayMode == OverlayMode::FactionTerritory) {
            const Settlement* rightSettlement = (rightOwner > 0) ? settlements.Get(rightOwner) : nullptr;
            const Settlement* downSettlement = (downOwner > 0) ? settlements.Get(downOwner) : nullptr;
            ownerKey = (settlement && settlement->factionId > 0) ? settlement->factionId : -1;
            rightKey =
                (rightSettlement && rightSettlement->factionId > 0) ? rightSettlement->factionId : -1;
            downKey =
                (downSettlement && downSettlement->factionId > 0) ? downSettlement->factionId : -1;
          }
          SDL_SetRenderDrawColor(renderer, 0, 0, 0, 90);
          if (rightKey != ownerKey && zx + 1 <= maxZoneX) {
            float x = worldX + width;
            SDL_Rect line = MakeDstRect(x - 1.0f, worldY, 2.0f, height, camera);
            SDL_RenderFillRect(renderer, &line);
          }
          if (downKey != ownerKey && zy + 1 <= maxZoneY) {
            float y = worldY + height;
            SDL_Rect line = MakeDstRect(worldX, y - 1.0f, width, 2.0f, camera);
            SDL_RenderFillRect(renderer, &line);
          }
        }
      }
    }
  }

  // If there's no overlay mode, still compute visible zone range for war visuals.
  if (zoneSize > 0 && zonesX > 0 && zonesY > 0 && maxZoneX < minZoneX) {
    minZoneX = std::max(0, minX / zoneSize);
    minZoneY = std::max(0, minY / zoneSize);
    maxZoneX = std::min(zonesX - 1, maxX / zoneSize);
    maxZoneY = std::min(zonesY - 1, maxY / zoneSize);
  }

	  CrashContextSetStage("Render::Buildings");
	  if (buildingsTexture_) {
	    SDL_Rect buildingSrc{0, 0, kTilePx, kTilePx};
	    const int buildMinX = std::max(0, minX - 6);
	    const int buildMinY = std::max(0, minY - 6);
	    const int buildMaxX = std::min(world.width() - 1, maxX + 6);
	    const int buildMaxY = std::min(world.height() - 1, maxY + 6);
	    SDL_SetTextureColorMod(shadowTexture_, 0, 0, 0);
	    SDL_SetTextureAlphaMod(shadowTexture_, 105);
	    for (int y = buildMinY; y <= buildMaxY; ++y) {
	      for (int x = buildMinX; x <= buildMaxX; ++x) {
	        const Tile& tile = world.At(x, y);
	        if (tile.building == BuildingType::None) continue;
	
		        if ((tile.building == BuildingType::TownHall && (townHallTexture_ || capitalTexture_)) ||
		            (tile.building == BuildingType::Market && marketTexture_) ||
		            (tile.building == BuildingType::Forge && forgeTexture_)) {
		          // Draw in a later pass (so it sits above trees/objects but below fire/humans).
		          continue;
		        }
	
	        AtlasCoord coord{0, 0};
	        switch (tile.building) {
	          case BuildingType::House:
	            coord = AtlasCoord{0, 0};
	            break;
	          case BuildingType::TownHall:
	            coord = AtlasCoord{0, 1};
	            break;
	          case BuildingType::Farm:
	            coord = AtlasCoord{0, 2};
	            break;
	          case BuildingType::Granary:
	            coord = AtlasCoord{1, 2};
	            break;
	          case BuildingType::Well:
	            coord = AtlasCoord{1, 1};
	            break;
	          case BuildingType::Market:
	            coord = AtlasCoord{0, 0};
	            break;
	          case BuildingType::Forge:
	            coord = AtlasCoord{0, 0};
	            break;
	          default:
	            coord = AtlasCoord{0, 0};
	            break;
	        }
	        buildingSrc.x = coord.col * kTilePx;
	        buildingSrc.y = coord.row * kTilePx;
	
	        uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y),
	                            0xB17D1E5Eu ^ static_cast<uint32_t>(tile.building));
	        int jitterX = static_cast<int>(h % 5u) - 2;
	        int jitterY = static_cast<int>((h >> 8) % 5u) - 2;
	        const float worldX = static_cast<float>(x) * tileSize + static_cast<float>(jitterX);
	        const float worldY = static_cast<float>(y) * tileSize + static_cast<float>(jitterY);
	
	        float shadowW = tileSize * 0.70f;
	        float shadowH = tileSize * 0.28f;
	        if (tile.building == BuildingType::Well) {
	          shadowW = tileSize * 0.55f;
	          shadowH = tileSize * 0.22f;
	        } else if (tile.building == BuildingType::Granary) {
	          shadowW = tileSize * 0.80f;
	          shadowH = tileSize * 0.30f;
	        } else if (tile.building == BuildingType::TownHall) {
	          shadowW = tileSize * 0.95f;
	          shadowH = tileSize * 0.33f;
	        }
	        const float shadowX = worldX + (tileSize - shadowW) * 0.5f + 1.5f;
	        const float shadowY = worldY + tileSize - shadowH * 0.6f + 1.5f;
	        SDL_Rect shadowDst = MakeDstRect(shadowX, shadowY, shadowW, shadowH, camera);
	        // Earthy ground AO under footprint (glues objects to terrain).
	        SDL_SetTextureColorMod(shadowTexture_, 85, 95, 60);
	        SDL_SetTextureAlphaMod(shadowTexture_, 55);
	        SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);
	        // Soft shadow.
	        SDL_SetTextureColorMod(shadowTexture_, 0, 0, 0);
	        SDL_SetTextureAlphaMod(shadowTexture_, 110);
	        SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);
	
	        SDL_Rect dst = MakeDstRect(worldX, worldY, tileSize, tileSize, camera);
	        // 1px drop shadow + cheap outline (lighter than units).
	        SDL_SetTextureColorMod(buildingsTexture_, 0, 0, 0);
	        SDL_SetTextureAlphaMod(buildingsTexture_, 70);
	        SDL_Rect drop = dst;
	        drop.x += 1;
	        drop.y += 1;
	        SDL_RenderCopy(renderer, buildingsTexture_, &buildingSrc, &drop);
	        SDL_SetTextureAlphaMod(buildingsTexture_, 55);
	        SDL_Rect o1 = dst;
	        SDL_Rect o2 = dst;
	        SDL_Rect o3 = dst;
	        SDL_Rect o4 = dst;
	        o1.x += 1;
	        o2.x -= 1;
	        o3.y += 1;
	        o4.y -= 1;
	        SDL_RenderCopy(renderer, buildingsTexture_, &buildingSrc, &o1);
	        SDL_RenderCopy(renderer, buildingsTexture_, &buildingSrc, &o2);
	        SDL_RenderCopy(renderer, buildingsTexture_, &buildingSrc, &o3);
	        SDL_RenderCopy(renderer, buildingsTexture_, &buildingSrc, &o4);
	        SDL_SetTextureColorMod(buildingsTexture_, 255, 255, 255);
	        SDL_SetTextureAlphaMod(buildingsTexture_, 255);
	        SDL_RenderCopy(renderer, buildingsTexture_, &buildingSrc, &dst);
	      }
	    }
	    SDL_SetTextureAlphaMod(shadowTexture_, 90);
	  }

  CrashContextSetStage("Render::Objects");
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const Tile& tile = world.At(x, y);
      if (tile.type != TileType::Land && tile.type != TileType::Sand) continue;
      if (tile.trees <= 0 && tile.food <= 0) continue;

      const float worldX = static_cast<float>(x) * tileSize;
      const float worldY = static_cast<float>(y) * tileSize;

      // Fallback: if the large tree sprites are missing, render the old atlas tree.
      const bool hasLargeTreeSprite =
          (treeTexture_ && treeTexW_ > 0 && treeTexH_ > 0) || (tree1Texture_ && tree1TexW_ > 0 && tree1TexH_ > 0);
      if (tile.trees > 0 && !hasLargeTreeSprite) {
        uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                            TreeRules::kLargeTreeSeed);
        SDL_Rect src = PickObjectVariant(kTreeCoords, h);
        int jitterX = static_cast<int>(h % 5u) - 2;
        int jitterY = static_cast<int>((h >> 8) % 5u) - 2;
        const float objX = worldX + static_cast<float>(jitterX);
        const float objY = worldY + static_cast<float>(jitterY);
        SDL_Rect dst = MakeDstRect(objX, objY, tileSize, tileSize, camera);
        SDL_RendererFlip flip = (h & (1u << 16)) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

        SDL_SetTextureColorMod(objectsTexture_, 0, 0, 0);
        SDL_SetTextureAlphaMod(objectsTexture_, 55);
        SDL_Rect o1 = dst;
        SDL_Rect o2 = dst;
        SDL_Rect o3 = dst;
        SDL_Rect o4 = dst;
        o1.x += 1;
        o2.x -= 1;
        o3.y += 1;
        o4.y -= 1;
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &o1, 0.0, nullptr, flip);
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &o2, 0.0, nullptr, flip);
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &o3, 0.0, nullptr, flip);
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &o4, 0.0, nullptr, flip);
        SDL_SetTextureColorMod(objectsTexture_, 255, 255, 255);
        SDL_SetTextureAlphaMod(objectsTexture_, 255);
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &dst, 0.0, nullptr, flip);
      }

      if (tile.food > 0) {
        uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), kFoodSeed);
        SDL_Rect src = PickObjectVariant(kFoodCoords, h);
        int jitterX = static_cast<int>(h % 5u) - 2;
        int jitterY = static_cast<int>((h >> 8) % 5u) - 2;
        const float objX = worldX + static_cast<float>(jitterX);
	        const float objY = worldY + static_cast<float>(jitterY);
	        SDL_Rect dst = MakeDstRect(objX, objY, tileSize, tileSize, camera);
	        SDL_RendererFlip flip = (h & (1u << 16)) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;

	        SDL_SetTextureColorMod(objectsTexture_, 0, 0, 0);
	        SDL_SetTextureAlphaMod(objectsTexture_, 48);
	        SDL_Rect o1 = dst;
	        SDL_Rect o2 = dst;
	        SDL_Rect o3 = dst;
        SDL_Rect o4 = dst;
        o1.x += 1;
        o2.x -= 1;
        o3.y += 1;
        o4.y -= 1;
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &o1, 0.0, nullptr, flip);
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &o2, 0.0, nullptr, flip);
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &o3, 0.0, nullptr, flip);
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &o4, 0.0, nullptr, flip);
        SDL_SetTextureColorMod(objectsTexture_, 255, 255, 255);
        SDL_SetTextureAlphaMod(objectsTexture_, 255);
        SDL_RenderCopyEx(renderer, objectsTexture_, &src, &dst, 0.0, nullptr, flip);
      }
    }
  }

  struct TreeCanopyDrawItem {
    SDL_Texture* texture = nullptr;
    SDL_Rect src{0, 0, 0, 0};
    SDL_Rect dst{0, 0, 0, 0};
    SDL_RendererFlip flip = SDL_FLIP_NONE;
    float depthKey = 0.0f;  // world-space Y used for occlusion ordering
  };
  std::vector<TreeCanopyDrawItem> treeCanopies;
  int treePadTiles = 2;

  // Large tree sprite pass: render above objects and below town hall/fire/humans.
  CrashContextSetStage("Render::Trees");
  const bool hasTree0 = (treeTexture_ && treeTexW_ > 0 && treeTexH_ > 0);
  const bool hasTree1 = (tree1Texture_ && tree1TexW_ > 0 && tree1TexH_ > 0);
  if (hasTree0 || hasTree1) {
    struct LargeTreeSprite {
      SDL_Texture* texture = nullptr;
      int texW = 0;
      int texH = 0;
      SDL_Rect trunkSrc{0, 0, 0, 0};
      SDL_Rect canopySrc{0, 0, 0, 0};
      int canopyOccludeSrcY = 0;
      bool allowFlip = true;
    };

    const LargeTreeSprite tree0{treeTexture_, treeTexW_, treeTexH_, treeTrunkSrc_, treeCanopySrc_,
                                treeCanopyOccludeSrcY_, true};
    const LargeTreeSprite tree1{tree1Texture_, tree1TexW_, tree1TexH_, tree1TrunkSrc_, tree1CanopySrc_,
                                tree1CanopyOccludeSrcY_, false};

    auto pickTree = [&](int x, int y) -> const LargeTreeSprite& {
      if (hasTree0 && hasTree1) {
        uint32_t choice =
            Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), TreeRules::kLargeTreeSeed ^ 0x7A24B31Fu);
        return ((choice % 100u) < 70u) ? tree1 : tree0;
      }
      return hasTree1 ? tree1 : tree0;
    };

    float scale = tileSize / static_cast<float>(kTilePx);
    float maxDrawW = 0.0f;
    float maxDrawH = 0.0f;
    if (hasTree0) {
      maxDrawW = std::max(maxDrawW, static_cast<float>(tree0.texW) * scale);
      maxDrawH = std::max(maxDrawH, static_cast<float>(tree0.texH) * scale);
    }
    if (hasTree1) {
      maxDrawW = std::max(maxDrawW, static_cast<float>(tree1.texW) * scale);
      maxDrawH = std::max(maxDrawH, static_cast<float>(tree1.texH) * scale);
    }
    int pad = 6;
    pad = std::max(pad, static_cast<int>(std::ceil(maxDrawW / tileSize)) + 2);
    pad = std::max(pad, static_cast<int>(std::ceil(maxDrawH / tileSize)) + 2);
    treePadTiles = pad;
    const int objMinX = std::max(0, minX - pad);
    const int objMinY = std::max(0, minY - pad);
    const int objMaxX = std::min(world.width() - 1, maxX + pad);
    const int objMaxY = std::min(world.height() - 1, maxY + pad);
    // Ground shadow mod.
    SDL_Rect shadowSrc = ShadowSrc();
    for (int y = objMinY; y <= objMaxY; ++y) {
      for (int x = objMinX; x <= objMaxX; ++x) {
        const Tile& tile = world.At(x, y);
        if (tile.type != TileType::Land) continue;
        if (tile.trees <= 0) continue;
        if (!TreeRules::IsLargeTreeAnchor(world, x, y)) continue;

        uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                            TreeRules::kLargeTreeSeed);
        const LargeTreeSprite& tree = pickTree(x, y);
        int jitterX = static_cast<int>(h % 5u) - 2;
        int jitterY = static_cast<int>((h >> 8) % 5u) - 2;
        SDL_RendererFlip flip = SDL_FLIP_NONE;
        if (tree.allowFlip) {
          flip = (h & (1u << 16)) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        }

        const bool splitTree =
            (tree.trunkSrc.h > 0 && tree.canopySrc.h > 0 && tree.canopyOccludeSrcY > 0);
        const float drawW = static_cast<float>(tree.texW) * scale;
        const float drawH = static_cast<float>(tree.texH) * scale;

        float anchorX = (static_cast<float>(x) + 0.5f) * tileSize;
        float anchorY = (static_cast<float>(y) + 1.0f) * tileSize;
        float worldX = anchorX - drawW * 0.5f + static_cast<float>(jitterX);
        float worldY = anchorY - drawH + static_cast<float>(jitterY);

        float shadowW = tileSize * 1.35f;
        float shadowH = tileSize * 0.55f;
        float shadowX = anchorX - shadowW * 0.5f + 2.0f;
        float shadowY = anchorY - shadowH * 0.55f + 2.0f;
        SDL_Rect shadowDst = MakeDstRect(shadowX, shadowY, shadowW, shadowH, camera);
        // Earthy ground AO under footprint.
        SDL_SetTextureColorMod(shadowTexture_, 85, 95, 60);
        SDL_SetTextureAlphaMod(shadowTexture_, 55);
        SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);
        // Soft shadow.
        SDL_SetTextureColorMod(shadowTexture_, 0, 0, 0);
        SDL_SetTextureAlphaMod(shadowTexture_, 110);
        SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);

        if (!splitTree) {
          SDL_Rect dst = MakeDstRect(worldX, worldY, drawW, drawH, camera);
          // Cheap outline (lighter than units).
          SDL_SetTextureColorMod(tree.texture, 0, 0, 0);
          SDL_SetTextureAlphaMod(tree.texture, 55);
          SDL_Rect o1 = dst;
          SDL_Rect o2 = dst;
          SDL_Rect o3 = dst;
          SDL_Rect o4 = dst;
          o1.x += 1;
          o2.x -= 1;
          o3.y += 1;
          o4.y -= 1;
          SDL_RenderCopyEx(renderer, tree.texture, nullptr, &o1, 0.0, nullptr, flip);
          SDL_RenderCopyEx(renderer, tree.texture, nullptr, &o2, 0.0, nullptr, flip);
          SDL_RenderCopyEx(renderer, tree.texture, nullptr, &o3, 0.0, nullptr, flip);
          SDL_RenderCopyEx(renderer, tree.texture, nullptr, &o4, 0.0, nullptr, flip);
          SDL_SetTextureColorMod(tree.texture, 255, 255, 255);
          SDL_SetTextureAlphaMod(tree.texture, 255);
          SDL_RenderCopyEx(renderer, tree.texture, nullptr, &dst, 0.0, nullptr, flip);
        } else {
          const float trunkHWorld = static_cast<float>(tree.trunkSrc.h) * scale;
          const float canopyHWorld = static_cast<float>(tree.canopySrc.h) * scale;
          const float trunkWorldY = worldY + (drawH - trunkHWorld);

          SDL_Rect trunkDst = MakeDstRect(worldX, trunkWorldY, drawW, trunkHWorld, camera);
          SDL_SetTextureColorMod(tree.texture, 0, 0, 0);
          SDL_SetTextureAlphaMod(tree.texture, 55);
          SDL_Rect o1 = trunkDst;
          SDL_Rect o2 = trunkDst;
          SDL_Rect o3 = trunkDst;
          SDL_Rect o4 = trunkDst;
          o1.x += 1;
          o2.x -= 1;
          o3.y += 1;
          o4.y -= 1;
          SDL_RenderCopyEx(renderer, tree.texture, &tree.trunkSrc, &o1, 0.0, nullptr, flip);
          SDL_RenderCopyEx(renderer, tree.texture, &tree.trunkSrc, &o2, 0.0, nullptr, flip);
          SDL_RenderCopyEx(renderer, tree.texture, &tree.trunkSrc, &o3, 0.0, nullptr, flip);
          SDL_RenderCopyEx(renderer, tree.texture, &tree.trunkSrc, &o4, 0.0, nullptr, flip);
          SDL_SetTextureColorMod(tree.texture, 255, 255, 255);
          SDL_SetTextureAlphaMod(tree.texture, 255);
          SDL_RenderCopyEx(renderer, tree.texture, &tree.trunkSrc, &trunkDst, 0.0, nullptr, flip);

          TreeCanopyDrawItem canopy;
          canopy.texture = tree.texture;
          canopy.src = tree.canopySrc;
          canopy.dst = MakeDstRect(worldX, worldY, drawW, canopyHWorld, camera);
          canopy.flip = flip;
          canopy.depthKey = worldY + static_cast<float>(tree.canopyOccludeSrcY) * scale;
          treeCanopies.push_back(canopy);
        }
      }
    }
    SDL_SetTextureAlphaMod(shadowTexture_, 90);
  }

		  // Large town hall sprite pass: render above objects and below fire/humans.
		  // Uses Capital.png for the founding settlement's original town hall tile; all other town halls use TH.png.
		  CrashContextSetStage("Render::TownHall");
		  if ((townHallTexture_ && townHallTexW_ > 0 && townHallTexH_ > 0) ||
		      (capitalTexture_ && capitalTexW_ > 0 && capitalTexH_ > 0)) {
		    float scale = tileSize / static_cast<float>(kTilePx);

		    int maxW = 0;
		    int maxH = 0;
		    if (townHallTexture_ && townHallTexW_ > 0 && townHallTexH_ > 0) {
		      maxW = std::max(maxW, townHallTexW_);
		      maxH = std::max(maxH, townHallTexH_);
		    }
		    if (capitalTexture_ && capitalTexW_ > 0 && capitalTexH_ > 0) {
		      maxW = std::max(maxW, capitalTexW_);
		      maxH = std::max(maxH, capitalTexH_);
		    }
		    float maxDrawW = static_cast<float>(maxW) * scale;
		    float maxDrawH = static_cast<float>(maxH) * scale;

		    int pad = 6;
		    pad = std::max(pad, static_cast<int>(std::ceil(maxDrawW / tileSize)) + 2);
		    pad = std::max(pad, static_cast<int>(std::ceil(maxDrawH / tileSize)) + 2);
		    const int buildMinX = std::max(0, minX - pad);
		    const int buildMinY = std::max(0, minY - pad);
		    const int buildMaxX = std::min(world.width() - 1, maxX + pad);
		    const int buildMaxY = std::min(world.height() - 1, maxY + pad);

		    auto resolveTownHallSprite = [&](int tileX, int tileY, const Tile& tile, int& outW, int& outH)
		        -> SDL_Texture* {
		      SDL_Texture* base = townHallTexture_ ? townHallTexture_ : capitalTexture_;
		      outW = townHallTexture_ ? townHallTexW_ : capitalTexW_;
		      outH = townHallTexture_ ? townHallTexH_ : capitalTexH_;

		      if (!capitalTexture_ || capitalTexW_ <= 0 || capitalTexH_ <= 0) return base;
		      const Settlement* owner = settlements.Get(tile.buildingOwnerId);
		      if (!owner) return base;
		      if (!owner->isCapital) return base;
		      if (tileX != owner->centerX || tileY != owner->centerY) return base;

		      outW = capitalTexW_;
		      outH = capitalTexH_;
		      return capitalTexture_;
		    };

		    SDL_SetTextureColorMod(shadowTexture_, 0, 0, 0);
		    SDL_SetTextureAlphaMod(shadowTexture_, 130);
		    for (int y = buildMinY; y <= buildMaxY; ++y) {
		      for (int x = buildMinX; x <= buildMaxX; ++x) {
		        const Tile& tile = world.At(x, y);
		        if (tile.building != BuildingType::TownHall) continue;

		        int texW = 0;
		        int texH = 0;
		        SDL_Texture* tex = resolveTownHallSprite(x, y, tile, texW, texH);
		        if (!tex || texW <= 0 || texH <= 0) continue;

		        float drawW = static_cast<float>(texW) * scale;
		        float drawH = static_cast<float>(texH) * scale;
		        float anchorX = (static_cast<float>(x) + 0.5f) * tileSize;
		        float anchorY = (static_cast<float>(y) + 1.0f) * tileSize;
		        float worldX = anchorX - drawW * 0.5f;
		        float worldY = anchorY - drawH;
		        float shadowW = drawW * 0.58f;
		        float shadowH = tileSize * 0.55f;
		        float shadowX = anchorX - shadowW * 0.5f + 2.0f;
		        float shadowY = anchorY - shadowH * 0.55f + 2.0f;
		        SDL_Rect shadowDst = MakeDstRect(shadowX, shadowY, shadowW, shadowH, camera);
		        SDL_SetTextureColorMod(shadowTexture_, 85, 95, 60);
		        SDL_SetTextureAlphaMod(shadowTexture_, 60);
		        SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);
		        SDL_SetTextureColorMod(shadowTexture_, 0, 0, 0);
		        SDL_SetTextureAlphaMod(shadowTexture_, 135);
		        SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);

		        SDL_Rect dst = MakeDstRect(worldX, worldY, drawW, drawH, camera);
		        SDL_SetTextureColorMod(tex, 0, 0, 0);
		        SDL_SetTextureAlphaMod(tex, 70);
		        SDL_Rect drop = dst;
		        drop.x += 1;
		        drop.y += 1;
		        SDL_RenderCopy(renderer, tex, nullptr, &drop);
		        SDL_SetTextureAlphaMod(tex, 50);
		        SDL_Rect o1 = dst;
		        SDL_Rect o2 = dst;
		        SDL_Rect o3 = dst;
		        SDL_Rect o4 = dst;
		        o1.x += 1;
		        o2.x -= 1;
		        o3.y += 1;
		        o4.y -= 1;
		        SDL_RenderCopy(renderer, tex, nullptr, &o1);
		        SDL_RenderCopy(renderer, tex, nullptr, &o2);
		        SDL_RenderCopy(renderer, tex, nullptr, &o3);
		        SDL_RenderCopy(renderer, tex, nullptr, &o4);
		        SDL_SetTextureColorMod(tex, 255, 255, 255);
		        SDL_SetTextureAlphaMod(tex, 255);
		        SDL_RenderCopy(renderer, tex, nullptr, &dst);
		      }
		    }
		    SDL_SetTextureAlphaMod(shadowTexture_, 90);
		  }

  CrashContextSetStage("Render::LargeEconomyBuildings");
  if ((marketTexture_ && marketTexW_ > 0 && marketTexH_ > 0) ||
      (forgeTexture_ && forgeTexW_ > 0 && forgeTexH_ > 0)) {
    float scale = tileSize / static_cast<float>(kTilePx);
    int maxW = std::max(marketTexW_, forgeTexW_);
    int maxH = std::max(marketTexH_, forgeTexH_);
    int pad = 6;
    pad = std::max(pad, static_cast<int>(std::ceil((static_cast<float>(maxW) * scale) / tileSize)) + 2);
    pad = std::max(pad, static_cast<int>(std::ceil((static_cast<float>(maxH) * scale) / tileSize)) + 2);
    const int buildMinX = std::max(0, minX - pad);
    const int buildMinY = std::max(0, minY - pad);
    const int buildMaxX = std::min(world.width() - 1, maxX + pad);
    const int buildMaxY = std::min(world.height() - 1, maxY + pad);

    SDL_SetTextureColorMod(shadowTexture_, 0, 0, 0);
    SDL_SetTextureAlphaMod(shadowTexture_, 125);
    for (int y = buildMinY; y <= buildMaxY; ++y) {
      for (int x = buildMinX; x <= buildMaxX; ++x) {
        const Tile& tile = world.At(x, y);
        SDL_Texture* tex = nullptr;
        int texW = 0;
        int texH = 0;
        if (tile.building == BuildingType::Market && marketTexture_) {
          tex = marketTexture_;
          texW = marketTexW_;
          texH = marketTexH_;
        } else if (tile.building == BuildingType::Forge && forgeTexture_) {
          tex = forgeTexture_;
          texW = forgeTexW_;
          texH = forgeTexH_;
        }
        if (!tex || texW <= 0 || texH <= 0) continue;

        float drawW = static_cast<float>(texW) * scale;
        float drawH = static_cast<float>(texH) * scale;
        float anchorX = (static_cast<float>(x) + 0.5f) * tileSize;
        float anchorY = (static_cast<float>(y) + 1.0f) * tileSize;
        float worldX = anchorX - drawW * 0.5f;
        float worldY = anchorY - drawH;
        float shadowW = drawW * 0.58f;
        float shadowH = tileSize * 0.42f;
        float shadowX = anchorX - shadowW * 0.5f + 2.0f;
        float shadowY = anchorY - shadowH * 0.55f + 2.0f;
        SDL_Rect shadowDst = MakeDstRect(shadowX, shadowY, shadowW, shadowH, camera);
        SDL_SetTextureColorMod(shadowTexture_, 85, 95, 60);
        SDL_SetTextureAlphaMod(shadowTexture_, 55);
        SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);
        SDL_SetTextureColorMod(shadowTexture_, 0, 0, 0);
        SDL_SetTextureAlphaMod(shadowTexture_, 125);
        SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);

        SDL_Rect dst = MakeDstRect(worldX, worldY, drawW, drawH, camera);
        SDL_SetTextureColorMod(tex, 0, 0, 0);
        SDL_SetTextureAlphaMod(tex, 65);
        SDL_Rect drop = dst;
        drop.x += 1;
        drop.y += 1;
        SDL_RenderCopy(renderer, tex, nullptr, &drop);
        SDL_SetTextureAlphaMod(tex, 45);
        SDL_Rect o1 = dst;
        SDL_Rect o2 = dst;
        SDL_Rect o3 = dst;
        SDL_Rect o4 = dst;
        o1.x += 1;
        o2.x -= 1;
        o3.y += 1;
        o4.y -= 1;
        SDL_RenderCopy(renderer, tex, nullptr, &o1);
        SDL_RenderCopy(renderer, tex, nullptr, &o2);
        SDL_RenderCopy(renderer, tex, nullptr, &o3);
        SDL_RenderCopy(renderer, tex, nullptr, &o4);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
      }
    }
    SDL_SetTextureAlphaMod(shadowTexture_, 90);
  }

  CrashContextSetStage("Render::Caravans");
  for (const auto& caravan : settlements.Caravans()) {
    const Settlement* from = settlements.Get(caravan.fromSettlementId);
    const Settlement* to = settlements.Get(caravan.toSettlementId);
    if (from && to) {
      float fromSX = 0.0f;
      float fromSY = 0.0f;
      float toSX = 0.0f;
      float toSY = 0.0f;
      WorldToScreen(camera, (static_cast<float>(from->centerX) + 0.5f) * tileSize,
                    (static_cast<float>(from->centerY) + 0.5f) * tileSize, fromSX, fromSY);
      WorldToScreen(camera, (static_cast<float>(to->centerX) + 0.5f) * tileSize,
                    (static_cast<float>(to->centerY) + 0.5f) * tileSize, toSX, toSY);
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 235, 205, 120, 42);
      SDL_RenderDrawLine(renderer, static_cast<int>(std::round(fromSX)), static_cast<int>(std::round(fromSY)),
                         static_cast<int>(std::round(toSX)), static_cast<int>(std::round(toSY)));
    }

    if (caravan.x < static_cast<float>(minX - 2) || caravan.x > static_cast<float>(maxX + 2) ||
        caravan.y < static_cast<float>(minY - 2) || caravan.y > static_cast<float>(maxY + 2)) {
      continue;
    }

    float caravanW = tileSize * 1.75f;
    float caravanH = tileSize * 1.25f;
    if (caravanTexW_ > 0 && caravanTexH_ > 0) {
      caravanH = caravanW * static_cast<float>(caravanTexH_) / static_cast<float>(caravanTexW_);
    }
    const float worldX = caravan.x * tileSize - caravanW * 0.5f;
    const float worldY = caravan.y * tileSize - caravanH * 0.78f;
    SDL_Rect dst = MakeDstRect(worldX, worldY, caravanW, caravanH, camera);
    SDL_Rect shadowDst = MakeDstRect(worldX + caravanW * 0.18f, worldY + caravanH * 0.78f,
                                     caravanW * 0.68f, caravanH * 0.18f, camera);
    SDL_SetTextureColorMod(shadowTexture_, 0, 0, 0);
    SDL_SetTextureAlphaMod(shadowTexture_, 90);
    SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);

    if (caravanTexture_) {
      SDL_RenderCopy(renderer, caravanTexture_, nullptr, &dst);
    } else {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 170, 120, 65, 230);
      SDL_RenderFillRect(renderer, &dst);
    }
  }

  CrashContextSetStage("Render::Fire");
  SDL_Rect fireSrc = FireSrc();
  for (int y = minY; y <= maxY; ++y) {
    for (int x = minX; x <= maxX; ++x) {
      const Tile& tile = world.At(x, y);
      if (!tile.burning) continue;

      const float worldX = static_cast<float>(x) * tileSize;
      const float worldY = static_cast<float>(y) * tileSize;
      uint32_t h = Hash2D(static_cast<uint32_t>(x), static_cast<uint32_t>(y), kFireSeed);
      int offsetX = static_cast<int>(h % 3u) - 1;
      int offsetY = static_cast<int>((h >> 8) % 3u) - 1;
      const float fireSize = tileSize * 0.35f;
      const float fireX = worldX + (tileSize - fireSize) * 0.5f + static_cast<float>(offsetX);
      const float fireY = worldY + (tileSize - fireSize) * 0.5f + static_cast<float>(offsetY);
      SDL_Rect dst = MakeDstRect(fireX, fireY, fireSize, fireSize, camera);
      SDL_RenderCopy(renderer, fireTexture_, &fireSrc, &dst);
    }
  }

  SDL_SetTextureColorMod(shadowTexture_, 0, 0, 0);
  SDL_SetTextureAlphaMod(shadowTexture_, 110);

  struct HumanBodyDrawItem {
    SDL_Texture* texture = nullptr;
    SDL_Rect src{0, 0, 0, 0};
    SDL_Rect dst{0, 0, 0, 0};
    bool hasSoldierDot = false;
    SDL_Rect dotDst{0, 0, 0, 0};
    SDL_Color dotColor{0, 0, 0, 0};
    float depthKey = 0.0f;  // world-space feet Y
  };
  std::vector<HumanBodyDrawItem> humanBodies;

  CrashContextSetStage("Render::HumanGround");
  SDL_Rect humanSrc{0, 0, spriteWidth_, spriteHeight_};
  if (config.showHumans) {
    const auto& list = humans.Humans();
    humanBodies.reserve(list.size());
    for (const auto& human : list) {
    if (!human.alive) continue;
    if (human.x < minX || human.x > maxX || human.y < minY || human.y > maxY) continue;

    const float tileWorldX = static_cast<float>(human.x) * tileSize;
    const float tileWorldY = static_cast<float>(human.y) * tileSize;
    const float worldX = (human.px - 0.5f) * tileSize + human.personalOffsetX * tileSize;
    const float worldY = (human.py - 0.5f) * tileSize + human.personalOffsetY * tileSize;

    if (config.showSoldierTileMarkers && human.role == Role::Soldier) {
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 40, 255, 80, 70);
      SDL_Rect tileDst = MakeDstRect(tileWorldX, tileWorldY, tileSize, tileSize, camera);
      SDL_RenderFillRect(renderer, &tileDst);
    }

    const float shadowW = tileSize * 0.55f;
    const float shadowH = tileSize * 0.22f;
    const float shadowX = worldX + (tileSize - shadowW) * 0.5f + 1.5f;
    const float shadowY = worldY + tileSize - shadowH * 0.6f + 1.5f;
    SDL_Rect shadowDst = MakeDstRect(shadowX, shadowY, shadowW, shadowH, camera);
    SDL_RenderCopy(renderer, shadowTexture_, &shadowSrc, &shadowDst);

    SDL_Texture* bodyTex = humansTexture_;
    SDL_Rect bodySrc = humanSrc;
    if (human.role == Role::Soldier && soldierTexture_) {
      bodyTex = soldierTexture_;
      bodySrc.w = soldierSpriteWidth_;
      bodySrc.h = soldierSpriteHeight_;
      int dir = static_cast<int>(human.facing);
      if (dir > 3) dir = 3;
      bodySrc.x = dir * soldierSpriteWidth_;
      bodySrc.y = 0;
    } else if (human.role == Role::Farmer && farmerTexture_) {
      bodyTex = farmerTexture_;
      bodySrc.w = farmerSpriteWidth_;
      bodySrc.h = farmerSpriteHeight_;
      int dir = static_cast<int>(human.facing);
      if (dir > 3) dir = 3;
      int row = human.female ? 1 : 0;
      bodySrc.x = dir * farmerSpriteWidth_;
      bodySrc.y = row * farmerSpriteHeight_;
    } else {
      int row = human.female ? 1 : 0;
      int col = human.animFrame + (human.moving ? 2 : 0);
      bodySrc.x = col * spriteWidth_;
      bodySrc.y = row * spriteHeight_;
    }
    if (!bodyTex) continue;

    HumanBodyDrawItem item;
    item.texture = bodyTex;
    item.src = bodySrc;
    item.dst = MakeDstRect(worldX, worldY, tileSize, tileSize, camera);
    item.depthKey = worldY + tileSize;

    if (human.role == Role::Soldier) {
      SDL_Color color{220, 220, 220, 220};
      if (human.settlementId > 0) {
        const Settlement* settlement = settlements.Get(human.settlementId);
        const Faction* faction =
            (settlement && settlement->factionId > 0) ? factions.Get(settlement->factionId) : nullptr;
        if (faction) {
          color = SDL_Color{faction->color.r, faction->color.g, faction->color.b, 220};
        }
      }
      item.hasSoldierDot = true;
      item.dotColor = color;
      const float dotSize = human.isGeneral ? (tileSize * 0.22f) : (tileSize * 0.12f);
      const float dotX = worldX + tileSize * 0.5f - dotSize * 0.5f;
      const float dotY = worldY + tileSize * 0.05f;
      item.dotDst = MakeDstRect(dotX, dotY, dotSize, dotSize, camera);
    }

      humanBodies.push_back(item);
    }
  }

  CrashContextSetStage("Render::YInterleave");
  if (!treeCanopies.empty() || !humanBodies.empty()) {
    struct YDrawRef {
      float depthKey = 0.0f;
      uint32_t index = 0;
      bool isHuman = false;
    };

    const int bucketPad = std::max(4, treePadTiles + 2);
    const int baseBucket = minY - bucketPad;
    const int bucketCount = (maxY - minY + 1) + bucketPad * 2;
    if (bucketCount > 0) {
      std::vector<std::vector<YDrawRef>> buckets(static_cast<size_t>(bucketCount));

      auto bucketIndexForDepth = [&](float depthKey) -> int {
        const int bucket = static_cast<int>(std::floor(depthKey / tileSize));
        int idx = bucket - baseBucket;
        if (idx < 0) idx = 0;
        if (idx >= bucketCount) idx = bucketCount - 1;
        return idx;
      };

      for (size_t i = 0; i < treeCanopies.size(); ++i) {
        const int b = bucketIndexForDepth(treeCanopies[i].depthKey);
        buckets[static_cast<size_t>(b)].push_back(
            YDrawRef{treeCanopies[i].depthKey, static_cast<uint32_t>(i), false});
      }
      for (size_t i = 0; i < humanBodies.size(); ++i) {
        const int b = bucketIndexForDepth(humanBodies[i].depthKey);
        buckets[static_cast<size_t>(b)].push_back(
            YDrawRef{humanBodies[i].depthKey, static_cast<uint32_t>(i), true});
      }

      auto drawTreeCanopy = [&](const TreeCanopyDrawItem& canopy) {
        if (!canopy.texture) return;
        SDL_SetTextureColorMod(canopy.texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(canopy.texture, 55);
        SDL_Rect o1 = canopy.dst;
        SDL_Rect o2 = canopy.dst;
        SDL_Rect o3 = canopy.dst;
        SDL_Rect o4 = canopy.dst;
        o1.x += 1;
        o2.x -= 1;
        o3.y += 1;
        o4.y -= 1;
        SDL_RenderCopyEx(renderer, canopy.texture, &canopy.src, &o1, 0.0, nullptr, canopy.flip);
        SDL_RenderCopyEx(renderer, canopy.texture, &canopy.src, &o2, 0.0, nullptr, canopy.flip);
        SDL_RenderCopyEx(renderer, canopy.texture, &canopy.src, &o3, 0.0, nullptr, canopy.flip);
        SDL_RenderCopyEx(renderer, canopy.texture, &canopy.src, &o4, 0.0, nullptr, canopy.flip);
        SDL_SetTextureColorMod(canopy.texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(canopy.texture, 255);
        SDL_RenderCopyEx(renderer, canopy.texture, &canopy.src, &canopy.dst, 0.0, nullptr, canopy.flip);
      };

      auto drawHumanBody = [&](const HumanBodyDrawItem& human) {
        SDL_Texture* tex = human.texture;
        if (!tex) return;
        SDL_SetTextureColorMod(tex, 0, 0, 0);
        SDL_SetTextureAlphaMod(tex, 130);
        SDL_Rect d1 = human.dst;
        SDL_Rect d2 = human.dst;
        SDL_Rect d3 = human.dst;
        SDL_Rect d4 = human.dst;
        d1.x += 1;
        d2.x -= 1;
        d3.y += 1;
        d4.y -= 1;
        SDL_RenderCopy(renderer, tex, &human.src, &d1);
        SDL_RenderCopy(renderer, tex, &human.src, &d2);
        SDL_RenderCopy(renderer, tex, &human.src, &d3);
        SDL_RenderCopy(renderer, tex, &human.src, &d4);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_RenderCopy(renderer, tex, &human.src, &human.dst);

        if (human.hasSoldierDot) {
          SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
          SDL_SetRenderDrawColor(renderer, human.dotColor.r, human.dotColor.g, human.dotColor.b,
                                 human.dotColor.a);
          SDL_RenderFillRect(renderer, &human.dotDst);
        }
      };

      for (auto& bucket : buckets) {
        if (bucket.size() > 1) {
          std::stable_sort(bucket.begin(), bucket.end(),
                           [](const YDrawRef& a, const YDrawRef& b) { return a.depthKey < b.depthKey; });
        }
        for (const auto& item : bucket) {
          if (item.isHuman) {
            drawHumanBody(humanBodies[static_cast<size_t>(item.index)]);
          } else {
            drawTreeCanopy(treeCanopies[static_cast<size_t>(item.index)]);
          }
        }
      }
    }
  }

  // Full-screen post: tiny grade + vignette + ultra-subtle grain.
  CrashContextSetStage("Render::Grade");
  {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect full{0, 0, windowWidth, windowHeight};
    // Slight desaturation: blend toward a neutral gray.
    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 10);
    SDL_RenderFillRect(renderer, &full);
    // Slight warm bias to unify palette (keep water cooling via WaterTint()).
    SDL_SetRenderDrawColor(renderer, 255, 235, 220, 6);
    SDL_RenderFillRect(renderer, &full);
  }

  CrashContextSetStage("Render::Vignette");
  if (vignetteTexture_) {
    SDL_SetTextureColorMod(vignetteTexture_, 0, 0, 0);
    SDL_SetTextureAlphaMod(vignetteTexture_, 34);
    SDL_Rect dst{0, 0, windowWidth, windowHeight};
    SDL_RenderCopy(renderer, vignetteTexture_, nullptr, &dst);
    SDL_SetTextureAlphaMod(vignetteTexture_, 255);
  }

  CrashContextSetStage("Render::Grain");
  if (grainTexture_ && grainTexW_ > 0 && grainTexH_ > 0) {
    SDL_SetTextureAlphaMod(grainTexture_, 6);  // ~2%
    SDL_SetTextureColorMod(grainTexture_, 255, 255, 255);
    SDL_Rect dst{0, 0, windowWidth, windowHeight};
    SDL_RenderCopy(renderer, grainTexture_, nullptr, &dst);
    SDL_SetTextureAlphaMod(grainTexture_, 255);
  }

  const auto& arrows = humans.Arrows();
  if (!arrows.empty()) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (const auto& arrow : arrows) {
      int tx = static_cast<int>(arrow.x);
      int ty = static_cast<int>(arrow.y);
      if (tx < minX - 2 || tx > maxX + 2 || ty < minY - 2 || ty > maxY + 2) continue;

      SDL_Color color{220, 220, 220, 200};
      if (arrow.shooterFactionId > 0) {
        const Faction* faction = factions.Get(arrow.shooterFactionId);
        if (faction) {
          color = SDL_Color{faction->color.r, faction->color.g, faction->color.b, 210};
        }
      }
      SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

      float worldX0 = arrow.prevX * tileSize;
      float worldY0 = arrow.prevY * tileSize;
      float worldX1 = arrow.x * tileSize;
      float worldY1 = arrow.y * tileSize;
      float sx0 = 0.0f, sy0 = 0.0f, sx1 = 0.0f, sy1 = 0.0f;
      WorldToScreen(camera, worldX0, worldY0, sx0, sy0);
      WorldToScreen(camera, worldX1, worldY1, sx1, sy1);
      SDL_RenderDrawLine(renderer, static_cast<int>(std::lround(sx0)),
                         static_cast<int>(std::lround(sy0)),
                         static_cast<int>(std::lround(sx1)),
                         static_cast<int>(std::lround(sy1)));
    }
  }

  if (config.showWarArrows && zoneSize > 0 && zonesX > 0 && zonesY > 0) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    auto drawArrow = [&](float worldFromX, float worldFromY, float worldToX, float worldToY, SDL_Color color) {
      float sx0 = 0.0f, sy0 = 0.0f, sx1 = 0.0f, sy1 = 0.0f;
      WorldToScreen(camera, worldFromX, worldFromY, sx0, sy0);
      WorldToScreen(camera, worldToX, worldToY, sx1, sy1);
      float dx = sx1 - sx0;
      float dy = sy1 - sy0;
      float len = std::sqrt(dx * dx + dy * dy);
      if (len < 10.0f) return;
      float ux = dx / len;
      float uy = dy / len;
      float size = 14.0f;
      float ax = sx1 - ux * size;
      float ay = sy1 - uy * size;
      float px = -uy;
      float py = ux;
      float wing = size * 0.55f;
      SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
      SDL_RenderDrawLine(renderer, static_cast<int>(std::lround(sx0)),
                         static_cast<int>(std::lround(sy0)),
                         static_cast<int>(std::lround(sx1)),
                         static_cast<int>(std::lround(sy1)));
      SDL_RenderDrawLine(renderer, static_cast<int>(std::lround(sx1)),
                         static_cast<int>(std::lround(sy1)),
                         static_cast<int>(std::lround(ax + px * wing)),
                         static_cast<int>(std::lround(ay + py * wing)));
      SDL_RenderDrawLine(renderer, static_cast<int>(std::lround(sx1)),
                         static_cast<int>(std::lround(sy1)),
                         static_cast<int>(std::lround(ax - px * wing)),
                         static_cast<int>(std::lround(ay - py * wing)));
    };

    for (const auto& settlement : settlements.Settlements()) {
      if (settlement.population <= 0) continue;
      if (settlement.centerX < minX || settlement.centerX > maxX ||
          settlement.centerY < minY || settlement.centerY > maxY) {
        continue;
      }
      if (settlement.warId <= 0 || settlement.factionId <= 0) continue;
      const Faction* faction = factions.Get(settlement.factionId);
      SDL_Color color{220, 220, 220, 180};
      if (faction) {
        color = SDL_Color{faction->color.r, faction->color.g, faction->color.b, 190};
      }
      bool attacker = factions.WarIsAttacker(settlement.warId, settlement.factionId);
      float fromX = settlement.centerX * tileSize + tileSize * 0.5f;
      float fromY = settlement.centerY * tileSize + tileSize * 0.5f;
      if (attacker && settlement.warTargetSettlementId > 0) {
        const Settlement* target = settlements.Get(settlement.warTargetSettlementId);
        if (target) {
          float toX = target->centerX * tileSize + tileSize * 0.5f;
          float toY = target->centerY * tileSize + tileSize * 0.5f;
          drawArrow(fromX, fromY, toX, toY, color);
        }
      } else if (!attacker && settlement.hasDefenseTarget) {
        float toX = settlement.defenseTargetX * tileSize + tileSize * 0.5f;
        float toY = settlement.defenseTargetY * tileSize + tileSize * 0.5f;
        drawArrow(fromX, fromY, toX, toY, SDL_Color{80, 200, 255, 190});
      }
    }
  }

  if (labelFont_) {
    UpdateLabelCache(renderer, settlements, factions);
    const int padding = 3;
    for (const auto& entry : labelCache_) {
      if (!entry.texture) continue;
      const Settlement* settlement = settlements.Get(entry.settlementId);
      if (!settlement) continue;
      if (settlement->centerX < minX || settlement->centerX > maxX ||
          settlement->centerY < minY || settlement->centerY > maxY) {
        continue;
      }

      float worldX = settlement->centerX * tileSize + tileSize * 0.5f - entry.width * 0.5f;
      float worldY = settlement->centerY * tileSize - entry.height - tileSize * 0.3f;
      float bgX = worldX - padding;
      float bgY = worldY - padding;
      float bgW = entry.width + padding * 2.0f;
      float bgH = entry.height + padding * 2.0f;

      SDL_Rect bgDst = MakeDstRect(bgX, bgY, bgW, bgH, camera);
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
      SDL_RenderFillRect(renderer, &bgDst);

      SDL_Rect dst = MakeDstRect(worldX, worldY, static_cast<float>(entry.width),
                                 static_cast<float>(entry.height), camera);
      SDL_RenderCopy(renderer, entry.texture, nullptr, &dst);

      if (settlement->captureProgress > 0.0f) {
        SDL_Color color{255, 70, 70, 220};
        const Faction* capFaction =
            (settlement->captureLeaderFactionId > 0) ? factions.Get(settlement->captureLeaderFactionId) : nullptr;
        if (capFaction) {
          color = SDL_Color{capFaction->color.r, capFaction->color.g, capFaction->color.b, 220};
        }
        float pct = std::max(0.0f, std::min(100.0f, settlement->captureProgress)) / 100.0f;
        const float barH = 4.0f;
        float barX = bgX;
        float barY = bgY - barH - 1.0f;
        float barW = bgW;
        SDL_Rect barBg = MakeDstRect(barX, barY, barW, barH, camera);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
        SDL_RenderFillRect(renderer, &barBg);
        SDL_Rect barFill =
            MakeDstRect(barX + 1.0f, barY + 1.0f, (barW - 2.0f) * pct, barH - 2.0f, camera);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer, &barFill);
      }
    }
  }

  if (config.showTroopCounts && labelFont_ && zoneSize > 0 && zonesX > 0 && zonesY > 0 &&
      minZoneX <= maxZoneX && minZoneY <= maxZoneY) {
    const int zoneCount = zonesX * zonesY;
    std::vector<int> zoneTotal(zoneCount, 0);
    std::vector<int> zoneTopFaction(zoneCount, -1);
    std::vector<int> zoneTopCount(zoneCount, 0);
    std::vector<int> zoneSecondFaction(zoneCount, -1);
    std::vector<int> zoneSecondCount(zoneCount, 0);

    auto bumpFaction = [&](int idx, int factionId) {
      zoneTotal[idx]++;
      if (zoneTopFaction[idx] == factionId) {
        zoneTopCount[idx]++;
        return;
      }
      if (zoneSecondFaction[idx] == factionId) {
        zoneSecondCount[idx]++;
        if (zoneSecondCount[idx] > zoneTopCount[idx]) {
          std::swap(zoneSecondFaction[idx], zoneTopFaction[idx]);
          std::swap(zoneSecondCount[idx], zoneTopCount[idx]);
        }
        return;
      }
      if (zoneTopFaction[idx] == -1 || zoneTopCount[idx] == 0) {
        zoneTopFaction[idx] = factionId;
        zoneTopCount[idx] = 1;
        return;
      }
      if (zoneSecondFaction[idx] == -1 || zoneSecondCount[idx] == 0) {
        zoneSecondFaction[idx] = factionId;
        zoneSecondCount[idx] = 1;
        return;
      }
      if (zoneTopCount[idx] <= zoneSecondCount[idx]) {
        zoneTopFaction[idx] = factionId;
        zoneTopCount[idx] = 1;
      } else {
        zoneSecondFaction[idx] = factionId;
        zoneSecondCount[idx] = 1;
      }
    };

    for (const auto& human : humans.Humans()) {
      if (!human.alive) continue;
      if (human.role != Role::Soldier) continue;
      if (human.settlementId <= 0) continue;
      if (human.x < minX || human.x > maxX || human.y < minY || human.y > maxY) continue;
      const Settlement* home = settlements.Get(human.settlementId);
      if (!home || home->factionId <= 0) continue;
      int zx = human.x / zoneSize;
      int zy = human.y / zoneSize;
      if (zx < minZoneX || zx > maxZoneX || zy < minZoneY || zy > maxZoneY) continue;
      int idx = zy * zonesX + zx;
      if (idx < 0 || idx >= zoneCount) continue;
      bumpFaction(idx, home->factionId);
    }

    auto getTextTexture = [&](const std::string& text, SDL_Color color, int& outW, int& outH) -> SDL_Texture* {
      for (auto& entry : textCache_) {
        if (entry.text == text && SameColor(entry.color, color) && entry.texture) {
          entry.lastUsedFrame = frameCounter_;
          outW = entry.width;
          outH = entry.height;
          return entry.texture;
        }
      }
      SDL_Surface* surface = TTF_RenderUTF8_Blended(labelFont_, text.c_str(), color);
      if (!surface) {
        outW = 0;
        outH = 0;
        return nullptr;
      }
      SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
      if (!texture) {
        SDL_FreeSurface(surface);
        outW = 0;
        outH = 0;
        return nullptr;
      }
      SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
      SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
      TextCacheEntry entry;
      entry.text = text;
      entry.color = color;
      entry.texture = texture;
      entry.width = surface->w;
      entry.height = surface->h;
      entry.lastUsedFrame = frameCounter_;
      textCache_.push_back(entry);
      outW = entry.width;
      outH = entry.height;
      SDL_FreeSurface(surface);
      return texture;
    };

    // Simple pruning to keep cache bounded.
    const size_t kMaxTextCache = 256;
    if (textCache_.size() > kMaxTextCache) {
      std::sort(textCache_.begin(), textCache_.end(),
                [](const TextCacheEntry& a, const TextCacheEntry& b) { return a.lastUsedFrame < b.lastUsedFrame; });
      while (textCache_.size() > kMaxTextCache) {
        auto& victim = textCache_.front();
        if (victim.texture) SDL_DestroyTexture(victim.texture);
        textCache_.erase(textCache_.begin());
      }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (int zy = minZoneY; zy <= maxZoneY; ++zy) {
      for (int zx = minZoneX; zx <= maxZoneX; ++zx) {
        int idx = zy * zonesX + zx;
        if (idx < 0 || idx >= zoneCount) continue;
        if (zoneTotal[idx] <= 0) continue;
        int intensity = settlements.ZoneConflictAt(zx, zy);
        if (!config.showTroopCountsAllZones && intensity <= 0) continue;

        std::string text = std::to_string(zoneTotal[idx]);
        int tw = 0;
        int th = 0;
        SDL_Texture* tex = getTextTexture(text, SDL_Color{255, 255, 255, 255}, tw, th);
        if (!tex || tw <= 0 || th <= 0) continue;

        float worldX = (zx * zoneSize + zoneSize * 0.5f) * tileSize - tw * 0.5f;
        float worldY = (zy * zoneSize + zoneSize * 0.5f) * tileSize - th * 0.5f;
        SDL_Rect bg = MakeDstRect(worldX - 3.0f, worldY - 2.0f, tw + 6.0f, th + 4.0f, camera);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
        SDL_RenderFillRect(renderer, &bg);

        if (zoneTopFaction[idx] > 0) {
          const Faction* f = factions.Get(zoneTopFaction[idx]);
          if (f) {
            SDL_SetRenderDrawColor(renderer, f->color.r, f->color.g, f->color.b, 220);
            SDL_Rect dot =
                MakeDstRect(worldX - 9.0f, worldY + th * 0.5f - 3.0f, 6.0f, 6.0f, camera);
            SDL_RenderFillRect(renderer, &dot);
          }
        }
        if (zoneSecondFaction[idx] > 0) {
          const Faction* f = factions.Get(zoneSecondFaction[idx]);
          if (f) {
            SDL_SetRenderDrawColor(renderer, f->color.r, f->color.g, f->color.b, 220);
            SDL_Rect dot =
                MakeDstRect(worldX + tw + 3.0f, worldY + th * 0.5f - 3.0f, 6.0f, 6.0f, camera);
            SDL_RenderFillRect(renderer, &dot);
          }
        }

        SDL_Rect dst = MakeDstRect(worldX, worldY, static_cast<float>(tw), static_cast<float>(th), camera);
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
      }
    }
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  for (const auto& marker : villageMarkers) {
    if (marker.ttlDays <= 0) continue;
    if (marker.x < minX || marker.x > maxX || marker.y < minY || marker.y > maxY) continue;
    float t = static_cast<float>(marker.ttlDays) / 25.0f;
    int alpha = static_cast<int>(50.0f + t * 205.0f);
    if (alpha > 255) alpha = 255;
    SDL_SetRenderDrawColor(renderer, 255, 40, 40, static_cast<Uint8>(alpha));

    const float markerSize = 6.0f;
    float worldX = marker.x * tileSize + tileSize * 0.5f - markerSize * 0.5f;
    float worldY = marker.y * tileSize + tileSize * 0.5f - markerSize * 0.5f;
    SDL_Rect dst = MakeDstRect(worldX, worldY, markerSize, markerSize, camera);
    SDL_RenderFillRect(renderer, &dst);
  }

  if (hoverValid) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 60, 60, 220);
    const float dotSize = 4.0f;
    float dotX = hoverTileX * tileSize + tileSize * 0.5f - dotSize * 0.5f;
    float dotY = hoverTileY * tileSize + tileSize * 0.5f - dotSize * 0.5f;
    SDL_Rect dotDst = MakeDstRect(dotX, dotY, dotSize, dotSize, camera);
    SDL_RenderFillRect(renderer, &dotDst);

    int radius = brushSize / 2;
    float brushX = static_cast<float>(hoverTileX - radius) * tileSize;
    float brushY = static_cast<float>(hoverTileY - radius) * tileSize;
    float brushW = static_cast<float>(brushSize) * tileSize;
    float brushH = static_cast<float>(brushSize) * tileSize;
    SDL_Rect brushRect = MakeDstRect(brushX, brushY, brushW, brushH, camera);
    SDL_SetRenderDrawColor(renderer, 255, 90, 90, 140);
    SDL_RenderDrawRect(renderer, &brushRect);
  }
}
