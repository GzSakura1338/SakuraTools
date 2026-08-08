

#include "../native/classfile.h"
#include "../native/class_edit.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

static int g_failures = 0;

#define CHECK(cond, msg) do {                                                \
    if (!(cond)) {                                                           \
        std::fprintf(stderr, "FAIL %s:%d — %s\n", __FILE__, __LINE__, msg); \
        ++g_failures;                                                        \
    }                                                                        \
} while (0)

struct RawClass {
    const u1* p;
    size_t len;

    size_t cp_count;
    size_t cp_end_offset;
    size_t methods_offset;
};

static u2 be16(const u1* p) { return (u2(p[0]) << 8) | p[1]; }
static u4 be32(const u1* p) {
    return (u4(p[0]) << 24) | (u4(p[1]) << 16) | (u4(p[2]) << 8) | p[3];
}

static RawClass locateSections(const std::vector<u1>& bytes) {
    RawClass r{};
    r.p = bytes.data();
    r.len = bytes.size();
    const u1* p = r.p;
    p += 4;
    p += 2;
    p += 2;
    u2 cpc = be16(p); p += 2;
    r.cp_count = cpc;
    for (u2 i = 1; i < cpc; ++i) {
        u1 tag = *p++;
        size_t bodySz;
        switch (tag) {
            case CP_Utf8: bodySz = 2 + be16(p); break;
            case CP_Class: case CP_String: bodySz = 2; break;
            case CP_Fieldref: case CP_Methodref: case CP_InterfaceMethodref:
            case CP_NameAndType: bodySz = 4; break;
            case 3: case 4: bodySz = 4; break;
            case 5: case 6: bodySz = 8; break;
            case 15: bodySz = 3; break;
            case 16: case 19: case 20: bodySz = 2; break;
            case 17: case 18: bodySz = 4; break;
            default: throw std::runtime_error("test: unexpected tag");
        }
        p += bodySz;
        if (tag == 5 || tag == 6) ++i;
    }
    r.cp_end_offset = static_cast<size_t>(p - r.p);

    p += 2;
    p += 2;
    p += 2;
    u2 ifc = be16(p); p += 2;
    p += size_t(ifc) * 2;

    u2 fc = be16(p); p += 2;
    for (u2 i = 0; i < fc; ++i) {
        p += 6;
        u2 ac = be16(p); p += 2;
        for (u2 j = 0; j < ac; ++j) {
            p += 2;
            u4 al = be32(p); p += 4;
            p += al;
        }
    }
    r.methods_offset = static_cast<size_t>(p - r.p);
    return r;
}

static void test_prepend_static_call() {

    ClassBuilder cb("test/Foo", "java/lang/Object", 52);

    std::vector<u1> code = {0x1B, 0x3D, 0x1C, 0xAC};
    cb.addCodedMethod("demo", "(I)I", ACC_PUBLIC, code, 1, 3);
    std::vector<u1> classBytes = cb.build();

    CHECK(classBytes.size() > 32, "class bytes not empty");

    ClassFileEditor ed(classBytes.data(), classBytes.size());
    CHECK(ed.thisClassInternalName() == "test/Foo", "this class name resolved");

    u2 mref = ed.addMethodRef("java/lang/System", "currentTimeMillis", "()J");
    CHECK(mref != 0, "methodref added");

    std::vector<u1> ins = {
        0xB8, u1((mref >> 8) & 0xFF), u1(mref & 0xFF),
        0x58,
    };
    bool ok = ed.prependToMethodCode("demo", "(I)I", ins, 2);
    CHECK(ok, "prepend to method succeeded");

    std::vector<u1> outBytes = ed.serialize();
    CHECK(outBytes.size() > classBytes.size(), "output grew (cp + code)");

    ClassFileEditor ed2(outBytes.data(), outBytes.size());
    CHECK(ed2.thisClassInternalName() == "test/Foo", "reparse: name still correct");

    RawClass raw = locateSections(outBytes);

    u2 mc = be16(outBytes.data() + raw.methods_offset);
    CHECK(mc == 1, "methods_count == 1");

    size_t methodStart = raw.methods_offset + 2;
    u2 attrsCount = be16(outBytes.data() + methodStart + 6);
    CHECK(attrsCount == 1, "method has 1 attribute (Code)");

    size_t attrStart = methodStart + 8;
    u2 attrNameIdx = be16(outBytes.data() + attrStart);
    (void)attrNameIdx;
    u4 attrLen = be32(outBytes.data() + attrStart + 2);

    size_t codeAttrStart = attrStart + 6;
    u2 outMaxStack = be16(outBytes.data() + codeAttrStart);
    u2 outMaxLocals = be16(outBytes.data() + codeAttrStart + 2);
    u4 outCodeLen = be32(outBytes.data() + codeAttrStart + 4);
    CHECK(outMaxStack == 2, "max_stack raised to 2 (was 1, extraStack 2)");
    CHECK(outMaxLocals == 3, "max_locals unchanged");
    CHECK(outCodeLen == 4 + 4, "code_length = original(4) + insertion(4)");

    const u1* codePtr = outBytes.data() + codeAttrStart + 8;
    CHECK(codePtr[0] == 0xB8 && codePtr[1] == u1((mref >> 8) & 0xFF) &&
          codePtr[2] == u1(mref & 0xFF) && codePtr[3] == 0x58,
          "insertion bytes at code[0..3]");

    CHECK(codePtr[4] == 0x1B && codePtr[5] == 0x3D &&
          codePtr[6] == 0x1C && codePtr[7] == 0xAC,
          "original bytecode preserved at code[4..7]");

    (void)attrLen;
}

static void test_stackmap_promotion() {

    std::vector<u1> body = {
        0x00, 0x01,
        60,
    };

    ClassBuilder cb("test/Bar", "java/lang/Object", 52);

    std::vector<u1> code(63, 0x00);
    code.push_back(0x01);
    code.push_back(0xBF);
    cb.addCodedMethod("t", "()V", ACC_PUBLIC, code, 1, 1);
    std::vector<u1> raw = cb.build();

    ClassFileEditor ed(raw.data(), raw.size());

    bool ok = ed.prependToMethodCode("t", "()V", {0x00, 0x00, 0x00, 0x00, 0x00}, 0);
    CHECK(ok, "prepend without StackMapTable succeeds");
    auto out = ed.serialize();
    ClassFileEditor ed2(out.data(), out.size());
    CHECK(ed2.thisClassInternalName() == "test/Bar", "roundtrip name");
}

int main() {
    try {
        test_prepend_static_call();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "test_prepend_static_call threw: %s\n", e.what());
        ++g_failures;
    }
    try {
        test_stackmap_promotion();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "test_stackmap_promotion threw: %s\n", e.what());
        ++g_failures;
    }
    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::printf("%d test failures\n", g_failures);
    return 1;
}
