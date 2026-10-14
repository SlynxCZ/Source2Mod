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
#include "http.h"

#include "json.h"
#include "slowguard.h"
#include "utils/log.h"

namespace http
{
    HTTPManager httpManager;

    namespace
    {
        /// Steam's HTTP client, or nullptr until the API activates.
        ISteamHTTP* s_pSteamHTTP = nullptr;

        EHTTPMethod ToSteamMethod(ESource2ModHTTPMethod method)
        {
            switch (method)
            {
                case ESource2ModHTTPMethod::Post:   return k_EHTTPMethodPOST;
                case ESource2ModHTTPMethod::Put:    return k_EHTTPMethodPUT;
                case ESource2ModHTTPMethod::Patch:  return k_EHTTPMethodPATCH;
                case ESource2ModHTTPMethod::Delete: return k_EHTTPMethodDELETE;
                case ESource2ModHTTPMethod::Head:   return k_EHTTPMethodHEAD;
                case ESource2ModHTTPMethod::Get:
                default:                         return k_EHTTPMethodGET;
            }
        }
    }

    /* =========================
    TrackedRequest
    ========================= */

    HTTPManager::TrackedRequest::TrackedRequest(PluginId owner, HTTPRequestHandle hRequest,
                                                SteamAPICall_t hCall, Source2ModHTTPCallback callback)
        : m_Owner(owner), m_hRequest(hRequest), m_Callback(std::move(callback))
    {
        m_CallResult.Set(hCall, this, &TrackedRequest::OnCompleted);
    }

    void HTTPManager::TrackedRequest::Cancel()
    {
        // Set() registered this object with the Steam callback dispatcher;
        // deleting it without taking it back out leaves the dispatcher holding
        // a pointer into freed memory.
        m_CallResult.Cancel();

        if (s_pSteamHTTP)
            s_pSteamHTTP->ReleaseHTTPRequest(m_hRequest);

        delete this;
    }

    void HTTPManager::TrackedRequest::OnCompleted(HTTPRequestCompleted_t* pResult, bool bIOFailure)
    {
        Source2ModHTTPResponse response;
        response.m_bSuccess = !bIOFailure && pResult && pResult->m_bRequestSuccessful;

        if (pResult)
            response.m_nStatusCode = static_cast<int>(pResult->m_eStatusCode);

        if (response.m_bSuccess && s_pSteamHTTP)
        {
            uint32 nSize = 0;
            s_pSteamHTTP->GetHTTPResponseBodySize(m_hRequest, &nSize);

            if (nSize > 0)
            {
                response.m_sBody.resize(nSize);
                s_pSteamHTTP->GetHTTPResponseBodyData(
                    m_hRequest, reinterpret_cast<uint8*>(response.m_sBody.data()), nSize);
            }
        }

        // Parse the body once here rather than making every callback do it.
        // The document is owned for exactly the duration of the callback, which
        // is why m_pJson is documented as not outliving it.
        ISource2ModJSONDocument* pDoc = nullptr;
        if (!response.m_sBody.empty())
        {
            pDoc = json::jsonManager.Parse(response.m_sBody.c_str(),
                                           static_cast<int>(response.m_sBody.size()));
            if (pDoc->IsValid())
                response.m_pJson = pDoc->Root();
        }

        if (m_Callback)
        {
            ::slow::Guard slowGuard("HTTP callback", "", m_Owner);
            m_Callback(response);
        }

        if (pDoc)
            pDoc->Release();

        if (s_pSteamHTTP)
            s_pSteamHTTP->ReleaseHTTPRequest(m_hRequest);

        // Ran to completion; nothing else refers to this.
        std::erase(httpManager.m_TrackedRequests, this);
        delete this;
    }

    /* =========================
    Sending
    ========================= */

    void HTTPManager::Send(PluginId owner, ESource2ModHTTPMethod method, const char* pszUrl,
                           const char* pszBody, bool bHasBody, Source2ModHTTPCallback callback,
                           const std::vector<Source2ModHTTPHeader>* pHeaders)
    {
        HTTPRequestHandle hRequest = s_pSteamHTTP->CreateHTTPRequest(ToSteamMethod(method), pszUrl);

        if (hRequest == INVALID_HTTPREQUEST_HANDLE)
        {
            FP_ERROR("Failed to create an HTTP request for '{}'", pszUrl);

            if (callback)
                callback(Source2ModHTTPResponse{});

            return;
        }

        if (pHeaders)
        {
            for (const auto& header : *pHeaders)
                s_pSteamHTTP->SetHTTPRequestHeaderValue(hRequest, header.m_sName.c_str(), header.m_sValue.c_str());
        }

        if (bHasBody && pszBody)
        {
            s_pSteamHTTP->SetHTTPRequestRawPostBody(
                hRequest, "application/json",
                reinterpret_cast<uint8*>(const_cast<char*>(pszBody)),
                static_cast<uint32>(strlen(pszBody)));
        }

        SteamAPICall_t hCall = k_uAPICallInvalid;

        if (!s_pSteamHTTP->SendHTTPRequest(hRequest, &hCall))
        {
            FP_ERROR("Failed to send an HTTP request to '{}'", pszUrl);
            s_pSteamHTTP->ReleaseHTTPRequest(hRequest);

            if (callback)
                callback(Source2ModHTTPResponse{});

            return;
        }

        // Deletes itself once the call lands, taking itself back out of the
        // list below on the way.
        m_TrackedRequests.push_back(new TrackedRequest(owner, hRequest, hCall, std::move(callback)));
    }

    bool HTTPManager::PostRaw(PluginId owner, const char* pszUrl, const char* pszContentType,
                              const void* pData, size_t nSize, Source2ModHTTPCallback callback)
    {
        if (!pszUrl || !s_pSteamHTTP)
            return false;

        HTTPRequestHandle hRequest = s_pSteamHTTP->CreateHTTPRequest(k_EHTTPMethodPOST, pszUrl);

        if (hRequest == INVALID_HTTPREQUEST_HANDLE)
        {
            FP_ERROR("Failed to create an HTTP request for '{}'", pszUrl);
            return false;
        }

        if (pData && nSize > 0 && !s_pSteamHTTP->SetHTTPRequestRawPostBody(
                hRequest, pszContentType,
                reinterpret_cast<uint8*>(const_cast<void*>(pData)),
                static_cast<uint32>(nSize)))
        {
            FP_ERROR("Failed to set the raw POST body for '{}'", pszUrl);
            s_pSteamHTTP->ReleaseHTTPRequest(hRequest);
            return false;
        }

        SteamAPICall_t hCall = k_uAPICallInvalid;

        if (!s_pSteamHTTP->SendHTTPRequest(hRequest, &hCall))
        {
            FP_ERROR("Failed to send an HTTP request to '{}'", pszUrl);
            s_pSteamHTTP->ReleaseHTTPRequest(hRequest);
            return false;
        }

        m_TrackedRequests.push_back(new TrackedRequest(owner, hRequest, hCall, std::move(callback)));
        return true;
    }

    void HTTPManager::Request(PluginId owner, ESource2ModHTTPMethod method, const char* pszUrl,
                              const char* pszBody, Source2ModHTTPCallback callback,
                              const std::vector<Source2ModHTTPHeader>* pHeaders)
    {
        if (!pszUrl)
            return;

        if (!s_pSteamHTTP)
        {
            // Steam has not handed its client over yet -- hold on to this and
            // send it from OnSteamAPIActivated().
            QueuedRequest queued;
            queued.owner = owner;
            queued.method = method;
            queued.url = pszUrl;
            queued.hasBody = pszBody != nullptr;
            queued.body = pszBody ? pszBody : "";
            queued.callback = std::move(callback);

            if (pHeaders)
                queued.headers = *pHeaders;

            m_QueuedRequests.push_back(std::move(queued));
            return;
        }

        Send(owner, method, pszUrl, pszBody, pszBody != nullptr, std::move(callback), pHeaders);
    }

    void HTTPManager::Get(PluginId owner, const char* pszUrl, Source2ModHTTPCallback callback,
                          const std::vector<Source2ModHTTPHeader>* pHeaders)
    {
        Request(owner, ESource2ModHTTPMethod::Get, pszUrl, nullptr, std::move(callback), pHeaders);
    }

    void HTTPManager::Post(PluginId owner, const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback,
                           const std::vector<Source2ModHTTPHeader>* pHeaders)
    {
        Request(owner, ESource2ModHTTPMethod::Post, pszUrl, pszBody, std::move(callback), pHeaders);
    }

    void HTTPManager::Put(PluginId owner, const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback,
                          const std::vector<Source2ModHTTPHeader>* pHeaders)
    {
        Request(owner, ESource2ModHTTPMethod::Put, pszUrl, pszBody, std::move(callback), pHeaders);
    }

    void HTTPManager::Patch(PluginId owner, const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback,
                            const std::vector<Source2ModHTTPHeader>* pHeaders)
    {
        Request(owner, ESource2ModHTTPMethod::Patch, pszUrl, pszBody, std::move(callback), pHeaders);
    }

    void HTTPManager::Delete(PluginId owner, const char* pszUrl, const char* pszBody, Source2ModHTTPCallback callback,
                             const std::vector<Source2ModHTTPHeader>* pHeaders)
    {
        Request(owner, ESource2ModHTTPMethod::Delete, pszUrl, pszBody, std::move(callback), pHeaders);
    }

    /* =========================
    Lifecycle
    ========================= */

    bool HTTPManager::IsAvailable() const { return s_pSteamHTTP != nullptr; }

    int HTTPManager::PendingCount() const { return static_cast<int>(m_QueuedRequests.size()); }

    void HTTPManager::OnSteamAPIActivated()
    {
        s_pSteamHTTP = SteamGameServerHTTP();

        if (!s_pSteamHTTP)
        {
            FP_ERROR("Steam API activated but its HTTP client is unavailable.");
            return;
        }

        if (m_QueuedRequests.empty())
            return;

        FP_INFO("Steam HTTP available, sending {} queued request(s).", m_QueuedRequests.size());

        // Moved out first: a callback firing synchronously on a send failure is
        // free to queue another request.
        std::vector<QueuedRequest> queued;
        queued.swap(m_QueuedRequests);

        for (auto& request : queued)
        {
            Send(request.owner, request.method, request.url.c_str(),
                 request.hasBody ? request.body.c_str() : nullptr, request.hasBody,
                 std::move(request.callback), &request.headers);
        }
    }

    void HTTPManager::OnSteamAPIDeactivated()
    {
        s_pSteamHTTP = nullptr;
    }

    void HTTPManager::RemoveAllForPlugin(PluginId id)
    {
        std::erase_if(m_QueuedRequests, [id](const QueuedRequest& r) { return r.owner == id; });

        // Cancel() unregisters and deletes, so the list is walked over a copy
        // of the entries that are going.
        std::vector<TrackedRequest*> going;

        for (auto* request : m_TrackedRequests)
        {
            if (request->Owner() == id)
                going.push_back(request);
        }

        for (auto* request : going)
        {
            std::erase(m_TrackedRequests, request);
            request->Cancel();
        }
    }

    void HTTPManager::Shutdown()
    {
        m_QueuedRequests.clear();

        auto tracked = std::move(m_TrackedRequests);
        m_TrackedRequests.clear();

        for (auto* request : tracked)
            request->Cancel();

        s_pSteamHTTP = nullptr;
    }
}
