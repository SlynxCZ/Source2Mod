/**
* vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * Source2Mod
 * Copyright (C) 2025-2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
 * All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 * gives you permission to link the code of this program
 * (as well as its derivative works) to "Counter-Strike 2," "Source 2,"
 * "Steam," and any Game MODs or server software running on software by
 * Valve Corporation. You must obey the GNU General Public License in all
 * respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: Source2Mod
 */
#pragma once
#include "hookid.h"

#include <source2mod/ISource2ModHTTP.h>
#include <steam/steam_gameserver.h>

#include <vector>

namespace http {
    class HTTPManager final : public ISource2ModHTTP {
    public:
        bool IsAvailable() const override;
        int PendingCount() const override;

        void Request(ESource2ModHTTPMethod method, const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback,
                     const std::vector<Source2ModHTTPHeader>* pHeaders) override
        {
            const PluginId owner = hookid::OwnerOfHandler(callback);
            Request(owner, method, pszUrl, pszBody, std::move(callback), pHeaders);
        }
        void Get(const char* pszUrl, Source2ModHTTPCallback callback, const std::vector<Source2ModHTTPHeader>* pHeaders) override
        {
            const PluginId owner = hookid::OwnerOfHandler(callback);
            Get(owner, pszUrl, std::move(callback), pHeaders);
        }
        void Post(const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback, const std::vector<Source2ModHTTPHeader>* pHeaders) override
        {
            const PluginId owner = hookid::OwnerOfHandler(callback);
            Post(owner, pszUrl, pszBody, std::move(callback), pHeaders);
        }
        void Put(const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback, const std::vector<Source2ModHTTPHeader>* pHeaders) override
        {
            const PluginId owner = hookid::OwnerOfHandler(callback);
            Put(owner, pszUrl, pszBody, std::move(callback), pHeaders);
        }
        void Patch(const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback, const std::vector<Source2ModHTTPHeader>* pHeaders) override
        {
            const PluginId owner = hookid::OwnerOfHandler(callback);
            Patch(owner, pszUrl, pszBody, std::move(callback), pHeaders);
        }
        void Delete(const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback, const std::vector<Source2ModHTTPHeader>* pHeaders) override
        {
            const PluginId owner = hookid::OwnerOfHandler(callback);
            Delete(owner, pszUrl, pszBody, std::move(callback), pHeaders);
        }

        // The same with the owner spelled out: what the calls above resolve
        // to, and what the core itself calls.
        void Request(PluginId owner, ESource2ModHTTPMethod method, const char* pszUrl, const char* pszBody,
                     Source2ModHTTPCallback callback,
                     const std::vector<Source2ModHTTPHeader>* pHeaders);

        void Get(PluginId owner, const char* pszUrl, Source2ModHTTPCallback callback,
                 const std::vector<Source2ModHTTPHeader>* pHeaders);
        void Post(PluginId owner, const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback,
                  const std::vector<Source2ModHTTPHeader>* pHeaders);
        void Put(PluginId owner, const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback,
                 const std::vector<Source2ModHTTPHeader>* pHeaders);
        void Patch(PluginId owner, const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback,
                   const std::vector<Source2ModHTTPHeader>* pHeaders);
        void Delete(PluginId owner, const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback,
                    const std::vector<Source2ModHTTPHeader>* pHeaders);

    public:
        /// Core-internal: a POST whose body is arbitrary bytes under a
        /// content type of the caller's choosing -- the interface above only
        /// ever sends JSON. The crash reporter needs it for Discord's
        /// multipart uploads. Not queued: returns false when Steam's HTTP
        /// client is not there yet, and the caller keeps its payload.
        bool PostRaw(PluginId owner, const char* pszUrl, const char* pszContentType,
                     const void* pData, size_t nSize, Source2ModHTTPCallback callback);

        /// Steam handed its HTTP client over -- sends whatever was queued while
        /// it was not there yet. Driven by the ISource2Server vtable hook.
        void OnSteamAPIActivated();

        /// Steam took it away again; queue from here on.
        void OnSteamAPIDeactivated();

        /// Drops every in-flight and queued request. Called on unload -- a
        /// callback is a std::function holding code inside a plugin's library.
        void Shutdown();

        /// Cancels this plugin's requests, in flight and queued alike, while
        /// its library is still mapped. A request that outlived its plugin
        /// would land on a callback that is no longer there.
        void RemoveAllForPlugin(PluginId id);

    private:
        /// One request Steam is already working on. Owns its own call result,
        /// so it has to outlive the call and delete itself when it lands.
        class TrackedRequest {
        public:
            TrackedRequest(PluginId owner, HTTPRequestHandle hRequest, SteamAPICall_t hCall,
                           Source2ModHTTPCallback callback);
            TrackedRequest(const TrackedRequest&) = delete;
            TrackedRequest& operator=(const TrackedRequest&) = delete;

            /// Drops the Steam call and the request handle without running the
            /// callback. Only for a request whose owner is going away.
            void Cancel();

            PluginId Owner() const { return m_Owner; }

        private:
            void OnCompleted(HTTPRequestCompleted_t* pResult, bool bIOFailure);

            PluginId m_Owner;
            HTTPRequestHandle m_hRequest;
            CCallResult<TrackedRequest, HTTPRequestCompleted_t> m_CallResult;
            Source2ModHTTPCallback m_Callback;
        };

        struct QueuedRequest {
            PluginId owner = 0;
            ESource2ModHTTPMethod method;
            std::string url;
            std::string body;
            bool hasBody = false;
            Source2ModHTTPCallback callback;
            std::vector<Source2ModHTTPHeader> headers;
        };

        void Send(PluginId owner, ESource2ModHTTPMethod method, const char* pszUrl, const char* pszBody,
                  bool bHasBody, Source2ModHTTPCallback callback,
                  const std::vector<Source2ModHTTPHeader>* pHeaders);

        std::vector<QueuedRequest> m_QueuedRequests;

        // A TrackedRequest used to be reachable only from the Steam call it
        // was waiting on, which left no way to cancel one. Kept here so a
        // plugin's requests can be found and dropped when it unloads; each
        // entry takes itself back out when its call lands.
        std::vector<TrackedRequest*> m_TrackedRequests;
    };

    extern HTTPManager httpManager;
}
