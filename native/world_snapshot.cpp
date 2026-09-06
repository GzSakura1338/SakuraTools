#include "world_snapshot.h"
#include "snapshot_jni.h"
#include "classfile.h"
#include "random_name.h"
#include "runtime_gate.h"
#include "login_handoff.h"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace {
constexpr const char* MC = "net.minecraft.client.Minecraft";
constexpr const char* LISTENER = "net.minecraft.client.multiplayer.ClientPacketListener";
constexpr const char* LEVEL = "net.minecraft.client.multiplayer.ClientLevel";
constexpr const char* ENTITY = "net.minecraft.world.entity.Entity";
constexpr const char* PLAYER = "net.minecraft.world.entity.player.Player";
constexpr const char* CACHE = "net.minecraft.client.multiplayer.ClientChunkCache";
constexpr const char* STORAGE = "net.minecraft.client.multiplayer.ClientChunkCache$Storage";
#define PACKET(NAME) "net.minecraft.network.protocol.game." NAME

struct Job {
    std::mutex mutex;
    std::condition_variable ready;
    bool done = false, cancelled = false, ok = false;
    jobject task = nullptr, minecraft = nullptr, connection = nullptr;
    SnapshotBoundary boundary = nullptr;
    std::vector<jobject> packets;
};
std::mutex jobsMutex;
std::vector<std::shared_ptr<Job>> jobs;
jobject snapshotLoader = nullptr;
jclass taskClass = nullptr;
jmethodID taskCtor = nullptr;

void releasePackets(JNIEnv* env, std::vector<jobject>& packets) {
    for (jobject packet : packets) env->DeleteGlobalRef(packet);
    packets.clear();
}

void captureNow(JNIEnv* env, Job& job) {
    SnapshotJni j(env, snapshotLoader);
    jobject listener = j.object(job.minecraft, MC, "getConnection", "m_91403_", "()Lnet/minecraft/client/multiplayer/ClientPacketListener;");
    jobject connection = j.object(listener, LISTENER, "getConnection", "m_104910_", "()Lnet/minecraft/network/Connection;");
    if (!env->IsSameObject(connection, job.connection)) throw std::runtime_error("A changed connection during snapshot");
    jobject level = j.required(j.get(job.minecraft, MC, "level", "f_91073_", "Lnet/minecraft/client/multiplayer/ClientLevel;"));
    jobject player = j.required(j.get(job.minecraft, MC, "player", "f_91074_", "Lnet/minecraft/client/player/LocalPlayer;"));
    jobject mode = j.get(job.minecraft, MC, "gameMode", "f_91072_", "Lnet/minecraft/client/multiplayer/MultiPlayerGameMode;");
    jobject gameType = j.object(mode, "net.minecraft.client.multiplayer.MultiPlayerGameMode", "getPlayerMode", "m_105295_", "()Lnet/minecraft/world/level/GameType;");
    jobject registry = j.object(listener, LISTENER, "registryAccess", "m_105152_", "()Lnet/minecraft/core/RegistryAccess;");
    jobject frozen = j.object(registry, "net.minecraft.core.RegistryAccess", "freeze", "m_203557_", "()Lnet/minecraft/core/RegistryAccess$Frozen;");
    jint id = j.integer(player, ENTITY, "getId", "m_19879_", "()I");
    jdouble x = j.real(player, ENTITY, "getX", "m_20185_", "()D");
    jdouble y = j.real(player, ENTITY, "getY", "m_20186_", "()D");
    jdouble z = j.real(player, ENTITY, "getZ", "m_20189_", "()D");
    jobject cache = j.object(level, LEVEL, "getChunkSource", "m_7726_", "()Lnet/minecraft/client/multiplayer/ClientChunkCache;");
    jobject storage = j.get(cache, CACHE, "storage", "f_104410_", "Lnet/minecraft/client/multiplayer/ClientChunkCache$Storage;");
    jint radius = j.getInt(storage, STORAGE, "chunkRadius", "f_104467_");
    jint viewRadius = std::max<jint>(2, radius - 3);
    jint cx = j.getInt(storage, STORAGE, "viewCenterX", "f_104469_");
    jint cz = j.getInt(storage, STORAGE, "viewCenterZ", "f_104470_");
    jobject chunks = j.get(storage, STORAGE, "chunks", "f_104466_", "Ljava/util/concurrent/atomic/AtomicReferenceArray;");
    jobject light = j.object(cache, CACHE, "getLightEngine", "m_7827_", "()Lnet/minecraft/world/level/lighting/LevelLightEngine;");
    auto add = [&](jobject packet) {
        j.required(packet);
        jobject global = j.required(env->NewGlobalRef(packet));
        try { job.packets.push_back(global); } catch (...) { env->DeleteGlobalRef(global); throw; }
        env->DeleteLocalRef(packet);
    };
    jobject levels = j.object(listener, LISTENER, "levels", "m_105151_", "()Ljava/util/Set;");
    jobject dimension = j.object(level, LEVEL, "dimension", "m_46472_", "()Lnet/minecraft/resources/ResourceKey;");
    jobject dimensionType = j.object(level, LEVEL, "dimensionTypeId", "m_220362_", "()Lnet/minecraft/resources/ResourceKey;");
    jobject empty = j.stat("java.util.Optional", "empty", "empty", "()Ljava/util/Optional;");
    add(j.make(PACKET("ClientboundLoginPacket"),
        "(IZLnet/minecraft/world/level/GameType;Lnet/minecraft/world/level/GameType;Ljava/util/Set;Lnet/minecraft/core/RegistryAccess$Frozen;Lnet/minecraft/resources/ResourceKey;Lnet/minecraft/resources/ResourceKey;JIIIZZZZLjava/util/Optional;I)V",
        id, JNI_FALSE, gameType, (jobject)nullptr, levels, frozen, dimensionType, dimension,
        (jlong)0, (jint)1, viewRadius, viewRadius, JNI_FALSE, JNI_TRUE, JNI_FALSE, JNI_FALSE, empty, (jint)0));
    add(j.make(PACKET("ClientboundSetChunkCacheRadiusPacket"), "(I)V", viewRadius));
    add(j.make(PACKET("ClientboundSetChunkCacheCenterPacket"), "(II)V", cx, cz));
    jlong time = j.longValue(level, LEVEL, "getGameTime", "m_46467_", "()J");
    jlong day = j.longValue(level, LEVEL, "getDayTime", "m_46468_", "()J");
    add(j.make(PACKET("ClientboundSetTimePacket"), "(JJZ)V", time, day, JNI_TRUE));

    auto each = [&](jobject iterable, auto work) {
        jobject iterator = j.object(iterable, "java.lang.Iterable", "iterator", "iterator", "()Ljava/util/Iterator;");
        while (j.boolean(iterator, "java.util.Iterator", "hasNext", "hasNext", "()Z")) {
            SnapshotLocalFrame frame(env);
            work(j.object(iterator, "java.util.Iterator", "next", "next", "()Ljava/lang/Object;"));
        }
        env->DeleteLocalRef(iterator);
    };
    const char* infoType = "net.minecraft.client.multiplayer.PlayerInfo";
    const char* actionType = PACKET("ClientboundPlayerInfoUpdatePacket$Action");
    jobject actions = j.stat("java.util.EnumSet", "noneOf", "noneOf", "(Ljava/lang/Class;)Ljava/util/EnumSet;", j.type(actionType));
    for (const char* name : {"ADD_PLAYER", "UPDATE_GAME_MODE", "UPDATE_LISTED", "UPDATE_LATENCY", "UPDATE_DISPLAY_NAME"}) {
        auto field = env->GetStaticFieldID(j.type(actionType), name, "Lnet/minecraft/network/protocol/game/ClientboundPlayerInfoUpdatePacket$Action;");
        j.check();
        jobject action = env->GetStaticObjectField(j.type(actionType), field);
        j.boolean(actions, "java.util.Collection", "add", "add", "(Ljava/lang/Object;)Z", action);
        env->DeleteLocalRef(action);
    }
    jobject entries = j.make("java.util.ArrayList", "()V");
    jobject listed = j.object(listener, LISTENER, "getListedOnlinePlayers", "m_246170_", "()Ljava/util/Collection;");
    jobject online = j.object(listener, LISTENER, "getOnlinePlayers", "m_105142_", "()Ljava/util/Collection;");
    each(online, [&](jobject info) {
        jobject profile = j.object(info, infoType, "getProfile", "m_105312_", "()Lcom/mojang/authlib/GameProfile;");
        jobject uuid = j.object(profile, "com.mojang.authlib.GameProfile", "getId", "getId", "()Ljava/util/UUID;");
        jboolean visible = j.boolean(listed, "java.util.Collection", "contains", "contains", "(Ljava/lang/Object;)Z", info);
        jint latency = j.integer(info, infoType, "getLatency", "m_105330_", "()I");
        jobject gm = j.object(info, infoType, "getGameMode", "m_105325_", "()Lnet/minecraft/world/level/GameType;");
        jobject display = j.object(info, infoType, "getTabListDisplayName", "m_105342_", "()Lnet/minecraft/network/chat/Component;");
        jobject entry = j.make(PACKET("ClientboundPlayerInfoUpdatePacket$Entry"),
            "(Ljava/util/UUID;Lcom/mojang/authlib/GameProfile;ZILnet/minecraft/world/level/GameType;Lnet/minecraft/network/chat/Component;Lnet/minecraft/network/chat/RemoteChatSession$Data;)V",
            uuid, profile, visible, latency, gm, display, (jobject)nullptr);
        j.boolean(entries, "java.util.Collection", "add", "add", "(Ljava/lang/Object;)Z", entry);
    });
    jobject emptyList = j.stat("java.util.Collections", "emptyList", "emptyList", "()Ljava/util/List;");
    jobject infoPacket = j.make(PACKET("ClientboundPlayerInfoUpdatePacket"), "(Ljava/util/EnumSet;Ljava/util/Collection;)V", actions, emptyList);
    auto entriesField = j.field(PACKET("ClientboundPlayerInfoUpdatePacket"), "entries", "f_244436_", "Ljava/util/List;");
    env->SetObjectField(infoPacket, entriesField, entries);
    j.check();
    add(infoPacket);
    jobject scoreboard = j.object(level, LEVEL, "getScoreboard", "m_6188_", "()Lnet/minecraft/world/scores/Scoreboard;");
    jobject teams = j.object(scoreboard, "net.minecraft.world.scores.Scoreboard", "getPlayerTeams", "m_83491_", "()Ljava/util/Collection;");
    each(teams, [&](jobject team) {
        add(j.stat(PACKET("ClientboundSetPlayerTeamPacket"), "createAddOrModifyPacket", "m_179332_",
            "(Lnet/minecraft/world/scores/PlayerTeam;Z)Lnet/minecraft/network/protocol/game/ClientboundSetPlayerTeamPacket;", team, JNI_TRUE));
    });
    bool playerChunk = false;
    jint count = j.integer(chunks, "java.util.concurrent.atomic.AtomicReferenceArray", "length", "length", "()I");
    int chunkCount = 0;
    for (jint i = 0; i < count; ++i) {
        SnapshotLocalFrame frame(env);
        jobject chunk = j.object(chunks, "java.util.concurrent.atomic.AtomicReferenceArray", "get", "get", "(I)Ljava/lang/Object;", i);
        if (!chunk) continue;
        jobject pos = j.object(chunk, "net.minecraft.world.level.chunk.LevelChunk", "getPos", "m_7697_", "()Lnet/minecraft/world/level/ChunkPos;");
        jint px = j.getInt(pos, "net.minecraft.world.level.ChunkPos", "x", "f_45578_");
        jint pz = j.getInt(pos, "net.minecraft.world.level.ChunkPos", "z", "f_45579_");
        if (std::abs(px - cx) > radius || std::abs(pz - cz) > radius) continue;
        playerChunk |= px == static_cast<int>(std::floor(x / 16.0)) && pz == static_cast<int>(std::floor(z / 16.0));
        add(j.make(PACKET("ClientboundLevelChunkWithLightPacket"),
            "(Lnet/minecraft/world/level/chunk/LevelChunk;Lnet/minecraft/world/level/lighting/LevelLightEngine;Ljava/util/BitSet;Ljava/util/BitSet;)V",
            chunk, light, (jobject)nullptr, (jobject)nullptr));
        ++chunkCount;
    }
    if (!playerChunk) throw std::runtime_error("A's player chunk is not loaded");
    jobject entities = j.object(level, LEVEL, "entitiesForRendering", "m_104735_", "()Ljava/lang/Iterable;");
    each(entities, [&](jobject entity) {
        if (env->IsSameObject(entity, player)) return;
        jobject spawn = env->IsInstanceOf(entity, j.type(PLAYER))
            ? j.make(PACKET("ClientboundAddPlayerPacket"), "(Lnet/minecraft/world/entity/player/Player;)V", entity)
            : j.object(entity, ENTITY, "getAddEntityPacket", "m_5654_", "()Lnet/minecraft/network/protocol/Packet;");
        // Forge custom spawn packets require a modded B handshake, which this proxy does not provide.
        if (!env->IsInstanceOf(spawn, j.type(PACKET("ClientboundAddEntityPacket"))) &&
            !env->IsInstanceOf(spawn, j.type(PACKET("ClientboundAddPlayerPacket")))) return;
        add(spawn);
        jobject data = j.object(entity, ENTITY, "getEntityData", "m_20088_", "()Lnet/minecraft/network/syncher/SynchedEntityData;");
        jobject values = j.object(data, "net.minecraft.network.syncher.SynchedEntityData", "getNonDefaultValues", "m_252804_", "()Ljava/util/List;");
        if (values) add(j.make(PACKET("ClientboundSetEntityDataPacket"), "(ILjava/util/List;)V",
            j.integer(entity, ENTITY, "getId", "m_19879_", "()I"), values));
    });
    jobject abilities = j.object(player, PLAYER, "getAbilities", "m_150110_", "()Lnet/minecraft/world/entity/player/Abilities;");
    add(j.make(PACKET("ClientboundPlayerAbilitiesPacket"), "(Lnet/minecraft/world/entity/player/Abilities;)V", abilities));
    jobject food = j.object(player, PLAYER, "getFoodData", "m_36324_", "()Lnet/minecraft/world/food/FoodData;");
    jfloat health = j.decimal(player, "net.minecraft.world.entity.LivingEntity", "getHealth", "m_21223_", "()F");
    jint foodLevel = j.integer(food, "net.minecraft.world.food.FoodData", "getFoodLevel", "m_38702_", "()I");
    jfloat saturation = j.decimal(food, "net.minecraft.world.food.FoodData", "getSaturationLevel", "m_38722_", "()F");
    add(j.make(PACKET("ClientboundSetHealthPacket"), "(FIF)V", health, foodLevel, saturation));
    jobject menu = j.get(player, PLAYER, "inventoryMenu", "f_36095_", "Lnet/minecraft/world/inventory/InventoryMenu;");
    const char* menuType = "net.minecraft.world.inventory.AbstractContainerMenu";
    jint state = j.integer(menu, menuType, "getStateId", "m_182424_", "()I");
    jobject items = j.object(menu, menuType, "getItems", "m_38927_", "()Lnet/minecraft/core/NonNullList;");
    jobject carried = j.object(menu, menuType, "getCarried", "m_142621_", "()Lnet/minecraft/world/item/ItemStack;");
    add(j.make(PACKET("ClientboundContainerSetContentPacket"), "(IILnet/minecraft/core/NonNullList;Lnet/minecraft/world/item/ItemStack;)V", (jint)0, state, items, carried));
    jobject spawnPos = j.object(level, LEVEL, "getSharedSpawnPos", "m_220360_", "()Lnet/minecraft/core/BlockPos;");
    jfloat spawnAngle = j.decimal(level, LEVEL, "getSharedSpawnAngle", "m_220361_", "()F");
    add(j.make(PACKET("ClientboundSetDefaultSpawnPositionPacket"), "(Lnet/minecraft/core/BlockPos;F)V", spawnPos, spawnAngle));
    jobject relative = j.stat("java.util.Collections", "emptySet", "emptySet", "()Ljava/util/Set;");
    jfloat yaw = j.decimal(player, ENTITY, "getYRot", "m_146908_", "()F");
    jfloat pitch = j.decimal(player, ENTITY, "getXRot", "m_146909_", "()F");
    add(j.make(PACKET("ClientboundPlayerPositionPacket"), "(DDDFFLjava/util/Set;I)V", x, y, z, yaw, pitch, relative, (jint)LoginHandoff::teleportId));
    LogTo("mid-login: native snapshot chunks=%d packets=%zu position=%.2f,%.2f,%.2f", chunkCount, job.packets.size(), x, y, z);
}

void JNICALL runSnapshot(JNIEnv* env, jobject task) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(jobsMutex);
        auto it = std::find_if(jobs.begin(), jobs.end(), [&](const auto& entry) { return env->IsSameObject(task, entry->task); });
        if (it == jobs.end()) return;
        job = *it;
        jobs.erase(it);
    }
    bool ok = false;
    RuntimeCallback callback;
    try {
        SnapshotLocalFrame frame(env);
        bool cancelled;
        { std::lock_guard<std::mutex> lock(job->mutex); cancelled = job->cancelled; }
        if (callback && !cancelled && job->boundary(env)) {
            captureNow(env, *job);
            ok = true;
        }
    } catch (const std::exception& e) { LogTo("mid-login: native snapshot failed: %s", e.what()); }
    LogAndClearException(env, "native snapshot");
    std::lock_guard<std::mutex> lock(job->mutex);
    if (!ok || job->cancelled) releasePackets(env, job->packets);
    env->DeleteGlobalRef(job->task);
    env->DeleteGlobalRef(job->minecraft);
    env->DeleteGlobalRef(job->connection);
    job->ok = ok && !job->cancelled;
    job->done = true;
    job->ready.notify_all();
}
}

bool InstallWorldSnapshot(JNIEnv* env, jobject loader) {
    if (taskClass && snapshotLoader && taskCtor) return true;
    std::string name = MakeInternalName(GetTrampolinePackage(), GenerateRandomClassName(2, 3));
    ClassBuilder cb(name, "java/lang/Thread", 52);
    u2 ctor = cb.methodRef("java/lang/Thread", "<init>", "()V");
    cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, {0x2A, 0xB7, u1(ctor >> 8), u1(ctor), 0xB1}, 1, 1);
    cb.addNativeMethod("run", "()V", ACC_PUBLIC | ACC_NATIVE);
    auto bytes = cb.build();
    jclass cls = env->DefineClass(name.c_str(), loader, reinterpret_cast<const jbyte*>(bytes.data()), (jsize)bytes.size());
    if (!cls) { LogAndClearException(env, "native snapshot bridge"); return false; }
    JNINativeMethod binding{const_cast<char*>("run"), const_cast<char*>("()V"), reinterpret_cast<void*>(&runSnapshot)};
    bool ok = env->RegisterNatives(cls, &binding, 1) == JNI_OK;
    if (ok) taskCtor = env->GetMethodID(cls, "<init>", "()V");
    if (ok && taskCtor) {
        jobject loaderRef = env->NewGlobalRef(loader);
        jclass classRef = loaderRef ? static_cast<jclass>(env->NewGlobalRef(cls)) : nullptr;
        if (loaderRef && classRef) {
            snapshotLoader = loaderRef;
            taskClass = classRef;
        } else {
            if (loaderRef) env->DeleteGlobalRef(loaderRef);
            if (classRef) env->DeleteGlobalRef(classRef);
        }
    }
    env->DeleteLocalRef(cls);
    LogAndClearException(env, "native snapshot install");
    return taskClass && snapshotLoader && taskCtor;
}

bool CaptureWorldSnapshot(JNIEnv* env, jobject minecraft, jobject connection,
                          SnapshotBoundary boundary, std::vector<jobject>& packets) {
    auto job = std::make_shared<Job>();
    if (!taskClass || !taskCtor || !boundary) return false;
    jobject task = env->NewObject(taskClass, taskCtor);
    if (!task || env->ExceptionCheck()) return false;
    job->task = env->NewGlobalRef(task);
    job->minecraft = env->NewGlobalRef(minecraft);
    job->connection = env->NewGlobalRef(connection);
    job->boundary = boundary;
    auto discard = [&] {
        if (job->task) env->DeleteGlobalRef(job->task);
        if (job->minecraft) env->DeleteGlobalRef(job->minecraft);
        if (job->connection) env->DeleteGlobalRef(job->connection);
    };
    if (!job->task || !job->minecraft || !job->connection) { discard(); env->DeleteLocalRef(task); return false; }
    { std::lock_guard<std::mutex> lock(jobsMutex); jobs.push_back(job); }
    jclass minecraftClass = env->GetObjectClass(minecraft);
    jmethodID execute = minecraftClass ? env->GetMethodID(minecraftClass, "execute", "(Ljava/lang/Runnable;)V") : nullptr;
    if (minecraftClass) env->DeleteLocalRef(minecraftClass);
    if (execute && !env->ExceptionCheck()) env->CallVoidMethod(minecraft, execute, task);
    env->DeleteLocalRef(task);
    bool failed = !execute || env->ExceptionCheck();
    if (!failed) {
        std::unique_lock<std::mutex> lock(job->mutex);
        if (job->ready.wait_for(lock, std::chrono::seconds(15), [&] { return job->done; })) {
            packets.swap(job->packets);
            return job->ok;
        }
    }
    {
        std::lock_guard<std::mutex> lock(job->mutex);
        job->cancelled = true;
        if (job->done) releasePackets(env, job->packets);
    }
    {
        std::lock_guard<std::mutex> lock(jobsMutex);
        auto it = std::find(jobs.begin(), jobs.end(), job);
        if (it != jobs.end()) { jobs.erase(it); discard(); }
        // A running callback owns its references and discards results after cancellation.
    }
    LogAndClearException(env, "native snapshot dispatch");
    return false;
}
