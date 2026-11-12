#include "Tone3000.h"
#include "CaptureLibrary.h"
#include <juce_cryptography/juce_cryptography.h>
#include <random>
#if JUCE_WINDOWS
 #include <windows.h>
 #include <winhttp.h>
 #pragma comment (lib, "winhttp.lib")
 #ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
  #define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
 #endif
 #ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
  #define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
 #endif
#endif

namespace t3k
{
namespace
{
    const char* kClientId = "t3k_pub_9-dcfUClV_GUgK5rXdoGN_mcwLmHlNkP";   // cle publique (client_id OAuth), sans valeur secrete
    const juce::String kApi ("https://www.tone3000.com/api/v1");

    juce::File settingsFile() { return CaptureLibrary::settingsDir().getChildFile ("tone3000.xml"); }

    juce::String str (const juce::var& v) { return v.isVoid() || v.isUndefined() ? juce::String() : v.toString(); }

    juce::String randomString (int n)
    {
        static const char* cs = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
        std::random_device rd;
        juce::String s;
        for (int i = 0; i < n; ++i) s << cs[rd() % 66];
        return s;
    }

    juce::String base64Url (const void* data, size_t size)
    {
        return juce::Base64::toBase64 (data, size).replace ("+", "-").replace ("/", "_").trimCharactersAtEnd ("=");
    }

    juce::String nowIso() { return juce::Time::getCurrentTime().toISO8601 (true); }

    juce::String lastHttpError;     // derniere erreur HTTP (affichee a l'utilisateur et ecrite dans tone3000.log)

    void logLine (const juce::String& s)
    {
        const auto f = CaptureLibrary::settingsDir().getChildFile ("tone3000.log");
        if (f.getSize() > 200000) f.deleteFile();
        f.appendText (juce::Time::getCurrentTime().toISO8601 (true) + "  " + s + "\n");
    }

    // ---- requete HTTP(S) : WinHTTP sous Windows (erreurs detaillees), pile reseau de JUCE ailleurs.
    //      La reponse est ecrite dans « out » ; renvoie false si aucune reponse (err explique pourquoi).
    bool httpRequest (const juce::String& method, const juce::String& address, const juce::String& headers,
                      const juce::MemoryBlock& body, int& status, juce::OutputStream& out, juce::String& err,
                      const std::atomic<bool>* cancel = nullptr)
    {
        status = 0;
       #if JUCE_WINDOWS
        URL_COMPONENTS uc {};
        uc.dwStructSize = sizeof (uc);
        uc.dwHostNameLength = (DWORD) -1; uc.dwUrlPathLength = (DWORD) -1; uc.dwExtraInfoLength = (DWORD) -1; uc.dwSchemeLength = (DWORD) -1;
        const std::wstring wurl (address.toWideCharPointer());
        if (! WinHttpCrackUrl (wurl.c_str(), 0, 0, &uc)) { err = "bad address (" + juce::String ((int) GetLastError()) + ")"; return false; }
        const std::wstring host (uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path (uc.lpszUrlPath, uc.dwUrlPathLength);
        if (uc.lpszExtraInfo != nullptr) path += std::wstring (uc.lpszExtraInfo, uc.dwExtraInfoLength);
        HINTERNET s = WinHttpOpen (L"Ampire/" JucePlugin_VersionString, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (s == nullptr) s = WinHttpOpen (L"Ampire/" JucePlugin_VersionString, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (s == nullptr) { err = "WinHTTP unavailable (" + juce::String ((int) GetLastError()) + ")"; return false; }
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (! WinHttpSetOption (s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof (protocols)))
        {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption (s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof (protocols));
        }
        WinHttpSetTimeouts (s, 15000, 20000, 30000, 60000);
        HINTERNET c = WinHttpConnect (s, host.c_str(), uc.nPort, 0);
        HINTERNET r = c != nullptr ? WinHttpOpenRequest (c, method.toWideCharPointer(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                         WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                                   : nullptr;
        bool ok = r != nullptr;
        if (ok)
        {
            const std::wstring wh (headers.toWideCharPointer());
            ok = WinHttpSendRequest (r, wh.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wh.c_str(), wh.empty() ? 0 : (DWORD) -1L,
                                     body.getSize() > 0 ? const_cast<void*> (body.getData()) : WINHTTP_NO_REQUEST_DATA,
                                     (DWORD) body.getSize(), (DWORD) body.getSize(), 0)
              && WinHttpReceiveResponse (r, nullptr);
        }
        if (! ok) err = "WinHTTP error " + juce::String ((int) GetLastError());
        else
        {
            DWORD code = 0, sz = sizeof (code);
            WinHttpQueryHeaders (r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            status = (int) code;
            juce::HeapBlock<char> buf (65536);
            for (;;)
            {
                if (cancel != nullptr && cancel->load()) break;
                DWORD rd = 0;
                if (! WinHttpReadData (r, buf.getData(), 65536, &rd) || rd == 0) break;
                if (! out.write (buf.getData(), rd)) { ok = false; err = "write error"; break; }
            }
        }
        if (r != nullptr) WinHttpCloseHandle (r);
        if (c != nullptr) WinHttpCloseHandle (c);
        WinHttpCloseHandle (s);
        return ok;
       #else
        juce::URL u (address);
        if (body.getSize() > 0) u = u.withPOSTData (body);
        const auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                              .withExtraHeaders (headers)
                              .withConnectionTimeoutMs (20000)
                              .withStatusCode (&status)
                              .withHttpRequestCmd (method);
        auto in = u.createInputStream (opts);
        if (in == nullptr) { err = "no connection"; return false; }
        juce::HeapBlock<char> buf (65536);
        for (;;)
        {
            if (cancel != nullptr && cancel->load()) break;
            const int n = in->read (buf.getData(), 65536);
            if (n <= 0) break;
            if (! out.write (buf.getData(), (size_t) n)) { err = "write error"; return false; }
        }
        if (status == 0) status = 200;
        return true;
       #endif
    }

    // requete JSON (GET, ou POST de formulaire si form != nullptr)
    juce::var httpJson (const juce::String& address, const juce::String& bearer, int& status, const juce::StringPairArray* form = nullptr)
    {
        juce::String headers = "Accept: application/json\r\n";
        if (bearer.isNotEmpty()) headers << "Authorization: Bearer " << bearer << "\r\n";
        juce::MemoryBlock body;
        if (form != nullptr)
        {
            juce::String b;
            for (auto& k : form->getAllKeys())
                b << (b.isEmpty() ? "" : "&") << juce::URL::addEscapeChars (k, true) << "=" << juce::URL::addEscapeChars ((*form)[k], true);
            body.append (b.toRawUTF8(), b.getNumBytesAsUTF8());
            headers << "Content-Type: application/x-www-form-urlencoded\r\n";
        }
        juce::MemoryOutputStream mo;
        juce::String e;
        if (! httpRequest (form != nullptr ? "POST" : "GET", address, headers, body, status, mo, e))
        {
            lastHttpError = e;
            logLine (address.upToFirstOccurrenceOf ("?", false, false) + " : " + e);
            status = 0;
            return {};
        }
        const auto text = mo.toString();
        auto v = juce::JSON::parse (text);
        const bool err = status < 200 || status >= 300 || v.isVoid() || v.hasProperty ("error");
        if (err)
        {
            lastHttpError = "HTTP " + juce::String (status);
            if (v.isObject()) lastHttpError << ": " << v["error"].toString() << (v["error_description"].toString().isNotEmpty() ? " - " + v["error_description"].toString() : juce::String());
            else lastHttpError << ": " << text.substring (0, 80).replaceCharacters ("\r\n", "  ");
            if (status >= 200 && status < 300) status = 502;
            logLine (address.upToFirstOccurrenceOf ("?", false, false) + " : " + lastHttpError);
        }
        return v;
    }

    juce::String archOf (const juce::var& m) { return str (m["architecture_version"]); }
    int archRank (const juce::String& a) { return a == "2" ? 3 : a == "1" ? 2 : a == "custom" ? 1 : 0; }

    juce::String extensionFor (const juce::var& m)
    {
        auto u = str (m["model_url"]).upToFirstOccurrenceOf ("?", false, false);
        auto ext = u.fromLastOccurrenceOf (".", false, false).toLowerCase();
        if (ext == "nam" || ext == "wav") return "." + ext;
        return archOf (m).isEmpty() ? ".wav" : ".nam";
    }

    juce::File uniqueIn (const juce::File& dir, const juce::String& name)
    {
        auto f = dir.getChildFile (name);
        if (! f.exists()) return f;
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S");
        return dir.getChildFile (f.getFileNameWithoutExtension() + " (" + stamp + ")" + f.getFileExtension());
    }
}

// ============================================================================ Tone
Tone Tone::fromVar (const juce::var& v)
{
    Tone t;
    t.id = str (v["id"]);
    t.title = str (v["title"]);
    t.gear = str (v["gear"]);
    t.format = str (v["format"]);
    if (t.format.isEmpty()) t.format = str (v["platform"]);
    t.license = str (v["license"]);
    t.url = str (v["url"]);
    if (t.url.isEmpty() && t.id.isNotEmpty()) t.url = "https://www.tone3000.com/tones/" + t.id;
    t.updatedAt = str (v["updated_at"]);
    if (auto* imgs = v["images"].getArray()) if (! imgs->isEmpty()) t.image = str ((*imgs)[0]);
    const auto& u = v["user"];
    t.user = str (u["display_name"]);
    if (t.user.isEmpty()) t.user = str (u["username"]);
    t.avatar = str (u["avatar_url"]);
    t.a1 = (int) v["a1_models_count"]; t.a2 = (int) v["a2_models_count"];
    t.irs = (int) v["irs_count"];      t.models = (int) v["models_count"];
    return t;
}

juce::String Tone::gearLabel() const
{
    if (gear == "amp-cab" || gear == "full-rig") return "Amp + Cab";
    if (gear == "amp") return "Amp";
    if (gear == "pedal") return "Pedal";
    if (gear == "cab" || gear == "ir") return "Cabinet";
    if (gear == "outboard") return "Outboard";
    if (gear == "space") return "Space";
    if (gear == "experimental") return "Experimental";
    return gear.isNotEmpty() ? gear.substring (0, 1).toUpperCase() + gear.substring (1) : juce::String ("Tone");
}

juce::String Tone::formatBadge() const
{
    if (format == "ir" || gear == "ir") return "IR";
    if (a2 > 0) return "A2";
    if (format == "nam" || format.isEmpty()) return "A1";
    return format.toUpperCase().substring (0, 4);
}

juce::String Tone::category() const
{
    if (format == "ir" || gear == "cab" || gear == "ir") return "Cabinets_IRs";
    if (gear == "amp-cab" || gear == "full-rig") return "Amp_and_Cab";
    if (gear == "amp") return "Amps";
    if (gear == "pedal") return "Pedals";
    if (gear == "space") return "Spaces";
    if (gear == "experimental") return "Experimental";
    if (gear == "outboard") return "Outboard";
    return "Unknown";
}

// ============================================================================ connexion : serveur local de redirection
class Client::Login : public juce::Thread
{
public:
    Login (Client& c, bool sel) : juce::Thread ("TONE3000 sign-in"), client (c), select (sel) {}
    ~Login() override { stop(); }

    bool begin()
    {
        if (! server.createListener (0, "127.0.0.1")) return false;
        port = server.getBoundPort();
        if (port <= 0) return false;
        verifier = randomString (64);
        state = randomString (24);
        juce::SHA256 sha (verifier.toRawUTF8(), (size_t) verifier.getNumBytesAsUTF8());
        const auto digest = sha.getRawData();
        const auto challenge = base64Url (digest.getData(), digest.getSize());
        juce::URL u (kApi + "/oauth/authorize");
        u = u.withParameter ("client_id", kClientId)
             .withParameter ("redirect_uri", redirect())
             .withParameter ("response_type", "code")
             .withParameter ("code_challenge", challenge)
             .withParameter ("code_challenge_method", "S256")
             .withParameter ("state", state);
        if (select)
        {
            u = u.withParameter ("prompt", "select_tone").withParameter ("format", "nam").withParameter ("preview", "true");
            const int vm = client.getVersionMode();
            if (vm == vA1) u = u.withParameter ("architecture", "1");
            else if (vm == vCustom) u = u.withParameter ("architecture", "custom");
            else if (vm != vAll) u = u.withParameter ("architecture", "2");
        }
        startThread();
        u.launchInDefaultBrowser();
        return true;
    }
    void stop()
    {
        signalThreadShouldExit();
        server.close();
        stopThread (4000);
    }
    juce::String redirect() const { return "http://127.0.0.1:" + juce::String (port) + "/callback"; }

private:
    void run() override
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 10 * 60 * 1000;
        while (! threadShouldExit() && juce::Time::getMillisecondCounter() < deadline)
        {
            std::unique_ptr<juce::StreamingSocket> s (server.waitForNextConnection());
            if (s == nullptr || threadShouldExit()) break;
            juce::String req;
            char buf[2048];
            while (! req.contains ("\r\n\r\n") && req.length() < 16000)
            {
                if (s->waitUntilReady (true, 3000) <= 0) break;
                const int n = s->read (buf, (int) sizeof (buf), false);
                if (n <= 0) break;
                req += juce::String::fromUTF8 (buf, n);
            }
            const auto line = req.upToFirstOccurrenceOf ("\r\n", false, false);
            const auto target = line.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
            if (! target.startsWith ("/callback"))
            {
                const char* nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                s->write (nf, (int) strlen (nf));
                continue;
            }
            juce::StringPairArray q;
            for (auto& kv : juce::StringArray::fromTokens (target.fromFirstOccurrenceOf ("?", false, false), "&", ""))
                q.set (juce::URL::removeEscapeChars (kv.upToFirstOccurrenceOf ("=", false, false).replace ("+", " ")),
                       juce::URL::removeEscapeChars (kv.fromFirstOccurrenceOf ("=", false, false).replace ("+", " ")));
            const bool ok = q["code"].isNotEmpty() && q["state"] == state;
            const juce::String html = juce::String ("<!doctype html><html><head><meta charset=\"utf-8\"><title>Ampire</title></head>"
                "<body style=\"background:#13171d;color:#eef1f5;font-family:system-ui,sans-serif;display:flex;align-items:center;"
                "justify-content:center;height:100vh;margin:0\"><div style=\"text-align:center\"><h2>")
                + (ok ? "Ampire is connected to TONE3000" : "TONE3000 sign-in was not completed")
                + "</h2><p style=\"color:#8d96a4\">You can close this tab and go back to Ampire.</p></div></body></html>";
            const auto body = html.toStdString();
            const auto head = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string (body.size())
                              + "\r\nConnection: close\r\n\r\n";
            s->write (head.data(), (int) head.size());
            s->write (body.data(), (int) body.size());
            s->close();
            server.close();

            if (q["canceled"] == "true") { client.loginFinished ("Sign-in canceled", {}); return; }
            if (q["error"].isNotEmpty()) { client.loginFinished ("TONE3000: " + q["error"], {}); return; }
            if (! ok) { client.loginFinished ("Sign-in failed (invalid answer)", {}); return; }
            if (! client.exchangeCode (q["code"], verifier, redirect())) { client.loginFinished ("Could not sign in to TONE3000 (" + lastHttpError + ")", {}); return; }
            client.fetchUser();
            client.loginFinished ({}, q["tone_id"]);
            return;
        }
        if (! threadShouldExit()) client.loginFinished ("Sign-in timed out", {});
    }

    Client& client;
    bool select;
    juce::StreamingSocket server;
    int port = 0;
    juce::String verifier, state;
};

// ============================================================================ Client
Client::Client()
{
    loadSettings();
}

Client::~Client()
{
    quitting = true;
    cancelPendingUpdate();
    if (login != nullptr) login->stop();
    pool.removeAllJobs (true, 20000);
    imagePool.removeAllJobs (true, 10000);
}

bool Client::isSignedIn() const     { const juce::ScopedLock sl (lock); return refresh_.isNotEmpty() || access.isNotEmpty(); }
bool Client::isConnecting() const   { return connecting.load(); }
juce::String Client::getUsername() const  { const juce::ScopedLock sl (lock); return username; }
juce::String Client::getAvatarUrl() const { const juce::ScopedLock sl (lock); return avatarUrl; }
juce::String Client::getAuthError() const { const juce::ScopedLock sl (lock); return authError; }

void Client::signIn (bool selectTone)
{
    if (login != nullptr) login->stop();
    login = std::make_unique<Login> (*this, selectTone);
    { const juce::ScopedLock sl (lock); authError.clear(); }
    connecting = true;
    if (! login->begin())
    {
        connecting = false;
        const juce::ScopedLock sl (lock);
        authError = "Could not open the local sign-in port";
    }
    changed();
}

void Client::cancelSignIn()
{
    if (login != nullptr) login->stop();
    connecting = false;
    changed();
}

void Client::loginFinished (const juce::String& error, const juce::String& toneId)
{
    connecting = false;
    {
        const juce::ScopedLock sl (lock);
        authError = error;
    }
    if (error.isEmpty())
    {
        refresh (favorites);
        if (toneId.isNotEmpty())
        {
            pool.addJob ([this, toneId]
            {
                const auto v = apiGet ("/tones/" + toneId);
                if (v.isObject() && ! quitting)
                {
                    auto t = Tone::fromVar (v);
                    { const juce::ScopedLock sl (lock); selectedTone = t; }
                    selectCount++;
                    changed();
                }
            });
        }
    }
    changed();
}

void Client::signOut()
{
    if (login != nullptr) login->stop();
    {
        const juce::ScopedLock sl (lock);
        access.clear(); refresh_.clear(); username.clear(); avatarUrl.clear(); authError.clear();
        expiresAt = 0;
        for (auto& l : lists) l = {};
    }
    saveSettings();
    changed();
}

// ---------------------------------------------------------------- jetons
bool Client::exchangeCode (const juce::String& code, const juce::String& verifier, const juce::String& redirect)
{
    juce::StringPairArray f;
    f.set ("grant_type", "authorization_code");
    f.set ("code", code);
    f.set ("code_verifier", verifier);
    f.set ("redirect_uri", redirect);
    f.set ("client_id", kClientId);
    int status = 0;
    const auto v = httpJson (kApi + "/oauth/token", {}, status, &f);
    if (str (v["access_token"]).isEmpty()) return false;
    {
        const juce::ScopedLock sl (lock);
        access = str (v["access_token"]);
        refresh_ = str (v["refresh_token"]);
        expiresAt = juce::Time::currentTimeMillis() + (juce::int64) ((int) v["expires_in"]) * 1000;
    }
    saveSettings();
    return true;
}

bool Client::refreshToken()
{
    juce::String rt;
    { const juce::ScopedLock sl (lock); rt = refresh_; }
    if (rt.isEmpty()) return false;
    juce::StringPairArray f;
    f.set ("grant_type", "refresh_token");
    f.set ("refresh_token", rt);
    f.set ("client_id", kClientId);
    int status = 0;
    const auto v = httpJson (kApi + "/oauth/token", {}, status, &f);
    if (str (v["access_token"]).isNotEmpty())
    {
        {
            const juce::ScopedLock sl (lock);
            access = str (v["access_token"]);
            if (str (v["refresh_token"]).isNotEmpty()) refresh_ = str (v["refresh_token"]);
            expiresAt = juce::Time::currentTimeMillis() + (juce::int64) ((int) v["expires_in"]) * 1000;
        }
        saveSettings();
        return true;
    }
    if (status == 400 || status == 401)     // invalid_grant : la session a expire, il faut se reconnecter
    {
        {
            const juce::ScopedLock sl (lock);
            access.clear(); refresh_.clear();
            authError = "Your TONE3000 session has expired: sign in again";
        }
        saveSettings();
        changed();
    }
    return false;
}

juce::String Client::accessToken()
{
    {
        const juce::ScopedLock sl (lock);
        if (access.isNotEmpty() && juce::Time::currentTimeMillis() < expiresAt - 60000) return access;
    }
    refreshToken();
    const juce::ScopedLock sl (lock);
    return access;
}

juce::var Client::apiGet (const juce::String& pathAndQuery, int* statusOut)
{
    int status = 0;
    auto v = httpJson (kApi + pathAndQuery, accessToken(), status);
    if (status == 401 && refreshToken())
        v = httpJson (kApi + pathAndQuery, accessToken(), status);
    if (statusOut != nullptr) *statusOut = status;
    return status >= 200 && status < 300 ? v : juce::var();
}

void Client::fetchUser()
{
    const auto v = apiGet ("/user");
    if (! v.isObject()) return;
    {
        const juce::ScopedLock sl (lock);
        username = str (v["display_name"]);
        if (username.isEmpty()) username = str (v["username"]);
        avatarUrl = str (v["avatar_url"]);
    }
    saveSettings();
    changed();
}

// ---------------------------------------------------------------- listes
ListState Client::getList (Tab t) const
{
    if (t == synced)
    {
        ListState s;
        const juce::ScopedLock sl (lock);
        juce::Array<Local> all;
        for (juce::HashMap<juce::String, Local>::Iterator it (local); it.next();) all.add (it.getValue());
        std::sort (all.begin(), all.end(), [] (const Local& a, const Local& b) { return a.syncedAt > b.syncedAt; });
        for (auto& l : all) s.tones.add (l.tone);
        s.total = s.tones.size();
        s.loaded = true;
        return s;
    }
    const juce::ScopedLock sl (lock);
    return lists[t];
}

void Client::refresh (Tab t)
{
    if (t == synced) { pool.addJob ([this] { scanLocal(); checkUpdates(); }); return; }
    {
        const juce::ScopedLock sl (lock);
        if (lists[t].loading) return;
        lists[t].loading = true;
        lists[t].error.clear();
    }
    changed();
    pool.addJob ([this, t] { fetchPage (t, 1); });
}

void Client::loadMore (Tab t)
{
    int next = 0;
    {
        const juce::ScopedLock sl (lock);
        auto& l = lists[t];
        if (t == synced || t == trending || t == latest || l.loading || ! l.loaded || l.page >= l.totalPages) return;
        l.loading = true;
        next = l.page + 1;
    }
    changed();
    pool.addJob ([this, t, next] { fetchPage (t, next); });
}

void Client::fetchPage (Tab t, int page)
{
    juce::String path;
    switch (t)
    {
        case favorites: path = "/tones/favorited"; break;
        case created:   path = "/tones/created"; break;
        case trending:  path = "/tones/trending"; break;
        case latest:    path = "/tones/latest"; break;
        default: return;
    }
    if (t == favorites || t == created) path << "?page=" << page << "&page_size=25";
    int status = 0;
    const auto v = apiGet (path, &status);
    juce::Array<Tone> got;
    int totalPages = 1, total = 0;
    const juce::var* arr = nullptr;
    if (v.isArray()) arr = &v;
    else if (v["data"].isArray()) { arr = &v["data"]; totalPages = juce::jmax (1, (int) v["total_pages"]); total = (int) v["total"]; }
    if (arr != nullptr) for (auto& e : *arr->getArray()) got.add (Tone::fromVar (e));
    {
        const juce::ScopedLock sl (lock);
        auto& l = lists[t];
        l.loading = false;
        if (arr == nullptr)
        {
            l.error = status == 0 ? "TONE3000 is not reachable: check your connection" : "TONE3000 answered with an error (" + juce::String (status) + ")";
        }
        else
        {
            if (page <= 1) l.tones.clearQuick();
            l.tones.addArray (got);
            l.page = page; l.totalPages = totalPages;
            l.total = total > 0 ? total : l.tones.size();
            l.loaded = true; l.error.clear();
            for (auto& g : got) remoteDates.set (g.id, g.updatedAt);
        }
    }
    changed();
}

// ---------------------------------------------------------------- tones synchronises
void Client::setRoot (const juce::File& dir)
{
    if (! dir.isDirectory()) return;       // une fenetre sans dossier ne vide pas la liste des autres
    {
        const juce::ScopedLock sl (lock);
        if (dir == root) return;
        root = dir;
    }
    pool.addJob ([this] { scanLocal(); });
}

juce::File Client::getRoot() const { const juce::ScopedLock sl (lock); return root; }

bool Client::findLocal (const juce::String& id, Local& out) const
{
    const juce::ScopedLock sl (lock);
    if (! local.contains (id)) return false;
    out = local[id];
    return true;
}

juce::String Client::remoteUpdatedAt (const juce::String& id) const
{
    const juce::ScopedLock sl (lock);
    return remoteDates.contains (id) ? remoteDates[id] : juce::String();
}

void Client::scanLocal()
{
    if (demo) return;
    const auto dir = getRoot();
    juce::HashMap<juce::String, Local> found;
    if (dir.isDirectory())
    {
        for (auto& cat : dir.findChildFiles (juce::File::findDirectories, false))
        {
            if (cat.getFileName().startsWith ("_") || cat.getFileName().startsWith (".")) continue;
            for (auto& f : cat.findChildFiles (juce::File::findDirectories, false))
            {
                const auto meta = f.getChildFile ("metadata.json");
                if (! meta.existsAsFile()) continue;
                const auto v = juce::JSON::parse (meta);
                if (! v.isObject() || str (v["id"]).isEmpty()) continue;
                Local l;
                l.tone = Tone::fromVar (v);
                if (l.tone.updatedAt.isEmpty()) l.tone.updatedAt = str (v["tone_updated_at"]);
                l.folder = f;
                l.syncedAt = str (v["synced_at"]);
                if (l.syncedAt.isEmpty()) l.syncedAt = meta.getLastModificationTime().toISO8601 (true);
                found.set (l.tone.id, l);
            }
        }
    }
    {
        const juce::ScopedLock sl (lock);
        local.swapWith (found);
    }
    changed();
}

void Client::checkUpdates()
{
    if (demo) return;
    if (! isSignedIn()) return;
    juce::StringArray ids;
    {
        const juce::ScopedLock sl (lock);
        for (juce::HashMap<juce::String, Local>::Iterator it (local); it.next();)
            if (! remoteDates.contains (it.getKey())) ids.add (it.getKey());
    }
    int n = 0;
    for (auto& id : ids)
    {
        if (quitting || ++n > 40) break;          // 100 requetes / minute maximum chez TONE3000
        const auto v = apiGet ("/tones/" + id);
        if (v.isObject())
        {
            const juce::ScopedLock sl (lock);
            remoteDates.set (id, str (v["updated_at"]));
        }
        changed();
        juce::Thread::sleep (400);
    }
}

// ---------------------------------------------------------------- synchronisation (rangement NAMMAN)
int Client::getVersionMode() const { const juce::ScopedLock sl (lock); return versionMode; }
void Client::setVersionMode (int m) { { const juce::ScopedLock sl (lock); versionMode = juce::jlimit (0, numVersionModes - 1, m); } saveSettings(); changed(); }

juce::String Client::versionModeName (int m)
{
    switch (m)
    {
        case vA2: return "A2 only";
        case vA1: return "A1 only";
        case vCustom: return "Custom only";
        case vAll: return "All versions";
        default: return "Best";
    }
}

juce::String Client::safeName (const juce::String& s)
{
    juce::String o;
    for (auto c : s)
        o << ((juce::CharacterFunctions::isLetterOrDigit (c) && c < 128) || c == ' ' || c == '_' || c == '-' ? juce::String::charToString (c) : juce::String ("_"));
    o = o.trim();
    return o.isEmpty() ? juce::String ("Tone") : o.substring (0, 120);
}

Job Client::getJob (const juce::String& id) const
{
    const juce::ScopedLock sl (lock);
    return jobs.contains (id) ? jobs[id] : Job();
}

void Client::sync (const Tone& t, const juce::String& folderName)
{
    {
        const juce::ScopedLock sl (lock);
        if (jobs.contains (t.id) && jobs[t.id].running) return;
        Job j; j.running = true;
        jobs.set (t.id, j);
    }
    changed();
    pool.addJob ([this, t, folderName] { runSync (t, folderName); });
}

void Client::runSync (Tone t, juce::String folderName)
{
    auto fail = [this, &t] (const juce::String& e)
    {
        { const juce::ScopedLock sl (lock); Job j; j.failed = true; j.error = e; jobs.set (t.id, j); }
        logLine ("sync " + t.id + " (" + t.title + ") : " + e);
        changed();
    };
    const auto dir = getRoot();
    if (! dir.isDirectory()) { fail ("Choose your capture folder first"); return; }

    // tone a jour + liste des captures
    const auto tv = apiGet ("/tones/" + t.id);
    if (tv.isObject()) t = Tone::fromVar (tv);
    // captures du tone : sans parametre, l'API ne renvoie pas les A2 ; on interroge donc chaque architecture
    // (sans parametre pour les IR et les A1 / custom, puis architecture=2) et on fusionne par id
    juce::Array<juce::var> models;
    juce::StringArray seen;
    bool anyAnswer = false;
    for (auto arch : { "", "2", "1", "custom" })
    {
        for (int page = 1; page <= 20 && ! quitting; ++page)
        {
            int status = 0;
            juce::String q = "/models?tone_id=" + t.id + "&page=" + juce::String (page) + "&page_size=300";
            if (*arch != 0) q << "&architecture=" << arch;
            const auto v = apiGet (q, &status);
            const juce::var* arr = v.isArray() ? &v : (v["data"].isArray() ? &v["data"] : nullptr);
            if (arr == nullptr) break;
            anyAnswer = true;
            for (auto& m : *arr->getArray())
                if (! seen.contains (str (m["id"]))) { seen.add (str (m["id"])); models.add (m); }
            if (v.isArray() || page >= (int) v["total_pages"]) break;
        }
    }
    if (! anyAnswer) { fail ("Could not read this tone from TONE3000"); return; }
    if (models.isEmpty()) { fail ("This tone has no downloadable capture"); return; }

    // versions gardees
    const int mode = getVersionMode();
    juce::Array<juce::var> keep;
    if (mode == vBest)
    {
        juce::HashMap<juce::String, int> best;     // nom -> index
        for (int i = 0; i < models.size(); ++i)
        {
            const auto a = archOf (models[i]);
            if (a.isEmpty()) { keep.add (models[i]); continue; }    // IR : toujours garde
            const auto name = str (models[i]["name"]);
            if (! best.contains (name) || archRank (a) > archRank (archOf (models[best[name]]))) best.set (name, i);
        }
        for (int i = 0; i < models.size(); ++i)
        {
            const auto name = str (models[i]["name"]);
            if (archOf (models[i]).isNotEmpty() && best.contains (name) && best[name] == i) keep.add (models[i]);
        }
    }
    else
    {
        const juce::String want = mode == vA2 ? "2" : mode == vA1 ? "1" : mode == vCustom ? "custom" : "";
        for (auto& m : models)
            if (archOf (m).isEmpty() || want.isEmpty() || archOf (m) == want) keep.add (m);
    }
    if (keep.isEmpty()) { fail ("No capture matches the version setting (" + versionModeName (mode) + ")"); return; }

    // dossier : celui deja utilise par ce tone, sinon <Categorie>/<nom du tone>
    Local old;
    const bool hadLocal = findLocal (t.id, old);
    const auto folder = hadLocal ? old.folder : dir.getChildFile (t.category()).getChildFile (safeName (folderName.trim().isNotEmpty() ? folderName : t.title));
    folder.createDirectory();
    juce::HashMap<juce::String, juce::String> oldDates;    // fichier -> date de la capture deja presente
    if (hadLocal)
        if (auto mv = juce::JSON::parse (folder.getChildFile ("metadata.json")); mv.isObject())
            if (auto* arr = mv["models"].getArray())
                for (auto& m : *arr) oldDates.set (str (m["file"]), str (m["updated_at"]) + "|" + str (m["id"]));

    // noms de fichiers (suffixe d'architecture si deux versions portent le meme nom)
    juce::StringArray names;
    juce::Array<juce::var> outModels;
    for (auto& m : keep)
    {
        auto base = safeName (str (m["name"]).isNotEmpty() ? str (m["name"]) : "model-" + str (m["id"]));
        auto file = base + extensionFor (m);
        if (names.contains (file, true)) file = base + " [" + (archOf (m) == "2" ? "A2" : archOf (m) == "1" ? "A1" : "Custom") + "]" + extensionFor (m);
        names.add (file);
    }

    const auto archiveDir = dir.getChildFile ("_Archive").getChildFile (folder.getParentDirectory().getFileName()).getChildFile (folder.getFileName());
    auto archive = [&] (const juce::File& f)
    {
        archiveDir.createDirectory();
        f.moveFileTo (uniqueIn (archiveDir, f.getFileName()));
    };
    // fichiers qui ne font plus partie du tone : archives
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.nam;*.wav"))
        if (! names.contains (f.getFileName(), true)) archive (f);

    { const juce::ScopedLock sl (lock); Job j; j.running = true; j.total = keep.size(); jobs.set (t.id, j); }
    changed();

    for (int i = 0; i < keep.size(); ++i)
    {
        if (quitting) return;
        const auto& m = keep.getReference (i);
        const auto target = folder.getChildFile (names[i]);
        const auto stamp = str (m["updated_at"]) + "|" + str (m["id"]);
        juce::var entry (new juce::DynamicObject());
        auto* o = entry.getDynamicObject();
        o->setProperty ("id", m["id"]);
        o->setProperty ("name", m["name"]);
        o->setProperty ("file", names[i]);
        o->setProperty ("architecture_version", m["architecture_version"]);
        o->setProperty ("size", m["size"]);
        o->setProperty ("updated_at", m["updated_at"]);
        outModels.add (entry);

        if (! (target.existsAsFile() && oldDates.contains (names[i]) && oldDates[names[i]] == stamp))
        {
            const auto part = folder.getChildFile (".part-" + str (m["id"]));
            bool ok = false;
            for (int attempt = 0; attempt < 2 && ! ok && ! quitting; ++attempt)
            {
                int status = 0;
                const juce::String headers = "Authorization: Bearer " + accessToken() + "\r\n";
                part.deleteFile();
                juce::String e;
                {
                    juce::FileOutputStream os (part);
                    if (os.openedOk() && httpRequest ("GET", str (m["model_url"]), headers, {}, status, os, e, &quitting) && status >= 200 && status < 300)
                    {
                        os.flush();
                        ok = ! os.getStatus().failed() && os.getPosition() > 0;
                    }
                }
                if (! ok) logLine ("download " + str (m["name"]) + " : " + (e.isNotEmpty() ? e : "HTTP " + juce::String (status)));
                if (status == 401) refreshToken();
            }
            if (! ok) { part.deleteFile(); fail ("Download failed: " + str (m["name"])); return; }
            if (target.existsAsFile()) archive (target);         // version remplacee : archivee, jamais effacee
            part.moveFileTo (target);
        }
        { const juce::ScopedLock sl (lock); auto j = jobs[t.id]; j.done = i + 1; jobs.set (t.id, j); }
        changed();
    }

    // metadata.json (champs de l'API TONE3000 + liste des captures, comme NAMMAN)
    juce::var meta (new juce::DynamicObject());
    auto* mo = meta.getDynamicObject();
    if (tv.isObject()) if (auto* src = tv.getDynamicObject()) for (auto& p : src->getProperties()) mo->setProperty (p.name, p.value);
    mo->setProperty ("id", t.id);
    mo->setProperty ("title", t.title);
    mo->setProperty ("gear", t.gear);
    mo->setProperty ("format", t.format);
    mo->setProperty ("license", t.license);
    mo->setProperty ("url", t.url);
    mo->setProperty ("tone_updated_at", t.updatedAt);
    mo->setProperty ("models", outModels);
    mo->setProperty ("version_mode", versionModeName (mode));
    mo->setProperty ("synced_by", "Ampire");
    mo->setProperty ("synced_at", nowIso());
    folder.getChildFile ("metadata.json").replaceWithText (juce::JSON::toString (meta));

    {
        const juce::ScopedLock sl (lock);
        jobs.remove (t.id);
        remoteDates.set (t.id, t.updatedAt);
    }
    scanLocal();
    libraryVersion++;
    changed();
}

// ---------------------------------------------------------------- images
juce::Image Client::getImage (const juce::String& url)
{
    if (url.isEmpty()) return {};
    {
        const juce::ScopedLock sl (lock);
        if (images.contains (url)) return images[url];
        if (pendingImages.contains (url) || failedImages.contains (url)) return {};
        pendingImages.add (url);
    }
    imagePool.addJob ([this, url]
    {
        juce::Image img;
        if (! quitting)
        {
            juce::MemoryOutputStream mo;
            int status = 0;
            juce::String e;
            if (httpRequest ("GET", url, {}, {}, status, mo, e, &quitting) && status >= 200 && status < 300)
            {
                img = juce::ImageFileFormat::loadFrom (mo.getData(), mo.getDataSize());
                if (img.isValid() && img.getWidth() > 640)
                    img = img.rescaled (640, juce::jmax (1, img.getHeight() * 640 / img.getWidth()), juce::Graphics::highResamplingQuality);
            }
        }
        {
            const juce::ScopedLock sl (lock);
            pendingImages.removeString (url);
            if (img.isValid())
            {
                if (images.size() > 300) images.clear();
                images.set (url, img);
            }
            else failedImages.add (url);
        }
        changed();
    });
    return {};
}

// ---------------------------------------------------------------- reglages (jetons dans le dossier de reglages de l'utilisateur)
void Client::loadSettings()
{
    if (auto x = juce::XmlDocument::parse (settingsFile()))
    {
        const juce::ScopedLock sl (lock);
        access = x->getStringAttribute ("access");
        refresh_ = x->getStringAttribute ("refresh");
        expiresAt = (juce::int64) x->getDoubleAttribute ("expires");
        username = x->getStringAttribute ("user");
        avatarUrl = x->getStringAttribute ("avatar");
        versionMode = juce::jlimit (0, numVersionModes - 1, x->getIntAttribute ("versions", vBest));
        askFolderName = x->getBoolAttribute ("askFolderName", true);
    }
}

void Client::saveSettings()
{
    juce::XmlElement x ("Tone3000");
    {
        const juce::ScopedLock sl (lock);
        x.setAttribute ("access", access);
        x.setAttribute ("refresh", refresh_);
        x.setAttribute ("expires", (double) expiresAt);
        x.setAttribute ("user", username);
        x.setAttribute ("avatar", avatarUrl);
        x.setAttribute ("versions", versionMode);
        x.setAttribute ("askFolderName", askFolderName);
    }
    CaptureLibrary::settingsDir().createDirectory();
    x.writeTo (settingsFile());
}

#if AMPIRE_TOOL
void Client::previewDemo (bool signedIn)
{
    demo = true;
    const juce::ScopedLock sl (lock);
    if (! signedIn) { access.clear(); refresh_.clear(); return; }
    access = "demo"; refresh_ = "demo"; expiresAt = juce::Time::currentTimeMillis() + 3600000; username = "studiowiga";
    struct D { const char* id; const char* title; const char* gear; const char* fmt; int a2; const char* user; };
    const D d[] = { { "1", "Fender Super Reverb 1965 - Edge of Breakup - A2", "amp-cab", "nam", 5, "amalgamaudio" },
                    { "2", "Two-Rock SSS - Ultimate Clean", "amp-cab", "nam", 6, "amalgamaudio" },
                    { "3", "EVH 5150III 50W w/ Celestion Raven Fatbox", "amp-cab", "nam", 4, "kaidownguitar" },
                    { "4", "Fortin Modded TS-9 Tube Screamer", "pedal", "nam", 3, "colossaldave" },
                    { "5", "Mesa Oversized 4x12 V30", "cab", "ir", 0, "ampspedalspickups" },
                    { "6", "Marshall JCM800 + V30 1960", "amp-cab", "nam", 8, "akturja" },
                    { "7", "Soldano SLO-100 - Drive", "amp", "nam", 4, "flaviospanker" },
                    { "8", "Boss SD-1 Super Overdrive", "pedal", "nam", 0, "ampspedalspickups" },
                    { "9", "Orange V30 4x12 - XR IR Pack", "cab", "ir", 0, "ampspedalspickups" },
                    { "10", "Two-Rock SSS - Lead", "amp-cab", "nam", 3, "amalgamaudio" } };
    auto& l = lists[favorites];
    l.tones.clear();
    for (auto& x : d)
    {
        Tone t; t.id = x.id; t.title = x.title; t.gear = x.gear; t.format = x.fmt; t.a2 = x.a2; t.a1 = x.a2 == 0 ? 2 : 0; t.user = x.user;
        t.updatedAt = "2026-11-01T10:00:00Z";
        l.tones.add (t);
        remoteDates.set (t.id, t.updatedAt);
    }
    l.loaded = true; l.page = 1; l.totalPages = 2; l.total = 12;
    for (auto* id : { "1", "6", "9", "4" })
    {
        Local lo; lo.tone = l.tones[juce::String (id).getIntValue() - 1]; lo.folder = juce::File::getSpecialLocation (juce::File::tempDirectory);
        if (juce::String (id) == "4") lo.tone.updatedAt = "2026-10-01T10:00:00Z";
        lo.syncedAt = "2026-11-02T10:00:00Z";
        local.set (id, lo);
    }
    Job j; j.running = true; j.total = 5; j.done = 3; jobs.set ("3", j);
}
#endif

// ---------------------------------------------------------------- nom du dossier
bool Client::getAskFolderName() const { const juce::ScopedLock sl (lock); return askFolderName; }
void Client::setAskFolderName (bool b) { { const juce::ScopedLock sl (lock); askFolderName = b; } saveSettings(); changed(); }

Tone Client::takeSelectedTone()
{
    const juce::ScopedLock sl (lock);
    auto t = selectedTone;
    selectedTone = {};
    return t;
}

bool Client::folderTaken (const Tone& t, const juce::String& folderName) const
{
    const auto dir = getRoot();
    Local l;
    const bool have = findLocal (t.id, l);
    const auto target = (have ? l.folder.getParentDirectory() : dir.getChildFile (t.category())).getChildFile (safeName (folderName));
    if (have && target == l.folder) return false;
    return target.exists();
}

bool Client::renameLocal (const juce::String& toneId, const juce::String& newName, juce::String& error)
{
    Local l;
    if (! findLocal (toneId, l)) { error = "This tone is not synced"; return false; }
    if (getJob (toneId).running) { error = "Wait for the download to finish"; return false; }
    const auto target = l.folder.getParentDirectory().getChildFile (safeName (newName));
    if (target == l.folder) return true;
    if (target.exists()) { error = "A folder with this name already exists"; return false; }
    if (! l.folder.moveFileTo (target)) { error = "Could not rename the folder (is a file in use?)"; return false; }
    logLine ("renamed " + l.folder.getFullPathName() + " -> " + target.getFileName());
    scanLocal();
    libraryVersion++;
    changed();
    return true;
}

bool Client::isBusy (const juce::File& f) const
{
    const juce::ScopedLock sl (lock);
    for (juce::HashMap<juce::String, Job>::Iterator it (jobs); it.next();)
    {
        if (! it.getValue().running) continue;
        if (! local.contains (it.getKey())) return f.isDirectory();          // nouveau tone : dossier pas encore connu
        const auto folder = local[it.getKey()].folder;
        if (folder == f || folder.isAChildOf (f) || f.isAChildOf (folder)) return true;
    }
    return false;
}

void Client::refreshLocal() { pool.addJob ([this] { scanLocal(); }); }

// ---------------------------------------------------------------- Re-sync
void Client::setNotice (const juce::String& s)
{
    { const juce::ScopedLock sl (lock); notice = s; noticeUntil = juce::Time::getMillisecondCounter() + 8000; }
    changed();
}

juce::String Client::getNotice() const
{
    const juce::ScopedLock sl (lock);
    return juce::Time::getMillisecondCounter() < noticeUntil ? notice : juce::String();
}

void Client::rescanAll()
{
    { const juce::ScopedLock sl (lock); remoteDates.clear(); }
    pool.addJob ([this]
    {
        scanLocal();
        int n = 0;
        { const juce::ScopedLock sl (lock); n = local.size(); }
        setNotice (juce::String (n) + (n == 1 ? " synced tone found" : " synced tones found") + " in your capture folder");
        checkUpdates();
    });
}

void Client::importFolder (const juce::File& src)
{
    pool.addJob ([this, src]
    {
        const auto dir = getRoot();
        if (! dir.isDirectory()) return;
        if (src == dir || src.isAChildOf (dir)) { scanLocal(); setNotice ("This folder is already in your capture folder: rescanned"); return; }
        int moved = 0, skipped = 0, failed = 0;
        juce::Array<juce::File> metas;
        for (const auto& e : juce::RangedDirectoryIterator (src, true, "metadata.json", juce::File::findFiles))
            if (! e.getFile().getFullPathName().contains ("_Archive")) metas.add (e.getFile());
        for (auto& meta : metas)
        {
            if (quitting) return;
            const auto v = juce::JSON::parse (meta);
            if (! v.isObject() || str (v["id"]).isEmpty()) continue;
            const auto toneDir = meta.getParentDirectory();
            const auto t = Tone::fromVar (v);
            // categorie : dossier parent quand il est sous le dossier choisi (rangement NAMMAN), sinon d'apres le type du tone
            const auto parent = toneDir.getParentDirectory();
            const auto cat = parent != src && parent.isAChildOf (src) ? parent.getFileName() : (parent == src ? t.category() : t.category());
            const auto target = dir.getChildFile (cat).getChildFile (toneDir.getFileName());
            if (target.exists()) { ++skipped; continue; }
            target.getParentDirectory().createDirectory();
            bool ok = toneDir.moveFileTo (target);
            if (! ok && toneDir.copyDirectoryTo (target)) ok = toneDir.moveToTrash() || true;
            if (ok) ++moved; else ++failed;
            logLine ("import " + toneDir.getFullPathName() + " -> " + target.getFullPathName() + (ok ? "" : " : failed"));
        }
        scanLocal();
        libraryVersion++;
        juce::String msg = juce::String (moved) + (moved == 1 ? " tone added" : " tones added");
        if (skipped > 0) msg << ", " << skipped << " already in your capture folder";
        if (failed > 0) msg << ", " << failed << " could not be moved";
        if (metas.isEmpty()) msg = "No synced tone found in this folder";
        setNotice (msg);
        checkUpdates();
    });
}

void Client::updateAll()
{
    juce::Array<Tone> todo;
    {
        const juce::ScopedLock sl (lock);
        for (juce::HashMap<juce::String, Local>::Iterator it (local); it.next();)
        {
            const auto& l = it.getValue();
            const auto remote = remoteDates.contains (it.getKey()) ? remoteDates[it.getKey()] : juce::String();
            if (remote.isNotEmpty() && l.tone.updatedAt.isNotEmpty() && remote > l.tone.updatedAt) todo.add (l.tone);
        }
    }
    for (auto& t : todo) sync (t);
    setNotice (todo.isEmpty() ? juce::String ("All synced tones are up to date") : juce::String (todo.size()) + (todo.size() == 1 ? " tone is being updated" : " tones are being updated"));
}
}
