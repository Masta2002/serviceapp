#ifndef __m3u8variant__h
#define __m3u8variant__h
#include <map>
#include <vector>
#include <string>
#include <sstream>

#include "wrappers.h"
#include "common.h"

struct M3U8StreamInfo
{
    std::string url;
    HeaderMap headers;
    std::string codecs;
    std::string resolution;
    // TODO audio/video/subtitles..
    unsigned long int bitrate;

    M3U8StreamInfo(): bitrate(0) {}

    bool operator<(const M3U8StreamInfo& m) const
    {
        return bitrate < m.bitrate;
    }
};

/* getStreams() runs in the thread of HlsExplorer; the service waits for it
 * before the player starts, so the whole fetch of a master playlist -
 * DNS aside, connect, TLS handshake, request, every redirect and the
 * response - shares this one time budget, and the response is size capped */
#define M3U8_FETCH_TIMEOUT_MS 10000
#define M3U8_MAX_LINE ((size_t) 64 * 1024)
#define M3U8_MAX_RESPONSE ((size_t) 2 * 1024 * 1024)

class M3U8VariantsExplorer
{
    std::string url;
    HeaderMap headers;
    std::vector<M3U8StreamInfo> streams;
    const unsigned int redirectLimit;
    const int fetchTimeoutMs;
    int parseStreamInfoAttributes(const char *line, M3U8StreamInfo& info);
    int getVariantsFromMasterUrl(const std::string& url, HeaderMap& headers, long long deadlineMs);
    int fetchMasterUrl(const std::string& url, HeaderMap& headers, long long deadlineMs, std::string& location);
public:
    M3U8VariantsExplorer(const std::string& url, const HeaderMap& headers):
        url(url),
        headers(headers),
        redirectLimit(5),
        fetchTimeoutMs(M3U8_FETCH_TIMEOUT_MS){};
    std::vector<M3U8StreamInfo> getStreams();

};

bool isM3U8Url(const std::string& url);
#endif

