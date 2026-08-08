

#pragma once

#include "classfile.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class ClassFileEditor {
public:

    explicit ClassFileEditor(const u1* bytes, size_t length);

    u2 addUtf8(std::string_view s);
    u2 addClass(std::string_view internalName);
    u2 addNameAndType(std::string_view name, std::string_view desc);
    u2 addMethodRef(std::string_view klass,
                    std::string_view name,
                    std::string_view desc);

    bool prependToMethodCode(std::string_view name,
                             std::string_view desc,
                             const std::vector<u1>& insertion,
                             u2 extraStack);

    std::vector<u1> serialize() const;

    const std::string& thisClassInternalName() const { return this_name_; }

private:

    struct CpEntry {
        u1              tag = 0;
        std::vector<u1> body;
        bool            wide = false;
    };

    std::vector<CpEntry> cp_;

    u2 addRawEntry(u1 tag, std::vector<u1> body, bool wide = false);
    u2 findUtf8(std::string_view s) const;
    std::string cpUtf8Value(u2 idx) const;

    u4 magic_ = 0;
    u2 minor_version_ = 0;
    u2 major_version_ = 0;
    u2 access_flags_ = 0;
    u2 this_class_ = 0;
    u2 super_class_ = 0;
    std::string this_name_;
    std::vector<u2> interfaces_;

    std::vector<u1> fields_raw_;
    std::vector<u1> class_attrs_raw_;

    struct MethodInfo {
        u2 access_flags = 0;
        u2 name_index = 0;
        u2 descriptor_index = 0;

        std::vector<u1> attributes_raw_;
    };
    std::vector<MethodInfo> methods_;

    void parse(const u1* p, const u1* end);
    static u2 rd16(const u1*& p);
    static u4 rd32(const u1*& p);
    static void wr16(std::vector<u1>& o, u2 v);
    static void wr32(std::vector<u1>& o, u4 v);

    static void copyN(std::vector<u1>& out, const u1*& p, size_t n);

    static size_t cpBodySize(u1 tag, const u1* p);

    std::vector<u1> rebuildCodeAttribute(
        const std::vector<u1>& originalAttrs,
        u2 codeAttrNameIdx,
        const std::vector<u1>& insertion,
        u2 extraStack) const;

    static std::vector<u1> shiftStackMap(const std::vector<u1>& originalBody,
                                         u4 delta);

    static std::vector<u1> shiftLineNumberTable(const std::vector<u1>& originalBody,
                                                u4 delta);

    static std::vector<u1> shiftLocalVarTable(const std::vector<u1>& originalBody,
                                              u4 delta);
};
