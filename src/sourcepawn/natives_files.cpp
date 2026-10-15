/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn natives for files (source2mod/files.inc): Source2ModFiles,
 * Source2ModFile, Source2ModKeyValues (a tree with a cursor) and
 * Source2ModKvParser (the streaming reader), on ISource2ModFiles.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/files.h"

#include <sp_callable.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace SourcePawn;

namespace sourcepawn
{
    namespace
    {
        PluginId CallerId(IPluginContext* ctx)
        {
            ScriptPlugin* plugin = ScriptPlugin::FromContext(ctx);
            return plugin ? plugin->Id() : 0;
        }

        files::FilesManager& Files()
        {
            return files::filesManager;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        /// Cells an ArrayList block needs for strings up to `longest` bytes.
        cell_t BlockFor(size_t longest)
        {
            return static_cast<cell_t>((longest + 1 + sizeof(cell_t) - 1) / sizeof(cell_t));
        }

        // ---- Source2ModFiles ------------------------------------------------------------

        cell_t N_ResolvePath(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], Files().ResolvePath(LocalString(ctx, params[1])).c_str());
        }

        cell_t N_FileExists(IPluginContext* ctx, const cell_t* params)
        {
            return Files().FileExists(LocalString(ctx, params[1]));
        }

        cell_t N_DirExists(IPluginContext* ctx, const cell_t* params)
        {
            return Files().DirExists(LocalString(ctx, params[1]));
        }

        cell_t N_FileSize(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(Files().FileSize(LocalString(ctx, params[1])));
        }

        cell_t N_FileTime(IPluginContext* ctx, const cell_t* params)
        {
            return static_cast<cell_t>(Files().FileTime(LocalString(ctx, params[1])));
        }

        cell_t N_ReadFile(IPluginContext* ctx, const cell_t* params)
        {
            std::string contents;
            if (!Files().ReadFile(LocalString(ctx, params[1]), contents))
                return -1;
            return ToBuffer(ctx, params[2], params[3], contents.c_str());
        }

        cell_t N_WriteFile(IPluginContext* ctx, const cell_t* params)
        {
            const char* text = LocalString(ctx, params[2]);
            return Files().WriteFile(LocalString(ctx, params[1]), text, std::strlen(text), params[0] >= 3 && params[3]);
        }

        cell_t N_DeleteFile(IPluginContext* ctx, const cell_t* params)
        {
            return Files().DeleteFile(LocalString(ctx, params[1]));
        }

        cell_t N_RenameFile(IPluginContext* ctx, const cell_t* params)
        {
            return Files().RenameFile(LocalString(ctx, params[1]), LocalString(ctx, params[2]));
        }

        cell_t N_CreateDirectory(IPluginContext* ctx, const cell_t* params)
        {
            return Files().CreateDirectory(LocalString(ctx, params[1]));
        }

        cell_t N_RemoveDirectory(IPluginContext* ctx, const cell_t* params)
        {
            return Files().RemoveDirectory(LocalString(ctx, params[1]));
        }

        cell_t N_ListDirectory(IPluginContext* ctx, const cell_t* params)
        {
            std::vector<std::string> entries;
            size_t longest = 0;
            const bool ok = Files().ListDirectory(LocalString(ctx, params[1]), [&](const char* name, bool directory)
            {
                entries.emplace_back(std::string(name) + (directory ? "/" : ""));
                longest = std::max(longest, entries.back().size());
                return true;
            });
            if (!ok)
                return 0;
            return FillStringList(ctx, 0, entries, false, BlockFor(longest), CallerId(ctx));
        }

        // ---- Source2ModFile -------------------------------------------------------------

        struct FileObj : HandleObject
        {
            FILE* file = nullptr;
            ~FileObj() override
            {
                if (file)
                    std::fclose(file);
            }
        };

        HandleType FileType()
        {
            static const HandleType t = Handles().CreateType("Source2ModFile", [](void* p) { delete static_cast<FileObj*>(p); });
            return t;
        }

        FileObj* FileOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<FileObj*>(ReadHandle(ctx, handle, FileType()));
        }

        // Open(path, mode)
        cell_t N_FileOpen(IPluginContext* ctx, const cell_t* params)
        {
            const std::string path = Files().ResolvePath(LocalString(ctx, params[1]));
            const char* mode = LocalString(ctx, params[2]);
            if (std::strchr(mode, 'w') || std::strchr(mode, 'a'))
            {
                std::error_code ec;
                std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
            }
            FILE* file = std::fopen(path.c_str(), mode);
            if (!file)
                return 0;
            auto* obj = new FileObj();
            obj->file = file;
            const cell_t h = Handles().Create(FileType(), obj, CallerId(ctx));
            if (!h)
                delete obj;
            return h;
        }

        // ReadLine(buffer, maxlen)
        cell_t N_FileReadLine(IPluginContext* ctx, const cell_t* params)
        {
            FileObj* f = FileOf(ctx, params[1]);
            if (!f)
                return 0;
            std::string line;
            char chunk[512];
            bool any = false;
            while (std::fgets(chunk, sizeof(chunk), f->file))
            {
                any = true;
                line += chunk;
                if (!line.empty() && line.back() == '\n')
                    break;
            }
            if (!any)
                return 0;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            ToBuffer(ctx, params[2], params[3], line.c_str());
            return 1;
        }

        cell_t N_FileWriteString(IPluginContext* ctx, const cell_t* params)
        {
            FileObj* f = FileOf(ctx, params[1]);
            if (!f)
                return 0;
            const char* text = LocalString(ctx, params[2]);
            const size_t len = std::strlen(text);
            return std::fwrite(text, 1, len, f->file) == len;
        }

        // WriteLine(format, ...)
        cell_t N_FileWriteLine(IPluginContext* ctx, const cell_t* params)
        {
            FileObj* f = FileOf(ctx, params[1]);
            if (!f)
                return 0;
            std::vector<char> buffer(8192);
            size_t written = 0;
            if (!FormatParams(ctx, params, 2, buffer.data(), buffer.size(), &written))
                return 0;
            return std::fwrite(buffer.data(), 1, written, f->file) == written && std::fputc('\n', f->file) != EOF;
        }

        cell_t N_FileEndOfFile(IPluginContext* ctx, const cell_t* params)
        {
            FileObj* f = FileOf(ctx, params[1]);
            if (!f)
                return 1;
            const int c = std::fgetc(f->file);
            if (c == EOF)
                return 1;
            std::ungetc(c, f->file);
            return 0;
        }

        cell_t N_FileSeek(IPluginContext* ctx, const cell_t* params)
        {
            FileObj* f = FileOf(ctx, params[1]);
            if (!f)
                return 0;
            const int whence = params[0] >= 3 ? params[3] : 0;
            return std::fseek(f->file, params[2], whence == 1 ? SEEK_CUR : whence == 2 ? SEEK_END : SEEK_SET) == 0;
        }

        cell_t N_FilePosition(IPluginContext* ctx, const cell_t* params)
        {
            FileObj* f = FileOf(ctx, params[1]);
            return f ? static_cast<cell_t>(std::ftell(f->file)) : -1;
        }

        cell_t N_FileFlush(IPluginContext* ctx, const cell_t* params)
        {
            FileObj* f = FileOf(ctx, params[1]);
            return f && std::fflush(f->file) == 0;
        }

        // ---- Source2ModKeyValues ---------------------------------------------------------

        /// The tree and a cursor into it: the nodes from the root down. Only
        /// this handle edits its tree, and every edit is under the cursor or
        /// of the cursor's own key, so the nodes on the stack stay valid.
        struct KvObj : HandleObject
        {
            std::unique_ptr<files::KeyValues> root;
            std::vector<files::KeyValues*> stack;

            files::KeyValues* Cur() { return stack.empty() ? root.get() : stack.back(); }
        };

        HandleType KvType()
        {
            static const HandleType t = Handles().CreateType("Source2ModKeyValues", [](void* p) { delete static_cast<KvObj*>(p); });
            return t;
        }

        KvObj* KvOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<KvObj*>(ReadHandle(ctx, handle, KvType()));
        }

        cell_t NewKv(IPluginContext* ctx, files::KeyValues* tree)
        {
            auto* obj = new KvObj();
            obj->root.reset(tree);
            const cell_t h = Handles().Create(KvType(), obj, CallerId(ctx));
            if (!h)
                delete obj;
            return h;
        }

        files::KeyValues* CurOf(IPluginContext* ctx, cell_t handle)
        {
            KvObj* kv = KvOf(ctx, handle);
            return kv ? kv->Cur() : nullptr;
        }

        cell_t N_KvNew(IPluginContext* ctx, const cell_t* params)
        {
            auto* tree = new files::KeyValues();
            tree->SetName(LocalString(ctx, params[1]));
            return NewKv(ctx, tree);
        }

        cell_t N_KvLoadFromFile(IPluginContext* ctx, const cell_t* params)
        {
            KvObj* kv = KvOf(ctx, params[1]);
            if (!kv)
                return 0;
            kv->stack.clear();
            return kv->root->LoadFromFile(LocalString(ctx, params[2]), params[0] >= 3 && params[3]);
        }

        cell_t N_KvLoadFromString(IPluginContext* ctx, const cell_t* params)
        {
            KvObj* kv = KvOf(ctx, params[1]);
            if (!kv)
                return 0;
            kv->stack.clear();
            return kv->root->LoadFromString(LocalString(ctx, params[2]), params[0] >= 3 && params[3]);
        }

        cell_t N_KvSaveToFile(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            return cur && cur->SaveToFile(LocalString(ctx, params[2]), params[0] >= 3 && params[3]);
        }

        cell_t N_KvSaveToString(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            if (!cur)
                return 0;
            const std::string text = cur->ToText(params[0] >= 4 && params[4]);
            ToBuffer(ctx, params[2], params[3], text.c_str());
            return static_cast<cell_t>(text.size());
        }

        cell_t N_KvGetSectionName(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            if (cur)
                ToBuffer(ctx, params[2], params[3], cur->GetName());
            return 0;
        }

        cell_t N_KvSetSectionName(IPluginContext* ctx, const cell_t* params)
        {
            if (files::KeyValues* cur = CurOf(ctx, params[1]))
                cur->SetName(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_KvJumpToKey(IPluginContext* ctx, const cell_t* params)
        {
            KvObj* kv = KvOf(ctx, params[1]);
            if (!kv)
                return 0;
            const char* key = LocalString(ctx, params[2]);
            const bool create = params[0] >= 3 && params[3];

            // Every key of the path goes on the stack, so GoBack walks it back.
            files::KeyValues* node = kv->Cur();
            std::vector<files::KeyValues*> path;
            std::string rest = key;
            while (!rest.empty())
            {
                const size_t slash = rest.find('/');
                const std::string part = rest.substr(0, slash);
                files::KeyValues* next = node->Child(part);
                if (!next)
                {
                    if (!create)
                        return 0;
                    next = node->AddChild(part);
                }
                path.push_back(node = next);
                rest = slash == std::string::npos ? std::string() : rest.substr(slash + 1);
            }
            kv->stack.insert(kv->stack.end(), path.begin(), path.end());
            return 1;
        }

        cell_t N_KvGotoFirstSubKey(IPluginContext* ctx, const cell_t* params)
        {
            KvObj* kv = KvOf(ctx, params[1]);
            if (!kv)
                return 0;
            const bool keysOnly = params[0] < 2 || params[2];
            auto* next = static_cast<files::KeyValues*>(keysOnly ? kv->Cur()->GetFirstTrueSubKey() : kv->Cur()->GetFirstSubKey());
            if (!next)
                return 0;
            kv->stack.push_back(next);
            return 1;
        }

        cell_t N_KvGotoNextKey(IPluginContext* ctx, const cell_t* params)
        {
            KvObj* kv = KvOf(ctx, params[1]);
            if (!kv || kv->stack.empty())
                return 0;
            const bool keysOnly = params[0] < 2 || params[2];
            auto* next = static_cast<files::KeyValues*>(keysOnly ? kv->Cur()->GetNextTrueSubKey() : kv->Cur()->GetNextKey());
            if (!next)
                return 0;
            kv->stack.back() = next;
            return 1;
        }

        cell_t N_KvGoBack(IPluginContext* ctx, const cell_t* params)
        {
            KvObj* kv = KvOf(ctx, params[1]);
            if (!kv || kv->stack.empty())
                return 0;
            kv->stack.pop_back();
            return 1;
        }

        cell_t N_KvRewind(IPluginContext* ctx, const cell_t* params)
        {
            if (KvObj* kv = KvOf(ctx, params[1]))
                kv->stack.clear();
            return 0;
        }

        cell_t N_KvNodesInStack(IPluginContext* ctx, const cell_t* params)
        {
            KvObj* kv = KvOf(ctx, params[1]);
            return kv ? static_cast<cell_t>(kv->stack.size()) : 0;
        }

        cell_t N_KvGetDataType(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            return cur ? cur->GetDataType(LocalString(ctx, params[2])) : 0;
        }

        // GetString(key, buffer, maxlen, defvalue)
        cell_t N_KvGetString(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            if (!cur)
                return 0;
            const std::string def = params[0] >= 5 ? LocalString(ctx, params[5]) : "";
            return ToBuffer(ctx, params[3], params[4], cur->GetString(LocalString(ctx, params[2]), def.c_str()));
        }

        cell_t N_KvGetNum(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            return cur ? cur->GetInt(LocalString(ctx, params[2]), params[0] >= 3 ? params[3] : 0) : 0;
        }

        cell_t N_KvGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            const float def = params[0] >= 3 ? sp_ctof(params[3]) : 0.0f;
            return sp_ftoc(cur ? cur->GetFloat(LocalString(ctx, params[2]), def) : def);
        }

        // GetUInt64(key, buffer, maxlen, defvalue)
        cell_t N_KvGetUInt64(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            if (!cur)
                return 0;
            const uint64_t def = params[0] >= 5 ? std::strtoull(LocalString(ctx, params[5]), nullptr, 10) : 0;
            const std::string text = std::to_string(cur->GetUint64(LocalString(ctx, params[2]), def));
            return ToBuffer(ctx, params[3], params[4], text.c_str());
        }

        cell_t N_KvGetColor(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            if (!cur)
                return 0;
            const uint32_t rgba = cur->GetColor(LocalString(ctx, params[2]), 0);
            cell_t* out;
            ctx->LocalToPhysAddr(params[3], &out);
            for (int i = 0; i < 4; i++)
                out[i] = static_cast<cell_t>((rgba >> (8 * i)) & 0xFF);
            return 0;
        }

        cell_t N_KvSetString(IPluginContext* ctx, const cell_t* params)
        {
            if (files::KeyValues* cur = CurOf(ctx, params[1]))
                cur->SetString(LocalString(ctx, params[2]), LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_KvSetNum(IPluginContext* ctx, const cell_t* params)
        {
            if (files::KeyValues* cur = CurOf(ctx, params[1]))
                cur->SetInt(LocalString(ctx, params[2]), params[3]);
            return 0;
        }

        cell_t N_KvSetFloat(IPluginContext* ctx, const cell_t* params)
        {
            if (files::KeyValues* cur = CurOf(ctx, params[1]))
                cur->SetFloat(LocalString(ctx, params[2]), sp_ctof(params[3]));
            return 0;
        }

        cell_t N_KvSetUInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (files::KeyValues* cur = CurOf(ctx, params[1]))
                cur->SetUint64(LocalString(ctx, params[2]), std::strtoull(LocalString(ctx, params[3]), nullptr, 10));
            return 0;
        }

        cell_t N_KvSetColor(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            if (!cur)
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[3], &in);
            uint32_t rgba = 0;
            for (int i = 0; i < 4; i++)
                rgba |= static_cast<uint32_t>(in[i] & 0xFF) << (8 * i);
            cur->SetColor(LocalString(ctx, params[2]), rgba);
            return 0;
        }

        cell_t N_KvDeleteKey(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            const char* key = LocalString(ctx, params[2]);
            // "" would be the cursor itself: that is DeleteThis.
            return cur && *key && cur->RemoveKey(key);
        }

        cell_t N_KvDeleteThis(IPluginContext* ctx, const cell_t* params)
        {
            KvObj* kv = KvOf(ctx, params[1]);
            if (!kv || kv->stack.empty())
                return 0;
            files::KeyValues* node = kv->stack.back();
            kv->stack.pop_back();
            return kv->Cur()->RemoveChild(node);
        }

        cell_t N_KvImport(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            files::KeyValues* from = cur ? CurOf(ctx, params[2]) : nullptr;
            if (from && from != cur)
            {
                // Through a copy: from may be under cur in the same tree.
                files::KeyValues copy;
                copy.CopyFrom(*from);
                cur->CopyFrom(copy);
            }
            return 0;
        }

        cell_t N_KvMerge(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            files::KeyValues* from = cur ? CurOf(ctx, params[2]) : nullptr;
            if (from && from != cur)
            {
                files::KeyValues copy;
                copy.CopyFrom(*from);
                cur->Merge(&copy);
            }
            return 0;
        }

        cell_t N_KvClone(IPluginContext* ctx, const cell_t* params)
        {
            files::KeyValues* cur = CurOf(ctx, params[1]);
            return cur ? NewKv(ctx, static_cast<files::KeyValues*>(cur->Clone())) : 0;
        }

        // ---- Source2ModKvParser ---------------------------------------------------------

        struct ParserObj : HandleObject
        {
            IPluginFunction* onStart = nullptr;
            IPluginFunction* onEnter = nullptr;
            IPluginFunction* onKeyValue = nullptr;
            IPluginFunction* onLeave = nullptr;
            IPluginFunction* onRawLine = nullptr;
            IPluginFunction* onEnd = nullptr;
        };

        HandleType ParserType()
        {
            static const HandleType t = Handles().CreateType("Source2ModKvParser", [](void* p) { delete static_cast<ParserObj*>(p); });
            return t;
        }

        ParserObj* ParserOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ParserObj*>(ReadHandle(ctx, handle, ParserType()));
        }

        /// Calls the script's callbacks; a failed call stops the read like HALT_FAIL.
        class ScriptListener final : public ISource2ModKvListener
        {
        public:
            ScriptListener(ParserObj* parser, cell_t handle) : m_parser(parser), m_handle(handle) {}

            void OnStart() override
            {
                if (!m_parser->onStart)
                    return;
                sp::CallArgs args;
                args.PushCell(m_handle);
                Invoke(m_parser->onStart, args);
            }

            ESource2ModKvParseResult OnEnterSection(const char* name, bool quoted) override
            {
                if (!m_parser->onEnter)
                    return SOURCE2MOD_KVPARSE_CONTINUE;
                sp::CallArgs args;
                args.PushCell(m_handle);
                args.PushString(name);
                args.PushCell(quoted);
                return Call(m_parser->onEnter, args);
            }

            ESource2ModKvParseResult OnKeyValue(const char* key, const char* value, bool keyQuoted, bool valueQuoted) override
            {
                if (!m_parser->onKeyValue)
                    return SOURCE2MOD_KVPARSE_CONTINUE;
                sp::CallArgs args;
                args.PushCell(m_handle);
                args.PushString(key);
                args.PushString(value);
                args.PushCell(keyQuoted);
                args.PushCell(valueQuoted);
                return Call(m_parser->onKeyValue, args);
            }

            ESource2ModKvParseResult OnLeaveSection() override
            {
                if (!m_parser->onLeave)
                    return SOURCE2MOD_KVPARSE_CONTINUE;
                sp::CallArgs args;
                args.PushCell(m_handle);
                return Call(m_parser->onLeave, args);
            }

            ESource2ModKvParseResult OnRawLine(const char* line, int lineNumber) override
            {
                if (!m_parser->onRawLine)
                    return SOURCE2MOD_KVPARSE_CONTINUE;
                sp::CallArgs args;
                args.PushCell(m_handle);
                args.PushString(line);
                args.PushCell(lineNumber);
                return Call(m_parser->onRawLine, args);
            }

            void OnEnd(bool halted, bool failed) override
            {
                if (!m_parser->onEnd)
                    return;
                sp::CallArgs args;
                args.PushCell(m_handle);
                args.PushCell(halted);
                args.PushCell(failed);
                Invoke(m_parser->onEnd, args);
            }

        private:
            static ESource2ModKvParseResult Call(IPluginFunction* fn, const sp::CallArgs& args)
            {
                cell_t result = SOURCE2MOD_KVPARSE_CONTINUE;
                if (!Invoke(fn, args, &result))
                    return SOURCE2MOD_KVPARSE_HALT_FAIL;
                return static_cast<ESource2ModKvParseResult>(result);
            }

            ParserObj* m_parser;
            cell_t m_handle;
        };

        cell_t N_ParserNew(IPluginContext* ctx, const cell_t*)
        {
            auto* obj = new ParserObj();
            const cell_t h = Handles().Create(ParserType(), obj, CallerId(ctx));
            if (!h)
                delete obj;
            return h;
        }

        IPluginFunction* FunctionOrNull(IPluginContext* ctx, cell_t id)
        {
            // An unset typedef'd function is -1 (INVALID_FUNCTION) or 0.
            return (id == -1 || id == 0) ? nullptr : ctx->GetFunctionById(static_cast<funcid_t>(id));
        }

        template <IPluginFunction* ParserObj::*Field>
        cell_t N_ParserSet(IPluginContext* ctx, const cell_t* params)
        {
            if (ParserObj* p = ParserOf(ctx, params[1]))
                p->*Field = FunctionOrNull(ctx, params[2]);
            return 0;
        }

        cell_t ParseWith(IPluginContext* ctx, const cell_t* params, bool file)
        {
            ParserObj* p = ParserOf(ctx, params[1]);
            if (!p)
                return SOURCE2MOD_KVERROR_STREAM_ERROR;
            ScriptListener listener(p, params[1]);
            int line = 0, col = 0;
            const bool escapes = params[0] < 5 || params[5];
            const char* source = LocalString(ctx, params[2]);
            // The text is copied: a callback may change the script's buffer.
            const std::string text = source;
            const ESource2ModKvError err = file ? Files().ParseKeyValuesFile(text.c_str(), &listener, &line, &col, escapes)
                                                : Files().ParseKeyValues(text.c_str(), &listener, &line, &col, escapes);
            cell_t* out;
            if (params[0] >= 3 && ctx->LocalToPhysAddr(params[3], &out) == SP_ERROR_NONE)
                *out = line;
            if (params[0] >= 4 && ctx->LocalToPhysAddr(params[4], &out) == SP_ERROR_NONE)
                *out = col;
            return err;
        }

        cell_t N_ParserParseFile(IPluginContext* ctx, const cell_t* params)
        {
            return ParseWith(ctx, params, true);
        }

        cell_t N_ParserParseString(IPluginContext* ctx, const cell_t* params)
        {
            return ParseWith(ctx, params, false);
        }

        cell_t N_ParserGetErrorString(IPluginContext* ctx, const cell_t* params)
        {
            return ToBuffer(ctx, params[2], params[3], Files().KvErrorString(static_cast<ESource2ModKvError>(params[1])));
        }

        const sp_nativeinfo_t g_fileNatives[] = {
            { "Source2ModFiles.ResolvePath", N_ResolvePath },
            { "Source2ModFiles.FileExists", N_FileExists },
            { "Source2ModFiles.DirExists", N_DirExists },
            { "Source2ModFiles.FileSize", N_FileSize },
            { "Source2ModFiles.FileTime", N_FileTime },
            { "Source2ModFiles.ReadFile", N_ReadFile },
            { "Source2ModFiles.WriteFile", N_WriteFile },
            { "Source2ModFiles.DeleteFile", N_DeleteFile },
            { "Source2ModFiles.RenameFile", N_RenameFile },
            { "Source2ModFiles.CreateDirectory", N_CreateDirectory },
            { "Source2ModFiles.RemoveDirectory", N_RemoveDirectory },
            { "Source2ModFiles.ListDirectory", N_ListDirectory },
            { "Source2ModFile.Open", N_FileOpen },
            { "Source2ModFile.ReadLine", N_FileReadLine },
            { "Source2ModFile.WriteString", N_FileWriteString },
            { "Source2ModFile.WriteLine", N_FileWriteLine },
            { "Source2ModFile.EndOfFile", N_FileEndOfFile },
            { "Source2ModFile.Seek", N_FileSeek },
            { "Source2ModFile.Position", N_FilePosition },
            { "Source2ModFile.Flush", N_FileFlush },
            { "Source2ModKeyValues.Source2ModKeyValues", N_KvNew },
            { "Source2ModKeyValues.LoadFromFile", N_KvLoadFromFile },
            { "Source2ModKeyValues.LoadFromString", N_KvLoadFromString },
            { "Source2ModKeyValues.SaveToFile", N_KvSaveToFile },
            { "Source2ModKeyValues.SaveToString", N_KvSaveToString },
            { "Source2ModKeyValues.GetSectionName", N_KvGetSectionName },
            { "Source2ModKeyValues.SetSectionName", N_KvSetSectionName },
            { "Source2ModKeyValues.JumpToKey", N_KvJumpToKey },
            { "Source2ModKeyValues.GotoFirstSubKey", N_KvGotoFirstSubKey },
            { "Source2ModKeyValues.GotoNextKey", N_KvGotoNextKey },
            { "Source2ModKeyValues.GoBack", N_KvGoBack },
            { "Source2ModKeyValues.Rewind", N_KvRewind },
            { "Source2ModKeyValues.NodesInStack", N_KvNodesInStack },
            { "Source2ModKeyValues.GetDataType", N_KvGetDataType },
            { "Source2ModKeyValues.GetString", N_KvGetString },
            { "Source2ModKeyValues.GetNum", N_KvGetNum },
            { "Source2ModKeyValues.GetFloat", N_KvGetFloat },
            { "Source2ModKeyValues.GetUInt64", N_KvGetUInt64 },
            { "Source2ModKeyValues.GetColor", N_KvGetColor },
            { "Source2ModKeyValues.SetString", N_KvSetString },
            { "Source2ModKeyValues.SetNum", N_KvSetNum },
            { "Source2ModKeyValues.SetFloat", N_KvSetFloat },
            { "Source2ModKeyValues.SetUInt64", N_KvSetUInt64 },
            { "Source2ModKeyValues.SetColor", N_KvSetColor },
            { "Source2ModKeyValues.DeleteKey", N_KvDeleteKey },
            { "Source2ModKeyValues.DeleteThis", N_KvDeleteThis },
            { "Source2ModKeyValues.Import", N_KvImport },
            { "Source2ModKeyValues.Merge", N_KvMerge },
            { "Source2ModKeyValues.Clone", N_KvClone },
            { "Source2ModKvParser.Source2ModKvParser", N_ParserNew },
            { "Source2ModKvParser.OnStart.set", N_ParserSet<&ParserObj::onStart> },
            { "Source2ModKvParser.OnEnterSection.set", N_ParserSet<&ParserObj::onEnter> },
            { "Source2ModKvParser.OnKeyValue.set", N_ParserSet<&ParserObj::onKeyValue> },
            { "Source2ModKvParser.OnLeaveSection.set", N_ParserSet<&ParserObj::onLeave> },
            { "Source2ModKvParser.OnRawLine.set", N_ParserSet<&ParserObj::onRawLine> },
            { "Source2ModKvParser.OnEnd.set", N_ParserSet<&ParserObj::onEnd> },
            { "Source2ModKvParser.ParseFile", N_ParserParseFile },
            { "Source2ModKvParser.ParseString", N_ParserParseString },
            { "Source2ModKvParser.GetErrorString", N_ParserGetErrorString },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetFileNatives()
    {
        return g_fileNatives;
    }
}
