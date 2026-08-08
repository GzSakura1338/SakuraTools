#include "world_cache.h"

#include <cstring>

WorldStateCache g_cache;

namespace {

bool starts(std::string_view fqcn, std::string_view prefix) {
    return fqcn.size() >= prefix.size() &&
           std::memcmp(fqcn.data(), prefix.data(), prefix.size()) == 0;
}

std::string classNameFor(JNIEnv* env, jobject o) {
    if (!o || !g_jvmti) return {};
    jclass c = env->GetObjectClass(o);
    if (!c) return {};
    char* sig = nullptr;
    jvmtiError rc = g_jvmti->GetClassSignature(c, &sig, nullptr);
    env->DeleteLocalRef(c);
    if (rc != JVMTI_ERROR_NONE || !sig) return {};
    std::string out;
    const char* p = sig;
    if (*p == 'L') {
        ++p;
        for (; *p && *p != ';'; ++p) out.push_back(*p == '/' ? '.' : *p);
    } else {
        out = sig;
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
    return out;
}

}

WorldStateCache::Category WorldStateCache::categorize(std::string_view fqcn) {
    static constexpr const char kGame[] = "net.minecraft.network.protocol.game.";
    if (!starts(fqcn, kGame)) return Category::Ignored;
    std::string_view leaf = fqcn.substr(sizeof(kGame) - 1);

    auto dollar = leaf.find('$');
    if (dollar != std::string_view::npos) leaf = leaf.substr(0, dollar);

    if (leaf == "ClientboundLoginPacket")                    return Category::LatestLogin;
    if (leaf == "ClientboundGameEventPacket")                return Category::LatestGameEvent;
    if (leaf == "ClientboundSetTimePacket")                  return Category::LatestSetTime;
    if (leaf == "ClientboundPlayerPositionPacket")           return Category::LatestPlayerPosition;
    if (leaf == "ClientboundPlayerInfoUpdatePacket" ||
        leaf == "ClientboundPlayerInfoRemovePacket")         return Category::AppendPlayerInfo;
    if (leaf == "ClientboundAddEntityPacket" ||
        leaf == "ClientboundAddPlayerPacket" ||
        leaf == "ClientboundRemoveEntitiesPacket")           return Category::AppendEntity;
    if (leaf == "ClientboundLevelChunkWithLightPacket" ||
        leaf == "ClientboundForgetLevelChunkPacket")         return Category::AppendChunk;
    return Category::Ignored;
}

void WorldStateCache::observe(JNIEnv* env, jobject packetLocalRef) {
    if (!packetLocalRef) return;
    std::string cls = classNameFor(env, packetLocalRef);
    Category cat = categorize(cls);
    if (cat == Category::Ignored) return;

    jobject g = env->NewGlobalRef(packetLocalRef);
    if (!g) return;

    std::lock_guard<std::mutex> lock(m_);

    auto replaceLatest = [&](jobject& slot) {
        if (slot) env->DeleteGlobalRef(slot);
        slot = g;
    };
    auto pushCapped = [&](std::vector<jobject>& list, size_t cap) {
        list.push_back(g);
        while (list.size() > cap) {
            env->DeleteGlobalRef(list.front());
            list.erase(list.begin());
        }
    };

    switch (cat) {
        case Category::LatestLogin:          replaceLatest(login_); break;
        case Category::LatestGameEvent:      replaceLatest(gameEvent_); break;
        case Category::LatestSetTime:        replaceLatest(setTime_); break;
        case Category::LatestPlayerPosition: replaceLatest(playerPos_); break;
        case Category::AppendPlayerInfo:     pushCapped(playerInfo_, kMaxPlayerInfo); break;
        case Category::AppendEntity:         pushCapped(entities_, kMaxEntities); break;
        case Category::AppendChunk:          pushCapped(chunks_, kMaxChunks); break;
        default: env->DeleteGlobalRef(g); break;
    }
}

void WorldStateCache::replay(JNIEnv* env, jobject connection,
                             jmethodID connSendMid) const {
    if (!connection || !connSendMid) return;
    auto send = [&](jobject p) {
        if (!p) return;
        env->CallVoidMethod(connection, connSendMid, p);
        if (env->ExceptionCheck()) {
            LogTo("replay: send threw for one packet");
            env->ExceptionClear();
        }
    };
    std::lock_guard<std::mutex> lock(m_);
    LogTo("replay: login=%p gameEvent=%p setTime=%p playerInfo=%zu entities=%zu chunks=%zu pos=%p",
          (void*)login_, (void*)gameEvent_, (void*)setTime_,
          playerInfo_.size(), entities_.size(), chunks_.size(),
          (void*)playerPos_);

    send(login_);
    send(gameEvent_);
    send(setTime_);
    for (jobject p : playerInfo_) send(p);
    for (jobject p : entities_)   send(p);
    for (jobject p : chunks_)     send(p);
    send(playerPos_);
    LogTo("replay: done");
}

void WorldStateCache::clear(JNIEnv* env) {
    std::lock_guard<std::mutex> lock(m_);
    auto drop = [&](jobject& slot) { if (slot) { env->DeleteGlobalRef(slot); slot = nullptr; } };
    auto dropAll = [&](std::vector<jobject>& list) {
        for (jobject p : list) env->DeleteGlobalRef(p);
        list.clear();
    };
    drop(login_); drop(gameEvent_); drop(setTime_); drop(playerPos_);
    dropAll(playerInfo_); dropAll(entities_); dropAll(chunks_);
}

void WorldStateCache::dump() const {
    std::lock_guard<std::mutex> lock(m_);
    LogTo("WorldStateCache: login=%d gameEvent=%d setTime=%d pos=%d "
          "playerInfo=%zu entities=%zu chunks=%zu",
          login_ ? 1 : 0, gameEvent_ ? 1 : 0, setTime_ ? 1 : 0,
          playerPos_ ? 1 : 0,
          playerInfo_.size(), entities_.size(), chunks_.size());
}
