

#include "../native/classfile.h"
#include "../native/class_edit.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static std::vector<u1> readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(2); }
    std::streamsize sz = f.tellg();
    f.seekg(0);
    std::vector<u1> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

static void writeAll(const std::string& path, const std::vector<u1>& bytes) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); std::exit(2); }
    f.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: emit_samples <outdir> <FakeConnection.class path>\n");
        return 2;
    }
    std::string outdir = argv[1];
    std::string fakeConnPath = argv[2];

    {
        ClassBuilder cb("gen/HookBridge", "java/lang/Object", 52);
        cb.addNativeMethod("trace", "(Ljava/lang/String;)V");
        cb.addNativeMethod("onChannelActive",
                           "(Ljava/lang/Object;Ljava/lang/Object;)V");
        writeAll(outdir + "/HookBridge.class", cb.build());
    }

    {
        const char* super_ = "io/netty/channel/ChannelDuplexHandler";
        ClassBuilder cb("gen/RelayHandler", super_, 52);
        u2 superInit = cb.methodRef(super_, "<init>", "()V");
        std::vector<u1> ctor = {
            0x2A,
            0xB7, u1((superInit >> 8) & 0xFF), u1(superInit & 0xFF),
            0xB1,
        };
        cb.addCodedMethod("<init>", "()V", ACC_PUBLIC, ctor, 1, 1);
        cb.addNativeMethod("channelRead",
                           "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;)V",
                           ACC_PUBLIC | ACC_NATIVE);
        cb.addNativeMethod("write",
                           "(Lio/netty/channel/ChannelHandlerContext;Ljava/lang/Object;Lio/netty/channel/ChannelPromise;)V",
                           ACC_PUBLIC | ACC_NATIVE);
        writeAll(outdir + "/RelayHandler.class", cb.build());
    }

    {
        std::vector<u1> fake = readAll(fakeConnPath);
        ClassFileEditor ed(fake.data(), fake.size());
        u2 mref = ed.addMethodRef(
            "gen/HookBridge",
            "onChannelActive",
            "(Ljava/lang/Object;Ljava/lang/Object;)V");
        std::vector<u1> insertion = {
            0x2A,
            0x2B,
            0xB8, u1((mref >> 8) & 0xFF), u1(mref & 0xFF),
        };
        bool ok = ed.prependToMethodCode(
            "channelActive", "(Ljava/lang/Object;)V",
            insertion, 2);
        if (!ok) {
            std::fprintf(stderr,
                         "emit_samples: FakeConnection lacks channelActive(Object)V\n");
            return 2;
        }
        writeAll(outdir + "/PatchedConnection.class", ed.serialize());
    }

    std::printf("emit_samples: wrote HookBridge / RelayHandler / PatchedConnection to %s\n",
                outdir.c_str());
    return 0;
}
