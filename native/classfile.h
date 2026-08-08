

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

using u1 = uint8_t;
using u2 = uint16_t;
using u4 = uint32_t;

enum : u1 {
    CP_Utf8               = 1,
    CP_Class              = 7,
    CP_String             = 8,
    CP_Fieldref           = 9,
    CP_Methodref          = 10,
    CP_InterfaceMethodref = 11,
    CP_NameAndType        = 12,
};

enum : u2 {
    ACC_PUBLIC    = 0x0001,
    ACC_PRIVATE   = 0x0002,
    ACC_PROTECTED = 0x0004,
    ACC_STATIC    = 0x0008,
    ACC_FINAL     = 0x0010,
    ACC_SUPER     = 0x0020,
    ACC_NATIVE    = 0x0100,
    ACC_ABSTRACT  = 0x0400,
};

class ClassBuilder {
public:

    ClassBuilder(std::string thisInternal,
                 std::string superInternal,
                 u2 majorVersion = 52);

    u2 utf8(std::string_view s);
    u2 classRef(std::string_view internalName);
    u2 nameAndType(std::string_view name, std::string_view desc);
    u2 methodRef(std::string_view klass, std::string_view name, std::string_view desc);
    u2 fieldRef(std::string_view klass, std::string_view name, std::string_view desc);
    u2 stringRef(std::string_view s);

    void addNativeMethod(std::string_view name,
                         std::string_view descriptor,
                         u2 accessFlags = ACC_PUBLIC | ACC_STATIC | ACC_NATIVE);

    void addCodedMethod(std::string_view name,
                        std::string_view descriptor,
                        u2 accessFlags,
                        std::vector<u1> code,
                        u2 maxStack,
                        u2 maxLocals);

    void setAccessFlags(u2 flags) { access_flags_ = flags; }

    std::vector<u1> build();

    const std::string& internalName() const { return this_internal_; }

private:
    struct RawEntry {
        u1 tag;
        std::vector<u1> body;
    };

    u2 addEntry(u1 tag, std::vector<u1> body);
    u2 findUtf8(std::string_view s) const;

    static void be16(std::vector<u1>& out, u2 v);
    static void be32(std::vector<u1>& out, u4 v);

    std::string this_internal_;
    std::string super_internal_;
    u2          major_version_;
    u2          access_flags_ = ACC_PUBLIC | ACC_SUPER;

    std::vector<RawEntry>          cp_;
    std::map<std::string, u2>      utf8_index_;
    std::map<u2, u2>               class_index_;
    std::map<uint64_t, u2>         nat_index_;
    std::map<uint64_t, u2>         ref_index_;

    struct MethodInfo {
        u2 access_flags;
        u2 name_index;
        u2 descriptor_index;

        std::vector<u1> code_attr;
    };
    std::vector<MethodInfo> methods_;

    u2 code_utf8_ = 0;
};
