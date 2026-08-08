#include "random_name.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
#include <mutex>
#include <algorithm>
#include <cctype>

namespace {

constexpr std::array<const char*, 96> kWords = {
    "Azure",   "Amber",   "Arcane",   "Aurora",   "Blaze",   "Bramble",
    "Bright",  "Cinder",  "Cipher",   "Cobalt",   "Comet",   "Crimson",
    "Crystal", "Dawn",    "Deep",     "Divine",   "Dream",   "Ember",
    "Ethereal","Fable",   "Fallen",   "Feral",    "Flare",   "Flux",
    "Frost",   "Gale",    "Gilded",   "Glacier",  "Glass",   "Glimmer",
    "Glow",    "Grace",   "Grim",     "Hallow",   "Haze",    "Heavy",
    "Hidden",  "Hollow",  "Horizon",  "Iron",     "Ivory",   "Jade",
    "Karma",   "Keen",    "Lantern",  "Lattice",  "Lich",    "Lucid",
    "Lunar",   "Marble",  "Mirror",   "Mist",     "Moon",    "Myth",
    "Nebula",  "Nexus",   "Night",    "Nova",     "Obsidian","Onyx",
    "Orbit",   "Pale",    "Phantom",  "Phase",    "Prism",   "Pulse",
    "Quartz",  "Quiet",   "Radiant",  "Raven",    "Reactor", "Relic",
    "Ridge",   "Rift",    "Rune",     "Saga",     "Sable",   "Scarlet",
    "Shadow",  "Shard",   "Silent",   "Silver",   "Solstice","Somber",
    "Solar",   "Sonic",   "Spark",    "Stellar",  "Storm",   "Swift",
    "Thunder", "Titan",   "Umbral",   "Vector",   "Vestige", "Vortex",
};

std::mt19937_64 make_rng() {
    LARGE_INTEGER pc{};
    QueryPerformanceCounter(&pc);
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    uint64_t seed =
        (uint64_t)pc.QuadPart ^
        ((uint64_t)ft.dwHighDateTime << 32 | ft.dwLowDateTime) ^
        ((uint64_t)GetCurrentProcessId() << 16) ^
        (uint64_t)GetCurrentThreadId();
    return std::mt19937_64(seed);
}

}

std::string GenerateRandomClassName(int minWords, int maxWords) {
    if (minWords < 1) minWords = 1;
    if (maxWords < minWords) maxWords = minWords;

    std::mt19937_64 rng = make_rng();

    std::uniform_int_distribution<int> nDist(minWords, maxWords);
    std::uniform_int_distribution<size_t> wDist(0, kWords.size() - 1);

    int n = nDist(rng);
    std::string out;
    out.reserve(48);
    for (int i = 0; i < n; ++i) {
        out += kWords[wDist(rng)];
    }
    return out;
}

std::string MakeInternalName(const std::string& internalPackage,
                             const std::string& simpleName) {
    if (internalPackage.empty()) return simpleName;
    std::string out = internalPackage;
    if (out.back() != '/') out.push_back('/');
    out += simpleName;
    return out;
}

namespace {

std::string GenerateRandomPackageName() {
    std::mt19937_64 rng = make_rng();
    std::uniform_int_distribution<int> depthDist(1, 3);
    std::uniform_int_distribution<size_t> wDist(0, kWords.size() - 1);

    int depth = depthDist(rng);
    std::string out;
    for (int i = 0; i < depth; ++i) {
        if (i > 0) {
            out += "/";
        }
        std::uniform_int_distribution<int> wordCountDist(1, 2);
        int wordCount = wordCountDist(rng);

        std::string firstWord = kWords[wDist(rng)];
        for (char& c : firstWord) {
            c = std::tolower(static_cast<unsigned char>(c));
        }
        out += firstWord;

        for (int j = 1; j < wordCount; ++j) {
            out += kWords[wDist(rng)];
        }
    }
    return out;
}

std::string g_trampolinePackage;
std::once_flag g_packageInitFlag;

}

const std::string& GetTrampolinePackage() {
    std::call_once(g_packageInitFlag, []() {
        g_trampolinePackage = GenerateRandomPackageName();
    });
    return g_trampolinePackage;
}

