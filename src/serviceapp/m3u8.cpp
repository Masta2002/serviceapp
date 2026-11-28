#include "m3u8.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <strings.h>
#include <openssl/evp.h>

#define M3U8_HEADER "#EXTM3U"
#define M3U8_HEADER_MAX_LINE 5

#define M3U8_STREAM_INFO "#EXT-X-STREAM-INF"
#define M3U8_MEDIA_SEQUENCE "#EXT-X-MEDIA-SEQUENCE"

// "scheme://": a relative reference such as "https_720p.m3u8" starts with
// "http" as well, so the prefix alone does not make a URL absolute
static bool hasScheme(const std::string& ref)
{
    size_t schemeEnd = ref.find("://");
    if (schemeEnd == std::string::npos || schemeEnd == 0 || !isalpha((unsigned char)ref[0]))
        return false;
    for (size_t i = 1; i < schemeEnd; i++)
    {
        unsigned char c = ref[i];
        if (!isalnum(c) && c != '+' && c != '-' && c != '.')
            return false;
    }
    return true;
}

// a variant URL relative to the master playlist, its dot segments resolved
static std::string resolveUrl(const std::string& base, const std::string& ref)
{
    if (hasScheme(ref))
        return ref;
    size_t schemeEnd = base.find("://");
    if (schemeEnd == std::string::npos)
        return ref;
    if (!ref.compare(0, 2, "//"))
        return base.substr(0, schemeEnd + 1) + ref;
    size_t pathStart = base.find('/', schemeEnd + 3);
    std::string origin = base.substr(0, pathStart);
    std::string path;
    if (!ref.empty() && ref[0] == '/')
        path = ref;
    else
    {
        std::string basePath = pathStart == std::string::npos ? "/" : base.substr(pathStart);
        basePath = basePath.substr(0, basePath.find_first_of("?#"));
        path = basePath.substr(0, basePath.rfind('/') + 1) + ref;
    }
    size_t queryStart = path.find_first_of("?#");
    std::string query = queryStart == std::string::npos ? "" : path.substr(queryStart);
    path = path.substr(0, queryStart);

    std::vector<std::string> segments;
    std::istringstream parts(path.substr(1));
    std::string part;
    bool directory = path.size() > 1 && path[path.size() - 1] == '/';
    while (std::getline(parts, part, '/'))
    {
        directory = part == "." || part == "..";
        if (part == "..")
        {
            if (!segments.empty())
                segments.pop_back();
        }
        else if (part != ".")
            segments.push_back(part);
    }
    std::string resolved;
    for (size_t i = 0; i < segments.size(); i++)
        resolved += "/" + segments[i];
    if (directory || resolved.empty())
        resolved += "/";
    return origin + resolved + query;
}

bool isM3U8Url(const std::string &url)
{
    Url purl(url);
    std::string path = purl.path();
    size_t delim_idx = path.rfind(".");
    if((purl.proto() == "http" || purl.proto() == "https")
            && delim_idx != std::string::npos
            && !path.compare(delim_idx, 5, ".m3u8"))
        return true;
    return false;
}

int parse_attribute(char **ptr, char **key, char **value)
{
    if (ptr == NULL || *ptr == NULL || key == NULL || value == NULL)
        return -1;

    char *end;
    char *p;
    p = end = strchr(*ptr, ',');
    if (end)
    {
        char *q = strchr(*ptr, '"');
        if (q && q < end)
        {
            q = strchr(++q, '"');
            if (q)
            {
                p = end = strchr(++q, ',');
            }
        }
    }
    if (end)
    {
        do
        {
            end++;
        }
        while(*end && *end == ' ');
        *p = '\0';
    }

    *key = *ptr;
    p = strchr(*ptr, '=');
    if (!p)
        return -1;
    *p++ = '\0';
    *value = p;
    *ptr = end;
    return 0;
}

//https://tools.ietf.org/html/draft-pantos-http-live-streaming-13#section-3.4.10
int M3U8VariantsExplorer::parseStreamInfoAttributes(const char *attributes, M3U8StreamInfo& info)
{
    std::string myline(attributes);
    char *ptr = &myline[0];
    char *key = NULL;
    char *value = NULL;
    while (!parse_attribute(&ptr, &key, &value))
    {
        if (!strcasecmp(key, "bandwidth"))
            info.bitrate = atoi(value);
        if (!strcasecmp(key, "resolution"))
            info.resolution = value;
        if (!strcasecmp(key, "codecs"))
            info.codecs = value;
    }
    return 0;
}

static std::string trimmed(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

static int defaultPort(Url& purl)
{
    if (purl.port() > 0)
        return purl.port();
    if (purl.proto() == "http")
        return 80;
    if (purl.proto() == "https")
        return 443;
    return -1;
}

// cookies and credentials were given for the host of the master URL: a
// redirect to another host must not receive them (as curl does)
static bool sameHost(const std::string& from, const std::string& to)
{
    Url a(from), b(to);
    return !strcasecmp(a.host().c_str(), b.host().c_str()) && defaultPort(a) == defaultPort(b);
}

// Set-Cookie: only name=value goes back to the server - its attributes
// (Path, Expires, HttpOnly ...) are not cookies. A cookie set again (in a
// redirect chain) replaces the old value instead of being sent twice.
static void addCookie(HeaderMap& headers, const std::string& setCookie)
{
    std::string pair = trimmed(setCookie.substr(0, setCookie.find(';')));
    size_t eq = pair.find('=');
    if (eq == std::string::npos || eq == 0)
        return;
    std::string name = trimmed(pair.substr(0, eq));
    std::string cookies;
    HeaderMap::const_iterator it = headers.find("Cookie");
    if (it != headers.end())
    {
        std::istringstream parts(it->second);
        std::string part;
        while (std::getline(parts, part, ';'))
        {
            part = trimmed(part);
            if (part.empty() || trimmed(part.substr(0, part.find('='))) == name)
                continue;
            cookies.append(cookies.empty() ? "" : "; ").append(part);
        }
    }
    cookies.append(cookies.empty() ? "" : "; ").append(pair);
    headers["Cookie"] = cookies;
}

static std::string basicAuth(const std::string& userinfo)
{
    std::string credentials;
    if (!urlUnquote(userinfo, credentials, false))
        credentials = userinfo;
    std::vector<unsigned char> encoded(4 * ((credentials.size() + 2) / 3) + 1);
    EVP_EncodeBlock(encoded.data(), (const unsigned char *)credentials.data(), (int)credentials.size());
    return std::string("Basic ") + (const char *)encoded.data();
}

namespace {

// One HTTP connection of the explorer: a non-blocking socket driven with
// select() against the deadline of the whole fetch, read in blocks into a
// buffer (instead of one syscall and a fresh timeout per byte) and capped
// in line length and response size.
class HttpConnection
{
    int m_sd;
    SSL *m_ssl;
    SSL_CTX *m_ctx;
    const long long m_deadline;
    char m_buffer[16384];
    size_t m_pos;
    size_t m_len;
    size_t m_total;
    // bytes left until Content-Length, -1 = up to the end of the stream
    long long m_remaining;
    bool m_eof;
public:
    explicit HttpConnection(long long deadline):
        m_sd(-1), m_ssl(NULL), m_ctx(NULL), m_deadline(deadline), m_buffer(),
        m_pos(0), m_len(0), m_total(0), m_remaining(-1), m_eof(false) {}
    HttpConnection(const HttpConnection&) = delete;
    HttpConnection& operator=(const HttpConnection&) = delete;
    ~HttpConnection()
    {
        if (m_ssl)
        {
            SSL_free(m_ssl);
            SSL_CTX_free(m_ctx);
        }
        if (m_sd >= 0)
            ::close(m_sd);
    }

    int open(Url& purl, int port)
    {
        if ((m_sd = Connect(purl.host().c_str(), port, m_deadline)) < 0)
        {
            fprintf(stderr, "[%s] - Error in Connect\n", __func__);
            return -1;
        }
        if (purl.proto() != "https")
            return 0;
        if (SSLConnect(purl.host().c_str(), m_sd, &m_ssl, &m_ctx, m_deadline) < 0)
        {
            // SSLConnect has freed both already
            m_ssl = NULL;
            m_ctx = NULL;
            return -1;
        }
        fprintf(stderr, "[%s] - (SSL) Connected with %s encryption\n",
                __func__, SSL_get_cipher(m_ssl));

        // just inform about verification error but continue to work
        if (SSL_get_verify_result(m_ssl) != X509_V_OK)
        {
            fprintf(stderr, "[%s] - (SSL) Error in certificate verification: %s\n",
                    __func__, X509_verify_cert_error_string(SSL_get_verify_result(m_ssl)));
        }
        return 0;
    }

    int send(const std::string& data)
    {
        ssize_t written = writeAll(m_ssl, m_sd, data.c_str(), data.length(), m_deadline);
        if (written < (ssize_t) data.length())
        {
            fprintf(stderr, "[%s] - writeAll, %s\n", __func__, written == -2 ? "timed out" : "didn't write everything");
            return -1;
        }
        return 0;
    }

    // the body ends after this many bytes from here on: the last line may
    // come without a newline, and nothing after the body is read
    void setBodyLength(long long length)
    {
        m_remaining = length;
    }

    // 1 = a line (without CR/LF), 0 = end of the response, -1 = error,
    // timeout or a limit exceeded (logged)
    int readLine(std::string& line)
    {
        line.clear();
        bool gotData = false;
        while (1)
        {
            if (m_remaining == 0)
                return gotData ? 1 : 0;
            if (m_pos == m_len)
            {
                if (m_eof)
                    return gotData ? 1 : 0;
                if (m_total >= M3U8_MAX_RESPONSE)
                {
                    fprintf(stderr, "[%s] - response larger than %zu bytes\n", __func__, M3U8_MAX_RESPONSE);
                    return -1;
                }
                ssize_t result = readSome(m_ssl, m_sd, m_buffer, sizeof(m_buffer), m_deadline);
                if (result == -2)
                {
                    fprintf(stderr, "[%s] - timed out (%d ms for the whole fetch)\n", __func__, M3U8_FETCH_TIMEOUT_MS);
                    return -1;
                }
                if (result < 0)
                    return -1;
                if (result == 0)
                {
                    m_eof = true;
                    continue;
                }
                m_pos = 0;
                m_len = result;
                m_total += result;
            }
            size_t available = m_len - m_pos;
            if (m_remaining > 0 && (long long) available > m_remaining)
                available = m_remaining;
            const char *start = m_buffer + m_pos;
            const char *newline = (const char *) memchr(start, '\n', available);
            size_t consumed = newline ? (size_t)(newline - start) + 1 : available;
            line.append(start, newline ? consumed - 1 : consumed);
            m_pos += consumed;
            // every byte counts against Content-Length, the CR and LF too
            if (m_remaining > 0)
                m_remaining -= consumed;
            gotData = true;
            if (line.size() > M3U8_MAX_LINE)
            {
                fprintf(stderr, "[%s] - line longer than %zu bytes\n", __func__, M3U8_MAX_LINE);
                return -1;
            }
            if (newline)
            {
                if (!line.empty() && line[line.size() - 1] == '\r')
                    line.erase(line.size() - 1);
                return 1;
            }
        }
    }
};

}

// -1 = error, 0 = the playlist was read, 1 = redirect to location
int M3U8VariantsExplorer::fetchMasterUrl(const std::string& url, HeaderMap& headers, long long deadlineMs, std::string& location)
{
    Url purl(url);

    int port = defaultPort(purl);
    if (port < 0)
    {
        fprintf(stderr, "[%s] - not defined port!\n", __func__);
        return -1;
    }
    HttpConnection connection(deadlineMs);
    if (connection.open(purl, port) < 0)
        return -1;

    std::string userAgent = "HbbTV/1.1.1 (+PVR+RTSP+DL; Sonic; TV44; 1.32.455; 2.002) Bee/3.5";
    HeaderMap::const_iterator it;
    if ((it = headers.find("User-Agent")) != headers.end())
    {
        userAgent = it->second;
    }
    headers["User-Agent"] = userAgent;

    std::string path = purl.path();
    std::string query = purl.query();
    if (!query.empty())
        path += "?" + query;
    // an IPv6 literal goes into the Host header in brackets
    std::string host = purl.host();
    if (host.find(':') != std::string::npos)
        host = "[" + host + "]";
    // HTTP/1.0: the response then comes without chunked transfer encoding,
    // which this reader does not decode (chunk sizes became variant URLs)
    std::string request = "GET ";
    request.append(path).append(" HTTP/1.0\r\n");
    request.append("Host: ").append(host);
    if (purl.port() > 0)
    {
        request.append(":").append(std::to_string(purl.port()));
    }
    request.append("\r\n");
    request.append("User-Agent: ").append(userAgent).append("\r\n");
    request.append("Accept: */*\r\n");
    if (!purl.userinfo().empty() && headers.find("Authorization") == headers.end())
    {
        request.append("Authorization: ").append(basicAuth(purl.userinfo())).append("\r\n");
    }
    for (HeaderMap::const_iterator it(headers.begin()); it != headers.end(); it++)
    {
        if ((it->first).compare("User-Agent"))
        {
            request.append(it->first + ": ").append(it->second).append("\r\n");
        }
    }
    request.append("Connection: close\r\n");
    request.append("\r\n");

    fprintf(stderr, "[%s] - Request:\n", __func__);
    fprintf(stderr, "%s\n", request.c_str());

    if (connection.send(request) < 0)
        return -1;

    int lines = 0;
    std::string line;
    int result = connection.readLine(line);
    fprintf(stderr, "[%s] Response[%d](size=%d): %s\n", __func__, lines++, result > 0 ? (int)line.size() : result, line.c_str());
    if (result <= 0)
    {
        fprintf(stderr, "[%s] - no response\n", __func__);
        return -1;
    }
    int statusCode = 0;
    char protocol[64] = "";
    // the reason phrase is optional ("HTTP/1.1 200")
    bool redirect = false;
    if (sscanf(line.c_str(), "%63s %d", protocol, &statusCode) >= 2 && !strncmp(protocol, "HTTP/", 5))
        redirect = statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308;
    else
        statusCode = 0;
    if (statusCode != 200 && !redirect)
    {
        fprintf(stderr, "[%s] - wrong http response code: %d\n", __func__, statusCode);
        return -1;
    }

    long long contentLength = -1;
    location.clear();
    while (1)
    {
        result = connection.readLine(line);
        fprintf(stderr, "[%s] Response[%d](size=%d): %s\n", __func__, lines++, result > 0 ? (int)line.size() : result, line.c_str());
        if (result <= 0)
        {
            fprintf(stderr, "[%s] - response ended in the headers\n", __func__);
            return -1;
        }
        if (line.empty())
            break;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        // header names are case-insensitive, the value starts after any
        // whitespace following the colon
        std::string name = trimmed(line.substr(0, colon));
        std::string value = trimmed(line.substr(colon + 1));
        if (!strcasecmp(name.c_str(), "Content-Length"))
        {
            char *end = NULL;
            long long length = strtoll(value.c_str(), &end, 10);
            if (end != value.c_str() && *end == '\0' && length >= 0)
                contentLength = length;
        }
        else if (!strcasecmp(name.c_str(), "Content-Type"))
        {
            // not checked: servers send playlists as application/octet-stream,
            // text/html ... - the #EXTM3U check below decides
            fprintf(stderr, "[%s] - content type: %s\n", __func__, value.c_str());
        }
        else if (!strcasecmp(name.c_str(), "Transfer-Encoding") && strcasecmp(value.c_str(), "identity") != 0)
        {
            // not allowed in a response to HTTP/1.0, and not decoded here
            fprintf(stderr, "[%s] - unsupported transfer encoding: %s\n", __func__, value.c_str());
            return -1;
        }
        else if (!strcasecmp(name.c_str(), "Location"))
        {
            location = value;
        }
        else if (!strcasecmp(name.c_str(), "Set-Cookie"))
        {
            addCookie(headers, value);
        }
    }
    if (redirect)
    {
        if (location.empty())
        {
            fprintf(stderr, "[%s] - redirect %d without Location\n", __func__, statusCode);
            return -1;
        }
        return 1;
    }
    fprintf(stderr, "[%s] - content part started\n", __func__);
    if (contentLength >= 0)
    {
        if (contentLength > (long long) M3U8_MAX_RESPONSE)
        {
            fprintf(stderr, "[%s] - Content-Length %lld larger than %zu bytes\n", __func__, contentLength, M3U8_MAX_RESPONSE);
            return -1;
        }
        connection.setBodyLength(contentLength);
    }

    int contentLines = 0;
    bool m3u8HeaderParsed = false;
    bool m3u8StreamInfoParsing = false;
    // audio of its own (EXT-X-MEDIA) is not in the variant playlists
    bool alternativeAudio = false;
    M3U8StreamInfo m3u8StreamInfo;
    while (1)
    {
        result = connection.readLine(line);
        fprintf(stderr, "[%s] Response[%d](size=%d): %s\n", __func__, lines++, result > 0 ? (int)line.size() : result, line.c_str());
        if (result < 0)
        {
            // a cut-off variant list (timeout, limit) is not offered
            streams.clear();
            return -1;
        }
        if (result == 0)
        {
            fprintf(stderr, "[%s] - end of read\n", __func__);
            break;
        }
        contentLines++;
        if (!m3u8HeaderParsed)
        {
            if (contentLines > M3U8_HEADER_MAX_LINE)
            {
                fprintf(stderr, "[%s] - invalid M3U8 playlist, '%s' header is not in first %d lines\n",
                        __func__, M3U8_HEADER, M3U8_HEADER_MAX_LINE);
                break;
            }
            // a UTF-8 byte order mark in front of the header
            if (contentLines == 1 && !line.compare(0, 3, "\xEF\xBB\xBF"))
                line.erase(0, 3);

            // find M3U8 header
            if (!line.compare(0, strlen(M3U8_HEADER), M3U8_HEADER))
            {
                m3u8HeaderParsed = true;
            }
            continue;
        }

        if (!line.compare(0, strlen(M3U8_MEDIA_SEQUENCE), M3U8_MEDIA_SEQUENCE))
        {
            fprintf(stderr, "[%s] - we need master playlist not media sequence!\n", __func__);
            break;
        }

        if (m3u8StreamInfoParsing)
        {
            // there shouldn't be any empty line
            if (line.empty())
            {
                m3u8StreamInfoParsing = false;
                continue;
            }

            fprintf(stderr, "[%s] - continue parsing m3u8 stream info\n", __func__);
            m3u8StreamInfo.url = resolveUrl(url, trimmed(line));
            m3u8StreamInfo.headers = headers;
            streams.push_back(m3u8StreamInfo);
            m3u8StreamInfoParsing = false;
        }
        else
        {
            if (!line.compare(0, strlen(M3U8_STREAM_INFO), M3U8_STREAM_INFO))
            {
                m3u8StreamInfoParsing = true;
                m3u8StreamInfo = M3U8StreamInfo(); // no attributes of the previous variant
                // a bare "#EXT-X-STREAM-INF" without ":attrs" makes substr(18)
                // throw std::out_of_range, which aborts enigma2
                parseStreamInfoAttributes(line.size() > 18 ? line.substr(18).c_str() : "", m3u8StreamInfo);
            }
            else if (!line.compare(0, 13, "#EXT-X-MEDIA:") && line.find("TYPE=AUDIO") != std::string::npos
                    && line.find("URI=") != std::string::npos)
            {
                alternativeAudio = true;
            }
            else
            {
                fprintf(stderr, "[%s] - skipping unrecognised data\n", __func__);
            }
        }
    }
    // a single variant would play without that audio: the player gets the
    // master playlist and its audio tracks
    if (alternativeAudio && !streams.empty())
    {
        fprintf(stderr, "[%s] - audio tracks of their own, playing the master playlist\n", __func__);
        streams.clear();
    }
    return streams.empty() ? -1 : 0;
}

int M3U8VariantsExplorer::getVariantsFromMasterUrl(const std::string& masterUrl, HeaderMap& headers, long long deadlineMs)
{
    // a loop, not recursion: every redirect runs against the same deadline
    std::string currentUrl = masterUrl;
    for (unsigned int redirect = 0; ; redirect++)
    {
        if (redirect > redirectLimit)
        {
            fprintf(stderr, "[%s] - reached maximum number of %u - redirects\n", __func__, redirectLimit);
            return -1;
        }
        std::string location;
        int ret = fetchMasterUrl(currentUrl, headers, deadlineMs, location);
        if (ret != 1)
            return ret;
        // Location may be relative or protocol-relative
        std::string redirectUrl = resolveUrl(currentUrl, location);
        fprintf(stderr, "[%s] - redirecting to: %s\n", __func__, redirectUrl.c_str());
        if (!sameHost(currentUrl, redirectUrl))
        {
            if (headers.erase("Cookie") + headers.erase("Authorization"))
                fprintf(stderr, "[%s] - other host, cookies/authorization not sent there\n", __func__);
        }
        currentUrl = redirectUrl;
    }
}

std::vector<M3U8StreamInfo> M3U8VariantsExplorer::getStreams()
{
    streams.clear();
    long long deadline = monotonicMs() + fetchTimeoutMs;
    std::vector<std::string> masterUrl;
    masterUrl.push_back(url);
    // try also some common master playlist filenames
    // masterUrl.push_back(url.substr(0, url.rfind('/') + 1) + "master.m3u8");
    // masterUrl.push_back(url.substr(0, url.rfind('/') + 1) + "playlist.m3u8");
    for (std::vector<std::string>::const_iterator it(masterUrl.begin()); it != masterUrl.end(); it++)
    {
        int ret = getVariantsFromMasterUrl(*it, headers, deadline);
        if (ret < 0)
            continue;
        break;
    }
    return streams;
}
