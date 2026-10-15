/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * Files and KeyValues (see files.h).
 *
 * Project: Source2Mod
 */
#include "files.h"

#include "paths.h"
#include "smc.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

namespace fs = std::filesystem;

namespace files
{
    FilesManager filesManager;

    namespace
    {
        fs::path Resolve(const char* path)
        {
            fs::path p(path ? path : "");
            if (p.is_absolute())
                return p;
            return fs::path(paths::pathsManager.GameDirectory()) / p;
        }

        std::string Quote(const std::string& s, bool escapes)
        {
            if (!escapes)
                return "\"" + s + "\"";
            std::string out = "\"";
            for (char c : s)
            {
                switch (c)
                {
                case '\n': out += "\\n"; break;
                case '\t': out += "\\t"; break;
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                default: out += c; break;
                }
            }
            return out + "\"";
        }

        /// SMC events into a tree: the first section becomes `target`.
        class TreeReader : public smc::Listener
        {
        public:
            explicit TreeReader(KeyValues* target) : m_target(target) {}

            smc::Result EnterSection(const char* name, bool) override
            {
                if (m_stack.empty())
                {
                    if (m_done)
                        return smc::Result::Continue;
                    m_target->SetName(name);
                    m_stack.push_back(m_target);
                    return smc::Result::Continue;
                }
                m_stack.push_back(m_stack.back()->AddChild(name));
                return smc::Result::Continue;
            }

            smc::Result KeyValue(const char* key, const char* value, bool, bool) override
            {
                if (!m_stack.empty())
                    m_stack.back()->AddChild(key)->SetString(nullptr, value);
                return smc::Result::Continue;
            }

            smc::Result LeaveSection() override
            {
                if (!m_stack.empty())
                {
                    m_stack.pop_back();
                    if (m_stack.empty())
                        m_done = true;
                }
                return smc::Result::Continue;
            }

            bool Found() const { return m_done; }

        private:
            KeyValues* m_target;
            std::vector<KeyValues*> m_stack;
            bool m_done = false;
        };

        /// ISource2ModKvListener behind the SMC reader.
        class ListenerBridge : public smc::Listener
        {
        public:
            explicit ListenerBridge(ISource2ModKvListener* l) : m_l(l) {}
            void Start() override { m_l->OnStart(); }
            smc::Result EnterSection(const char* name, bool quoted) override { return R(m_l->OnEnterSection(name, quoted)); }
            smc::Result KeyValue(const char* k, const char* v, bool kq, bool vq) override { return R(m_l->OnKeyValue(k, v, kq, vq)); }
            smc::Result LeaveSection() override { return R(m_l->OnLeaveSection()); }
            smc::Result RawLine(const char* line, int n) override { return R(m_l->OnRawLine(line, n)); }
            void End(bool halted, bool failed) override { m_l->OnEnd(halted, failed); }

        private:
            static smc::Result R(ESource2ModKvParseResult r) { return static_cast<smc::Result>(r); }
            ISource2ModKvListener* m_l;
        };
    }

    // ---- KeyValues ------------------------------------------------------------------

    KeyValues* KeyValues::Child(const std::string& name) const
    {
        for (const auto& c : m_children)
            if (strcasecmp(c->m_name.c_str(), name.c_str()) == 0)
                return c.get();
        return nullptr;
    }

    KeyValues* KeyValues::AddChild(const std::string& name)
    {
        // A value that gets a subkey becomes a section.
        m_type = SOURCE2MOD_KV_NONE;
        m_children.push_back(std::make_unique<KeyValues>());
        KeyValues* c = m_children.back().get();
        c->m_name = name;
        c->m_parent = this;
        return c;
    }

    bool KeyValues::RemoveChild(KeyValues* child)
    {
        for (auto it = m_children.begin(); it != m_children.end(); ++it)
        {
            if (it->get() == child)
            {
                m_children.erase(it);
                return true;
            }
        }
        return false;
    }

    ISource2ModKeyValues* KeyValues::FindKey(const char* key, bool create)
    {
        if (!key || !*key)
            return this;
        KeyValues* node = this;
        const char* p = key;
        while (true)
        {
            const char* slash = std::strchr(p, '/');
            const std::string part = slash ? std::string(p, slash) : std::string(p);
            KeyValues* next = node->Child(part);
            if (!next)
            {
                if (!create)
                    return nullptr;
                next = node->AddChild(part);
            }
            node = next;
            if (!slash)
                return node;
            p = slash + 1;
        }
    }

    KeyValues* KeyValues::First(bool sections, bool values) const
    {
        for (const auto& c : m_children)
            if ((sections && c->IsSection()) || (values && !c->IsSection()))
                return c.get();
        return nullptr;
    }

    KeyValues* KeyValues::Next(bool sections, bool values) const
    {
        if (!m_parent)
            return nullptr;
        bool passed = false;
        for (const auto& c : m_parent->m_children)
        {
            if (!passed)
            {
                passed = c.get() == this;
                continue;
            }
            if ((sections && c->IsSection()) || (values && !c->IsSection()))
                return c.get();
        }
        return nullptr;
    }

    ISource2ModKeyValues* KeyValues::GetFirstSubKey() { return First(true, true); }
    ISource2ModKeyValues* KeyValues::GetNextKey() { return Next(true, true); }
    ISource2ModKeyValues* KeyValues::GetFirstTrueSubKey() { return First(true, false); }
    ISource2ModKeyValues* KeyValues::GetNextTrueSubKey() { return Next(true, false); }
    ISource2ModKeyValues* KeyValues::GetFirstValue() { return First(false, true); }
    ISource2ModKeyValues* KeyValues::GetNextValue() { return Next(false, true); }

    ESource2ModKvType KeyValues::GetDataType(const char* key)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key));
        return n ? n->m_type : SOURCE2MOD_KV_NONE;
    }

    const char* KeyValues::GetString(const char* key, const char* def)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key));
        if (!n || n->IsSection())
            return def;
        char buf[64];
        switch (n->m_type)
        {
        case SOURCE2MOD_KV_STRING: return n->m_string.c_str();
        case SOURCE2MOD_KV_INT: snprintf(buf, sizeof(buf), "%d", static_cast<int>(n->m_int)); break;
        case SOURCE2MOD_KV_FLOAT: snprintf(buf, sizeof(buf), "%f", n->m_float); break;
        case SOURCE2MOD_KV_UINT64: snprintf(buf, sizeof(buf), "%" PRIu64, static_cast<uint64_t>(n->m_int)); break;
        case SOURCE2MOD_KV_COLOR:
            snprintf(buf, sizeof(buf), "%d %d %d %d", static_cast<int>(n->m_int & 0xFF), static_cast<int>((n->m_int >> 8) & 0xFF),
                     static_cast<int>((n->m_int >> 16) & 0xFF), static_cast<int>((n->m_int >> 24) & 0xFF));
            break;
        default: return def;
        }
        n->m_text = buf;
        return n->m_text.c_str();
    }

    int KeyValues::GetInt(const char* key, int def)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key));
        if (!n || n->IsSection())
            return def;
        switch (n->m_type)
        {
        case SOURCE2MOD_KV_STRING: return std::atoi(n->m_string.c_str());
        case SOURCE2MOD_KV_FLOAT: return static_cast<int>(n->m_float);
        default: return static_cast<int>(n->m_int);
        }
    }

    float KeyValues::GetFloat(const char* key, float def)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key));
        if (!n || n->IsSection())
            return def;
        switch (n->m_type)
        {
        case SOURCE2MOD_KV_STRING: return static_cast<float>(std::atof(n->m_string.c_str()));
        case SOURCE2MOD_KV_FLOAT: return n->m_float;
        default: return static_cast<float>(n->m_int);
        }
    }

    uint64_t KeyValues::GetUint64(const char* key, uint64_t def)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key));
        if (!n || n->IsSection())
            return def;
        switch (n->m_type)
        {
        case SOURCE2MOD_KV_STRING: return std::strtoull(n->m_string.c_str(), nullptr, 10);
        case SOURCE2MOD_KV_FLOAT: return static_cast<uint64_t>(n->m_float);
        default: return static_cast<uint64_t>(n->m_int);
        }
    }

    uint32_t KeyValues::GetColor(const char* key, uint32_t def)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key));
        if (!n || n->IsSection())
            return def;
        if (n->m_type == SOURCE2MOD_KV_STRING)
        {
            int r = 0, g = 0, b = 0, a = 0;
            if (std::sscanf(n->m_string.c_str(), "%d %d %d %d", &r, &g, &b, &a) < 3)
                return def;
            return (r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16) | (static_cast<uint32_t>(a & 0xFF) << 24);
        }
        return static_cast<uint32_t>(n->m_int);
    }

    void KeyValues::SetValue(ESource2ModKvType type)
    {
        m_children.clear();
        m_type = type;
    }

    void KeyValues::SetString(const char* key, const char* value)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key, true));
        n->SetValue(SOURCE2MOD_KV_STRING);
        n->m_string = value ? value : "";
    }

    void KeyValues::SetInt(const char* key, int value)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key, true));
        n->SetValue(SOURCE2MOD_KV_INT);
        n->m_int = value;
    }

    void KeyValues::SetFloat(const char* key, float value)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key, true));
        n->SetValue(SOURCE2MOD_KV_FLOAT);
        n->m_float = value;
    }

    void KeyValues::SetUint64(const char* key, uint64_t value)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key, true));
        n->SetValue(SOURCE2MOD_KV_UINT64);
        n->m_int = static_cast<int64_t>(value);
    }

    void KeyValues::SetColor(const char* key, uint32_t rgba)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key, true));
        n->SetValue(SOURCE2MOD_KV_COLOR);
        n->m_int = rgba;
    }

    bool KeyValues::RemoveKey(const char* key)
    {
        auto* n = static_cast<KeyValues*>(FindKey(key));
        return n && n != this && n->m_parent && n->m_parent->RemoveChild(n);
    }

    void KeyValues::CopyFrom(const KeyValues& other)
    {
        m_type = other.m_type;
        m_string = other.m_string;
        m_int = other.m_int;
        m_float = other.m_float;
        m_children.clear();
        for (const auto& c : other.m_children)
        {
            auto copy = std::make_unique<KeyValues>();
            copy->m_name = c->m_name;
            copy->m_parent = this;
            copy->CopyFrom(*c);
            m_children.push_back(std::move(copy));
        }
    }

    void KeyValues::CopySubkeys(ISource2ModKeyValues* from)
    {
        auto* src = static_cast<KeyValues*>(from);
        if (!src || src == this)
            return;
        for (const auto& c : src->m_children)
            AddChild(c->m_name)->CopyFrom(*c);
    }

    void KeyValues::Merge(ISource2ModKeyValues* from)
    {
        auto* src = static_cast<KeyValues*>(from);
        if (!src || src == this)
            return;
        for (const auto& c : src->m_children)
        {
            KeyValues* slot = Child(c->m_name);
            if (!slot)
                slot = AddChild(c->m_name);
            slot->CopyFrom(*c);
        }
    }

    ISource2ModKeyValues* KeyValues::Clone()
    {
        auto* copy = new KeyValues();
        copy->m_name = m_name;
        copy->CopyFrom(*this);
        return copy;
    }

    bool KeyValues::LoadFromFile(const char* path, bool escapes)
    {
        KeyValues loaded;
        TreeReader reader(&loaded);
        if (smc::ParseFile(Resolve(path).string().c_str(), reader, nullptr, escapes) != smc::Error::Okay || !reader.Found())
            return false;
        m_name = loaded.m_name;
        CopyFrom(loaded);
        return true;
    }

    bool KeyValues::LoadFromString(const char* text, bool escapes)
    {
        KeyValues loaded;
        TreeReader reader(&loaded);
        if (smc::ParseString(text ? text : "", reader, nullptr, escapes) != smc::Error::Okay || !reader.Found())
            return false;
        m_name = loaded.m_name;
        CopyFrom(loaded);
        return true;
    }

    void KeyValues::Write(std::string& out, int depth, bool escapes) const
    {
        const std::string tabs(depth, '\t');
        if (IsSection())
        {
            out += tabs + Quote(m_name, escapes) + "\n" + tabs + "{\n";
            for (const auto& c : m_children)
                c->Write(out, depth + 1, escapes);
            out += tabs + "}\n";
        }
        else
            out += tabs + Quote(m_name, escapes) + "\t\t" + Quote(const_cast<KeyValues*>(this)->GetString(), escapes) + "\n";
    }

    std::string KeyValues::ToText(bool escapes) const
    {
        std::string out;
        Write(out, 0, escapes);
        return out;
    }

    bool KeyValues::SaveToFile(const char* path, bool escapes)
    {
        const std::string text = ToText(escapes);
        return filesManager.WriteFile(path, text.data(), text.size());
    }

    size_t KeyValues::SaveToString(char* buffer, size_t maxlen, bool escapes)
    {
        const std::string text = ToText(escapes);
        if (buffer && maxlen)
        {
            const size_t n = std::min(text.size(), maxlen - 1);
            std::memcpy(buffer, text.data(), n);
            buffer[n] = '\0';
        }
        return text.size();
    }

    void KeyValues::Release()
    {
        if (!m_parent)
            delete this;
    }

    // ---- files ----------------------------------------------------------------------

    std::string FilesManager::ResolvePath(const char* path)
    {
        return Resolve(path).string();
    }

    bool FilesManager::FileExists(const char* path)
    {
        std::error_code ec;
        return fs::is_regular_file(Resolve(path), ec);
    }

    bool FilesManager::DirExists(const char* path)
    {
        std::error_code ec;
        return fs::is_directory(Resolve(path), ec);
    }

    int64_t FilesManager::FileSize(const char* path)
    {
        std::error_code ec;
        const auto size = fs::file_size(Resolve(path), ec);
        return ec ? -1 : static_cast<int64_t>(size);
    }

    int64_t FilesManager::FileTime(const char* path)
    {
        std::error_code ec;
        const auto t = fs::last_write_time(Resolve(path), ec);
        if (ec)
            return -1;
        // file_clock -> system_clock (C++20 clock_cast is not everywhere yet).
        const auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
    }

    bool FilesManager::ReadFile(const char* path, std::string& contents)
    {
        std::ifstream in(Resolve(path), std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        contents = ss.str();
        return true;
    }

    bool FilesManager::WriteFile(const char* path, const void* data, size_t size, bool append)
    {
        const fs::path p = Resolve(path);
        std::error_code ec;
        if (p.has_parent_path())
            fs::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
        if (!out)
            return false;
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        return out.good();
    }

    bool FilesManager::DeleteFile(const char* path)
    {
        std::error_code ec;
        const fs::path p = Resolve(path);
        return fs::is_regular_file(p, ec) && fs::remove(p, ec);
    }

    bool FilesManager::RenameFile(const char* from, const char* to)
    {
        std::error_code ec;
        fs::rename(Resolve(from), Resolve(to), ec);
        return !ec;
    }

    bool FilesManager::CreateDirectory(const char* path)
    {
        std::error_code ec;
        const fs::path p = Resolve(path);
        fs::create_directories(p, ec);
        return fs::is_directory(p, ec);
    }

    bool FilesManager::RemoveDirectory(const char* path)
    {
        std::error_code ec;
        const fs::path p = Resolve(path);
        return fs::is_directory(p, ec) && fs::remove(p, ec);
    }

    bool FilesManager::ListDirectory(const char* path, Source2ModDirCallback callback)
    {
        std::error_code ec;
        fs::directory_iterator it(Resolve(path), ec);
        if (ec)
            return false;
        for (const auto& entry : it)
        {
            std::error_code e2;
            if (callback && !callback(entry.path().filename().string().c_str(), entry.is_directory(e2)))
                break;
        }
        return true;
    }

    ISource2ModKeyValues* FilesManager::CreateKeyValues(const char* name)
    {
        auto* kv = new KeyValues();
        kv->SetName(name);
        return kv;
    }

    ESource2ModKvError FilesManager::ParseKeyValues(const char* text, ISource2ModKvListener* listener, int* line, int* col, bool escapes)
    {
        if (!listener)
            return SOURCE2MOD_KVERROR_CUSTOM;
        ListenerBridge bridge(listener);
        smc::States states;
        const smc::Error err = smc::ParseString(text ? text : "", bridge, &states, escapes);
        if (line)
            *line = states.line;
        if (col)
            *col = states.col;
        return static_cast<ESource2ModKvError>(err);
    }

    ESource2ModKvError FilesManager::ParseKeyValuesFile(const char* path, ISource2ModKvListener* listener, int* line, int* col, bool escapes)
    {
        if (!listener)
            return SOURCE2MOD_KVERROR_CUSTOM;
        ListenerBridge bridge(listener);
        smc::States states;
        const smc::Error err = smc::ParseFile(Resolve(path).string().c_str(), bridge, &states, escapes);
        if (line)
            *line = states.line;
        if (col)
            *col = states.col;
        return static_cast<ESource2ModKvError>(err);
    }

    const char* FilesManager::KvErrorString(ESource2ModKvError error)
    {
        const char* text = smc::ErrorString(static_cast<smc::Error>(error));
        return text ? text : "Unknown error";
    }
}
