#include "class_edit.h"

#include <cstring>
#include <stdexcept>
#include <string>

u2 ClassFileEditor::rd16(const u1*& p) {
    u2 v = (u2(p[0]) << 8) | p[1];
    p += 2;
    return v;
}

u4 ClassFileEditor::rd32(const u1*& p) {
    u4 v = (u4(p[0]) << 24) | (u4(p[1]) << 16) | (u4(p[2]) << 8) | p[3];
    p += 4;
    return v;
}

void ClassFileEditor::wr16(std::vector<u1>& o, u2 v) {
    o.push_back(u1(v >> 8));
    o.push_back(u1(v & 0xFF));
}

void ClassFileEditor::wr32(std::vector<u1>& o, u4 v) {
    o.push_back(u1((v >> 24) & 0xFF));
    o.push_back(u1((v >> 16) & 0xFF));
    o.push_back(u1((v >>  8) & 0xFF));
    o.push_back(u1( v        & 0xFF));
}

void ClassFileEditor::copyN(std::vector<u1>& out, const u1*& p, size_t n) {
    out.insert(out.end(), p, p + n);
    p += n;
}

size_t ClassFileEditor::cpBodySize(u1 tag, const u1* p) {
    switch (tag) {
        case CP_Utf8: {
            u2 len = (u2(p[0]) << 8) | p[1];
            return 2 + len;
        }
        case CP_Class:
        case CP_String:
        case 16 :
        case 19 :
        case 20 :
            return 2;
        case CP_Fieldref:
        case CP_Methodref:
        case CP_InterfaceMethodref:
        case CP_NameAndType:
        case 3  :
        case 4  :
        case 18 :
        case 17 :
            return 4;
        case 5  :
        case 6  :
            return 8;
        case 15 :
            return 3;
        default:
            throw std::runtime_error(
                "class_edit: unknown constant pool tag " + std::to_string((int)tag));
    }
}

ClassFileEditor::ClassFileEditor(const u1* bytes, size_t length) {
    parse(bytes, bytes + length);
}

void ClassFileEditor::parse(const u1* p, const u1* end) {
    if (end - p < 10) throw std::runtime_error("class_edit: too short");
    magic_ = rd32(p);
    if (magic_ != 0xCAFEBABE) throw std::runtime_error("class_edit: bad magic");
    minor_version_ = rd16(p);
    major_version_ = rd16(p);
    u2 cpCount = rd16(p);
    cp_.clear();
    cp_.push_back(CpEntry{});

    for (u2 i = 1; i < cpCount; ++i) {
        u1 tag = *p++;
        size_t bodyLen = cpBodySize(tag, p);
        CpEntry e;
        e.tag = tag;
        e.body.assign(p, p + bodyLen);
        e.wide = (tag == 5 || tag == 6);
        p += bodyLen;
        cp_.push_back(std::move(e));
        if (e.wide) {
            cp_.push_back(CpEntry{});
            ++i;
        }
    }

    access_flags_ = rd16(p);
    this_class_   = rd16(p);
    super_class_  = rd16(p);

    u2 ifCount = rd16(p);
    interfaces_.clear();
    interfaces_.reserve(ifCount);
    for (u2 i = 0; i < ifCount; ++i) interfaces_.push_back(rd16(p));

    if (this_class_ && cp_[this_class_].tag == CP_Class) {
        u2 nameIdx = (u2(cp_[this_class_].body[0]) << 8) | cp_[this_class_].body[1];
        this_name_ = cpUtf8Value(nameIdx);
    }

    const u1* fieldsStart = p;
    u2 fieldCount = rd16(p);
    for (u2 i = 0; i < fieldCount; ++i) {
        p += 6;
        u2 attrCount = rd16(p);
        for (u2 j = 0; j < attrCount; ++j) {
            p += 2;
            u4 attrLen = rd32(p);
            p += attrLen;
        }
    }
    fields_raw_.assign(fieldsStart, p);

    u2 methodCount = rd16(p);
    methods_.clear();
    methods_.reserve(methodCount);
    for (u2 i = 0; i < methodCount; ++i) {
        MethodInfo m;
        m.access_flags     = rd16(p);
        m.name_index       = rd16(p);
        m.descriptor_index = rd16(p);
        const u1* attrsStart = p;
        u2 attrCount = rd16(p);
        for (u2 j = 0; j < attrCount; ++j) {
            p += 2;
            u4 attrLen = rd32(p);
            p += attrLen;
        }
        m.attributes_raw_.assign(attrsStart, p);
        methods_.push_back(std::move(m));
    }

    class_attrs_raw_.assign(p, end);
}

std::string ClassFileEditor::cpUtf8Value(u2 idx) const {
    if (idx == 0 || idx >= cp_.size()) return {};
    const CpEntry& e = cp_[idx];
    if (e.tag != CP_Utf8) return {};
    if (e.body.size() < 2) return {};
    u2 len = (u2(e.body[0]) << 8) | e.body[1];
    return std::string(reinterpret_cast<const char*>(e.body.data() + 2), len);
}

u2 ClassFileEditor::findUtf8(std::string_view s) const {
    for (u2 i = 1; i < cp_.size(); ++i) {
        if (cp_[i].tag != CP_Utf8) continue;
        if (cp_[i].body.size() < 2) continue;
        u2 len = (u2(cp_[i].body[0]) << 8) | cp_[i].body[1];
        if (len != s.size()) continue;
        if (std::memcmp(cp_[i].body.data() + 2, s.data(), len) == 0) {
            return i;
        }
    }
    return 0;
}

u2 ClassFileEditor::addRawEntry(u1 tag, std::vector<u1> body, bool wide) {
    CpEntry e;
    e.tag = tag;
    e.body = std::move(body);
    e.wide = wide;
    u2 idx = static_cast<u2>(cp_.size());
    cp_.push_back(std::move(e));
    if (wide) cp_.push_back(CpEntry{});
    return idx;
}

u2 ClassFileEditor::addUtf8(std::string_view s) {
    if (u2 e = findUtf8(s)) return e;
    std::vector<u1> body;
    body.reserve(2 + s.size());
    wr16(body, static_cast<u2>(s.size()));
    body.insert(body.end(), s.begin(), s.end());
    return addRawEntry(CP_Utf8, std::move(body));
}

u2 ClassFileEditor::addClass(std::string_view internalName) {
    u2 nameIdx = addUtf8(internalName);

    for (u2 i = 1; i < cp_.size(); ++i) {
        if (cp_[i].tag == CP_Class &&
            cp_[i].body.size() == 2 &&
            ((u2(cp_[i].body[0]) << 8) | cp_[i].body[1]) == nameIdx) {
            return i;
        }
    }
    std::vector<u1> body;
    wr16(body, nameIdx);
    return addRawEntry(CP_Class, std::move(body));
}

u2 ClassFileEditor::addNameAndType(std::string_view name, std::string_view desc) {
    u2 nameIdx = addUtf8(name);
    u2 descIdx = addUtf8(desc);
    for (u2 i = 1; i < cp_.size(); ++i) {
        if (cp_[i].tag == CP_NameAndType && cp_[i].body.size() == 4) {
            u2 n = (u2(cp_[i].body[0]) << 8) | cp_[i].body[1];
            u2 d = (u2(cp_[i].body[2]) << 8) | cp_[i].body[3];
            if (n == nameIdx && d == descIdx) return i;
        }
    }
    std::vector<u1> body;
    wr16(body, nameIdx);
    wr16(body, descIdx);
    return addRawEntry(CP_NameAndType, std::move(body));
}

u2 ClassFileEditor::addMethodRef(std::string_view klass,
                                 std::string_view name,
                                 std::string_view desc) {
    u2 clsIdx = addClass(klass);
    u2 natIdx = addNameAndType(name, desc);
    for (u2 i = 1; i < cp_.size(); ++i) {
        if (cp_[i].tag == CP_Methodref && cp_[i].body.size() == 4) {
            u2 c = (u2(cp_[i].body[0]) << 8) | cp_[i].body[1];
            u2 n = (u2(cp_[i].body[2]) << 8) | cp_[i].body[3];
            if (c == clsIdx && n == natIdx) return i;
        }
    }
    std::vector<u1> body;
    wr16(body, clsIdx);
    wr16(body, natIdx);
    return addRawEntry(CP_Methodref, std::move(body));
}

std::vector<u1> ClassFileEditor::shiftStackMap(const std::vector<u1>& body, u4 delta) {
    if (body.empty()) return body;
    const u1* p = body.data();
    const u1* end = p + body.size();
    if (end - p < 2) return body;
    u2 numEntries = (u2(p[0]) << 8) | p[1];
    if (numEntries == 0) return body;

    u1 tag = p[2];
    std::vector<u1> out;
    out.reserve(body.size() + 3);

    out.push_back(p[0]);
    out.push_back(p[1]);

    const u1* framePtr = p + 3;
    u4 oldOffset = 0;
    bool wrote = false;

    auto emitU16Delta = [&](u2 newTag, u4 offset) {
        out.push_back(u1(newTag));
        out.push_back(u1((offset >> 8) & 0xFF));
        out.push_back(u1(offset & 0xFF));
    };

    if (tag <= 63) {

        oldOffset = tag;
        u4 newOffset = oldOffset + delta;
        if (newOffset <= 63) {
            out.push_back(u1(newOffset));
        } else {

            emitU16Delta(251, newOffset);
        }
        wrote = true;
    } else if (tag >= 64 && tag <= 127) {

        oldOffset = tag - 64;
        u4 newOffset = oldOffset + delta;
        if (newOffset <= 63) {
            out.push_back(u1(64 + newOffset));

        } else {

            emitU16Delta(247, newOffset);
        }
        wrote = true;
    } else if (tag == 247) {

        oldOffset = (u4(framePtr[0]) << 8) | framePtr[1];
        u4 newOffset = oldOffset + delta;
        out.push_back(tag);
        out.push_back(u1((newOffset >> 8) & 0xFF));
        out.push_back(u1(newOffset & 0xFF));
        framePtr += 2;
        wrote = true;
    } else if (tag >= 248 && tag <= 250) {

        oldOffset = (u4(framePtr[0]) << 8) | framePtr[1];
        u4 newOffset = oldOffset + delta;
        out.push_back(tag);
        out.push_back(u1((newOffset >> 8) & 0xFF));
        out.push_back(u1(newOffset & 0xFF));
        framePtr += 2;
        wrote = true;
    } else if (tag == 251) {

        oldOffset = (u4(framePtr[0]) << 8) | framePtr[1];
        u4 newOffset = oldOffset + delta;
        out.push_back(tag);
        out.push_back(u1((newOffset >> 8) & 0xFF));
        out.push_back(u1(newOffset & 0xFF));
        framePtr += 2;
        wrote = true;
    } else if (tag >= 252 && tag <= 254) {

        oldOffset = (u4(framePtr[0]) << 8) | framePtr[1];
        u4 newOffset = oldOffset + delta;
        out.push_back(tag);
        out.push_back(u1((newOffset >> 8) & 0xFF));
        out.push_back(u1(newOffset & 0xFF));
        framePtr += 2;
        wrote = true;
    } else if (tag == 255) {

        oldOffset = (u4(framePtr[0]) << 8) | framePtr[1];
        u4 newOffset = oldOffset + delta;
        out.push_back(tag);
        out.push_back(u1((newOffset >> 8) & 0xFF));
        out.push_back(u1(newOffset & 0xFF));
        framePtr += 2;
        wrote = true;
    }

    if (!wrote) {

        return body;
    }

    out.insert(out.end(), framePtr, end);
    return out;
}

std::vector<u1> ClassFileEditor::shiftLineNumberTable(const std::vector<u1>& body, u4 delta) {
    if (body.size() < 2) return body;
    std::vector<u1> out = body;
    u2 count = (u2(body[0]) << 8) | body[1];
    if (2 + size_t(count) * 4 > body.size()) return body;
    for (u2 i = 0; i < count; ++i) {
        size_t off = 2 + i * 4;
        u4 startPc = (u4(out[off]) << 8) | out[off + 1];
        startPc += delta;
        out[off]     = u1((startPc >> 8) & 0xFF);
        out[off + 1] = u1(startPc & 0xFF);
    }
    return out;
}

std::vector<u1> ClassFileEditor::shiftLocalVarTable(const std::vector<u1>& body, u4 delta) {

    if (body.size() < 2) return body;
    std::vector<u1> out = body;
    u2 count = (u2(body[0]) << 8) | body[1];
    const size_t entrySize = 10;
    if (2 + size_t(count) * entrySize > body.size()) return body;
    for (u2 i = 0; i < count; ++i) {
        size_t off = 2 + i * entrySize;
        u2 startPc = (u2(out[off]) << 8) | out[off + 1];
        u2 length  = (u2(out[off + 2]) << 8) | out[off + 3];

        if (startPc == 0) {
            length = static_cast<u2>(length + delta);
            out[off + 2] = u1((length >> 8) & 0xFF);
            out[off + 3] = u1(length & 0xFF);
        } else {
            startPc = static_cast<u2>(startPc + delta);
            out[off]     = u1((startPc >> 8) & 0xFF);
            out[off + 1] = u1(startPc & 0xFF);
        }
    }
    return out;
}

bool ClassFileEditor::prependToMethodCode(std::string_view name,
                                          std::string_view desc,
                                          const std::vector<u1>& insertion,
                                          u2 extraStack) {
    u2 nameIdx = findUtf8(name);
    u2 descIdx = findUtf8(desc);
    if (!nameIdx || !descIdx) return false;

    for (auto& m : methods_) {
        if (m.name_index != nameIdx || m.descriptor_index != descIdx) continue;

        u2 codeUtf8 = findUtf8("Code");
        if (!codeUtf8) codeUtf8 = addUtf8("Code");

        m.attributes_raw_ = rebuildCodeAttribute(
            m.attributes_raw_, codeUtf8, insertion, extraStack);
        return true;
    }
    return false;
}

std::vector<u1> ClassFileEditor::rebuildCodeAttribute(
        const std::vector<u1>& originalAttrs,
        u2 codeAttrNameIdx,
        const std::vector<u1>& insertion,
        u2 extraStack) const {

    const u1* p   = originalAttrs.data();
    const u1* end = p + originalAttrs.size();
    if (end - p < 2) return originalAttrs;
    u2 attrCount = rd16(p);

    std::vector<u1> out;
    wr16(out, attrCount);

    for (u2 i = 0; i < attrCount; ++i) {
        u2 nameIdx = rd16(p);
        u4 attrLen = rd32(p);
        const u1* attrEnd = p + attrLen;

        if (nameIdx != codeAttrNameIdx) {

            wr16(out, nameIdx);
            wr32(out, attrLen);
            out.insert(out.end(), p, attrEnd);
            p = attrEnd;
            continue;
        }

        u2 maxStack  = rd16(p);
        u2 maxLocals = rd16(p);
        u4 codeLen   = rd32(p);
        const u1* codeBytes = p;
        p += codeLen;
        u2 excCount = rd16(p);
        const u1* excStart = p;
        p += size_t(excCount) * 8;
        u2 innerAttrCount = rd16(p);
        const u1* innerAttrsStart = p;

        p = attrEnd;

        u4 delta = static_cast<u4>(insertion.size());
        u4 newCodeLen = codeLen + delta;

        u2 newMaxStack = (extraStack > maxStack) ? extraStack : maxStack;

        std::vector<u1> innerAttrsOut;
        wr16(innerAttrsOut, innerAttrCount);
        const u1* ip = innerAttrsStart;
        for (u2 j = 0; j < innerAttrCount; ++j) {
            u2 innerNameIdx = rd16(ip);
            u4 innerLen     = rd32(ip);
            const u1* innerEnd = ip + innerLen;
            std::vector<u1> innerBody(ip, innerEnd);
            ip = innerEnd;

            std::string innerName = cpUtf8Value(innerNameIdx);
            std::vector<u1> patched;
            if (innerName == "StackMapTable") {
                patched = shiftStackMap(innerBody, delta);
            } else if (innerName == "LineNumberTable") {
                patched = shiftLineNumberTable(innerBody, delta);
            } else if (innerName == "LocalVariableTable" ||
                       innerName == "LocalVariableTypeTable") {
                patched = shiftLocalVarTable(innerBody, delta);
            } else {
                patched = std::move(innerBody);
            }
            wr16(innerAttrsOut, innerNameIdx);
            wr32(innerAttrsOut, static_cast<u4>(patched.size()));
            innerAttrsOut.insert(innerAttrsOut.end(), patched.begin(), patched.end());
        }

        std::vector<u1> excOut;
        wr16(excOut, excCount);
        const u1* ep = excStart;
        for (u2 j = 0; j < excCount; ++j) {
            u2 startPc  = rd16(ep);
            u2 endPc    = rd16(ep);
            u2 handler  = rd16(ep);
            u2 catchTyp = rd16(ep);
            wr16(excOut, static_cast<u2>(startPc + delta));
            wr16(excOut, static_cast<u2>(endPc + delta));
            wr16(excOut, static_cast<u2>(handler + delta));
            wr16(excOut, catchTyp);
        }

        std::vector<u1> codeBody;
        wr16(codeBody, newMaxStack);
        wr16(codeBody, maxLocals);
        wr32(codeBody, newCodeLen);
        codeBody.insert(codeBody.end(), insertion.begin(), insertion.end());
        codeBody.insert(codeBody.end(), codeBytes, codeBytes + codeLen);
        codeBody.insert(codeBody.end(), excOut.begin(), excOut.end());
        codeBody.insert(codeBody.end(), innerAttrsOut.begin(), innerAttrsOut.end());

        wr16(out, nameIdx);
        wr32(out, static_cast<u4>(codeBody.size()));
        out.insert(out.end(), codeBody.begin(), codeBody.end());
    }

    return out;
}

std::vector<u1> ClassFileEditor::serialize() const {
    std::vector<u1> out;
    out.reserve(4096);

    wr32(out, magic_);
    wr16(out, minor_version_);
    wr16(out, major_version_);

    u2 count = static_cast<u2>(cp_.size());
    wr16(out, count);
    for (u2 i = 1; i < cp_.size(); ++i) {
        const CpEntry& e = cp_[i];
        if (e.tag == 0) continue;
        out.push_back(e.tag);
        out.insert(out.end(), e.body.begin(), e.body.end());
    }

    wr16(out, access_flags_);
    wr16(out, this_class_);
    wr16(out, super_class_);
    wr16(out, static_cast<u2>(interfaces_.size()));
    for (u2 x : interfaces_) wr16(out, x);

    out.insert(out.end(), fields_raw_.begin(), fields_raw_.end());

    wr16(out, static_cast<u2>(methods_.size()));
    for (const auto& m : methods_) {
        wr16(out, m.access_flags);
        wr16(out, m.name_index);
        wr16(out, m.descriptor_index);
        out.insert(out.end(), m.attributes_raw_.begin(), m.attributes_raw_.end());
    }

    out.insert(out.end(), class_attrs_raw_.begin(), class_attrs_raw_.end());
    return out;
}
