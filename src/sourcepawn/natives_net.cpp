/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * =============================================================================
 *
 * SourcePawn natives for JSON, HTTP and MySQL (source2mod/json.inc,
 * http.inc, mysql.inc).
 *
 * JSON values are lent handles kept by the document (or HTTP response) they
 * came from: the same value gives the same handle, and they go with it.
 *
 * HTTP requests are made in the calling script's name, so the core drops
 * them when it unloads. MySQL connections are made in the core's name and
 * owned by their handle instead: deleting it takes the connection down on
 * the next frame (a callback may be what deletes it), and a callback that
 * comes in after that is dropped.
 *
 * Project: Source2Mod
 */
#include "natives.h"
#include "handles.h"
#include "scriptplugin.h"

#include "core/http.h"
#include "core/json.h"
#include "core/mysql.h"
#include "core/sqlite.h"
#include "core/scheduler.h"

#include <sp_callable.h>

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
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

        IPluginFunction* Function(IPluginContext* ctx, cell_t id)
        {
            IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(id));
            if (!fn)
                ctx->ThrowNativeError("Invalid function id (%X)", id);
            return fn;
        }

        cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
        {
            size_t written = 0;
            ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
            return static_cast<cell_t>(written);
        }

        void WriteInt64(IPluginContext* ctx, cell_t addr, int64_t value)
        {
            cell_t* out;
            ctx->LocalToPhysAddr(addr, &out);
            out[0] = static_cast<cell_t>(static_cast<uint32_t>(value));
            out[1] = static_cast<cell_t>(static_cast<uint32_t>(static_cast<uint64_t>(value) >> 32));
        }

        // An optional int[2] argument (scripts built before it existed pass none): 0 when absent.
        int64_t ReadInt64Arg(IPluginContext* ctx, const cell_t* params, int index)
        {
            if (params[0] < index)
                return 0;
            cell_t* in;
            ctx->LocalToPhysAddr(params[index], &in);
            return static_cast<int64_t>(static_cast<uint64_t>(static_cast<uint32_t>(in[0])) | (static_cast<uint64_t>(static_cast<uint32_t>(in[1])) << 32));
        }

        HandleType FrameType(const char* name)
        {
            return Handles().CreateType(name, nullptr, 0, false);
        }

        // ---- JSON ---------------------------------------------------------------------

        struct JsonValues;

        struct JsonVal : HandleObject
        {
            const ISource2ModJSONValue* value = nullptr;
            JsonValues* root = nullptr;
        };

        HandleType ValueType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModJSONValue", [](void* p) { delete static_cast<JsonVal*>(p); }, 0, false);
            return t;
        }

        /// The value handles taken from one document or response, by the JSON
        /// node they wrap (the core makes a new wrapper on every Get/At).
        struct JsonValues
        {
            PluginId owner = 0;
            std::unordered_map<const void*, cell_t> handles;

            cell_t For(const ISource2ModJSONValue* value)
            {
                if (!value)
                    return 0;
                const void* key = static_cast<const json::CJSONValue*>(value)->Node();
                auto it = handles.find(key);
                if (it != handles.end())
                    return it->second;
                auto* obj = new JsonVal();
                obj->value = value;
                obj->root = this;
                const cell_t h = Handles().Create(ValueType(), obj, owner);
                if (!h)
                {
                    delete obj;
                    return 0;
                }
                handles.emplace(key, h);
                return h;
            }

            void FreeAll()
            {
                for (auto& [value, h] : handles)
                    Handles().Free(h, 0, true);
                handles.clear();
            }
        };

        struct JsonDoc : HandleObject
        {
            ISource2ModJSONDocument* doc = nullptr;
            JsonValues values;

            ~JsonDoc() override
            {
                values.FreeAll();
                if (doc)
                    doc->Release();
            }
        };

        HandleType DocType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModJSONDocument", [](void* p) { delete static_cast<JsonDoc*>(p); });
            return t;
        }

        JsonVal* ValOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<JsonVal*>(ReadHandle(ctx, handle, ValueType()));
        }

        JsonDoc* DocOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<JsonDoc*>(ReadHandle(ctx, handle, DocType()));
        }

#define S2M_JSON_TEST(name)                                                        \
        cell_t N_Json##name(IPluginContext* ctx, const cell_t* params)             \
        {                                                                          \
            JsonVal* v = ValOf(ctx, params[1]);                                    \
            return v && v->value->name();                                          \
        }
        S2M_JSON_TEST(IsNull)
        S2M_JSON_TEST(IsObject)
        S2M_JSON_TEST(IsArray)
        S2M_JSON_TEST(IsString)
        S2M_JSON_TEST(IsNumber)
        S2M_JSON_TEST(IsBool)
#undef S2M_JSON_TEST

        cell_t N_JsonHas(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v && v->value->Has(LocalString(ctx, params[2]));
        }

        cell_t N_JsonGet(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->root->For(v->value->Get(LocalString(ctx, params[2]))) : 0;
        }

        cell_t N_JsonCount(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->value->Count() : 0;
        }

        cell_t N_JsonAt(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->root->For(v->value->At(params[2])) : 0;
        }

        cell_t N_JsonKeyAt(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? ToBuffer(ctx, params[3], params[4], v->value->KeyAt(params[2])) : 0;
        }

        // AsString(buffer, maxlen, def)
        cell_t N_JsonAsString(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? ToBuffer(ctx, params[2], params[3], v->value->AsString(LocalString(ctx, params[4]))) : 0;
        }

        cell_t N_JsonAsInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (JsonVal* v = ValOf(ctx, params[1]))
                WriteInt64(ctx, params[2], v->value->AsInt64(ReadInt64Arg(ctx, params, 3)));
            return 0;
        }

        cell_t N_JsonAsInt(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->value->AsInt(params[2]) : 0;
        }

        cell_t N_JsonAsDouble(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? sp_ftoc(static_cast<float>(v->value->AsDouble(sp_ctof(params[2])))) : 0;
        }

        cell_t N_JsonAsFloat(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? sp_ftoc(v->value->AsFloat(sp_ctof(params[2]))) : 0;
        }

        cell_t N_JsonAsBool(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v && v->value->AsBool(params[2] != 0);
        }

        // GetString(key, buffer, maxlen, def)
        cell_t N_JsonGetString(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? ToBuffer(ctx, params[3], params[4], v->value->GetString(LocalString(ctx, params[2]), LocalString(ctx, params[5]))) : 0;
        }

        cell_t N_JsonGetInt64(IPluginContext* ctx, const cell_t* params)
        {
            if (JsonVal* v = ValOf(ctx, params[1]))
                WriteInt64(ctx, params[3], v->value->GetInt64(LocalString(ctx, params[2]), ReadInt64Arg(ctx, params, 4)));
            return 0;
        }

        cell_t N_JsonGetInt(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? v->value->GetInt(LocalString(ctx, params[2]), params[3]) : 0;
        }

        cell_t N_JsonGetDouble(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? sp_ftoc(static_cast<float>(v->value->GetDouble(LocalString(ctx, params[2]), sp_ctof(params[3])))) : 0;
        }

        cell_t N_JsonGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? sp_ftoc(v->value->GetFloat(LocalString(ctx, params[2]), sp_ctof(params[3]))) : 0;
        }

        cell_t N_JsonGetBool(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v && v->value->GetBool(LocalString(ctx, params[2]), params[3] != 0);
        }

        // Dump(buffer, maxlen, indent)
        cell_t N_JsonDump(IPluginContext* ctx, const cell_t* params)
        {
            JsonVal* v = ValOf(ctx, params[1]);
            return v ? ToBuffer(ctx, params[2], params[3], v->value->Dump(params[4])) : 0;
        }

        cell_t N_DocIsValid(IPluginContext* ctx, const cell_t* params)
        {
            JsonDoc* d = DocOf(ctx, params[1]);
            return d && d->doc->IsValid();
        }

        cell_t N_DocGetError(IPluginContext* ctx, const cell_t* params)
        {
            JsonDoc* d = DocOf(ctx, params[1]);
            return d ? ToBuffer(ctx, params[2], params[3], d->doc->GetError()) : 0;
        }

        cell_t N_DocRoot(IPluginContext* ctx, const cell_t* params)
        {
            JsonDoc* d = DocOf(ctx, params[1]);
            return d && d->doc->IsValid() ? d->values.For(d->doc->Root()) : 0;
        }

        // Parse(text, length)
        cell_t N_JsonParse(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModJSONDocument* parsed = json::jsonManager.Parse(LocalString(ctx, params[1]), params[2]);
            if (!parsed)
                return 0;
            auto* d = new JsonDoc();
            d->doc = parsed;
            d->values.owner = CallerId(ctx);
            const cell_t h = Handles().Create(DocType(), d, d->values.owner);
            if (!h)
                delete d;
            return h;
        }

        // ---- HTTP ---------------------------------------------------------------------

        struct Headers : HandleObject
        {
            std::vector<Source2ModHTTPHeader> list;
        };

        HandleType HeadersType()
        {
            static const HandleType t = Handles().CreateType("Source2ModHTTPHeaders", [](void* p) { delete static_cast<Headers*>(p); });
            return t;
        }

        Headers* HeadersOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Headers*>(ReadHandle(ctx, handle, HeadersType()));
        }

        struct ResponseFrame
        {
            const Source2ModHTTPResponse* response;
            JsonValues json;
        };

        HandleType ResponseType()
        {
            static const HandleType t = FrameType("Source2ModHTTPResponse");
            return t;
        }

        ResponseFrame* ResponseOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ResponseFrame*>(ReadHandle(ctx, handle, ResponseType()));
        }

        cell_t N_HeadersNew(IPluginContext* ctx, const cell_t*)
        {
            auto* h = new Headers();
            const cell_t handle = Handles().Create(HeadersType(), h, CallerId(ctx));
            if (!handle)
                delete h;
            return handle;
        }

        cell_t N_HeadersAdd(IPluginContext* ctx, const cell_t* params)
        {
            if (Headers* h = HeadersOf(ctx, params[1]))
                h->list.emplace_back(LocalString(ctx, params[2]), LocalString(ctx, params[3]));
            return 0;
        }

        cell_t N_HeadersCount(IPluginContext* ctx, const cell_t* params)
        {
            Headers* h = HeadersOf(ctx, params[1]);
            return h ? static_cast<cell_t>(h->list.size()) : 0;
        }

        cell_t N_HeadersClear(IPluginContext* ctx, const cell_t* params)
        {
            if (Headers* h = HeadersOf(ctx, params[1]))
                h->list.clear();
            return 0;
        }

        cell_t N_ResponseSuccess(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r && r->response->m_bSuccess;
        }

        cell_t N_ResponseStatusCode(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r ? r->response->m_nStatusCode : 0;
        }

        cell_t N_ResponseGetBody(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r ? ToBuffer(ctx, params[2], params[3], r->response->m_sBody.c_str()) : 0;
        }

        cell_t N_ResponseBodyLength(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r ? static_cast<cell_t>(r->response->m_sBody.size()) : 0;
        }

        cell_t N_ResponseJson(IPluginContext* ctx, const cell_t* params)
        {
            ResponseFrame* r = ResponseOf(ctx, params[1]);
            return r ? r->json.For(r->response->m_pJson) : 0;
        }

        Source2ModHTTPCallback HttpCallback(IPluginFunction* fn, cell_t data, PluginId owner)
        {
            return Source2ModHTTPCallback([fn, data, owner](const Source2ModHTTPResponse& response)
            {
                ResponseFrame frame{ &response, {} };
                frame.json.owner = owner;
                const cell_t handle = Handles().Create(ResponseType(), &frame, owner);
                sp::CallArgs args;
                args.PushCell(handle);
                args.PushCell(data);
                Invoke(fn, args);
                frame.json.FreeAll();
                Handles().Free(handle, 0, true);
            });
        }

        /// The headers argument (null allowed); false after a script error.
        bool HeadersArg(IPluginContext* ctx, cell_t handle, const std::vector<Source2ModHTTPHeader>** out)
        {
            *out = nullptr;
            if (!handle)
                return true;
            Headers* h = HeadersOf(ctx, handle);
            if (!h)
                return false;
            *out = &h->list;
            return true;
        }

        const char* BodyArg(IPluginContext* ctx, cell_t addr)
        {
            const char* body = LocalString(ctx, addr);
            return *body ? body : nullptr;
        }

        // Request(method, url, body, callback, data, headers)
        cell_t N_HttpRequest(IPluginContext* ctx, const cell_t* params)
        {
            if (params[1] < static_cast<cell_t>(ESource2ModHTTPMethod::Get) || params[1] > static_cast<cell_t>(ESource2ModHTTPMethod::Head))
                return ctx->ThrowNativeError("Invalid HTTP method %d", params[1]);
            IPluginFunction* fn = Function(ctx, params[4]);
            const std::vector<Source2ModHTTPHeader>* headers;
            if (!fn || !HeadersArg(ctx, params[6], &headers))
                return 0;
            const PluginId owner = CallerId(ctx);
            http::httpManager.Request(owner, static_cast<ESource2ModHTTPMethod>(params[1]), LocalString(ctx, params[2]), BodyArg(ctx, params[3]),
                                      HttpCallback(fn, params[5], owner), headers);
            return 0;
        }

        // Get(url, callback, data, headers)
        cell_t N_HttpGet(IPluginContext* ctx, const cell_t* params)
        {
            IPluginFunction* fn = Function(ctx, params[2]);
            const std::vector<Source2ModHTTPHeader>* headers;
            if (!fn || !HeadersArg(ctx, params[4], &headers))
                return 0;
            const PluginId owner = CallerId(ctx);
            http::httpManager.Get(owner, LocalString(ctx, params[1]), HttpCallback(fn, params[3], owner), headers);
            return 0;
        }

#define S2M_HTTP_WITH_BODY(name)                                                                                   \
        cell_t N_Http##name(IPluginContext* ctx, const cell_t* params)                                            \
        {                                                                                                         \
            IPluginFunction* fn = Function(ctx, params[3]);                                                       \
            const std::vector<Source2ModHTTPHeader>* headers;                                                     \
            if (!fn || !HeadersArg(ctx, params[5], &headers))                                                     \
                return 0;                                                                                         \
            const PluginId owner = CallerId(ctx);                                                                 \
            http::httpManager.name(owner, LocalString(ctx, params[1]), BodyArg(ctx, params[2]),                    \
                                   HttpCallback(fn, params[4], owner), headers);                                  \
            return 0;                                                                                             \
        }
        S2M_HTTP_WITH_BODY(Post)
        S2M_HTTP_WITH_BODY(Put)
        S2M_HTTP_WITH_BODY(Patch)
        S2M_HTTP_WITH_BODY(Delete)
#undef S2M_HTTP_WITH_BODY

        cell_t N_HttpIsAvailable(IPluginContext*, const cell_t*)
        {
            return http::httpManager.IsAvailable();
        }

        cell_t N_HttpPendingCount(IPluginContext*, const cell_t*)
        {
            return http::httpManager.PendingCount();
        }

        // ---- MySQL --------------------------------------------------------------------

        HandleType QueryType()
        {
            static const HandleType t = FrameType("ISource2ModSQLQuery");
            return t;
        }

        HandleType ResultType()
        {
            static const HandleType t = FrameType("ISource2ModSQLResult");
            return t;
        }

        /// A query lent to a callback, and the result-set handle taken from it.
        struct QueryFrame
        {
            ISource2ModSQLQuery* query = nullptr;
            PluginId owner = 0;
            ISource2ModSQLResult* result = nullptr;
            cell_t resultHandle = 0;
            cell_t handle = 0;

            void Lend()
            {
                handle = query ? Handles().Create(QueryType(), this, owner) : 0;
            }

            void Return()
            {
                if (resultHandle)
                    Handles().Free(resultHandle, 0, true);
                if (handle)
                    Handles().Free(handle, 0, true);
                resultHandle = handle = 0;
            }
        };

        QueryFrame* QueryOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<QueryFrame*>(ReadHandle(ctx, handle, QueryType()));
        }

        ISource2ModSQLResult* ResultOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<ISource2ModSQLResult*>(ReadHandle(ctx, handle, ResultType()));
        }

        struct Conn : HandleObject
        {
            ISource2ModSQLConnection* connection = nullptr;
            std::shared_ptr<bool> alive = std::make_shared<bool>(true);

            ~Conn() override
            {
                *alive = false;
                ISource2ModSQLConnection* c = connection;
                scheduler::schedulerManager.NextFrame(0, [c]()
                {
                    auto& all = mysql::mysqlManager.m_vecMysqlConnections;
                    auto& sqlite = mysql::mysqlManager.m_vecSqliteConnections;
                    if (std::find(all.begin(), all.end(), c) != all.end() ||
                        std::find(sqlite.begin(), sqlite.end(), c) != sqlite.end())
                        c->Destroy();
                });
            }
        };

        struct Serverless : HandleObject
        {
            ISource2ModSQLServerless* serverless = nullptr;
            std::shared_ptr<bool> alive = std::make_shared<bool>(true);

            ~Serverless() override
            {
                *alive = false;
                ISource2ModSQLServerless* s = serverless;
                scheduler::schedulerManager.NextFrame(0, [s]() { s->Destroy(); });
            }
        };

        struct Txn : HandleObject
        {
            Source2ModSQLTransaction txn;
        };

        HandleType ConnType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModSQLConnection", [](void* p) { delete static_cast<Conn*>(p); });
            return t;
        }

        HandleType ServerlessType()
        {
            static const HandleType t = Handles().CreateType("ISource2ModSQLServerless", [](void* p) { delete static_cast<Serverless*>(p); });
            return t;
        }

        HandleType TxnType()
        {
            static const HandleType t = Handles().CreateType("Source2ModSQLTransaction", [](void* p) { delete static_cast<Txn*>(p); });
            return t;
        }

        Conn* ConnOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Conn*>(ReadHandle(ctx, handle, ConnType()));
        }

        Serverless* ServerlessOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Serverless*>(ReadHandle(ctx, handle, ServerlessType()));
        }

        Txn* TxnOf(IPluginContext* ctx, cell_t handle)
        {
            return static_cast<Txn*>(ReadHandle(ctx, handle, TxnType()));
        }

        // Result set natives (lent, valid inside the query callback).
        cell_t N_ResGetRowCount(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r ? r->GetRowCount() : 0;
        }

        cell_t N_ResGetFieldCount(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r ? r->GetFieldCount() : 0;
        }

        cell_t N_ResFieldNameToNum(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            if (!r)
                return 0;
            unsigned int column = 0;
            if (!r->FieldNameToNum(LocalString(ctx, params[2]), &column))
                return 0;
            cell_t* out;
            ctx->LocalToPhysAddr(params[3], &out);
            *out = static_cast<cell_t>(column);
            return 1;
        }

        bool ColumnOk(IPluginContext* ctx, ISource2ModSQLResult* r, cell_t column)
        {
            if (column < 0 || column >= r->GetFieldCount())
            {
                ctx->ThrowNativeError("Invalid column %d (count: %d)", column, r->GetFieldCount());
                return false;
            }
            return true;
        }

        cell_t N_ResFieldNumToName(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && ColumnOk(ctx, r, params[2]) ? ToBuffer(ctx, params[3], params[4], r->FieldNumToName(params[2])) : 0;
        }

        cell_t N_ResMoreRows(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && r->MoreRows();
        }

        cell_t N_ResFetchRow(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && r->FetchRow() != nullptr;
        }

        cell_t N_ResCurrentRow(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && r->CurrentRow() != nullptr;
        }

        cell_t N_ResRewind(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && r->Rewind();
        }

        /// A column of the current row; false (and a script error) when there is no row or no such column.
        bool CellOk(IPluginContext* ctx, ISource2ModSQLResult* r, cell_t column)
        {
            if (!ColumnOk(ctx, r, column))
                return false;
            if (!r->CurrentRow())
            {
                ctx->ThrowNativeError("No current row (call FetchRow first)");
                return false;
            }
            return true;
        }

        cell_t N_ResGetFieldType(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && ColumnOk(ctx, r, params[2]) ? r->GetFieldType(params[2]) : 0;
        }

        cell_t N_ResGetString(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && CellOk(ctx, r, params[2]) ? ToBuffer(ctx, params[3], params[4], r->GetString(params[2])) : 0;
        }

        cell_t N_ResGetDataSize(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && CellOk(ctx, r, params[2]) ? static_cast<cell_t>(r->GetDataSize(params[2])) : 0;
        }

        cell_t N_ResGetFloat(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && CellOk(ctx, r, params[2]) ? sp_ftoc(r->GetFloat(params[2])) : 0;
        }

        cell_t N_ResGetInt(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && CellOk(ctx, r, params[2]) ? r->GetInt(params[2]) : 0;
        }

        cell_t N_ResGetInt64(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            if (r && CellOk(ctx, r, params[2]))
                WriteInt64(ctx, params[3], r->GetInt64(params[2]));
            return 0;
        }

        cell_t N_ResIsNull(IPluginContext* ctx, const cell_t* params)
        {
            ISource2ModSQLResult* r = ResultOf(ctx, params[1]);
            return r && CellOk(ctx, r, params[2]) && r->IsNull(params[2]);
        }

        // Query natives (lent).
        cell_t N_QueryGetResultSet(IPluginContext* ctx, const cell_t* params)
        {
            QueryFrame* q = QueryOf(ctx, params[1]);
            if (!q)
                return 0;
            ISource2ModSQLResult* r = q->query->GetResultSet();
            if (r != q->result)
            {
                if (q->resultHandle)
                    Handles().Free(q->resultHandle, 0, true);
                q->result = r;
                q->resultHandle = r ? Handles().Create(ResultType(), r, q->owner) : 0;
            }
            return q->resultHandle;
        }

        cell_t N_QueryFetchMoreResults(IPluginContext* ctx, const cell_t* params)
        {
            QueryFrame* q = QueryOf(ctx, params[1]);
            return q && q->query->FetchMoreResults();
        }

        cell_t N_QueryGetInsertId(IPluginContext* ctx, const cell_t* params)
        {
            QueryFrame* q = QueryOf(ctx, params[1]);
            return q ? static_cast<cell_t>(q->query->GetInsertId()) : 0;
        }

        cell_t N_QueryGetAffectedRows(IPluginContext* ctx, const cell_t* params)
        {
            QueryFrame* q = QueryOf(ctx, params[1]);
            return q ? static_cast<cell_t>(q->query->GetAffectedRows()) : 0;
        }

        // Transactions.
        cell_t N_TxnNew(IPluginContext* ctx, const cell_t*)
        {
            auto* t = new Txn();
            const cell_t h = Handles().Create(TxnType(), t, CallerId(ctx));
            if (!h)
                delete t;
            return h;
        }

        cell_t N_TxnAddQuery(IPluginContext* ctx, const cell_t* params)
        {
            if (Txn* t = TxnOf(ctx, params[1]))
                t->txn.queries.emplace_back(LocalString(ctx, params[2]));
            return 0;
        }

        cell_t N_TxnCount(IPluginContext* ctx, const cell_t* params)
        {
            Txn* t = TxnOf(ctx, params[1]);
            return t ? static_cast<cell_t>(t->txn.queries.size()) : 0;
        }

        Source2ModSQLConnectionInfo InfoArgs(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModSQLConnectionInfo info{};
            info.host = LocalString(ctx, params[1]);
            info.user = LocalString(ctx, params[2]);
            info.pass = LocalString(ctx, params[3]);
            info.database = LocalString(ctx, params[4]);
            info.port = params[5];
            info.timeout = params[6];
            if (params[0] >= 7)
                info.driver = params[7] == SOURCE2MOD_SQL_DRIVER_SQLITE ? SOURCE2MOD_SQL_DRIVER_SQLITE : SOURCE2MOD_SQL_DRIVER_MYSQL;
            return info;
        }

        cell_t NewConnection(IPluginContext* ctx, const Source2ModSQLConnectionInfo& info)
        {
            auto* c = new Conn();
            c->connection = mysql::mysqlManager.CreateConnection(0, info);
            const cell_t h = Handles().Create(ConnType(), c, CallerId(ctx));
            if (!h)
                delete c;
            return h;
        }

        cell_t NewServerless(IPluginContext* ctx, const Source2ModSQLConnectionInfo& info)
        {
            auto* s = new Serverless();
            s->serverless = mysql::mysqlManager.CreateServerless(0, info);
            const cell_t h = Handles().Create(ServerlessType(), s, CallerId(ctx));
            if (!h)
                delete s;
            return h;
        }

        // CreateConnection(host, user, pass, database, port, timeout, driver)
        cell_t N_MySQLCreateConnection(IPluginContext* ctx, const cell_t* params)
        {
            return NewConnection(ctx, InfoArgs(ctx, params));
        }

        cell_t N_MySQLCreateServerless(IPluginContext* ctx, const cell_t* params)
        {
            return NewServerless(ctx, InfoArgs(ctx, params));
        }

        // CreateNamedConnection(name): configs/databases.json; 0 for a name not there.
        cell_t N_SQLCreateNamedConnection(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModSQLConnectionInfo info{};
            if (!mysql::mysqlManager.GetNamedConnection(LocalString(ctx, params[1]), info))
                return 0;
            return NewConnection(ctx, info);
        }

        cell_t N_SQLCreateNamedServerless(IPluginContext* ctx, const cell_t* params)
        {
            Source2ModSQLConnectionInfo info{};
            if (!mysql::mysqlManager.GetNamedConnection(LocalString(ctx, params[1]), info))
                return 0;
            return NewServerless(ctx, info);
        }

        // GetDriver()
        cell_t N_ConnGetDriver(IPluginContext* ctx, const cell_t* params)
        {
            Conn* c = ConnOf(ctx, params[1]);
            return c ? c->connection->GetDriver() : 0;
        }

        // Connect(callback, data)
        cell_t N_ConnConnect(IPluginContext* ctx, const cell_t* params)
        {
            Conn* c = ConnOf(ctx, params[1]);
            IPluginFunction* fn = c ? Function(ctx, params[2]) : nullptr;
            if (!fn)
                return 0;
            const cell_t data = params[3];
            c->connection->Connect([fn, data, alive = c->alive](bool success)
            {
                if (!*alive)
                    return;
                sp::CallArgs args;
                args.PushCell(success ? 1 : 0);
                args.PushCell(data);
                Invoke(fn, args);
            });
            return 0;
        }

        // Query(query, callback, data)
        cell_t N_ConnQuery(IPluginContext* ctx, const cell_t* params)
        {
            Conn* c = ConnOf(ctx, params[1]);
            IPluginFunction* fn = c ? Function(ctx, params[3]) : nullptr;
            if (!fn)
                return 0;
            const cell_t data = params[4];
            const PluginId owner = CallerId(ctx);
            std::string query = LocalString(ctx, params[2]);
            c->connection->Query(query.data(), [fn, data, owner, alive = c->alive](ISource2ModSQLQuery* result)
            {
                if (!*alive)
                    return;
                QueryFrame frame;
                frame.query = result;
                frame.owner = owner;
                frame.Lend();
                sp::CallArgs args;
                args.PushCell(frame.handle);
                args.PushCell(data);
                Invoke(fn, args);
                frame.Return();
            });
            return 0;
        }

        // ExecuteTransaction(txn, success, failure, data)
        cell_t N_ConnExecuteTransaction(IPluginContext* ctx, const cell_t* params)
        {
            Conn* c = ConnOf(ctx, params[1]);
            Txn* t = c ? TxnOf(ctx, params[2]) : nullptr;
            IPluginFunction* success = t ? Function(ctx, params[3]) : nullptr;
            IPluginFunction* failure = success ? Function(ctx, params[4]) : nullptr;
            if (!failure)
                return 0;
            const cell_t data = params[5];
            const PluginId owner = CallerId(ctx);
            c->connection->ExecuteTransaction(t->txn,
                [success, data, owner, alive = c->alive](std::vector<ISource2ModSQLQuery*> queries)
                {
                    if (!*alive)
                        return;
                    std::vector<QueryFrame> frames(queries.size());
                    std::vector<cell_t> handles;
                    for (size_t i = 0; i < queries.size(); i++)
                    {
                        frames[i].query = queries[i];
                        frames[i].owner = owner;
                        frames[i].Lend();
                        handles.push_back(frames[i].handle);
                    }
                    const cell_t list = NewCellList(handles, owner);
                    sp::CallArgs args;
                    args.PushCell(list);
                    args.PushCell(data);
                    Invoke(success, args);
                    Handles().Free(list, 0, true);
                    for (QueryFrame& frame : frames)
                        frame.Return();
                },
                [failure, data, alive = c->alive](std::string error, int failIndex)
                {
                    if (!*alive)
                        return;
                    sp::CallArgs args;
                    args.PushString(error.c_str());
                    args.PushCell(failIndex);
                    args.PushCell(data);
                    Invoke(failure, args);
                });
            return 0;
        }

        // Escape(text, buffer, maxlen)
        cell_t N_ConnEscape(IPluginContext* ctx, const cell_t* params)
        {
            Conn* c = ConnOf(ctx, params[1]);
            if (!c)
                return 0;
            // mysql_real_escape_string needs the connection's character set.
            if (c->connection->GetDriver() == SOURCE2MOD_SQL_DRIVER_MYSQL &&
                !static_cast<mysql::MySQLConnection*>(c->connection)->GetDatabase())
                return ctx->ThrowNativeError("Escape needs a connected connection");
            const std::string escaped = c->connection->Escape(LocalString(ctx, params[2]));
            return ToBuffer(ctx, params[3], params[4], escaped.c_str());
        }

        Source2ModSQLServerlessCallbackFunc ServerlessCallback(IPluginFunction* fn, cell_t data, PluginId owner, std::shared_ptr<bool> alive)
        {
            return [fn, data, owner, alive = std::move(alive)](const Source2ModSQLServerlessResult& result)
            {
                if (!*alive)
                    return;
                QueryFrame frame;
                frame.query = result.m_pQuery;
                frame.owner = owner;
                frame.Lend();
                sp::CallArgs args;
                args.PushCell(result.m_bSuccess ? 1 : 0);
                args.PushCell(static_cast<cell_t>(result.m_nErrorCode));
                args.PushString(result.m_sError.c_str());
                args.PushCell(frame.handle);
                args.PushCell(data);
                Invoke(fn, args);
                frame.Return();
            };
        }

        // Query(query, callback, data)
        cell_t N_ServerlessQuery(IPluginContext* ctx, const cell_t* params)
        {
            Serverless* s = ServerlessOf(ctx, params[1]);
            IPluginFunction* fn = s ? Function(ctx, params[3]) : nullptr;
            if (!fn)
                return 0;
            s->serverless->Query(LocalString(ctx, params[2]), ServerlessCallback(fn, params[4], CallerId(ctx), s->alive));
            return 0;
        }

        // QueryWithParams(query, const char[][] params, numParams, callback, data)
        cell_t N_ServerlessQueryWithParams(IPluginContext* ctx, const cell_t* params)
        {
            Serverless* s = ServerlessOf(ctx, params[1]);
            IPluginFunction* fn = s ? Function(ctx, params[5]) : nullptr;
            if (!fn)
                return 0;
            if (params[4] < 0)
                return ctx->ThrowNativeError("Invalid parameter count %d", params[4]);
            cell_t* iv;
            ctx->LocalToPhysAddr(params[3], &iv);
            const bool direct = ctx->GetRuntime()->UsesDirectArrays();
            std::vector<std::string> values;
            for (cell_t i = 0; i < params[4]; i++)
            {
                const cell_t addr = direct ? iv[i] : params[3] + i * static_cast<cell_t>(sizeof(cell_t)) + iv[i];
                values.emplace_back(LocalString(ctx, addr));
            }
            s->serverless->Query(LocalString(ctx, params[2]), std::move(values), ServerlessCallback(fn, params[6], CallerId(ctx), s->alive));
            return 0;
        }

        const sp_nativeinfo_t g_netNatives[] = {
            { "ISource2ModJSONValue.IsNull", N_JsonIsNull },
            { "ISource2ModJSONValue.IsObject", N_JsonIsObject },
            { "ISource2ModJSONValue.IsArray", N_JsonIsArray },
            { "ISource2ModJSONValue.IsString", N_JsonIsString },
            { "ISource2ModJSONValue.IsNumber", N_JsonIsNumber },
            { "ISource2ModJSONValue.IsBool", N_JsonIsBool },
            { "ISource2ModJSONValue.Has", N_JsonHas },
            { "ISource2ModJSONValue.Get", N_JsonGet },
            { "ISource2ModJSONValue.Count", N_JsonCount },
            { "ISource2ModJSONValue.At", N_JsonAt },
            { "ISource2ModJSONValue.KeyAt", N_JsonKeyAt },
            { "ISource2ModJSONValue.AsString", N_JsonAsString },
            { "ISource2ModJSONValue.AsInt64", N_JsonAsInt64 },
            { "ISource2ModJSONValue.AsInt", N_JsonAsInt },
            { "ISource2ModJSONValue.AsDouble", N_JsonAsDouble },
            { "ISource2ModJSONValue.AsFloat", N_JsonAsFloat },
            { "ISource2ModJSONValue.AsBool", N_JsonAsBool },
            { "ISource2ModJSONValue.GetString", N_JsonGetString },
            { "ISource2ModJSONValue.GetInt64", N_JsonGetInt64 },
            { "ISource2ModJSONValue.GetInt", N_JsonGetInt },
            { "ISource2ModJSONValue.GetDouble", N_JsonGetDouble },
            { "ISource2ModJSONValue.GetFloat", N_JsonGetFloat },
            { "ISource2ModJSONValue.GetBool", N_JsonGetBool },
            { "ISource2ModJSONValue.Dump", N_JsonDump },
            { "ISource2ModJSONDocument.IsValid", N_DocIsValid },
            { "ISource2ModJSONDocument.GetError", N_DocGetError },
            { "ISource2ModJSONDocument.Root", N_DocRoot },
            { "Source2ModJSON.Parse", N_JsonParse },

            { "Source2ModHTTPHeaders.Source2ModHTTPHeaders", N_HeadersNew },
            { "Source2ModHTTPHeaders.Add", N_HeadersAdd },
            { "Source2ModHTTPHeaders.Count", N_HeadersCount },
            { "Source2ModHTTPHeaders.Clear", N_HeadersClear },
            { "Source2ModHTTPResponse.Success", N_ResponseSuccess },
            { "Source2ModHTTPResponse.StatusCode", N_ResponseStatusCode },
            { "Source2ModHTTPResponse.GetBody", N_ResponseGetBody },
            { "Source2ModHTTPResponse.BodyLength", N_ResponseBodyLength },
            { "Source2ModHTTPResponse.Json", N_ResponseJson },
            { "Source2ModHTTP.IsAvailable", N_HttpIsAvailable },
            { "Source2ModHTTP.PendingCount", N_HttpPendingCount },
            { "Source2ModHTTP.Request", N_HttpRequest },
            { "Source2ModHTTP.Get", N_HttpGet },
            { "Source2ModHTTP.Post", N_HttpPost },
            { "Source2ModHTTP.Put", N_HttpPut },
            { "Source2ModHTTP.Patch", N_HttpPatch },
            { "Source2ModHTTP.Delete", N_HttpDelete },

            { "ISource2ModSQLResult.GetRowCount", N_ResGetRowCount },
            { "ISource2ModSQLResult.GetFieldCount", N_ResGetFieldCount },
            { "ISource2ModSQLResult.FieldNameToNum", N_ResFieldNameToNum },
            { "ISource2ModSQLResult.FieldNumToName", N_ResFieldNumToName },
            { "ISource2ModSQLResult.MoreRows", N_ResMoreRows },
            { "ISource2ModSQLResult.FetchRow", N_ResFetchRow },
            { "ISource2ModSQLResult.CurrentRow", N_ResCurrentRow },
            { "ISource2ModSQLResult.Rewind", N_ResRewind },
            { "ISource2ModSQLResult.GetFieldType", N_ResGetFieldType },
            { "ISource2ModSQLResult.GetString", N_ResGetString },
            { "ISource2ModSQLResult.GetDataSize", N_ResGetDataSize },
            { "ISource2ModSQLResult.GetFloat", N_ResGetFloat },
            { "ISource2ModSQLResult.GetInt", N_ResGetInt },
            { "ISource2ModSQLResult.GetInt64", N_ResGetInt64 },
            { "ISource2ModSQLResult.IsNull", N_ResIsNull },
            { "ISource2ModSQLQuery.GetResultSet", N_QueryGetResultSet },
            { "ISource2ModSQLQuery.FetchMoreResults", N_QueryFetchMoreResults },
            { "ISource2ModSQLQuery.GetInsertId", N_QueryGetInsertId },
            { "ISource2ModSQLQuery.GetAffectedRows", N_QueryGetAffectedRows },
            { "Source2ModSQLTransaction.Source2ModSQLTransaction", N_TxnNew },
            { "Source2ModSQLTransaction.AddQuery", N_TxnAddQuery },
            { "Source2ModSQLTransaction.Count", N_TxnCount },
            { "ISource2ModSQLConnection.Connect", N_ConnConnect },
            { "ISource2ModSQLConnection.Query", N_ConnQuery },
            { "ISource2ModSQLConnection.ExecuteTransaction", N_ConnExecuteTransaction },
            { "ISource2ModSQLConnection.Escape", N_ConnEscape },
            { "ISource2ModSQLConnection.GetDriver", N_ConnGetDriver },
            { "ISource2ModSQLServerless.Query", N_ServerlessQuery },
            { "ISource2ModSQLServerless.QueryWithParams", N_ServerlessQueryWithParams },
            { "Source2ModSQL.CreateConnection", N_MySQLCreateConnection },
            { "Source2ModSQL.CreateServerless", N_MySQLCreateServerless },
            { "Source2ModSQL.CreateNamedConnection", N_SQLCreateNamedConnection },
            { "Source2ModSQL.CreateNamedServerless", N_SQLCreateNamedServerless },
            { nullptr, nullptr },
        };
    }

    const sp_nativeinfo_t* GetNetNatives()
    {
        return g_netNatives;
    }
}
