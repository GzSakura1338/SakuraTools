#include "classfile.h"

#include <cstring>
#include <stdexcept>

void ClassBuilder::be16(std::vector<u1>& out, u2 v) {
    out.push_back(static_cast<u1>(v >> 8));
    out.push_back(static_cast<u1>(v & 0xFF));
}

void ClassBuilder::be32(std::vector<u1>& out, u4 v) {
    out.push_back(static_cast<u1>((v >> 24) & 0xFF));
    out.push_back(static_cast<u1>((v >> 16) & 0xFF));
    out.push_back(static_cast<u1>((v >>  8) & 0xFF));
    out.push_back(static_cast<u1>( v        & 0xFF));
}

ClassBuilder::ClassBuilder(std::string thisInternal,
                           std::string superInternal,
                           u2 majorVersion)
    : this_internal_(std::move(thisInternal)),
      super_internal_(std::move(superInternal)),
      major_version_(majorVersion) {}

u2 ClassBuilder::addEntry(u1 tag, std::vector<u1> body) {
    cp_.push_back(RawEntry{tag, std::move(body)});
    if (cp_.size() > 0xFFFE) {

        throw std::runtime_error("classfile: constant pool overflow");
    }
    return static_cast<u2>(cp_.size());
}

u2 ClassBuilder::findUtf8(std::string_view s) const {
    auto it = utf8_index_.find(std::string(s));
    return it == utf8_index_.end() ? 0 : it->second;
}

u2 ClassBuilder::utf8(std::string_view s) {
    if (u2 existing = findUtf8(s)) return existing;
    if (s.size() > 0xFFFF) {
        throw std::runtime_error("classfile: utf8 too long");
    }
    std::vector<u1> body;
    body.reserve(2 + s.size());
    be16(body, static_cast<u2>(s.size()));
    body.insert(body.end(), s.begin(), s.end());
    u2 idx = addEntry(CP_Utf8, std::move(body));
    utf8_index_.emplace(std::string(s), idx);
    return idx;
}

u2 ClassBuilder::classRef(std::string_view internalName) {
    u2 nameIdx = utf8(internalName);
    auto it = class_index_.find(nameIdx);
    if (it != class_index_.end()) return it->second;
    std::vector<u1> body;
    be16(body, nameIdx);
    u2 idx = addEntry(CP_Class, std::move(body));
    class_index_.emplace(nameIdx, idx);
    return idx;
}

u2 ClassBuilder::nameAndType(std::string_view name, std::string_view desc) {
    u2 nameIdx = utf8(name);
    u2 descIdx = utf8(desc);
    uint64_t key = (uint64_t)nameIdx << 16 | descIdx;
    auto it = nat_index_.find(key);
    if (it != nat_index_.end()) return it->second;
    std::vector<u1> body;
    be16(body, nameIdx);
    be16(body, descIdx);
    u2 idx = addEntry(CP_NameAndType, std::move(body));
    nat_index_.emplace(key, idx);
    return idx;
}

static uint64_t make_ref_key(u1 tag, u2 clsIdx, u2 natIdx) {
    return (uint64_t)tag << 40 | (uint64_t)clsIdx << 16 | natIdx;
}

u2 ClassBuilder::methodRef(std::string_view klass,
                           std::string_view name,
                           std::string_view desc) {
    u2 clsIdx = classRef(klass);
    u2 natIdx = nameAndType(name, desc);
    uint64_t key = make_ref_key(CP_Methodref, clsIdx, natIdx);
    auto it = ref_index_.find(key);
    if (it != ref_index_.end()) return it->second;
    std::vector<u1> body;
    be16(body, clsIdx);
    be16(body, natIdx);
    u2 idx = addEntry(CP_Methodref, std::move(body));
    ref_index_.emplace(key, idx);
    return idx;
}

u2 ClassBuilder::fieldRef(std::string_view klass,
                          std::string_view name,
                          std::string_view desc) {
    u2 clsIdx = classRef(klass);
    u2 natIdx = nameAndType(name, desc);
    uint64_t key = make_ref_key(CP_Fieldref, clsIdx, natIdx);
    auto it = ref_index_.find(key);
    if (it != ref_index_.end()) return it->second;
    std::vector<u1> body;
    be16(body, clsIdx);
    be16(body, natIdx);
    u2 idx = addEntry(CP_Fieldref, std::move(body));
    ref_index_.emplace(key, idx);
    return idx;
}

u2 ClassBuilder::stringRef(std::string_view s) {
    u2 sIdx = utf8(s);
    std::vector<u1> body;
    be16(body, sIdx);
    return addEntry(CP_String, std::move(body));
}

void ClassBuilder::addNativeMethod(std::string_view name,
                                   std::string_view descriptor,
                                   u2 accessFlags) {
    MethodInfo m{};
    m.access_flags = accessFlags | ACC_NATIVE;
    m.name_index = utf8(name);
    m.descriptor_index = utf8(descriptor);

    methods_.push_back(std::move(m));
}

void ClassBuilder::addCodedMethod(std::string_view name,
                                  std::string_view descriptor,
                                  u2 accessFlags,
                                  std::vector<u1> code,
                                  u2 maxStack,
                                  u2 maxLocals) {

    if (code_utf8_ == 0) code_utf8_ = utf8("Code");
    std::vector<u1> attrBody;
    be16(attrBody, maxStack);
    be16(attrBody, maxLocals);
    be32(attrBody, static_cast<u4>(code.size()));
    attrBody.insert(attrBody.end(), code.begin(), code.end());
    be16(attrBody, 0);
    be16(attrBody, 0);

    std::vector<u1> attr;
    be16(attr, code_utf8_);
    be32(attr, static_cast<u4>(attrBody.size()));
    attr.insert(attr.end(), attrBody.begin(), attrBody.end());

    MethodInfo m{};
    m.access_flags = accessFlags;
    m.name_index = utf8(name);
    m.descriptor_index = utf8(descriptor);
    m.code_attr = std::move(attr);
    methods_.push_back(std::move(m));
}

std::vector<u1> ClassBuilder::build() {

    u2 thisClass  = classRef(this_internal_);
    u2 superClass = classRef(super_internal_);

    std::vector<u1> out;
    out.reserve(256);

    be32(out, 0xCAFEBABE);
    be16(out, 0);
    be16(out, major_version_);

    be16(out, static_cast<u2>(cp_.size() + 1));

    for (const auto& e : cp_) {
        out.push_back(e.tag);
        out.insert(out.end(), e.body.begin(), e.body.end());
    }

    be16(out, access_flags_);
    be16(out, thisClass);
    be16(out, superClass);
    be16(out, 0);

    be16(out, 0);

    be16(out, static_cast<u2>(methods_.size()));
    for (const auto& m : methods_) {
        be16(out, m.access_flags);
        be16(out, m.name_index);
        be16(out, m.descriptor_index);
        if (m.code_attr.empty()) {
            be16(out, 0);
        } else {
            be16(out, 1);
            out.insert(out.end(), m.code_attr.begin(), m.code_attr.end());
        }
    }

    be16(out, 0);
    return out;
}
