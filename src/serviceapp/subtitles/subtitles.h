#ifndef __serviceapp_subtitles_h
#define __serviceapp_subtitles_h

#include <map>
#include <memory>
#include <vector>
#include <iostream>
#include <stdint.h>

enum
{
    PROB_SCORE_MIN = 0,
    PROB_SCORE_MAX = 100
};

struct subtitleStream
{
    int id;
    std::string language_code; /* iso-639, if available. */
    std::string description; /* clear text codec description */
    std::string path; /* path to external subtitle */
    int forced; /* 1 for a forced track */
    std::string title; /* track title ("SDH", "Forced", "CC1"), if the player sends it */
    subtitleStream(): id(-1), forced(0){};
};

/* one picture of a graphic subtitle (PGS, DVB, XSUB): a PNG file exteplayer3
 * wrote into its -G directory, position and size in the -W x -H window */
struct subtitleImage
{
    int x, y, w, h;
    std::string file; /* full path */
    subtitleImage(): x(0), y(0), w(0), h(0){};
};

struct subtitleMessage
{
    uint32_t start_ms;
    uint32_t duration_ms;
    uint32_t end_ms;
    std::string text;
    bool graphic; /* a graphic page: images instead of text, none = clear the screen */
    std::vector<subtitleImage> images;
    subtitleMessage(): start_ms(0), duration_ms(0), end_ms(0), graphic(false){};
};

// endtime, message
typedef std::map<uint32_t, subtitleMessage> subtitleMap;

class BaseSubtitleParser
{
protected:
    virtual int _probe(std::istream &is) = 0;
    virtual bool _parse(std::istream &is, int fps, subtitleMap &map) = 0;
public:
    virtual ~BaseSubtitleParser(){}
    virtual std::string name() = 0;
    unsigned int probe(std::istream &is);
    bool parse(std::istream &is, int fps, subtitleMap &map);
};

class SubtitleParser
{
    std::vector<std::unique_ptr<BaseSubtitleParser> > m_parser_vec;
    void initParserList();
    void cleanParserList();
public:
    SubtitleParser(){initParserList();}
    ~SubtitleParser(){cleanParserList();}
    bool parse(std::istream &is, int fps, subtitleMap &submap);
};


class SubtitleManager
{
    SubtitleParser m_parser;
    bool m_convert_to_utf8;
    // path -> videofps,subtitlefps
    typedef std::pair<std::string, std::pair<int, int> > subtitleId;
    typedef std::map<subtitleId, subtitleMap> subtitles;
    std::multimap<subtitleId, subtitleMap> m_loaded_subtitles;
public:
    const subtitleMap *load(const std::string &filepath, int video_fps=-1, int subtitle_fps=-1, bool force_reload=false);

    SubtitleManager():m_convert_to_utf8(true){};
    ~SubtitleManager(){};
};

#endif

