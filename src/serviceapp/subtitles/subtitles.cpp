#include <cstdlib>
#include <cerrno>
#include <istream>
#include <fstream>
#include <map>

#include "common.h"
#include "subtitles.h"
#include "subrip.h"

static void skipBOM(std::istream& is)
{
    if (is.get() != 0xEF || is.get() != 0xBB || is.get() != 0xBF)
    {
        is.seekg(0, std::ios::beg);
    }
}

/* the whole file is read into memory, converted and parsed on the main
 * thread: a subtitle file is far below this, anything larger is not one */
#define SUBTITLE_MAX_FILE_SIZE (10 * 1024 * 1024)

bool BaseSubtitleParser::parse(std::istream &is, int fps, subtitleMap &submap)
{
    /* a previous probe may have hit the end of the stream: with failbit
     * set, seekg() does nothing and the parser would see no data */
    is.clear();
    is.seekg(0, std::ios::beg);
    skipBOM(is);
    bool res = _parse(is, fps, submap);
    fprintf(stderr,"%s::parse, %s\n", name().c_str(), res ? "success":"failed");
    return res;
}

unsigned int BaseSubtitleParser::probe(std::istream &is)
{
    is.clear();
    is.seekg(0, std::ios::beg);
    skipBOM(is);
    unsigned int res = _probe(is);
    fprintf(stderr,"%s::probe, score = %u\n", name().c_str(), res);
    return res;
}

void SubtitleParser::initParserList()
{
    m_parser_vec.push_back(std::make_unique<SubripParser>());
};

void SubtitleParser::cleanParserList()
{
    for (std::vector<std::unique_ptr<BaseSubtitleParser> >::iterator it(m_parser_vec.begin()); it != m_parser_vec.end();)
    {
        it = m_parser_vec.erase(it);
    }
};

bool SubtitleParser::parse(std::istream &is, int fps, subtitleMap& submap)
{
    std::multimap<int, BaseSubtitleParser*> map;
    for (std::vector<std::unique_ptr<BaseSubtitleParser> >::const_iterator it(m_parser_vec.begin()); it != m_parser_vec.end(); it++)
    {
        int score = (*it)->probe(is);
        if (score > PROB_SCORE_MIN)
            map.insert(std::pair<int, BaseSubtitleParser*>(score, it->get()));
    }
    for (std::multimap<int, BaseSubtitleParser*>::reverse_iterator it(map.rbegin()); it!=map.rend(); it++)
    {
        if (it->second->parse(is, fps, submap))
            return true;
    }
    return false;
}

const subtitleMap *SubtitleManager::load(const std::string &path, int video_fps, int subtitle_fps, bool force_reload)
{
    fprintf(stderr,"SubtitleManager::load(%s,video_fps=%d,subtitle_fps=%d)\n",path.c_str(), video_fps, subtitle_fps);
    bool convert_fps = false;
    subtitleId orig_sid(path, std::pair<int,int>(1,1));
    subtitleId sid(path, std::pair<int, int>(subtitle_fps, video_fps));
    if (subtitle_fps == 1 || video_fps == subtitle_fps || (video_fps == -1 || subtitle_fps == -1))
    {
        //fprintf(stderr,"SubtitleManager::load(%s,video_fps=%d,subtitle_fps=%d) missing or same video/subtitle fps, no conversion will be done.\n",
        //        path.c_str(), video_fps, subtitle_fps);
        sid = orig_sid;
    }
    subtitles::iterator it = m_loaded_subtitles.find(sid);
    //if (it != m_loaded_subtitles.end())
    //{
    //    fprintf(stderr,"SubtitleManager::load(%s,video_fps=%d,subtitle_fps=%d) already loaded!\n", 
    //            path.c_str(), video_fps, subtitle_fps);
    //}
    if (it == m_loaded_subtitles.end() && sid != orig_sid)
    {
        it = m_loaded_subtitles.find(orig_sid);
        if ((it != m_loaded_subtitles.end()))
        {
            //fprintf(stderr,"SubtitleManager::load(%s,video_fps=%d,subtitle_fps=%d) already loaded but need to convert fps\n", 
            //        path.c_str(), video_fps, subtitle_fps);
            convert_fps = true;
        }
    }
    if (it == m_loaded_subtitles.end() || force_reload)
    {
        if (it != m_loaded_subtitles.end())
        {
            it = m_loaded_subtitles.begin();
            while(it != m_loaded_subtitles.end())
            {
                if (it->first.first == path)
                {
                    m_loaded_subtitles.erase(it++);
                }
                else
                {
                    it++;
                }
            }
        }
        std::ifstream ifs(path.c_str(), std::ios::binary);
        if (!ifs.is_open())
        {
            fprintf(stderr,"SubtitleManager::load(%s,video_fps=%d,subtitle_fps=%d) - cannot open file: %s\n",
                    path.c_str(), video_fps, subtitle_fps, strerror(errno));
            return NULL;
        }
        ifs.seekg(0, std::ios::end);
        std::streamoff size = ifs.tellg();
        ifs.seekg(0, std::ios::beg);
        if (size < 0 || size > SUBTITLE_MAX_FILE_SIZE)
        {
            fprintf(stderr,"SubtitleManager::load(%s,video_fps=%d,subtitle_fps=%d) - file size %lld not accepted (max. %d bytes)\n",
                    path.c_str(), video_fps, subtitle_fps, (long long) size, SUBTITLE_MAX_FILE_SIZE);
            return NULL;
        }
        /* read once into a string, convert, then one copy for the parser
         * stream - the stringstream round trips copied the file 3-4 times */
        std::string content((size_t) size, '\0');
        ifs.read(&content[0], size);
        content.resize((size_t) ifs.gcount());
        if (m_convert_to_utf8)
        {
            std::string out;
            if (convertToUTF8(content, out) != 0)
            {
                fprintf(stderr,"SubtitleManager::load(%s,video_fps=%d,subtitle_fps=%d) - error in convert to utf-8\n",
                        path.c_str(), video_fps, subtitle_fps);
            }
            else
            {
                content.swap(out);
            }
        }
        std::istringstream ss(content);
        content = std::string();
        subtitleMap map;
        if (!m_parser.parse(ss, video_fps, map))
        {
            fprintf(stderr,"SubtitleManager::load(%s,video_fps=%d,subtitle_fps=%d) - cannot parse file\n", 
                    path.c_str(), video_fps, subtitle_fps);
            return NULL;
        }
        convert_fps = sid != orig_sid;
        m_loaded_subtitles.insert(std::pair<subtitleId, subtitleMap>(orig_sid, map));
    }

    if (convert_fps)
    {
        subtitleMap *origmap = &m_loaded_subtitles.find(orig_sid)->second;
        subtitleMap convmap;
        float fps_ratio = sid.second.second / (double) sid.second.first;
        for (subtitleMap::const_iterator sit(origmap->begin()); sit != origmap->end(); sit++)
        {
            subtitleMessage sub(sit->second);
            sub.start_ms *= fps_ratio;
            sub.end_ms *= fps_ratio;
            sub.duration_ms = sub.end_ms - sub.start_ms;
            convmap.insert(std::pair<int32_t, subtitleMessage>(sub.end_ms, sub));
        }
        m_loaded_subtitles.insert(std::pair<subtitleId, subtitleMap>(sid, convmap));
    }
    fprintf(stderr,"SubtitleManager::load(%s,video_fps=%d,subtitle_fps=%d) succesfully loaded\n",path.c_str(), video_fps, subtitle_fps);
    return &(m_loaded_subtitles.find(sid)->second);
}

