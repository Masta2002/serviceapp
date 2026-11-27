#include "Python.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sstream>

#include <lib/base/cfile.h>
#include <lib/base/eenv.h>
#include <lib/base/esettings.h>
#include <lib/base/esimpleconfig.h>
#include <lib/base/nconfig.h>
#include <lib/base/init.h>
#include <lib/base/init_num.h>
#include <lib/components/file_eraser.h>
#include <lib/service/service.h>
#ifdef HAVE_EPG
#include <lib/dvb/epgcache.h>
#endif
#include <lib/dvb/idvb.h>
#include <lib/dvb/db.h>
#include <lib/gui/esubtitle.h>
#include <lib/gdi/epng.h>
#include <endian.h>
#include <unistd.h>
#include <netinet/in.h>

#include "exteplayer3.h"
#include "gstplayer.h"
#include "serviceapp.h"

#include <Python.h>
#include <lib/base/estring.h>
#include <string>

enum { SUBSERVICES_INDEX_START = 1, SUBSERVICES_INDEX_END = 0xFF, SUBSERVICES_BITRATEKB_START = 0x100 };

enum {
	EXTEPLAYER3,
	GSTPLAYER,
};

enum {
	OPTIONS_SERVICEMP3,
	OPTIONS_SERVICEGSTPLAYER,
	OPTIONS_SERVICEEXTEPLAYER3,
	OPTIONS_USER,
};

static int g_playerServiceMP3 = GSTPLAYER;
static bool g_useUserSettings = false;

static GstPlayerOptions* g_GstPlayerOptionsServiceMP3;
static GstPlayerOptions* g_GstPlayerOptionsServiceGst;
static GstPlayerOptions* g_GstPlayerOptionsUser;

static ExtEplayer3Options* g_ExtEplayer3OptionsServiceMP3;
static ExtEplayer3Options* g_ExtEplayer3OptionsServiceExt3;
static ExtEplayer3Options* g_ExtEplayer3OptionsUser;

static eServiceAppOptions* g_ServiceAppOptionsServiceMP3;
static eServiceAppOptions* g_ServiceAppOptionsServiceExt3;
static eServiceAppOptions* g_ServiceAppOptionsServiceGst;
static eServiceAppOptions* g_ServiceAppOptionsUser;

static const std::string gReplaceServiceMP3Path = eEnv::resolve("$sysconfdir/enigma2/serviceapp_replaceservicemp3");
static const bool gReplaceServiceMP3 = (access(gReplaceServiceMP3Path.c_str(), F_OK) != -1);

static HeaderMap getHttpHeaders(const std::string& path) {
	HeaderMap headers = getHeaders(path);
	for (HeaderMap::iterator it(headers.begin()); it != headers.end();) {
		if (it->first.find("sapp_") == 0)
			headers.erase(it++);
		else
			it++;
	}
	return headers;
}

static void updatePlayerOptions(IOption& options, const HeaderMap& headers) {
	for (HeaderMap::const_iterator it(headers.begin()); it != headers.end(); it++) {
		if (it->first.find("sapp_") == 0) {
			options.update(it->first.substr(5), it->second);
		}
	}
}

static std::unique_ptr<BasePlayer> createPlayer(const eServiceReference& ref, const HeaderMap& headers, bool useUserSettings) {
	std::unique_ptr<BasePlayer> player;
	if (ref.type == eServiceFactoryApp::idServiceExtEplayer3 || (ref.type == eServiceFactoryApp::idServiceMP3 && g_playerServiceMP3 == EXTEPLAYER3)) {
		ExtEplayer3Options options;
		if (useUserSettings)
			options = *g_ExtEplayer3OptionsUser;
		else if (ref.type == eServiceFactoryApp::idServiceExtEplayer3)
			options = *g_ExtEplayer3OptionsServiceExt3;
		else
			options = *g_ExtEplayer3OptionsServiceMP3;
		updatePlayerOptions(options, headers);
		player = std::make_unique<ExtEplayer3>(options);
	} else if (ref.type == eServiceFactoryApp::idServiceGstPlayer || (ref.type == eServiceFactoryApp::idServiceMP3 && g_playerServiceMP3 == GSTPLAYER)) {
		GstPlayerOptions options;
		if (useUserSettings)
			options = *g_GstPlayerOptionsUser;
		else if (ref.type == eServiceFactoryApp::idServiceGstPlayer)
			options = *g_GstPlayerOptionsServiceGst;
		else
			options = *g_GstPlayerOptionsServiceMP3;
		updatePlayerOptions(options, headers);
		player = std::make_unique<GstPlayer>(options);
	}
	return player;
}

static std::unique_ptr<eServiceAppOptions> createOptions(const eServiceReference& ref, bool useUserSettings) {
	eServiceAppOptions* options = NULL;
	switch (ref.type) {
		case eServiceFactoryApp::idServiceMP3:
			options = g_ServiceAppOptionsServiceMP3;
			break;
		case eServiceFactoryApp::idServiceExtEplayer3:
			options = g_ServiceAppOptionsServiceExt3;
			break;
		case eServiceFactoryApp::idServiceGstPlayer:
			options = g_ServiceAppOptionsServiceGst;
			break;
		default:
			break;
	}
	if (useUserSettings) {
		options = g_ServiceAppOptionsUser;
	}
	return std::make_unique<eServiceAppOptions>(*options);
}


class eServiceOfflineOperations : public iServiceOfflineOperations {
	DECLARE_REF(eServiceOfflineOperations);
	eServiceReference m_ref;

public:
	eServiceOfflineOperations(const eServiceReference& ref);

	RESULT deleteFromDisk(int simulate);
	RESULT getListOfFilenames(std::list<std::string>&);
	RESULT reindex();
};

DEFINE_REF(eServiceOfflineOperations);

eServiceOfflineOperations::eServiceOfflineOperations(const eServiceReference& ref) : m_ref((const eServiceReference&)ref) {}

RESULT eServiceOfflineOperations::deleteFromDisk(int simulate) {
	if (!simulate) {
		std::list<std::string> res;
		if (getListOfFilenames(res))
			return -1;

		eBackgroundFileEraser* eraser = eBackgroundFileEraser::getInstance();
		if (!eraser)
			eDebug("[eServiceOfflineOperations] FATAL !! can't get background file eraser");

		for (std::list<std::string>::iterator i(res.begin()); i != res.end(); ++i) {
			eDebug("[eServiceOfflineOperations] Removing %s...", i->c_str());
			if (eraser)
				eraser->erase(i->c_str());
			else
				::unlink(i->c_str());
		}
	}
	return 0;
}

RESULT eServiceOfflineOperations::getListOfFilenames(std::list<std::string>& res) {
	res.clear();
	res.push_back(m_ref.path);
	return 0;
}

RESULT eServiceOfflineOperations::reindex() {
	return -1;
}


RESULT eServiceFactoryApp::offlineOperations(const eServiceReference& ref, ePtr<iServiceOfflineOperations>& ptr) {
	ptr = new eServiceOfflineOperations(ref);
	return 0;
}

/* The native 4097 factory of enigma2 (eServiceFactoryMP3::instance). It stays
   alive when "replace servicemp3" takes over the 4097 registration. The member
   is private, so it is looked up by its symbol name; weak, so images without it
   still load the plugin. Only the iServiceHandler interface is used. */
extern iServiceHandler* g_nativeServiceMP3Factory __asm__("_ZN18eServiceFactoryMP38instanceE") __attribute__((weak));

/* Recordings are done by the native 4097 recorder of enigma2, the same one
   that records 4097 streams. Player options (sapp_*) are dropped from the
   header part, they are no HTTP headers. */
RESULT eServiceFactoryApp::record(const eServiceReference& ref, ePtr<iRecordableService>& ptr) {
	ptr = 0;
	if (ref.path.find("://") == std::string::npos)
		return -1;

	eServiceReference recref(ref);
	recref.type = idServiceMP3;
	size_t pos = recref.path.find('#');
	if (pos != std::string::npos) {
		std::string fragment = recref.path.substr(pos + 1);
		std::string kept;
		size_t start = 0;
		while (start <= fragment.size()) {
			size_t end = fragment.find('&', start);
			if (end == std::string::npos)
				end = fragment.size();
			std::string part = fragment.substr(start, end - start);
			if (!part.empty() && part.compare(0, 5, "sapp_") != 0)
				kept += (kept.empty() ? "" : "&") + part;
			start = end + 1;
		}
		recref.path = recref.path.substr(0, pos);
		if (!kept.empty())
			recref.path += "#" + kept;
	}

	iServiceHandler* native = &g_nativeServiceMP3Factory ? g_nativeServiceMP3Factory : NULL;
	if (native)
		return native->record(recref, ptr);
	if (!gReplaceServiceMP3) {
		ePtr<iServiceHandler> sc;
		eServiceCenter::getInstance(sc);
		if (sc)
			return sc->record(recref, ptr);
	}
	eDebug("[eServiceFactoryApp] record: no native 4097 recorder available");
	return -1;
}


DEFINE_REF(eServiceApp);

eServiceApp::eServiceApp(eServiceReference ref)
	: m_ref(ref), m_subservices_checked(false), m_resolve_uri("resolve://"), m_event_started(false), m_paused(false), m_debug(false), m_framerate(-1),
	  m_width(-1), m_height(-1), m_progressive(-1), m_gamma(-1), m_subtitle_pages(0), m_prev_subtitle_message(0), m_prev_subtitle_fps(1), m_prev_decoder_time(-1),
	  m_decoder_time_valid_state(0), m_cuesheet_changed(0), m_cutlist_enabled(0), m_cuesheet_loaded(false), m_is_streaming(false), m_last_seek_pos(0),
	  m_media_length(0), m_pending_seek(-1) {
	/* the decision of use_user_settings() at the creation is kept for the
	 * player of a later reconnect */
	m_user_settings = g_useUserSettings;
	options = createOptions(ref, m_user_settings);
	if (!ref.alternativeurl.empty())
		m_ref.path = ref.alternativeurl;
	m_is_streaming = (m_ref.path.find("://") != std::string::npos);
	// Keep the original service identity, even if a resolver changes its URL.
	m_audioCacheReference = replace_all(ref.toString(), ",", "_");
	if (eSettings::audio_usecache && !getHeaders(m_ref.path).count("sapp_audio_id")) {
		for (const auto& service : eDVBDB::getInstance()->iptv_services) {
			if (service->m_reference_str == m_audioCacheReference) {
				m_cachedAudioTrack = service->getCacheEntry(eDVBService::cMPEGAPID);
				break;
			}
		}
	}

	extplayer = createPlayer(ref, getHeaders(m_ref.path), m_user_settings);
	player = std::make_unique<PlayerBackend>(extplayer.get());

	m_subtitle_widget = 0;
	m_subtitle_sync_timer = eTimer::create(eApp);
	CONNECT(m_subtitle_sync_timer->timeout, eServiceApp::pushSubtitles);
	m_event_updated_info_timer = eTimer::create(eApp);
	CONNECT(m_event_updated_info_timer->timeout, eServiceApp::signalEventUpdatedInfo);
	m_passthrough_fix_timer = eTimer::create(eApp);
	CONNECT(m_passthrough_fix_timer->timeout, eServiceApp::passthroughFix);
	m_decoder_video_checks = 0;
	m_decoder_video_position = -1;
	m_decoder_video = false;
	m_decoder_video_timer = eTimer::create(eApp);
	CONNECT(m_decoder_video_timer->timeout, eServiceApp::checkDecoderVideo);
	m_live_reconnects = 0;
	m_player_start_time = 0;
	m_player_started = false;
	m_stopping = false;
	m_reconnected = false;
	m_reconnect_channel = STEREO;
	m_clean_ends = 0;
	m_forced_only_pid = -1;
	m_forced_only_active = false;
	m_ffmpeg_reconnect = true;
	m_max_position = 0;
	m_live_check_timer = eTimer::create(eApp);
	CONNECT(m_live_check_timer->timeout, eServiceApp::checkLiveRestart);
	m_start_path = m_ref.path;
	m_live_reconnect_timer = eTimer::create(eApp);
	CONNECT(m_live_reconnect_timer->timeout, eServiceApp::liveReconnect);
	m_hls_explorer = std::make_unique<HlsExplorer>();
	CONNECT(m_hls_explorer->finished, eServiceApp::subservicesExplored);

#ifdef HAVE_EPG
	m_nownext_timer = eTimer::create(eApp);
	CONNECT(m_nownext_timer->timeout, eServiceApp::updateEpgCacheNowNext);
#endif
	CONNECT(player->gotPlayerMessage, eServiceApp::gotExtPlayerMessage);
};

eServiceApp::~eServiceApp() {
	/* returns at once: a fetch still running ends in the background and its
	 * result is dropped (see HlsExplorer) */
	m_hls_explorer.reset();
	options.reset();
	player.reset();
	extplayer.reset();
	m_resolver.reset();
	m_selected_subtitle_track.reset();

	if (m_subtitle_widget)
		m_subtitle_widget->destroy();
	m_subtitle_widget = 0;
	m_decoder_video_timer->stop();
	m_live_reconnect_timer->stop();
	m_live_check_timer->stop();
#ifdef HAVE_EPG
	m_nownext_timer->stop();
#endif
	g_useUserSettings = false;
};


void eServiceApp::passthroughFix() {
	eDebug("[ServiceApp] Setting 'passthrough' to force correct operation");
	CFile::writeStr("/proc/stb/audio/ac3", "passthrough");
	bool validposition = false;
	pts_t ppos = 0;
	if (getPlayPosition(ppos) >= 0) {
		validposition = true;
		ppos -= 90000;
		if (ppos < 0)
			ppos = 0;
	}
	if (validposition) {
		/* flush */
		seekTo(ppos);
	}
}

/* Starts reading the variants of m_ref.path in the background; false if
 * there is nothing to read (not an HLS url) or no thread: the subservices are
 * then known to be none. The result comes to subservicesExplored(). */
bool eServiceApp::exploreSubservices() {
	if (!isM3U8Url(m_ref.path)) {
		eDebug("eServiceApp::fillSubservices - failed to retrieve subservices, not supported url");
		return false;
	}
	return m_hls_explorer->start(m_ref.path, getHttpHeaders(m_ref.path));
}

void eServiceApp::cancelSubservicesExplorer() {
	m_hls_explorer->cancel();
	m_hls_start_pending = false;
}

void eServiceApp::subservicesExplored(const std::vector<M3U8StreamInfo>& streams) {
	fillSubservices(streams);
	/* checked before the event: the GUI asks getNumberOfSubservices() again */
	m_subservices_checked = true;
	m_event(this, evUpdatedEventInfo);
	/* stop() and liveReconnect() cancel the explorer and clear the flag, a
	 * stopped service never gets here with it set */
	if (m_hls_start_pending) {
		m_hls_start_pending = false;
		startPlayer();
	}
}

void eServiceApp::fillSubservices(const std::vector<M3U8StreamInfo>& streams) {
	m_subservice_vec.clear();
	m_subserviceref_vec.clear();

	m_subservice_vec = streams;
	if (m_subservice_vec.empty()) {
		eDebug("eServiceApp::fillSubservices - failed to retrieve subservices");
		return;
	}
	// sort subservices from best quality to worst (internally sorted according to bitrate)
	sort(m_subservice_vec.rbegin(), m_subservice_vec.rend());

	std::vector<M3U8StreamInfo>::const_iterator it;
	// find title from parent, if parent has already bitrate
	// string set, we look for this bitrate and separate original
	// name from it.
	std::stringstream sstm;
	std::string original_title(m_ref.name);
	for (it = m_subservice_vec.begin(); it != m_subservice_vec.end(); it++) {
		sstm.str(std::string());
		sstm << it->bitrate;
		std::string bitrate_str = sstm.str();
		size_t bitrate_idx = m_ref.name.find(": " + bitrate_str);
		if (bitrate_idx != std::string::npos) {
			original_title = m_ref.name.substr(0, bitrate_idx);
			break;
		}
	}

	int i = 0;
	for (it = m_subservice_vec.begin(); it != m_subservice_vec.end(); it++, i++) {
		if (SUBSERVICES_INDEX_START + i > SUBSERVICES_INDEX_END) {
			eWarning("eServiceApp::fillSubservices - cannot add more then %d subservices!", SUBSERVICES_INDEX_END);
			break;
		}
		// we need to copy all flags from parent service, neccessary for EPG
		eServiceReference ref(m_ref);
		// set index so we know which service to select from master playlist
		ref.setUnsignedData(7, SUBSERVICES_INDEX_START + i);
		// set parentTransportStreamId, since InfoBarSubservicesSupport
		// checks this flag when creating subservices menu. If it's available
		// at least for one subservice then it will allow to add subservices
		// to bouquet or favorites, see subserviceSelection.
		//
		// If it's not available it will only allow to quickzap subservices and it
		// will also remove name for subservice service, see playSubservice.
		eServiceReferenceDVB& dvb_ref = (eServiceReferenceDVB&)ref;
		if (dvb_ref.getTransportStreamID().get()) {
			// If user wants EPG, i.e. fills serviceId, transportStreamId then
			// we have to set these as parentServiceId and parentTransportStreamID
			// since epgcache uses those to create EPG query
			dvb_ref.setParentServiceID(dvb_ref.getServiceID());
			dvb_ref.setParentTransportStreamID(dvb_ref.getTransportStreamID());
		} else {
			dvb_ref.setParentTransportStreamID(1); // some random value
		}
		sstm.str(std::string());
		sstm << original_title << ": " << it->bitrate << "b/s";
		if (!it->resolution.empty())
			sstm << " - " << it->resolution;
		ref.name = sstm.str();
		m_subserviceref_vec.push_back(ref);
	}
	eDebug("eServiceApp::fillSubservices - found %zd subservices", m_subservice_vec.size());
}

#ifdef HAVE_EPG
void eServiceApp::updateEpgCacheNowNext() {
	bool update = false;
	ePtr<eServiceEvent> next = 0;
	ePtr<eServiceEvent> ptr = 0;
	eServiceReference ref(m_ref);
	ref.type = eServiceFactoryApp::idServiceMP3;
	ref.path.clear();
	if (eEPGCache::getInstance() && eEPGCache::getInstance()->lookupEventTime(ref, -1, ptr) >= 0) {
		ePtr<eServiceEvent> current = m_event_now;
		if (!current || !ptr || current->getEventId() != ptr->getEventId()) {
			update = true;
			m_event_now = ptr;
			time_t next_time = ptr->getBeginTime() + ptr->getDuration();
			if (eEPGCache::getInstance()->lookupEventTime(ref, next_time, ptr) >= 0) {
				next = ptr;
				m_event_next = ptr;
			}
		}
	}

	int refreshtime = 60;
	if (!next) {
		next = m_event_next;
	}
	if (next) {
		time_t now = eDVBLocalTimeHandler::getInstance()->nowTime();
		refreshtime = (int)(next->getBeginTime() - now) + 3;
		if (refreshtime <= 0 || refreshtime > 60) {
			refreshtime = 60;
		}
	}
	m_nownext_timer->startLongTimer(refreshtime);
	if (update) {
		m_event((iPlayableService*)this, evUpdatedEventInfo);
	}
}
#endif

ssize_t eServiceApp::getTrackPosition(const SubtitleTrack& track) {
	ssize_t track_pos = -1;
	std::vector<SubtitleTrack>::const_iterator it(m_subtitle_tracks.begin());
	for (size_t i = 0; it != m_subtitle_tracks.end(); it++, i++) {
		if (it->pid == track.pid && it->type == track.type && it->page_number == track.page_number && it->magazine_number == track.magazine_number && it->language_code == track.language_code) {
			track_pos = i;
			break;
		}
	}
	return track_pos;
}

void eServiceApp::addEmbeddedTrack(std::vector<struct SubtitleTrack>& subtitlelist, subtitleStream& s, int pid) {
	m_subtitle_streams.push_back(s);
	struct SubtitleTrack track;
	track.type = 2;
	track.page_number = 1;
	track.magazine_number = 0;
	track.pid = pid;
	track.language_code = s.language_code;
	/* the title of the track ("SDH", "CC1"), "forced" for a forced one without */
	if (!s.title.empty()) {
		track.title = s.title;
		std::string lower(s.title);
		std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
		if (s.forced && lower.find("forced") == std::string::npos)
			track.title += std::string(" ") + SAPP_TR("forced");
	} else if (s.forced) {
		/* TRANSLATORS: a forced subtitle track (only the foreign language parts) */
		track.title = SAPP_TR("forced");
	}

	subtitlelist.push_back(track);
	m_subtitle_tracks.push_back(track);
}

void eServiceApp::addExternalTrack(std::vector<struct SubtitleTrack>& subtitlelist, int pid, std::string lang, std::string path) {
	subtitleStream s;
	s.path = path;
	m_subtitle_streams.push_back(s);

	struct SubtitleTrack track;
	track.type = 2;
	track.page_number = 4;
	track.magazine_number = 0;
	track.pid = pid;
	track.language_code = lang;

	subtitlelist.push_back(track);
	m_subtitle_tracks.push_back(track);
}

bool eServiceApp::isEmbeddedTrack(const SubtitleTrack& track) {
	return (track.type == 2 && track.page_number == 1);
}

bool eServiceApp::isExternalTrack(const SubtitleTrack& track) {
	return (track.type == 2 && track.page_number == 4);
}

void eServiceApp::pullSubtitles() {
	std::queue<subtitleMessage> pulled;
	player->getSubtitles(pulled);
	if (m_debug)
		eDebug("eServiceApp::pullSubtitles - pulling %zu subtitles", pulled.size());
	addSubtitles(pulled);
	m_subtitle_sync_timer->start(1, true);
}

void eServiceApp::addSubtitles(std::queue<subtitleMessage>& pulled) {
	while (!pulled.empty()) {
		subtitleMessage sub = pulled.front();
		if (sub.graphic)
			addGraphicSubtitle(sub);
		else
			addTextSubtitle(sub);
		pulled.pop();
	}
}

void eServiceApp::addTextSubtitle(const subtitleMessage& sub) {
	/* The map is keyed by the end (the type is shared with the external
	 * subtitle parsers): a second cue with the same end was dropped. Its text
	 * joins the cue that is there; a graphic page there moves it by 1 ms. */
	uint32_t key = sub.end_ms;
	for (;;) {
		std::pair<subtitle_pages_map::iterator, bool> r = m_embedded_subtitle_pages.insert(subtitle_pages_map_pair(key, sub));
		if (r.second)
			return;
		subtitleMessage& m = r.first->second;
		if (!m.graphic) {
			if (m.text != sub.text && !sub.text.empty()) {
				m.text += m.text.empty() ? sub.text : "\n" + sub.text;
				if (sub.start_ms < m.start_ms) {
					m.start_ms = sub.start_ms;
					m.duration_ms = m.end_ms - m.start_ms;
				}
			}
			return;
		}
		if (key == std::numeric_limits<uint32_t>::max())
			return;
		key++;
	}
}

/* graphic subtitles: exteplayer3 writes each picture as a PNG file into its -G
 * directory in /tmp (RAM). A file is deleted once its cue is over, skipped or
 * dropped; the player's directory goes when it has stopped (ExtEplayer3). */
static void removeSubtitleFiles(const subtitleMessage& sub, const std::set<std::string>* keep = NULL) {
	for (size_t i = 0; i < sub.images.size(); i++) {
		if (keep && keep->count(sub.images[i].file))
			continue; /* a newer cue has a file of that name (same track and start) */
		unlink(sub.images[i].file.c_str());
	}
}

static int64_t monotonicMs() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void eServiceApp::addGraphicSubtitle(const subtitleMessage& sub) {
	/* A graphic page replaces the one on the screen: it ends the pages that are
	 * still open at its start (PGS sends no end, "e" null came as start + 30 s),
	 * as in the DreamOS port. The map is keyed by the end, a shortened page is
	 * inserted again. A page without pictures is only a clear. */
	std::set<std::string> files;
	for (size_t i = 0; i < sub.images.size(); i++)
		files.insert(sub.images[i].file);
	std::vector<subtitleMessage> cut;
	std::vector<bool> cut_shown;
	for (subtitle_pages_map::iterator it = m_embedded_subtitle_pages.begin(); it != m_embedded_subtitle_pages.end();) {
		const subtitleMessage& m = it->second;
		if (!m.graphic || m.start_ms > sub.start_ms || m.end_ms <= sub.start_ms) {
			++it;
			continue;
		}
		bool shown = (m_prev_subtitle_message == &m);
		if (m.start_ms == sub.start_ms) {
			/* the same page again: the new one takes its place */
			removeSubtitleFiles(m, &files);
			if (shown)
				m_prev_subtitle_message = NULL;
		} else {
			subtitleMessage c = m;
			c.end_ms = sub.start_ms;
			c.duration_ms = c.end_ms - c.start_ms;
			cut.push_back(c);
			cut_shown.push_back(shown);
		}
		m_embedded_subtitle_pages.erase(it++);
	}
	for (size_t k = 0; k < cut.size(); k++) {
		std::pair<subtitle_pages_map::iterator, bool> r = m_embedded_subtitle_pages.insert(subtitle_pages_map_pair(cut[k].end_ms, cut[k]));
		if (!r.second) {
			/* another cue ends there already: this one is not shown any more */
			std::set<std::string> keep(files);
			for (size_t i = 0; i < r.first->second.images.size(); i++)
				keep.insert(r.first->second.images[i].file);
			removeSubtitleFiles(cut[k], &keep);
			if (cut_shown[k])
				m_prev_subtitle_message = NULL;
		} else if (cut_shown[k]) {
			m_prev_subtitle_message = &(r.first->second);
		}
	}
	/* the page on the screen ends at the new start */
	if (m_graphic_shown && m_graphic_start_ms <= sub.start_ms && m_graphic_end_ms > sub.start_ms)
		m_graphic_end_ms = sub.start_ms;
	if (sub.images.empty())
		return;
	/* a later page that came first ends this one */
	subtitleMessage page = sub;
	for (subtitle_pages_map::const_iterator it = m_embedded_subtitle_pages.begin(); it != m_embedded_subtitle_pages.end(); ++it)
		if (it->second.graphic && it->second.start_ms > page.start_ms && it->second.start_ms < page.end_ms)
			page.end_ms = it->second.start_ms;
	page.duration_ms = page.end_ms - page.start_ms;
	if (!m_embedded_subtitle_pages.insert(subtitle_pages_map_pair(page.end_ms, page)).second) {
		std::set<std::string> keep;
		subtitle_pages_map::const_iterator other = m_embedded_subtitle_pages.find(page.end_ms);
		for (size_t i = 0; i < other->second.images.size(); i++)
			keep.insert(other->second.images[i].file);
		removeSubtitleFiles(page, &keep);
	}
}

void eServiceApp::removePastSubtitles(int32_t decoder_ms) {
	/* cues that are over (or were skipped) go: the files of graphic ones are
	 * not needed any more, and text cues piled up for the whole playback
	 * (live closed captions, ~17 MB a day) and were walked on every tick */
	if (decoder_ms <= 0)
		return;
	subtitle_pages_map::iterator end = m_embedded_subtitle_pages.lower_bound((uint32_t)decoder_ms);
	for (subtitle_pages_map::iterator it = m_embedded_subtitle_pages.begin(); it != end;) {
		if (it->second.graphic)
			removeSubtitleFiles(it->second);
		if (m_prev_subtitle_message == &(it->second))
			m_prev_subtitle_message = NULL;
		m_embedded_subtitle_pages.erase(it++);
	}
}

void eServiceApp::showGraphicSubtitle(const subtitleMessage& sub) {
	/* loadPNG in the main thread only; the pictures are kept in the page,
	 * the page is shown again from memory (updateGraphicSubtitle) */
	eDVBSubtitlePage page;
	page.m_show_time = (pts_t)sub.start_ms * 90;
	page.m_display_size = eSize(EXT3_GRAPHIC_SUB_WIDTH, EXT3_GRAPHIC_SUB_HEIGHT);
	for (size_t i = 0; i < sub.images.size(); i++) {
		ePtr<gPixmap> pixmap;
		if (loadPNG(pixmap, sub.images[i].file.c_str(), 0, 0) || !pixmap) {
			eWarning("eServiceApp::showGraphicSubtitle - cannot load %s", sub.images[i].file.c_str());
			continue;
		}
		eDVBSubtitleRegion region;
		region.m_pixmap = pixmap;
		region.m_position = ePoint(sub.images[i].x, sub.images[i].y);
		page.m_regions.push_back(region);
	}
	m_graphic_page = page;
	m_subtitle_widget->setPage(m_graphic_page);
	m_graphic_shown = true;
	m_graphic_start_ms = sub.start_ms;
	m_graphic_end_ms = sub.end_ms;
	m_graphic_shown_at = monotonicMs();
}

void eServiceApp::hideGraphicSubtitle() {
	/* an empty page with the same window; no clearPage(): older enigma2
	 * builds do not have it in iSubtitleUser */
	if (m_graphic_shown && m_subtitle_widget) {
		eDVBSubtitlePage page;
		page.m_show_time = 0;
		page.m_display_size = eSize(EXT3_GRAPHIC_SUB_WIDTH, EXT3_GRAPHIC_SUB_HEIGHT);
		m_subtitle_widget->setPage(page);
	}
	m_graphic_shown = false;
	m_graphic_page = eDVBSubtitlePage();
}

void eServiceApp::updateGraphicSubtitle(int32_t decoder_ms, int32_t& next_timer) {
	if (!m_graphic_shown)
		return;
	if (!m_subtitle_widget || decoder_ms > (int32_t)m_graphic_end_ms) {
		/* the cue is over and no other page took its place */
		hideGraphicSubtitle();
		return;
	}
	int64_t now = monotonicMs();
	if (now - m_graphic_shown_at >= 5000) {
		/* also while paused: the widget would hide the page after 7.5 s */
		m_subtitle_widget->setPage(m_graphic_page);
		m_graphic_shown_at = now;
	}
	int32_t wait = (int32_t)(m_graphic_shown_at + 5000 - now);
	/* paused, the position does not move towards the end: only the refresh */
	if (!m_paused)
		wait = std::min((int32_t)m_graphic_end_ms - decoder_ms + 1, wait);
	if (wait < 10)
		wait = 10;
	if (next_timer == 0 || wait < next_timer)
		next_timer = wait;
}

void eServiceApp::clearEmbeddedSubtitlePages(bool hide, const std::set<std::string>* keep) {
	for (subtitle_pages_map::const_iterator it = m_embedded_subtitle_pages.begin(); it != m_embedded_subtitle_pages.end(); ++it)
		if (it->second.graphic)
			removeSubtitleFiles(it->second, keep);
	m_embedded_subtitle_pages.clear();
	m_prev_subtitle_message = NULL;
	if (!hide)
		m_graphic_shown = false;
	hideGraphicSubtitle();
}

void eServiceApp::pushSubtitles() {
	pts_t running_pts = 0;
	int32_t next_timer = 0, decoder_ms = 0, start_ms, end_ms, diff_start_ms, diff_end_ms;
	bool clock_ok = false;
	subtitle_pages_map::const_iterator current;
	/* the text cues on the screen now: the first of them, their text, the
	 * time to the first end */
	const subtitleMessage* active_text = NULL;
	std::string text;
	int32_t text_timeout = -1;

	int delay = eSubtitleSettings::pango_subtitles_delay;
	if (isExternalTrack(*m_selected_subtitle_track)) {
		int subtitle_fps = eSubtitleSettings::pango_subtitles_fps;
		if (subtitle_fps != m_prev_subtitle_fps) {
			m_prev_subtitle_fps = subtitle_fps;
			// the selected track may have vanished after a getSubtitleList()
			// refresh -> getTrackPosition returns -1; indexing with that
			// would read out of bounds. Keep the already-loaded pages then.
			ssize_t track_pos = getTrackPosition(*m_selected_subtitle_track);
			const subtitleMap* submap = NULL;
			if (track_pos >= 0)
				submap = m_subtitle_manager.load(m_subtitle_streams[track_pos].path, m_framerate, subtitle_fps);
			if (submap) {
				m_prev_subtitle_message = NULL;
				m_subtitle_pages = submap;
			}
		}
	}

	if (!m_subtitle_pages)
		return;

	if (getPlayPosition(running_pts) < 0) {
		/* no position (yet): every 50 ms was a busy poll for nothing */
		m_decoder_time_valid_state = 0;
		next_timer = 500;
		goto exit;
	}
	if (m_decoder_time_valid_state < 3) {
		m_decoder_time_valid_state++;
		// this happens after we start seeking operation
		// decoder pts is not updated, we have to wait
		// for seek to finish.
		if (m_prev_decoder_time == running_pts) {
			m_decoder_time_valid_state = 0;
		}
		if (m_decoder_time_valid_state < 3) {
			// eDebug("eServiceApp::pushSubtitles - waiting for clock to stabilise: valid=%d, prev=%lld,current=%lld",
			//		m_decoder_time_valid_state, m_prev_decoder_time, decoder_ms);
			m_prev_decoder_time = running_pts;
			// we are updating play position every 100ms in extplayer
			// so to see any progress in decoder_ms we have to wait a little longer
			next_timer = 110;
			goto exit;
		}
		// eDebug("eServiceApp::pushSubtitles - push subtitles, clock stable");
	}
	/* the subtitle delay setting applies to graphic pages as to text ones:
	 * both are cues of exteplayer3 on the same clock */
	decoder_ms = (running_pts - delay) / 90;
	clock_ok = true;
	if (m_subtitle_pages == &m_embedded_subtitle_pages)
		removePastSubtitles(decoder_ms);

	for (current = m_subtitle_pages->lower_bound(decoder_ms); current != m_subtitle_pages->end(); current++) {
		start_ms = current->second.start_ms;
		end_ms = current->second.end_ms;
		diff_start_ms = start_ms - decoder_ms;
		diff_end_ms = end_ms - decoder_ms;

		// eDebug("eServiceApp::pushSubtitles - next subtitle: decoder: %d, start: %d, end: %d, duration_ms: %d, diff_start: %d, diff_end: %d : %s",
		//	decoder_ms, start_ms, end_ms, end_ms - start_ms, diff_start_ms, diff_end_ms, current->second.text.c_str());

		if (diff_end_ms < 0) {
			// eDebug("eServiceApp::pushSubtitles - current sub has already ended, skip: %d", diff_end_ms);
			continue;
		}
		if (diff_start_ms > 50) {
			// eDebug("eServiceApp::pushSubtitles - current sub in the future, start timer, %d", diff_start_ms);
			if (next_timer == 0 || diff_start_ms < next_timer)
				next_timer = diff_start_ms;
			break;
		}
		/* a cue on the screen: the next look at its end (or at the start of
		 * the next cue), not every 30 ms */
		if (next_timer == 0 || diff_end_ms + 1 < next_timer)
			next_timer = diff_end_ms + 1;
		if (current->second.graphic) {
			// don't show the same page twice
			if (m_prev_subtitle_message != &(current->second) && m_subtitle_widget && !m_paused) {
				m_prev_subtitle_message = &(current->second);
				showGraphicSubtitle(current->second);
			}
			continue;
		}
		/* overlapping text cues are shown together */
		if (!active_text)
			active_text = &(current->second);
		if (!text.empty())
			text += "\n";
		text += current->second.text;
		if (text_timeout < 0 || diff_end_ms < text_timeout)
			text_timeout = diff_end_ms;
	}
	// don't show the same text twice; a cue that ended or joined changes it
	if (active_text && m_subtitle_widget && !m_paused && !(m_prev_subtitle_message == active_text && m_prev_subtitle_text == text)) {
		// eDebug("eServiceApp::pushSubtitles - current sub actual, show!");
		m_prev_subtitle_message = active_text;
		m_prev_subtitle_text = text;
		ePangoSubtitlePage pango_page;
		gRGB rgbcol(0xD0, 0xD0, 0xD0);

		pango_page.m_elements.push_back(ePangoSubtitlePageElement(rgbcol, text.c_str()));
		pango_page.m_show_pts = active_text->start_ms * 90; // actually completely unused by widget!
		pango_page.m_timeout = text_timeout; // take late start into account

		m_subtitle_widget->setPage(pango_page);
	}
	/* paused: nothing moves; only the 5 s refresh of a graphic page (below)
	 * or a look every second, not every 30 ms */
	if (m_paused)
		next_timer = 1000;
exit:
	if (clock_ok)
		updateGraphicSubtitle(decoder_ms, next_timer);
	if (next_timer == 0) {
		// eDebug("eServiceApp::pushSubtitles - next timer = 0, set default timer!");
		next_timer = 1000;
	}
	m_subtitle_sync_timer->start(next_timer, true);
}

/* A stream FFmpeg has not seen a picture of (HLS, live TS over HTTP) is
 * reported by the player as a video track with a size of 0x0 and a guessed
 * frame rate. The decoder knows the picture a moment later; an exteplayer3
 * that does not pass that on leaves sVideoWidth/sVideoHeight unknown. The
 * values are then read where enigma2 reads them for its own services. */
void eServiceApp::startDecoderVideoCheck() {
	if (m_decoder_video) {
		/* values of the stream played before this start */
		m_width = m_height = -1;
		m_decoder_video = false;
	}
	m_decoder_video_checks = 0;
	m_decoder_video_position = -1;
	m_decoder_video_timer->start(2000, true);
}

void eServiceApp::checkDecoderVideo() {
	static const int max_checks = 30;
	if (!m_decoder_video && m_width > 0 && m_height > 0)
		return; // the player knows the size
	videoStream v;
	pts_t position = -1;
	bool playing = false;
	/* Right after the start the decoder still has the values of what it
	 * showed before. It shows this stream once the play position moves. */
	if (!player->videoGetTrackInfo(v, 0) && v.id >= 0 && getPlayPosition(position) >= 0) {
		if (m_decoder_video_position < 0)
			m_decoder_video_position = position;
		else
			playing = position != m_decoder_video_position;
	}
	if (playing) {
		/* formats as in eAVControl of enigma2: size hexadecimal, frame rate decimal */
		int width = -1, height = -1;
		if (CFile::parseIntHex(&width, "/proc/stb/vmpeg/0/xres") == -1 || CFile::parseIntHex(&height, "/proc/stb/vmpeg/0/yres") == -1)
			return; // no such decoder information on this box
		if (width > 0 && height > 0) {
			bool confirmed = m_decoder_video && width == m_width && height == m_height;
			int framerate = -1, progressive = -1;
			CFile::parseInt(&framerate, "/proc/stb/vmpeg/0/framerate");
			CFile::parseIntHex(&progressive, "/proc/stb/vmpeg/0/progressive");
			m_decoder_video = true;
			if (m_debug)
				eDebug("eServiceApp::checkDecoderVideo - %dx%d framerate %d progressive %d", width, height, framerate, progressive);
			if (width != m_width || height != m_height) {
				m_width = width;
				m_height = height;
				m_event(this, evVideoSizeChanged);
			}
			/* without a picture size the frame rate of the player is a guess */
			if (framerate > 0 && framerate != m_framerate) {
				m_framerate = framerate;
				m_event(this, evVideoFramerateChanged);
			}
			if (progressive >= 0 && m_progressive < 0) {
				m_progressive = progressive ? 1 : 0;
				m_event(this, evVideoProgressiveChanged);
			}
			if (confirmed)
				return; // read twice with the same result
		}
	}
	if (++m_decoder_video_checks < max_checks)
		m_decoder_video_timer->start(2000, true);
}

void eServiceApp::signalEventUpdatedInfo() {
	if (m_debug)
		eDebug("eServiceApp::signalEventUpdatedInfo");
	m_event(this, evUpdatedInfo);
	// the player knows by now whether the stream has a length
	m_event(this, evSeekableStatusChanged);
	bool is_passthrough_fix_enabled = eSimpleConfig::getBool("config.plugins.serviceapp.passthrough_fix_enable", false);
	if (is_passthrough_fix_enabled) {
		std::string pass = CFile::read("/proc/stb/audio/ac3");
		/* the AC3+ workaround seeks back a second: only for an E-AC-3 track,
		 * the proc value stays "passthrough" after an AC3+ service and the
		 * seek also hit AAC and AC3 streams */
		audioStream track;
		int current = player->audioGetCurrentTrackNum();
		bool eac3 = current >= 0 && player->audioGetTrackInfo(track, current) == 0 &&
					(track.description == "A_EAC3" || track.codec_name == "Dolby Digital +" || track.codec_name == "Dolby Atmos");
		if (eac3 && replace_all(replace_all(pass, "\r", ""), "\n", "") == "passthrough") {
			int passthrough_delay = eSimpleConfig::getInt("config.plugins.serviceapp.passthrough_fix_delay", 0);
			m_passthrough_fix_timer->stop();
			m_passthrough_fix_timer->start(passthrough_delay, true);
		}
	}
}

void eServiceApp::urlResolved(int success) {
	if (m_debug)
		eDebug("eServiceApp::urlResolved: %s", success ? "success" : "error");
	if (success) {
		std::string url = m_resolver->getUrl();
		if (url.compare(0, m_resolve_uri.size(), m_resolve_uri) != 0) {
			m_ref.path = url;
			if (m_debug)
				eDebug("eServiceApp::urlResolved: %s", m_ref.path.c_str());
			start();
			return;
		}
		/* start() would replace m_resolver while this callback of it runs */
		eWarning("eServiceApp::urlResolved - the resolver returned another resolve:// url, refused");
	}
	/* stop() only set m_stopping without an event: a black picture for good,
	 * also after a re-resolve of a reconnect. The user gets the reason (sUser+12)
	 * and the InfoBar the end. */
	m_error_message = SAPP_TR("Resolving the stream address failed");
	m_event(this, evUser + 12);
	m_event(this, evEOF);
}

void eServiceApp::gotExtPlayerMessage(int message) {
	switch (message) {
		case PlayerMessage::start:
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - start");
			m_player_started = true;
			m_player_start_time = ::time(0);
			if (m_reconnected) {
				/* the new player starts without the subtitle track of the old one */
				m_reconnected = false;
				if (m_selected_subtitle_track && isEmbeddedTrack(*m_selected_subtitle_track))
					player->subtitleSelectTrack(m_selected_subtitle_track->pid);
				if (m_reconnect_channel != STEREO)
					player->audioSelectChannel(m_reconnect_channel);
				if (m_forced_only_active)
					player->subtitleForcedOnly(true);
			}
			if (!m_is_streaming && !m_cuesheet_loaded)
				loadCuesheet();
			if (m_pending_seek >= 0) {
				/* a resume seek that came before the player accepted commands */
				eDebug("eServiceApp::gotExtPlayerMessage - deferred seek to %lld", (long long)m_pending_seek);
				player->seekToMs(int(m_pending_seek / 90));
				m_pending_seek = -1;
			}
			m_event_updated_info_timer->start(1000, true);
			startDecoderVideoCheck();
			if (m_cachedAudioTrack >= 0)
				player->audioGetNumberOfTracks(0); // Asynchronous; do not wait on the GUI thread.
#ifdef HAVE_EPG
			updateEpgCacheNowNext();
#endif
			break;
		case PlayerMessage::audioTracksAvailable:
			restoreAudioTrack();
			break;
		case PlayerMessage::audioChanged:
			// live channel count or codec of the current audio track changed
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - audioChanged");
			m_event(this, evUpdatedInfo);
			break;
		case PlayerMessage::stop:
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - stop");
			// evEOF signals that end of file was reached and we
			// could make operations like seek back or play again,
			// however when player signals stop, process
			// has already ended, so there is no possibility to do so.
			// This should be fixed on player's side so it doesn't end
			// immediately but waits at the end for our input..
			m_live_check_timer->stop();
			if (m_not_live) {
				/* checkLiveRestart() stopped a film without a length that
				 * FFmpeg started again: its end, no reconnect (it counted as
				 * another clean live end and the film played up to 4 times) */
				m_event(this, evEOF);
				break;
			}
			if (!m_stopping && isLiveStream() && !isPermanentPlayerError()) {
				/* a 24/7 stream that played for a while starts both counts
				 * again: they never decayed, after 3 clean ends in a session
				 * the stream was given up */
				if (m_player_started && ::time(0) - m_player_start_time >= 600) {
					m_clean_ends = 0;
					m_live_reconnects = 0;
					m_last_clean_end_ms = -1;
				}
				errorMessage e;
				if (player->getErrorMessage(e) != 0) {
					/* an end without an error: a server that closed the stream,
					 * or a film without a length that played to its end (reset
					 * after a run of 10 minutes). FFmpeg's own reconnect is not
					 * used from now on: it would start such a film again at its
					 * end, endlessly and unseen */
					m_ffmpeg_reconnect = false;
					int end_ms = -1;
					if (player->getPlayPosition(end_ms) < 0)
						end_ms = -1;
					/* played again up to the same position: a film, its end */
					if (end_ms >= 30000 && m_last_clean_end_ms >= 0 && abs(end_ms - m_last_clean_end_ms) <= 3000) {
						eDebug("[ServiceApp] stream ended without an error at %d s again, a film without a length: no reconnect", end_ms / 1000);
						m_event(this, evEOF);
						break;
					}
					m_last_clean_end_ms = end_ms;
					if (++m_clean_ends > 3) {
						eDebug("[ServiceApp] stream ended without an error %d times, a film without a length: no reconnect", m_clean_ends);
						m_event(this, evEOF);
						break;
					}
				} else if (m_player_started && ::time(0) - m_player_start_time >= 30) {
					/* lost connection: short runs and failed attempts count, a
					 * stream that played for a while starts the count again */
					m_live_reconnects = 0;
				}
				if (m_live_reconnects < 5) {
					m_live_reconnects++;
					eDebug("[ServiceApp] live stream ended, reconnect %d/5", m_live_reconnects);
					m_live_reconnect_timer->start(m_live_reconnects == 1 ? 500 : 3000, true);
					break;
				}
				eDebug("[ServiceApp] live stream: giving up after 5 reconnects");
			}
			m_event(this, evEOF);
			break;
		case PlayerMessage::pause:
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - pause");
			m_paused = true;
			break;
		case PlayerMessage::resume:
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - resume");
			m_paused = false;
			restoreAudioTrack();
			/* paused, the subtitle timer looks only every second */
			if (m_selected_subtitle_track && m_subtitle_pages)
				m_subtitle_sync_timer->start(1, true);
			break;
		case PlayerMessage::error:
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - error");
			m_event(this, evUser + 12);
			break;
		case PlayerMessage::videoSizeChanged: {
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - videoSizeChanged");
			videoStream v;
			if (!player->videoGetTrackInfo(v, 0)) {
				if (v.width > 0 && v.height > 0) {
					m_width = v.width;
					m_height = v.height;
					m_decoder_video = false;
				} else {
					/* the size got unknown (other track): ask the decoder again */
					m_width = m_height = -1;
					startDecoderVideoCheck();
				}
			}
			m_event(this, evVideoSizeChanged);
			break;
		}
		case PlayerMessage::videoFramerateChanged: {
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - videoFramerateChanged");
			videoStream v;
			if (!player->videoGetTrackInfo(v, 0)) {
				m_framerate = v.framerate;
			}
			m_event(this, evVideoFramerateChanged);
			break;
		}
		case PlayerMessage::videoProgressiveChanged: {
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - videoProgressiveChanged");
			videoStream v;
			if (!player->videoGetTrackInfo(v, 0)) {
				m_progressive = v.progressive;
			}
			m_event(this, evVideoProgressiveChanged);
			break;
		}
		case PlayerMessage::videoGammaChanged: {
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - videoGammaChanged");
			videoStream v;
			if (!player->videoGetTrackInfo(v, 0)) {
				m_gamma = v.gamma;
			}
			m_event(this, evVideoGammaChanged);
			break;
		}
		case PlayerMessage::windStart:
			m_event(this, evSOF);
			break;
		case PlayerMessage::windEnd:
			m_event(this, evEOF);
			break;
		case PlayerMessage::subtitleAvailable:
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - subtitleAvailable");
			if (m_selected_subtitle_track && isEmbeddedTrack(*m_selected_subtitle_track))
				pullSubtitles();
			else
				/* no embedded track is shown (gstplayer2 with -e, #sapp_subtitle_id):
				 * the queue was never drained, its pictures stayed in /tmp */
				discardQueuedSubtitles();
			break;
		case PlayerMessage::subtitleFlush:
			/* seek or track switch: cues of the old position must not come back */
			if (m_selected_subtitle_track && isEmbeddedTrack(*m_selected_subtitle_track)) {
				/* cues still queued came after the flush (the player thread dropped
				 * the older ones): they are for the new position, and a graphic one
				 * may have a file of the same name as an old cue */
				std::queue<subtitleMessage> pulled;
				player->getSubtitles(pulled);
				std::set<std::string> keep;
				std::queue<subtitleMessage> check(pulled);
				for (; !check.empty(); check.pop())
					for (size_t i = 0; i < check.front().images.size(); i++)
						keep.insert(check.front().images[i].file);
				m_prev_subtitle_message = NULL;
				clearEmbeddedSubtitlePages(true, &keep);
				addSubtitles(pulled);
				if (!m_embedded_subtitle_pages.empty())
					m_subtitle_sync_timer->start(1, true);
			}
			break;
		case PlayerMessage::subtitleTracksChanged:
			/* closed captions (CC1) showed up: the subtitle list is new */
			if (m_pending_subtitle_track && m_pending_subtitle_user) {
				SubtitleTrack track = *m_pending_subtitle_track;
				eDebug("eServiceApp::gotExtPlayerMessage - subtitle list arrived, selecting the pending track %d", track.pid);
				enableSubtitles(m_pending_subtitle_user, track);
			}
			m_event(this, evUpdatedInfo);
			break;
		case PlayerMessage::metadataChanged:
			/* title / artist of a radio stream: sTagTitle, sTagArtist */
			m_event(this, evUpdatedInfo);
			break;
		case PlayerMessage::chaptersChanged: {
			/* chapters of the file as marks of the cut list (as servicemp3 does
			 * with TOC chapters); they are not written to the .cuts file */
			std::vector<chapterEntry> chapters;
			player->getChapters(chapters);
			m_chapter_marks.clear();
			for (std::vector<chapterEntry>::const_iterator i(chapters.begin()); i != chapters.end(); ++i) {
				if (i->start_ms > 0)
					m_chapter_marks.insert((pts_t)i->start_ms * 90);
			}
			eDebug("eServiceApp::gotExtPlayerMessage - %zu chapters", chapters.size());
			m_event((iPlayableService*)this, evCuesheetChanged);
			break;
		}
		case PlayerMessage::coverAvailable:
			/* the embedded cover is in /tmp/.id3coverart; evUpdateIDv3Cover is
			 * what servicemp3 of openATV sends and the MediaPlayer shows */
			m_event((iPlayableService*)this, evUpdateIDv3Cover);
			break;
		default:
			if (m_debug)
				eDebug("eServiceApp::gotExtPlayerMessage - unhandled message");
			break;
	}
}


// __iPlayableService
RESULT eServiceApp::connectEvent(const sigc::slot<void(iPlayableService*, int)>& event, ePtr<eConnection>& connection) {
	connection = new eConnection((iPlayableService*)this, m_event.connect(event));
	return 0;
}

RESULT eServiceApp::start() {
	if (!m_event_started) {
		m_event(this, evUpdatedEventInfo);
		m_event(this, evStart);
		m_event_started = true;
	}
	/* the cut list of a local file does not need the player: load it now, the
	   resume check of the GUI runs a second after evStart */
	if (!m_is_streaming && !m_cuesheet_loaded)
		loadCuesheet();
	/* a "forced only" pick of getCachedSubtitle() is for one enableSubtitles() */
	m_forced_only_pid = -1;
	/* the .srt files are looked for again once per start */
	m_external_subtitles_scanned = false;
	std::string path_str(m_ref.path);

	if (path_str.find(m_resolve_uri) == 0) {
		m_resolver = std::make_unique<ResolveUrl>(m_ref.path.substr(m_resolve_uri.size()));
		CONNECT(m_resolver->urlResolved, eServiceApp::urlResolved);
		m_resolver->start();
		return 0;
	}
	if (options->HLSExplorer && options->autoSelectStream && !m_subservices_checked) {
		/* the variant is picked from the master playlist: the player starts
		 * when it has been read (subservicesExplored), the GUI goes on */
		if (exploreSubservices()) {
			m_hls_start_pending = true;
			return 0;
		}
		/* nothing to read: as a fetch that found no variants */
		fillSubservices(std::vector<M3U8StreamInfo>());
		m_subservices_checked = true;
		m_event(this, evUpdatedEventInfo);
	}
	startPlayer();
	return 0;
}

/* the second half of start(), after the master playlist has been read */
void eServiceApp::startPlayer() {
	std::string path_str(m_ref.path);
	HeaderMap headers = getHttpHeaders(m_ref.path);
	if (options->HLSExplorer && options->autoSelectStream) {
		size_t subservice_num = m_subservice_vec.size();
		if (subservice_num) {
			M3U8StreamInfo subservice = *(m_subservice_vec.begin());
			unsigned int subservice_flag = m_ref.getUnsignedData(7);
			bool bitrate_selection = (!subservice_flag || subservice_flag >= SUBSERVICES_BITRATEKB_START);
			if (bitrate_selection) {
				unsigned int bitrate = 0;
				if (subservice_flag)
					bitrate = (subservice_flag - SUBSERVICES_BITRATEKB_START);
				else
					bitrate = options->connectionSpeedInKb;
				// vector is sorted from best to lowest quality in fillSubservices
				std::vector<M3U8StreamInfo>::const_reverse_iterator it(m_subservice_vec.rbegin());
				while (it != m_subservice_vec.rend()) {
					if (it->bitrate > bitrate * 1000L) {
						if (it != m_subservice_vec.rbegin())
							subservice = *(--it);
						else
							subservice = *(it);
						break;
					}
					it++;
				}
				eDebug("eServiceApp::start - subservice(%lub/s) selected according to connection speed (%lu)", (unsigned long)subservice.bitrate, (unsigned long)bitrate * 1000UL);
			} else {
				unsigned int subservice_idx = subservice_flag - SUBSERVICES_INDEX_START;
				if (subservice_idx < subservice_num) {
					subservice = m_subservice_vec[subservice_idx];
				} else {
					eWarning("eServiceApp::start - subservice_idx(%u) >= subservice_num(%zu), assuming lowest quality", subservice_idx, subservice_num);
					subservice = *(m_subservice_vec.end() - 1);
				}
				eDebug("eServiceApp::start - subservice(%lub/s) selected according to index(%u)", subservice.bitrate, subservice_idx);
			}
			path_str = subservice.url;
			headers = subservice.headers;
		}
	}
	m_debug = eSimpleConfig::getBool("config.plugins.serviceapp.debug", false);
	player->setDebug(m_debug);

	// don't pass fragment part to player
	player->start(Url(path_str).url(), headers);
}

RESULT eServiceApp::stop() {
	if (m_debug)
		eDebug("eServiceApp::stop");
	m_stopping = true;
	m_live_reconnect_timer->stop();
	m_live_check_timer->stop();
	if (!m_is_streaming && m_cuesheet_loaded)
		saveCuesheet();
	if (m_resolver)
		m_resolver->stop();
	/* a master playlist still being read must not start the player later */
	cancelSubservicesExplorer();
	m_decoder_video_timer->stop();
	player->stop();
	/* graphic subtitles: the files of the pages left (the player removes its
	 * directory when it has ended); also pages of a track that was switched
	 * off or replaced by an external one */
	m_subtitle_sync_timer->stop();
	if (!m_embedded_subtitle_pages.empty() || m_graphic_shown)
		clearEmbeddedSubtitlePages(true);
	return 0;
}

void eServiceApp::checkLiveRestart() {
	/* live TS keeps its timestamps over a reconnect of FFmpeg; a film without
	 * a length that FFmpeg starts again at byte 0 jumps back to its start */
	pts_t pos;
	if (getPlayPosition(pos) < 0)
		return;
	if (pos > m_max_position) {
		m_max_position = pos;
		return;
	}
	if (m_max_position - pos > 30 * 90000LL && pos < 10 * 90000LL) {
		eDebug("[ServiceApp] position back from %lld to %lld s: a film without a length started again, ended", (long long)(m_max_position / 90000), (long long)(pos / 90000));
		m_live_check_timer->stop();
		m_ffmpeg_reconnect = false; /* the next start without the reconnect of FFmpeg */
		m_not_live = true; /* the stop handler ends it with evEOF */
		player->stop();
	}
}

bool eServiceApp::isLiveStream() {
	/* a network stream without a length; one that never started is live only
	 * while it is reconnected (a first open that fails ends as before) */
	if (!m_is_streaming || m_start_path.compare(0, 7, "file://") == 0)
		return false;
	if (!m_player_started)
		return m_live_reconnects > 0;
	return player->lengthMs() <= 0;
}

/* FFmpeg's FFERRTAG, the HTTP errors of FF_ERROR */
#define SAPP_FFERRTAG(a, b, c, d) (-(int)((unsigned)(a) | ((unsigned)(b) << 8) | ((unsigned)(c) << 16) | ((unsigned)(d) << 24)))

bool eServiceApp::isPermanentPlayerError() {
	/* a reconnect does not help: the player does not take its options, the
	 * file or the page is missing, access is denied, no key for the video */
	if (player->exitCode() == 1)
		return true;
	errorMessage e;
	if (player->getErrorMessage(e))
		return false;
	switch (e.code) {
		case -2:  // ENOENT
		case -13: // EACCES, also "No key for KID" of exteplayer3
		case SAPP_FFERRTAG(0xF8, '4', '0', '0'):
		case SAPP_FFERRTAG(0xF8, '4', '0', '1'):
		case SAPP_FFERRTAG(0xF8, '4', '0', '3'):
		case SAPP_FFERRTAG(0xF8, '4', '0', '4'):
		case SAPP_FFERRTAG(0xF8, '4', 'X', 'X'):
			eDebug("[ServiceApp] player error %d (%s): no reconnect", e.code, e.message.c_str());
			return true;
		default:
			return false;
	}
}

void eServiceApp::liveReconnect() {
	if (m_stopping)
		return;
	eDebug("[ServiceApp] live stream: reconnecting %s", m_start_path.c_str());
	/* keep the audio track; the subtitle track is selected again at the start */
	int audio_track = player->audioGetCurrentTrackNum();
	if (audio_track > 0)
		m_cachedAudioTrack = audio_track;
	m_reconnect_channel = player->audioGetChannel();
	m_subtitle_sync_timer->stop();
	m_prev_subtitle_message = NULL;
	clearEmbeddedSubtitlePages(true);
	m_decoder_video_timer->stop();
	if (m_resolver) {
		m_resolver->stop();
		m_resolver.reset();
	}
	/* a resolved url may have expired, resolve it again */
	m_ref.path = m_start_path;
	/* so may the HLS variant urls of the master playlist (tokens): read it
	 * again, the cached variant gave 403 and evEOF; a read still running
	 * (getNumberOfSubservices) may have the old urls, start() begins anew */
	cancelSubservicesExplorer();
	m_subservices_checked = false;
	m_subservice_vec.clear();
	m_subserviceref_vec.clear();
	player->stop();
	/* the old players go before the new one is created */
	player.reset();
	extplayer.reset();
	extplayer = createPlayer(m_ref, getHeaders(m_ref.path), m_user_settings);
	if (extplayer && m_ffmpeg_reconnect) {
		/* FFmpeg reconnects it itself from now on; watch for a film that
		 * starts again at byte 0 */
		extplayer->setLiveStream(true);
		m_max_position = 0;
		m_live_check_timer->start(1000, false);
	}
	player = std::make_unique<PlayerBackend>(extplayer.get());
	CONNECT(player->gotPlayerMessage, eServiceApp::gotExtPlayerMessage);
	if (m_selected_subtitle_track)
		player->setPositionInterval(100); // as enableSubtitles() set it for the old player
	m_player_started = false;
	m_paused = false;
	m_reconnected = true;
	start();
	/* an external subtitle file keeps running on the new player's position */
	if (m_selected_subtitle_track && isExternalTrack(*m_selected_subtitle_track) && m_subtitle_pages)
		m_subtitle_sync_timer->start(1, true);
}

// __iPausableService
RESULT eServiceApp::pause() {
	eDebug("eServiceApp::pause");
	player->pause();
	return 0;
}

RESULT eServiceApp::unpause() {
	eDebug("eServiceApp::unpause");
	player->resume();
	return 0;
}

RESULT eServiceApp::setSlowMotion(int ratio) {
	eDebug("eServiceApp::setSlowMotion - ratio = %d", ratio);
	return -1;
}

RESULT eServiceApp::setFastForward(int ratio) {
	eDebug("eServiceApp::setFastForward - ratio = %d", ratio);
	if (ratio == 0 || ratio == 1)
		return player->resume();
	return player->fastForward(ratio);
}


// __iSeekableService
RESULT eServiceApp::getLength(pts_t& pts) {
	// eDebug("eServiceApp::getLength");
	int length;
	if (player->getLength(length) < 0) {
		return -1;
	}
	pts = (pts_t)length * 90;
	m_media_length = pts;
	return 0;
}

RESULT eServiceApp::seekTo(pts_t to) {
	eDebug("eServiceApp::seekTo - position = %lld", to);
	pts_t length;
	if (to == -1 && player->canWind() && getLength(length) == 0 && length > 90000) {
		/* winding reached the end: a second before it */
		to = length - 90000;
	} else if (to < 0) {
		to = 0;
	} else if (getLength(length) < 0) {
		eWarning("eServiceApp::seekTo - cannot get length");
	} else if (length > 0 && to > length) {
		stop();
		return 0;
	}
	/* ms: exteplayer3 187 seeks with a fraction (gf), older players get seconds */
	if (player->seekToMs(int(to / 90)) < 0)
		m_pending_seek = to; /* the player has not started yet, seek when it has */
	else
		m_pending_seek = -1;

	m_prev_decoder_time = -1;
	m_decoder_time_valid_state = 0;
	if (m_selected_subtitle_track != NULL) {
		m_subtitle_sync_timer->start(1, true);
	}
	return 0;
}

RESULT eServiceApp::seekRelative(int direction, pts_t to) {
	eDebug("eServiceApp::seekRelative - position = %lld", direction * to);
	pts_t position;
	if (getPlayPosition(position) < 0) {
		eWarning("eServiceApp::seekRelative - cannot get play position");
		return -1;
	}
	return seekTo(position + (to * direction));
}

RESULT eServiceApp::getPlayPosition(pts_t& pts) {
	// eDebug("eServiceApp::getPlayPosition");
	int position;
	if (player->getPlayPosition(position) < 0) {
		return -1;
	}
	pts = (pts_t)position * 90;
	m_last_seek_pos = pts;
	return 0;
}

RESULT eServiceApp::setTrickmode(int trick) {
	if (m_debug)
		eDebug("eServiceApp::setTrickmode = %d", trick);
	return -1;
}

RESULT eServiceApp::isCurrentlySeekable() {
	if (m_debug)
		eDebug("eServiceApp::isCurrentlySeekable");
	/* a player that winds itself does it at every speed and backwards (bit 4)
	 * unless the user wants jumps; for the others just assume that seeking and
	 * fast/slow winding are possible */
	/* a live network stream has no length, a VOD stream has one;
	 * -2: the player runs but has no length */
	int lengthMs;
	if (m_ref.path.find("://") != std::string::npos && player->getLength(lengthMs) == -2)
		return 0;
	pts_t length;
	if (player->canWind() && getLength(length) == 0 && length > 0)
		return eConfigManager::getConfigBoolValue("config.seek.withjumps", true) ? 3 : 7;
	return 3;
}


// __iAudioTrackSelection
void eServiceApp::restoreAudioTrack() {
	if (m_paused || m_cachedAudioTrack < 0)
		return;
	audioStream track;
	if (player->audioGetTrackInfo(track, m_cachedAudioTrack) < 0)
		return; // GstPlayer can report a partial list before discovering all tracks.
	const int wanted = m_cachedAudioTrack;
	m_cachedAudioTrack = -1;
	// Translate the saved list index to the player's stream ID. A missing track
	// leaves the player's default/language selection intact and keeps the cache.
	player->audioSelectTrack(wanted);
}

void eServiceApp::saveAudioTrack(int track) {
	auto& services = eDVBDB::getInstance()->iptv_services;
	for (const auto& service : services) {
		if (service->m_reference_str == m_audioCacheReference) {
			service->setCacheEntry(eDVBService::cMPEGAPID, track);
			return;
		}
	}
	ePtr<eDVBService> service = new eDVBService;
	service->m_reference_str = m_audioCacheReference;
	service->setCacheEntry(eDVBService::cMPEGAPID, track);
	services.push_back(service);
}

int eServiceApp::getNumberOfTracks() {
	/* polled by the GUI: logged only in debug mode */
	if (m_debug)
		eDebug("eServiceApp::getNumberOfTracks");
	return player->audioGetNumberOfTracks(500);
}

RESULT eServiceApp::selectTrack(unsigned int i) {
	if (m_debug)
		eDebug("eServiceApp::selectTrack = %u", i);
	if (m_paused) {
		audioStream track;
		if (player->audioGetTrackInfo(track, i) < 0)
			return -1;
		// GstPlayer may reset a selector changed in PAUSED when it resumes.
		m_cachedAudioTrack = i;
		saveAudioTrack(i);
		return 0;
	}
	if (player->audioSelectTrack(i) < 0) {
		return -1;
	}
	m_cachedAudioTrack = -1;
	saveAudioTrack(i);
	return 0;
}

RESULT eServiceApp::getTrackInfo(iAudioTrackInfo& trackInfo, unsigned int n) {
	if (m_debug)
		eDebug("eServiceApp::getTrackInfo = %d", n);
	audioStream track;
	if (player->audioGetTrackInfo(track, n) < 0) {
		return -1;
	}

	std::string desc = track.description;

	/* exteplayer3 names MPEG-1/2 Layer I/II "A_MPEG/L3"; IPCM and LPCM are PCM, LPCM stays as it is */
	std::map<std::string, std::string> audioReplacements = {{"A_", ""},		   {"EAC3", "AC3+"},	   {"MPEG/L3", "MP2"},	{"IPCM", "PCM"},
															{"AAC_PLUS", "AAC+"}, {"AAC_LATM", "AAC"}, {"WMA/PRO", "WMA Pro"}};

	if (!desc.empty()) {
		for (auto const& x : audioReplacements) {
			std::string s = x.first;
			if (desc.length() >= s.length()) {
				size_t loc = desc.find(s);
				if (loc != std::string::npos) {
					desc.replace(loc, s.length(), x.second);
				}
			}
		}
	}

	/* newer exteplayer3 versions name the codec ("Dolby Atmos", "HE-AAC v2")
	 * and send the channel count */
	if (!track.codec_name.empty()) {
		desc = track.codec_name;
	}
	/* the layout the player names ("5.1(side)" -> "5.1"), else one from the channel count */
	std::string layout = track.channel_layout;
	size_t paren = layout.find('(');
	if (paren != std::string::npos)
		layout.erase(paren);
	if (!layout.empty() && isdigit((unsigned char)layout[0])) {
		desc += " " + layout;
	} else if (layout == "mono" || layout == "stereo") {
		desc += layout == "mono" ? " 1.0" : " 2.0";
	} else if (track.channels > 0) {
		static const char* layouts[] = {"", "1.0", "2.0", "2.1", "4.0", "5.0", "5.1", "6.1", "7.1"};
		char buf[16];
		if (track.channels < 9)
			snprintf(buf, sizeof(buf), " %s", layouts[track.channels]);
		else
			snprintf(buf, sizeof(buf), " %dch", track.channels);
		desc += buf;
	}
	/* track title of the container, e.g. "Commentary", "Audio Description" */
	if (!track.title.empty())
		desc += " (" + track.title + ")";

	trackInfo.m_description = desc;
	trackInfo.m_language = track.language_code;
	trackInfo.m_pid = track.id;
	return 0;
}

int eServiceApp::getCurrentTrack() {
	if (m_debug)
		eDebug("eServiceApp::getCurrentTrack");
	return player->audioGetCurrentTrackNum();
}


// __iAudioChannelSelection
int eServiceApp::getCurrentChannel() {
	if (m_debug)
		eDebug("eServiceApp::getCurrentChannel");
	/* dual mono with exteplayer3 187: LEFT 0, STEREO 1, RIGHT 2 = am<n> */
	if (player->canSelectAudioChannel())
		return player->audioGetChannel();
	return STEREO;
}

RESULT eServiceApp::selectChannel(int i) {
	if (m_debug)
		eDebug("eServiceApp::selectChannel %d", i);
	if (i != LEFT && i != STEREO && i != RIGHT)
		return -1;
	return player->audioSelectChannel(i);
}


// __iSubtitleOutput
/* the embedded track is no longer shown: the player stops decoding it, its
 * queued and kept cues (and their pictures in /tmp) go, a graphic page on the
 * screen is hidden. tell_player: an embedded track was selected. */
void eServiceApp::stopEmbeddedSubtitles(bool tell_player) {
	// an embedded track is decoded by the player until it is told to stop
	if (tell_player)
		player->subtitleSelectTrack(-1);
	discardQueuedSubtitles();
	clearEmbeddedSubtitlePages(true);
	if (m_forced_only_active) {
		player->subtitleForcedOnly(false);
		m_forced_only_active = false;
	}
}

/* cues the player queued that are not shown: their pictures can go */
void eServiceApp::discardQueuedSubtitles() {
	std::queue<subtitleMessage> pulled;
	player->getSubtitles(pulled);
	for (; !pulled.empty(); pulled.pop())
		removeSubtitleFiles(pulled.front());
}

RESULT eServiceApp::enableSubtitles(iSubtitleUser* user, struct SubtitleTrack& track) {
	m_subtitle_sync_timer->stop();
	bool was_embedded = m_selected_subtitle_track && isEmbeddedTrack(*m_selected_subtitle_track);
	/* the InfoBar selects the current track again (restartSubtitle() on
	 * evSeekableStatusChanged): it keeps "forced only" (box test: so1, then
	 * so0 0.9 s later). The subtitle menu calls only for another track. */
	const SubtitleTrack* prev = m_selected_subtitle_track.get();
	bool same_track = prev && prev->type == track.type && prev->pid == track.pid && prev->page_number == track.page_number
		&& prev->magazine_number == track.magazine_number && prev->language_code == track.language_code;
	m_prev_subtitle_message = NULL;
	m_subtitle_pages = NULL;
	m_selected_subtitle_track.reset();

	m_decoder_time_valid_state = 0;
	m_prev_decoder_time = -1;

	/* "forced only" belongs to the track getCachedSubtitle() just picked;
	 * any other call (also a failing one) leaves no stale pid behind */
	int forced_only_pid = m_forced_only_pid;
	m_forced_only_pid = -1;

	/* the page of the former track goes; hide it when the widget stays */
	if (m_subtitle_widget != user)
		m_graphic_shown = false;

	/* an embedded track switched to an external one, or a failed selection:
	 * the player went on sending the old track (queue and PNG files grew for
	 * the whole film, its last graphic page came back every 5 s) */
	auto fail = [&]() {
		stopEmbeddedSubtitles(was_embedded);
		player->setPositionInterval(500);
		return -1;
	};

	ssize_t track_pos = getTrackPosition(track);
	if (track_pos == -1) {
		/* the map is filled by getSubtitleList()/getCachedSubtitle(); the
		 * InfoBar can come first: restartSubtitle() on evSeekableStatusChanged
		 * with the track it had selected (box test: subtitles of a stream
		 * stayed off). Read the list now, without waiting. */
		std::vector<struct SubtitleTrack> tracks;
		if (fillSubtitleList(tracks, 0) == 0)
			track_pos = getTrackPosition(track);
	}
	if (track_pos == -1) {
		eWarning("eServiceApp::enableSubtitles - track is not in the map (type %d, pid %d, page %d, magazine %d, language '%s'; %zu tracks known)",
			track.type, track.pid, track.page_number, track.magazine_number, track.language_code.c_str(), m_subtitle_tracks.size());
		int ret = fail();
		/* no list yet: an embedded track is selected when the list arrives */
		if (m_subtitle_tracks.empty() && track.type == 2) {
			m_pending_subtitle_track = std::make_unique<SubtitleTrack const>(track);
			m_pending_subtitle_user = user;
		}
		return ret;
	}
	m_pending_subtitle_track.reset();
	m_pending_subtitle_user = nullptr;
	if (isEmbeddedTrack(track)) {
		eDebug("eServiceApp::enableSubtitles - track = %d (embedded)", track.pid);
		clearEmbeddedSubtitlePages(true);
		m_subtitle_pages = &m_embedded_subtitle_pages;
		player->subtitleSelectTrack(track.pid);
	} else if (isExternalTrack(track)) {
		eDebug("eServiceApp::enableSubtitles - track = %d (external)", track.pid);
		stopEmbeddedSubtitles(was_embedded);
		subtitleStream s = m_subtitle_streams[track_pos];
		m_subtitle_pages = m_subtitle_manager.load(s.path);
		if (m_subtitle_pages != NULL) {
			m_subtitle_sync_timer->start(1, true);
		} else {
			eWarning("eServiceApp::enableSubtitles - cannot load external subtitles");
			return fail();
		}
	} else {
		eWarning("eServiceApp::enableSubtitles - not supported track page_number %d", track.page_number);
		return fail();
	}
	// own a standalone copy: getSubtitleList() clears m_subtitle_tracks
	// while playing (the UI calls it to repopulate), which would leave a
	// pointer into freed vector storage dangling -> UAF in pushSubtitles.
	m_selected_subtitle_track = std::make_unique<SubtitleTrack const>(m_subtitle_tracks[track_pos]);
	m_subtitle_widget = user;
	/* the cue timing needs the position every 100 ms, the rest of the time
	 * 500 ms do (10 round trips to the player a second before) */
	player->setPositionInterval(100);
	/* forced only: the track the "Forced only" auto mode picked for its
	 * forced pictures; any other selection shows the whole track again */
	bool forced_only = isEmbeddedTrack(track) && (track.pid == forced_only_pid || (same_track && m_forced_only_active));
	if (forced_only != m_forced_only_active) {
		player->subtitleForcedOnly(forced_only);
		m_forced_only_active = forced_only;
	}
	return 0;
}

RESULT eServiceApp::disableSubtitles() {
	eDebug("eServiceApp::disableSubtitles");
	m_pending_subtitle_track.reset();
	m_pending_subtitle_user = nullptr;
	m_subtitle_sync_timer->stop();
	m_prev_subtitle_message = NULL;
	m_forced_only_pid = -1;
	/* queued cues are not shown any more: their pictures can go */
	stopEmbeddedSubtitles(true);
	m_subtitle_pages = NULL;
	m_selected_subtitle_track.reset();
	if (m_subtitle_widget)
		m_subtitle_widget->destroy();
	m_subtitle_widget = 0;
	player->setPositionInterval(500);

	m_decoder_time_valid_state = 0;
	m_prev_decoder_time = -1;
	return 0;
}

/* the space separated language codes of a value like config.autolanguage's "deu ger", "---" is none */
static void appendLanguageCodes(std::vector<std::string>& langs, const std::string& value) {
	std::istringstream ss(value);
	std::string code;
	while (ss >> code) {
		if (code == "---")
			continue;
		std::transform(code.begin(), code.end(), code.begin(), ::tolower);
		if (std::find(langs.begin(), langs.end(), code) == langs.end())
			langs.push_back(code);
	}
}

RESULT eServiceApp::getCachedSubtitle(struct SubtitleTrack& track) {
	/* a pick of an earlier call that enableSubtitles() did not get */
	m_forced_only_pid = -1;
	if (options->autoTurnOnSubtitles == SUBTITLES_AUTO_OFF) {
		eDebug("eServiceApp::getCachedSubtitle - auto-turning disabled in config");
		return -1;
	}
	std::vector<struct SubtitleTrack> tracks;
	/* no blocking wait: the InfoBar asks on every evUpdatedInfo; an unknown
	 * list is requested, an answer that changes it sends evUpdatedInfo */
	if (fillSubtitleList(tracks, 0) < 0 || tracks.empty()) {
		eDebug("eServiceApp::getCachedSubtitle - no subtitles available");
		return -1;
	}
	if (options->autoTurnOnSubtitles == SUBTITLES_AUTO_FORCED) {
		/* only an embedded forced track (the foreign parts of the audio) in the
		 * language of the current audio track, else in the preferred audio
		 * languages of the box */
		std::vector<std::string> langs;
		audioStream audio;
		if (player->audioGetTrackInfo(audio, player->audioGetCurrentTrackNum()) == 0)
			appendLanguageCodes(langs, audio.language_code);
		for (int i = 1; i <= 4; i++) {
			std::string key = "config.autolanguage.audio_autoselect" + std::to_string(i);
			appendLanguageCodes(langs, eSimpleConfig::getString(key.c_str(), ""));
		}
		std::vector<struct SubtitleTrack> forced_tracks;
		for (auto const& t : tracks) {
			if (!isEmbeddedTrack(t))
				continue;
			ssize_t pos = getTrackPosition(t);
			if (pos >= 0 && m_subtitle_streams[pos].forced > 0)
				forced_tracks.push_back(t);
		}
		for (auto const& lang : langs) {
			for (auto const& t : forced_tracks) {
				std::string code = t.language_code;
				std::transform(code.begin(), code.end(), code.begin(), ::tolower);
				if (code == lang) {
					track = t;
					eDebug("eServiceApp::getCachedSubtitle - selected forced track (%s)", code.c_str());
					return 0;
				}
			}
		}
		if (langs.empty() && !forced_tracks.empty()) {
			track = forced_tracks.front();
			eDebug("eServiceApp::getCachedSubtitle - selected forced track, no audio language known");
			return 0;
		}
		/* Blu-ray rips often have no forced track of their own: the forced
		 * pictures are marked in the full PGS track. exteplayer3 187 sends only
		 * those with "forcedonly" (so1), enableSubtitles turns it on. */
		if (player->canSubtitleForcedOnly()) {
			for (auto const& lang : langs) {
				for (auto const& t : tracks) {
					if (!isEmbeddedTrack(t))
						continue;
					ssize_t pos = getTrackPosition(t);
					if (pos < 0 || m_subtitle_streams[pos].description.find("PGS") == std::string::npos)
						continue;
					std::string code = t.language_code;
					std::transform(code.begin(), code.end(), code.begin(), ::tolower);
					if (code == lang) {
						track = t;
						m_forced_only_pid = t.pid;
						eDebug("eServiceApp::getCachedSubtitle - PGS track (%s) with only its forced pictures", code.c_str());
						return 0;
					}
				}
			}
		}
		eDebug("eServiceApp::getCachedSubtitle - no forced track in the audio language (%zu forced tracks)", forced_tracks.size());
		return -1;
	}
	// TODO consider language setting
	int ret = -1;
	std::vector<struct SubtitleTrack> embedded_tracks;
	std::vector<struct SubtitleTrack> external_tracks;
	std::remove_copy_if(tracks.begin(), tracks.end(), std::back_inserter(embedded_tracks), isExternalTrack);
	std::remove_copy_if(tracks.begin(), tracks.end(), std::back_inserter(external_tracks), isEmbeddedTrack);

	bool select_embedded = (options->preferEmbeddedSubtitles || external_tracks.empty()) && !embedded_tracks.empty();
	if (!select_embedded) {
		struct SubtitleTrack tmp_track = *external_tracks.begin();
		ssize_t tmp_pos = getTrackPosition(tmp_track);
		if (tmp_pos < 0) {
			// same unchecked -1 indexing as in pushSubtitles; fall back to embedded tracks
			select_embedded = !embedded_tracks.empty();
		} else {
			subtitleStream tmp_stream = m_subtitle_streams[tmp_pos];
			std::string video_base, subtitle_base, extension;
			splitExtension(m_ref.path, video_base, extension);
			splitExtension(tmp_stream.path, subtitle_base, extension);
			if (video_base == subtitle_base || external_tracks.size() == 1) {
				track = tmp_track;
				ret = 0;
			} else {
				select_embedded = !embedded_tracks.empty();
			}
		}
	}
	if (select_embedded) {
		track = *embedded_tracks.begin();
		ret = 0;
	}

	if (ret == 0) {
		if (options->preferEmbeddedSubtitles && isEmbeddedTrack(track))
			eDebug("eServiceApp::getCachedSubtitle - selected preferred embedded track");
		else if (options->preferEmbeddedSubtitles && !isEmbeddedTrack(track))
			eDebug("eServiceApp::getCachedSubtitle - selected external track");
		else if (!options->preferEmbeddedSubtitles && isExternalTrack(track))
			eDebug("eServiceApp::getCachedSubtitle - selected preferred external track");
		else if (!options->preferEmbeddedSubtitles && !isExternalTrack(track))
			eDebug("eServiceApp::getCachedSubtitle - selected embedded track");
	} else {
		eDebug("eServiceApp::getCachedSubtitle - no track selected, more than one external track found, name doesn't correspond to video file");
	}
	return ret;
}

RESULT eServiceApp::getSubtitleList(std::vector<struct SubtitleTrack>& subtitlelist) {
	return fillSubtitleList(subtitlelist, 500);
}

/* the .srt files next to a local file and in its Subs directory: scanned once
 * per start, the GUI asks for the list often (a stat() per directory entry,
 * thousands in a movie directory, in the GUI thread every time) */
void eServiceApp::scanExternalSubtitles() {
	m_external_subtitles.clear();
	m_external_subtitles_scanned = true;
	std::string basename, extension;
	splitExtension(m_ref.path, basename, extension);
	std::string subtitle_path(basename + ".srt");

	std::string dirname, filename;
	splitPath(subtitle_path, dirname, filename);
	// TODO
	//
	// - try to find out language code from filename if possible
	// - apply some sort of sorting which would add more relevant subtitles to beginning
	// of the list
	//
	// - probably whole thing should be moved to manager
	if (!access(subtitle_path.c_str(), F_OK)) {
		m_external_subtitles.push_back(std::make_pair(filename, subtitle_path));
	}
	std::vector<std::string> directories, files;
	if (listDir(dirname, &files, &directories) == 0) {
		std::vector<std::string>::const_iterator it;
		if ((std::find(directories.begin(), directories.end(), "Subs")) != directories.end()) {
			std::vector<std::string> subsdir_files;
			if (listDir(dirname + "/Subs", &subsdir_files, NULL) == 0) {
				for (it = subsdir_files.begin(); it != subsdir_files.end(); it++) {
					splitExtension(*it, basename, extension);
					if (extension == ".srt") {
						m_external_subtitles.push_back(std::make_pair(basename, dirname + "/Subs/" + *it));
					}
				}
			}
		}
		for (it = files.begin(); it != files.end(); it++) {
			splitExtension(*it, basename, extension);
			std::string path = dirname + "/" + *it;
			if (extension == ".srt" && subtitle_path != path) {
				m_external_subtitles.push_back(std::make_pair(basename, path));
			}
		}
	}
}

/* timeout of PlayerBackend::subtitleGetNumberOfTracks: 0 never waits */
RESULT eServiceApp::fillSubtitleList(std::vector<struct SubtitleTrack>& subtitlelist, int timeout) {
	m_subtitle_tracks.clear();
	m_subtitle_streams.clear();
	int embedded_track_num = player->subtitleGetNumberOfTracks(timeout);
	if (m_debug)
		eDebug("eServiceApp::getSubtitleList - found embedded tracks (%d)", embedded_track_num);
	int pid = 0;
	for (; pid < embedded_track_num; pid++) {
		subtitleStream s;
		if (player->subtitleGetTrackInfo(s, pid) == 0) {
			addEmbeddedTrack(subtitlelist, s, pid);
		}
	}
	if (m_is_streaming) {
		/* no .srt next to a url: listDir("http://...") only logs an error
		 * with the stream url on every call */
		return 0;
	}
	if (!m_external_subtitles_scanned)
		scanExternalSubtitles();
	for (size_t i = 0; i < m_external_subtitles.size(); i++)
		addExternalTrack(subtitlelist, pid++, m_external_subtitles[i].first, m_external_subtitles[i].second);
	if (m_debug)
		eDebug("eServiceApp::getSubtitleList - found external tracks (%d)", pid - embedded_track_num);
	return 0;
}

// __iSubservices
int eServiceApp::getNumberOfSubservices() {
	std::string path_str(m_ref.path);
	/* not for a resolve:// url, it is not resolved yet */
	if (options->HLSExplorer && path_str.find(m_resolve_uri) != 0 && !m_subservices_checked && !m_hls_explorer->running()) {
		/* never read on the GUI thread: 0 for now, evUpdatedEventInfo of
		 * subservicesExplored() makes the GUI ask again */
		if (!exploreSubservices()) {
			fillSubservices(std::vector<M3U8StreamInfo>());
			m_subservices_checked = true;
		}
	}
	if (m_debug)
		eDebug("eServiceApp::getNumberOfSubservices - %zu", m_subserviceref_vec.size());
	return m_subserviceref_vec.size();
}

RESULT eServiceApp::getSubservice(eServiceReference& subservice, unsigned int n) {
	if (m_debug)
		eDebug("eServiceApp::getSubservice - %u", n);
	if (n >= m_subserviceref_vec.size())
		return -1;
	subservice = m_subserviceref_vec[n];
	return 0;
}

// __iServiceInformation
RESULT eServiceApp::getName(std::string& name) {
	std::string title = m_ref.getName();
	if (title.empty()) {
		name = m_ref.path;
		size_t n = name.rfind('/');
		if (n != std::string::npos)
			name = name.substr(n + 1);
	} else
		name = title;
	return 0;
}

#ifdef HAVE_EPG
RESULT eServiceApp::getEvent(ePtr<eServiceEvent>& evt, int nownext) {
	evt = nownext ? m_event_next : m_event_now;
	if (!evt)
		return -1;
	return 0;
}
#endif

int eServiceApp::getInfo(int w) {
	switch (w) {
		case sVideoHeight:
			return m_height;
		case sVideoWidth:
			return m_width;
		case sFrameRate:
			return m_framerate;
		case sProgressive:
			return m_progressive;
		case sGamma:
			return m_gamma;
		case sAspect: {
			if (m_height <= 0 || m_width <= 0) {
				return -1;
			}
			float aspect = m_width / float(m_height);
			/* the sample aspect ratio of exteplayer3 (v_c "an"/"ad"): anamorphic
			 * SD (720x576 shown as 16:9) is wide although its pixels are not */
			videoStream v;
			if (!player->videoGetTrackInfo(v, 0) && v.sar_num > 0 && v.sar_den > 0)
				aspect = aspect * v.sar_num / float(v.sar_den);
			// according to wikipedia, widescreen is when width to height is greater then 1.37:1
			if (aspect > 1.37) {
				// WIDESCREEN values from ServiceInfo.py: 3, 4, 7, 8, 0xB, 0xC, 0xF, 0x10
				return 3;
			} else {
				// 4:3 values from ServiceInfo.py: 1, 2, 5, 6, 9, 0xA, 0xD, 0xE
				return 1;
			}
			return 2;
		}
		case sServiceref:
		case sTagTitle:
		case sTagArtist:
		case sTagAlbum:
		case sTagTitleSortname:
		case sTagArtistSortname:
		case sTagAlbumSortname:
		case sTagDate:
		case sTagComposer:
		case sTagGenre:
		case sTagComment:
		case sTagExtendedComment:
		case sTagLocation:
		case sTagHomepage:
		case sTagDescription:
		case sTagVersion:
		case sTagISRC:
		case sTagOrganization:
		case sTagCopyright:
		case sTagCopyrightURI:
		case sTagContact:
		case sTagLicense:
		case sTagLicenseURI:
		case sTagCodec:
		case sTagAudioCodec:
		case sTagVideoCodec:
		case sTagEncoder:
		case sTagLanguageCode:
		case sTagKeywords:
		case sTagChannelMode:
		case sUser + 12:
			return resIsString;
		case sTagTrackGain:
		case sTagTrackPeak:
		case sTagAlbumGain:
		case sTagAlbumPeak:
		case sTagReferenceLevel:
		case sTagBeatsPerMinute:
		case sTagImage:
		case sTagPreviewImage:
		case sTagAttachment:
			return resIsPyObject;
		case sTagBitrate: {
			/* bit rate of the audio track (as servicemp3), exteplayer3 187 "br" */
			int n = getCurrentTrack();
			audioStream a;
			if (n >= 0 && player->audioGetTrackInfo(a, n) == 0 && a.bitrate > 0)
				return a.bitrate;
			return resNA;
		}
		case sTagTrackNumber:
		case sTagTrackCount:
		case sTagAlbumVolumeNumber:
		case sTagAlbumVolumeCount:
		case sTagNominalBitrate:
		case sTagMinimumBitrate:
		case sTagMaximumBitrate:
		case sTagSerial:
		case sTagEncoderVersion:
		case sTagCRC:
		case sBuffer:
			return resNA;
		case sVideoType: {
			videoStream v;
			if (!player->videoGetTrackInfo(v, 0)) {
				// map exteplayer to stream type
				if (v.description == "V_MPEG2")
					return 0;
				else if (v.description == "V_MPEG4/ISO/AVC")
					return 1;
				else if (v.description.find("V_MPEG4") != std::string::npos)
					return 4;
				else if (v.description == "V_MPEG1")
					return 6;
				else if (v.description == "V_HEVC/ISO/MPEGH")
					return 7;
				else if (v.description == "V_VP8")
					return 8;
				else if (v.description == "V_VP9")
					return 9;

				// map gstplayer to stream type (mpeg might be mpeg1 or mpeg2, but can't tell from description ony)
				if (v.description == "video/mpeg" || v.description == "video/x-3ivx" || v.description == "video/x-msmpeg")
					return 4;
				else if (v.description == "video/x-h263")
					return 2;
				else if (v.description == "video/x-h264")
					return 1;
				else if (v.description == "video/x-h265")
					return 7;
				else if (v.description == "video/x-xvid")
					return 10;
				else if (v.description == "video/x-wmv")
					return 3;
				else if (v.description == "video/x-vp6" || v.description == "video/x-vp6-flash")
					return 18;
				else if (v.description == "video/x-vp8")
					return 8;
				else if (v.description == "video/x-vp9")
					return 9;
				else if (v.description == "video/x-flash-video")
					return 21;
			}
			return resNA;
		}
		case sSID:
			return m_ref.getData(1);
		default:
			return resNA;
	}
	return 0;
}

std::string eServiceApp::getInfoString(int w) {
	switch (w) {
		case sVideoInfo: {
			char buff[100];
			snprintf(buff, sizeof(buff), "%d|%d|%d|%d|%d|%d", m_width, m_height, m_framerate, m_progressive, getInfo(sAspect), m_gamma);
			std::string videoInfo = buff;
			return videoInfo;
		}
		case sProvider:
			return m_ref.path.find("://") != std::string::npos ? "IPTV" : "FILE";
		case sServiceref:
			return m_ref.toString();
		case sTagTitle:
		case sTagArtist: {
			/* ICY / radio text of exteplayer3 (m_t) */
			std::string title, artist, text;
			player->getMetadata(title, artist, text);
			if (w == sTagArtist)
				return artist;
			return title.empty() ? text : title;
		}
		case sTagVideoCodec: {
			/* "HEVC Main 10@5.1 10 bit DV8" with exteplayer3 187 */
			videoStream v;
			if (player->videoGetTrackInfo(v, 0))
				return "";
			std::string codec = v.description;
			if (codec.compare(0, 2, "V_") == 0)
				codec.erase(0, 2);
			size_t slash = codec.find('/');
			if (slash != std::string::npos && codec.compare(0, 5, "MPEG4") != 0)
				codec.erase(slash);
			if (codec == "MPEG4/ISO/AVC")
				codec = "H.264";
			if (!v.profile.empty())
				codec += " " + v.profile;
			if (v.bit_depth > 8)
				codec += " " + std::to_string(v.bit_depth) + " bit";
			if (v.dolby_vision > 0)
				codec += " DV" + std::to_string(v.dolby_vision);
			return codec;
		}
		case sTagAudioCodec: {
			/* the codec as the audio selection shows it, "Dolby Atmos 5.1" */
			int n = getCurrentTrack();
			audioStream a;
			if (n < 0 || player->audioGetTrackInfo(a, n) < 0)
				return "";
			std::string codec = a.codec_name.empty() ? a.description : a.codec_name;
			if (codec.compare(0, 2, "A_") == 0)
				codec.erase(0, 2);
			if (a.sample_rate > 0)
				codec += " " + std::to_string(a.sample_rate / 1000) + "." + std::to_string(a.sample_rate % 1000 / 100) + " kHz";
			return codec;
		}
		default:
			break;
	}

	if (w < sUser && w > 26)
		return "";
	switch (w) {
		case sUser + 12: {
			errorMessage e;
			if (!player->getErrorMessage(e))
				return e.message;
			return m_error_message;
		}
		default:
			return "";
	}
	return "";
}


// iCueSheet
/* a resume position this close to the end starts the file from the beginning */
static const pts_t RESUME_END_MARGIN = 10 * 90000;

PyObject* eServiceApp::getCutList() {
	ePyObject list = PyList_New(0);

	std::multiset<cueEntry> entries(m_cue_entries);
	/* chapter marks that are not in the cut list already */
	for (std::set<pts_t>::const_iterator c(m_chapter_marks.begin()); c != m_chapter_marks.end(); ++c) {
		bool found = false;
		for (std::multiset<cueEntry>::const_iterator i(m_cue_entries.begin()); i != m_cue_entries.end(); ++i) {
			if (i->what == 2 && i->where == *c) {
				found = true;
				break;
			}
		}
		if (!found)
			entries.insert(cueEntry(*c, 2));
	}
	for (std::multiset<cueEntry>::iterator i(entries.begin()); i != entries.end(); ++i) {
		ePyObject tuple = PyTuple_New(2);
		PyTuple_SET_ITEM(tuple, 0, PyLong_FromLongLong(i->where));
		PyTuple_SET_ITEM(tuple, 1, PyLong_FromLong(i->what));
		PyList_Append(list, tuple);
		Py_DECREF(tuple);
	}

	return list;
}

void eServiceApp::setCutList(ePyObject list) {
	if (!PyList_Check(list))
		return;
	int size = PyList_Size(list);

	m_cue_entries.clear();

	for (int i = 0; i < size; ++i) {
		ePyObject tuple = PyList_GET_ITEM(list, i);
		if (!PyTuple_Check(tuple) || PyTuple_Size(tuple) != 2)
			continue;
		ePyObject ppts = PyTuple_GET_ITEM(tuple, 0), ptype = PyTuple_GET_ITEM(tuple, 1);
		if (!(PyLong_Check(ppts) && PyLong_Check(ptype)))
			continue;
		pts_t pts = PyLong_AsLongLong(ppts);
		int type = PyLong_AsLong(ptype);
		m_cue_entries.insert(cueEntry(pts, type));
	}
	m_cuesheet_changed = 1;
	m_event((iPlayableService*)this, evCuesheetChanged);
}

void eServiceApp::setCutListEnable(int enable) {
	m_cutlist_enabled = enable;
}

void eServiceApp::loadCuesheet() {
	if (m_cuesheet_loaded)
		return;
	eDebug("eServiceApp::loadCuesheet");
	m_cuesheet_loaded = true;

	m_cue_entries.clear();

	std::string filename = m_ref.path + ".cuts";

	FILE* f = fopen(filename.c_str(), "rb");

	if (f) {
		while (1) {
			unsigned long long where;
			unsigned int what;

			if (!fread(&where, sizeof(where), 1, f))
				break;
			if (!fread(&what, sizeof(what), 1, f))
				break;

			where = be64toh(where);
			what = ntohl(what);

			if (what < 256)
				m_cue_entries.insert(cueEntry(where, what));
		}
		fclose(f);
		eDebug("eServiceApp::loadCuesheet - cuts file has %zd entries", m_cue_entries.size());
	} else
		eDebug("eServiceApp::loadCuesheet - cuts file not found");

	/* a last position at (or after) the end would resume into the end of the
	 * file: drop it, so playback starts from the beginning */
	pts_t length = 0;
	for (std::multiset<cueEntry>::iterator i(m_cue_entries.begin()); i != m_cue_entries.end(); ++i) {
		if (i->what == 5) /* CUT_TYPE_LENGTH */
			length = i->where;
	}
	if (length > 0) {
		for (std::multiset<cueEntry>::iterator i(m_cue_entries.begin()); i != m_cue_entries.end();) {
			if (i->what == 3 && i->where >= length - RESUME_END_MARGIN) { /* CUT_TYPE_LAST */
				eDebug("eServiceApp::loadCuesheet - last position %lld too close to the end %lld, ignored", (long long)i->where, (long long)length);
				i = m_cue_entries.erase(i);
			} else
				++i;
		}
	}

	m_cuesheet_changed = 0;
	m_event((iPlayableService*)this, evCuesheetChanged);
}

void eServiceApp::saveCuesheet() {
	std::string filename = m_ref.path;

	if (::access(filename.c_str(), R_OK) < 0)
		return;

	filename.append(".cuts");

	/* the latest values, the GUI may have stopped asking for them */
	pts_t tmp;
	getPlayPosition(tmp);
	getLength(tmp);

	/* a resume seek the player has not applied yet is where the user is */
	pts_t position = m_pending_seek >= 0 ? m_pending_seek : m_last_seek_pos;
	bool started = m_player_started || m_pending_seek >= 0;
	bool near_end = m_media_length > 0 && position >= m_media_length - RESUME_END_MARGIN;
	/* the player never started, or the user left within the first 10 s:
	 * the old resume point stays (it was erased and not written again) */
	bool keep_last = (m_cutlist_enabled & 2) == 0 && (!started || position <= 900000) && !near_end;

	if (!keep_last) {
		/* save old CUT_TYPE_LAST as CUT_TYPE_SAVEDLAST before replacing it */
		pts_t old_last = 0;
		for (std::multiset<cueEntry>::iterator i(m_cue_entries.begin()); i != m_cue_entries.end();) {
			if (i->what == 3) { /* CUT_TYPE_LAST */
				old_last = i->where;
				i = m_cue_entries.erase(i);
			} else if (i->what == 4) /* CUT_TYPE_SAVEDLAST */
				i = m_cue_entries.erase(i);
			else
				++i;
		}
		if (old_last > 0)
			m_cue_entries.insert(cueEntry(old_last, 4));

		/* no resume position near the end: the next start begins from 0 */
		if ((m_cutlist_enabled & 2) == 0 && position > 900000 && !near_end) {
			m_cue_entries.insert(cueEntry(position, 3));
			eDebug("eServiceApp::saveCuesheet - last play position saved: %lld", (long long)position);
		}
	}

	/* update CUT_TYPE_LENGTH with the media length */
	for (std::multiset<cueEntry>::iterator i(m_cue_entries.begin()); i != m_cue_entries.end();) {
		if (i->what == 5) /* CUT_TYPE_LENGTH */
			i = m_cue_entries.erase(i);
		else
			++i;
	}
	if (m_media_length > 0)
		m_cue_entries.insert(cueEntry(m_media_length, 5));

	size_t saved = 0;
	for (std::multiset<cueEntry>::iterator i(m_cue_entries.begin()); i != m_cue_entries.end(); ++i) {
		if (!(i->what == 2 && m_chapter_marks.count(i->where)))
			saved++;
	}
	if (!saved) {
		if (::access(filename.c_str(), F_OK) == 0)
			remove(filename.c_str());
		return;
	}

	FILE* f = fopen(filename.c_str(), "wb");
	if (f) {
		unsigned long long where;
		unsigned int what;

		for (std::multiset<cueEntry>::iterator i(m_cue_entries.begin()); i != m_cue_entries.end(); ++i) {
			/* chapter marks come from the file at every start; the GUI hands
			 * them back with setCutList, they do not go into the .cuts file */
			if (i->what == 2 && m_chapter_marks.count(i->where))
				continue;
			where = htobe64(i->where);
			what = htonl(i->what);
			fwrite(&where, sizeof(where), 1, f);
			fwrite(&what, sizeof(what), 1, f);
		}
		fclose(f);
		eDebug("eServiceApp::saveCuesheet - cuts file has been written");
	}
	m_cuesheet_changed = 0;
}

DEFINE_REF(eStaticServiceAppInfo);

RESULT eStaticServiceAppInfo::getName(const eServiceReference& ref, std::string& name) {
	if (ref.name.length())
		name = ref.name;
	else {
		size_t last = ref.path.rfind('/');
		if (last != std::string::npos)
			name = ref.path.substr(last + 1);
		else
			name = ref.path;
	}
	return 0;
}

int eStaticServiceAppInfo::getLength(const eServiceReference& ref) {
	/* the length saved in the cut list of a local file (CUT_TYPE_LENGTH), in
	   seconds, as servicemp3 reports it */
	if (ref.path.empty() || ref.path.find("://") != std::string::npos)
		return -1;
	std::string filename = ref.path + ".cuts";
	FILE* f = fopen(filename.c_str(), "rb");
	if (!f)
		return -1;
	int length = -1;
	while (1) {
		unsigned long long where;
		unsigned int what;
		if (!fread(&where, sizeof(where), 1, f) || !fread(&what, sizeof(what), 1, f))
			break;
		if (ntohl(what) == 5) {
			length = (int)(be64toh(where) / 90000);
			break;
		}
	}
	fclose(f);
	return length;
}

int eStaticServiceAppInfo::getInfo(const eServiceReference& ref, int w) {
	switch (w) {
		case iServiceInformation::sTimeCreate: {
			struct stat s;
			if (stat(ref.path.c_str(), &s) == 0) {
				return s.st_mtime;
			}
		} break;
		case iServiceInformation::sFileSize: {
			struct stat s;
			if (stat(ref.path.c_str(), &s) == 0) {
				return s.st_size;
			}
		} break;
	}
	return iServiceInformation::resNA;
}

long long eStaticServiceAppInfo::getFileSize(const eServiceReference& ref) {
	struct stat s;
	if (stat(ref.path.c_str(), &s) == 0) {
		return s.st_size;
	}
	return 0;
}

RESULT eStaticServiceAppInfo::getEvent(const eServiceReference& ref, ePtr<eServiceEvent>& evt, time_t start_time) {
#ifdef HAVE_EPG
	if (ref.path.find("://") != std::string::npos) {
		eServiceReference equivalentref(ref);
		equivalentref.type = eServiceFactoryApp::idServiceMP3;
		equivalentref.path.clear();
		return eEPGCache::getInstance()->lookupEventTime(equivalentref, start_time, evt);
	}
	evt = 0;
#endif
	return -1;
}


DEFINE_REF(eServiceFactoryApp)

eServiceFactoryApp::eServiceFactoryApp() {
	ePtr<eServiceCenter> sc;

	eServiceCenter::getPrivInstance(sc);
	if (sc) {
		std::list<std::string> extensions;
		extensions.push_back("dts");
		extensions.push_back("mp3");
		extensions.push_back("wav");
		extensions.push_back("wave");
		extensions.push_back("ogg");
		extensions.push_back("flac");
		extensions.push_back("m4a");
		extensions.push_back("mp2");
		extensions.push_back("m2a");
		extensions.push_back("wma");
		extensions.push_back("ac3");
		extensions.push_back("mka");
		extensions.push_back("aac");
		extensions.push_back("ape");
		extensions.push_back("alac");
		extensions.push_back("mpg");
		extensions.push_back("vob");
		extensions.push_back("m4v");
		extensions.push_back("mkv");
		extensions.push_back("avi");
		extensions.push_back("divx");
		extensions.push_back("dat");
		extensions.push_back("flv");
		extensions.push_back("mp4");
		extensions.push_back("mov");
		extensions.push_back("wmv");
		extensions.push_back("asf");
		extensions.push_back("3gp");
		extensions.push_back("3g2");
		extensions.push_back("mpeg");
		extensions.push_back("mpe");
		extensions.push_back("rm");
		extensions.push_back("rmvb");
		extensions.push_back("ogm");
		extensions.push_back("ogv");
		extensions.push_back("webm");
		extensions.push_back("stream");
		if (gReplaceServiceMP3) {
			sc->removeServiceFactory(eServiceFactoryApp::idServiceMP3);
			sc->addServiceFactory(eServiceFactoryApp::idServiceMP3, this, extensions);
		}
		extensions.clear();
		sc->addServiceFactory(eServiceFactoryApp::idServiceGstPlayer, this, extensions);
		sc->addServiceFactory(eServiceFactoryApp::idServiceExtEplayer3, this, extensions);
	}
	m_service_info = new eStaticServiceAppInfo();
}

eServiceFactoryApp::~eServiceFactoryApp() {
	ePtr<eServiceCenter> sc;

	eServiceCenter::getPrivInstance(sc);
	if (sc) {
		if (gReplaceServiceMP3) {
			sc->removeServiceFactory(eServiceFactoryApp::idServiceMP3);
		}
		sc->removeServiceFactory(eServiceFactoryApp::idServiceGstPlayer);
		sc->removeServiceFactory(eServiceFactoryApp::idServiceExtEplayer3);
	}
}


eAutoInitPtr<eServiceFactoryApp> init_eServiceFactoryApp(eAutoInitNumbers::service + 3, "eServiceFactoryApp");


static PyObject* use_user_settings(PyObject* self, PyObject* args) {
	g_useUserSettings = true;
	Py_RETURN_NONE;
}

static PyObject* servicemp3_exteplayer3_enable(PyObject* self, PyObject* args) {
	g_playerServiceMP3 = EXTEPLAYER3;
	Py_RETURN_NONE;
}

static PyObject* servicemp3_gstplayer_enable(PyObject* self, PyObject* args) {
	g_playerServiceMP3 = GSTPLAYER;
	Py_RETURN_NONE;
}


static PyObject* gstplayer_set_setting(PyObject* self, PyObject* args) {
	bool ret = true;

	int settingId;
	char *audioSink, *videoSink;
	bool subtitlesEnable;
	long bufferSize, bufferDuration;

	if (!PyArg_ParseTuple(args, "issbll", &settingId, &videoSink, &audioSink, &subtitlesEnable, &bufferSize, &bufferDuration))
		return NULL;

	GstPlayerOptions* options = NULL;
	switch (settingId) {
		case OPTIONS_SERVICEGSTPLAYER:
			options = g_GstPlayerOptionsServiceGst;
			eDebug("[gstplayer_set_setting] setting servicegstplayer options");
			break;
		case OPTIONS_SERVICEMP3:
			options = g_GstPlayerOptionsServiceMP3;
			eDebug("[gstplayer_set_setting] setting servicemp3 options");
			break;
		case OPTIONS_USER:
			options = g_GstPlayerOptionsUser;
			eDebug("[gstplayer_set_setting] setting user options");
			break;
		default:
			eWarning("[gstplayer_set_setting] option '%d' is not known, cannot be set!", settingId);
			ret = false;
			break;
	}
	if (options != NULL) {
		options->GetSettingMap()[GST_VIDEO_SINK].setValue(videoSink);
		options->GetSettingMap()[GST_AUDIO_SINK].setValue(audioSink);
		options->GetSettingMap()[GST_SUBTITLE_ENABLED].setValue(subtitlesEnable);
		options->GetSettingMap()[GST_BUFFER_SIZE].setValue(bufferSize);
		options->GetSettingMap()[GST_BUFFER_DURATION].setValue(bufferDuration);
	}
	return Py_BuildValue("b", ret);
}

static PyObject* exteplayer3_set_setting(PyObject* self, PyObject* args) {
	bool ret = true;

	int settingId;
	unsigned char aacSwDecoding; /* 0 off, 1 ADTS, 2 LATM: no bool, "b" writes a byte */
	bool ac3SwDecoding;
	bool eac3SwDecoding;
	bool dtsSwDecoding;
	bool mp3SwDecoding;
	bool wmaSwDecoding;
	bool downmix;
	bool lpcmInjection;
	int rtmpProtocol;
	/* optional; the plugin sends them only to a player that knows them */
	bool dtsAc3 = false;          /* exteplayer3 187, "xcode" & 1 */
	bool truehdAc3 = false;
	bool heaacAacLc = false;      /* "xcode" & 2 */
	bool dtsEac3 = false;         /* "xcode" & 4 */
	bool truehdEac3 = false;
	int hlsQuality = 0;           /* exteplayer3 185: 0 auto, 1 lowest, 2 highest */
	int hlsDefaultAudio = 0;      /* exteplayer3 185: 1 only the default audio rendition */
	const char *coverPath = NULL; /* "cover": file for the embedded cover, None or "" off */
	const char *graphicSubDir = NULL; /* -G (exteplayer3 63): graphic subtitles as PNG files there, None or "" off */

	if (!PyArg_ParseTuple(args, "ibbbbbbbbi|bbbbbiizz", &settingId, &aacSwDecoding, &dtsSwDecoding, &wmaSwDecoding, &lpcmInjection, &downmix, &ac3SwDecoding, &eac3SwDecoding, &mp3SwDecoding, &rtmpProtocol, &dtsAc3, &truehdAc3, &heaacAacLc, &dtsEac3, &truehdEac3, &hlsQuality, &hlsDefaultAudio, &coverPath, &graphicSubDir))
		return NULL;

	ExtEplayer3Options* options = NULL;
	switch (settingId) {
		case OPTIONS_SERVICEEXTEPLAYER3:
			options = g_ExtEplayer3OptionsServiceExt3;
			eDebug("[exteplayer3_set_setting] setting serviceextplayer3 options");
			break;
		case OPTIONS_SERVICEMP3:
			options = g_ExtEplayer3OptionsServiceMP3;
			eDebug("[exteplayer3_set_setting] setting servicemp3 options");
			break;
		case OPTIONS_USER:
			options = g_ExtEplayer3OptionsUser;
			eDebug("[exteplayer3_set_setting] setting user options");
			break;
		default:
			eWarning("[exteplayer3_set_setting] option '%d' is not known, cannot be set!", settingId);
			ret = false;
			break;
	}
	if (options != NULL) {
		options->GetSettingMap()[EXT3_SW_DECODING_AAC].setValue(aacSwDecoding);
		options->GetSettingMap()[EXT3_SW_DECODING_AC3].setValue(ac3SwDecoding);
		options->GetSettingMap()[EXT3_SW_DECODING_EAC3].setValue(eac3SwDecoding);
		options->GetSettingMap()[EXT3_SW_DECODING_DTS].setValue(dtsSwDecoding);
		options->GetSettingMap()[EXT3_SW_DECODING_WMA].setValue(wmaSwDecoding);
		options->GetSettingMap()[EXT3_SW_DECODING_MP3].setValue(mp3SwDecoding);
		options->GetSettingMap()[EXT3_LPCM_INJECTION].setValue(lpcmInjection);
		options->GetSettingMap()[EXT3_RTMP_PROTOCOL].setValue(rtmpProtocol);
		options->GetSettingMap()[EXT3_DOWNMIX].setValue(downmix);
		options->GetSettingMap()[EXT3_TRANSCODE_DTS].setValue(dtsAc3);
		options->GetSettingMap()[EXT3_TRANSCODE_TRUEHD].setValue(truehdAc3);
		options->GetSettingMap()[EXT3_TRANSCODE_HEAAC].setValue(heaacAacLc);
		options->GetSettingMap()[EXT3_TRANSCODE_DTS_EAC3].setValue(dtsEac3);
		options->GetSettingMap()[EXT3_TRANSCODE_TRUEHD_EAC3].setValue(truehdEac3);
		options->GetSettingMap()[EXT3_HLS_QUALITY].setValue(hlsQuality);
		options->GetSettingMap()[EXT3_HLS_DEFAULT_AUDIO].setValue(hlsDefaultAudio);
		if (coverPath && *coverPath)
			options->GetSettingMap()[EXT3_COVER].setValue(std::string(coverPath));
		else
			options->GetSettingMap()[EXT3_COVER] = SettingEntry("--cover", "string"); /* not set */
		if (graphicSubDir && *graphicSubDir)
			options->GetSettingMap()[EXT3_GRAPHIC_SUBS].setValue(std::string(graphicSubDir));
		else
			options->GetSettingMap()[EXT3_GRAPHIC_SUBS] = SettingEntry("-G", "subdir"); /* not set */
	}
	return Py_BuildValue("b", ret);
}

/* the version of AC_INIT in configure.ac, passed by configure as -DPACKAGE_VERSION */
#ifndef PACKAGE_VERSION
#define PACKAGE_VERSION "unknown"
#endif

static PyObject* serviceapp_version(PyObject* self, PyObject* args) {
	return Py_BuildValue("s", PACKAGE_VERSION);
}

static PyObject* serviceapp_can_record(PyObject* self, PyObject* args) {
	/* recordings need the native 4097 recorder, see eServiceFactoryApp::record */
	bool ret = (&g_nativeServiceMP3Factory != NULL) || !gReplaceServiceMP3;
	return Py_BuildValue("b", ret);
}

static PyObject* serviceapp_set_setting(PyObject* self, PyObject* args) {
	bool ret = true;

	int autoTurnOnSubtitles;
	int settingId;
	bool HLSExplorer;
	bool autoSelectStream;
	int32_t connectionSpeedInKb;

	if (!PyArg_ParseTuple(args, "ibbIi", &settingId, &HLSExplorer, &autoSelectStream, &connectionSpeedInKb, &autoTurnOnSubtitles))
		return NULL;

	eServiceAppOptions* options = NULL;
	switch (settingId) {
		case OPTIONS_SERVICEEXTEPLAYER3:
			options = g_ServiceAppOptionsServiceExt3;
			eDebug("[serviceapp_set_setting] setting serviceexteplayer3 options");
			break;
		case OPTIONS_SERVICEGSTPLAYER:
			options = g_ServiceAppOptionsServiceGst;
			eDebug("[serviceapp_set_setting] setting servicegstplayer options");
			break;
		case OPTIONS_SERVICEMP3:
			options = g_ServiceAppOptionsServiceMP3;
			eDebug("[serviceapp_set_setting] setting servicemp3 options");
			break;
		case OPTIONS_USER:
			options = g_ServiceAppOptionsUser;
			eDebug("[serviceapp_set_setting] setting user options");
			break;
		default:
			eWarning("[serviceapp_set_setting] option '%d' is not known, cannot be set!", settingId);
			ret = false;
			break;
	}
	if (options != NULL) {
		options->autoTurnOnSubtitles = autoTurnOnSubtitles;
		options->HLSExplorer = HLSExplorer;
		options->autoSelectStream = autoSelectStream;
		options->connectionSpeedInKb = connectionSpeedInKb;
	}
	return Py_BuildValue("b", ret);
}

static PyMethodDef serviceappMethods[] = {
	{"use_user_settings", use_user_settings, METH_NOARGS, "user settings will be used for creation of player"},
	{"servicemp3_exteplayer3_enable", servicemp3_exteplayer3_enable, METH_NOARGS, "use ffmpeg based extplayer3, when servicemp3 is replaced by serviceapp"},
	{"servicemp3_gstplayer_enable", servicemp3_gstplayer_enable, METH_NOARGS, "use gstreamer based player, when servicemp3 is replaced by serviceapp"},
	{"gstplayer_set_setting", gstplayer_set_setting, METH_VARARGS,
	 "set gstreamer player settings (setting_id, videoSink, audioSink, subtitlesEnabled, bufferSize, bufferDuration\n\n"
	 " setting_id - (0 - servicemp3, 1 - servicegst, 2 - serviceextep3, 3 - user)\n"
	 " videoSink - (dvbvideosink, dvbvideosinkexp, ...)\n"
	 " audioSink - (dvbaudiosink, dvbaudiosinkexp, ...)\n"
	 " subtitleEnable - (True, False)\n"
	 " bufferSize - in kilobytes\n"
	 " bufferDuration - in seconds\n"},
	{"exteplayer3_set_setting", exteplayer3_set_setting, METH_VARARGS,
	 "set exteplayer3 settings (setting_id, aacSwDecoding, dtsSwDecoding, wmaSwDecoding, lpcmInjection, downmix, ac3SwDecoding, eac3SwDecoding, mp3SwDecoding, rtmpProtocol"
	 "[, dtsAc3, truehdAc3, heaacAacLc, dtsEac3, truehdEac3, hlsQuality, hlsDefaultAudio, coverPath, graphicSubDir])\n\n"
	 " setting_id - (0 - servicemp3, 1 - servicegst, 2 - serviceextep3, 3 - user)\n"
	 " aacSwDecoding - (0 off, 1 ADTS, 2 LATM)\n"
	 " dtsSwDecoding - (True, False)\n"
	 " wmaSwDecoding - (True, False)\n"
	 " lpcmInjection - (True, False)\n"
	 " downmix - (True, False)\n"
	 " ac3SwDecoding - (True, False)\n"
	 " eac3SwDecoding - (True, False)\n"
	 " mp3SwDecoding - (True, False)\n"
	 " rtmpProtocol - (0|1|2)\n"
	 " optional, only for a player that knows them:\n"
	 " dtsAc3, truehdAc3 - transcode DTS / TrueHD to AC3 (True, False; exteplayer3 187 \"xcode\" & 1)\n"
	 " heaacAacLc - HE-AAC as AAC-LC (True, False; \"xcode\" & 2)\n"
	 " dtsEac3, truehdEac3 - transcode DTS / TrueHD to E-AC3 (True, False; \"xcode\" & 4)\n"
	 " hlsQuality - HLS variant (0 auto, 1 lowest, 2 highest; exteplayer3 185)\n"
	 " hlsDefaultAudio - only the default HLS audio rendition (0|1; exteplayer3 185)\n"
	 " coverPath - file for the embedded cover (None or \"\" off)\n"
	 " graphicSubDir - graphic subtitles as PNG files below this /tmp directory (None or \"\" off; exteplayer3 63)\n"},
	{"can_record", serviceapp_can_record, METH_NOARGS, "recording of 5001/5002 streams is available"},
	{"version", serviceapp_version, METH_NOARGS, "version of serviceapp (AC_INIT of configure.ac)"},
	{"serviceapp_set_setting", serviceapp_set_setting, METH_VARARGS,
	 "set serviceapp settings (setting_id, HLSExplorer, autoSelectStream, connectionSpeedInKb, autoTurnOnSubtitles\n\n"
	 " setting_id - (0 - servicemp3, 1 - servicegst, 2 - serviceextep3, 3 - user)\n"
	 " HLSExplorer - defines if HLS explorer will be used to retrieve streams from HLS master playlist (True, False))\n"
	 " autoSelectStream - if there are more streams available, it defines if stream will be auto-selected according to connectionSpeedInKb (True, False)\n"
	 " connectionSpeedInKb - defines bitrate in kilobits/s according to which will be selected stream from playlist <0, max(int32_t)>\n"
	 " autoTurnOnSubtitles - auto turn on subtitles if available (0/False - off, 1/True - on, 2 - only a forced track in the audio language)\n"},
	{NULL, NULL, 0, NULL}};

static struct PyModuleDef moduledef = {
	PyModuleDef_HEAD_INIT,
	"serviceapp", /* m_name */
	"serviceapp", /* m_doc */
	-1, /* m_size */
	serviceappMethods, /* m_methods */
	NULL, /* m_reload */
	NULL, /* m_traverse */
	NULL, /* m_clear */
	NULL, /* m_free */
};

PyMODINIT_FUNC PyInit_serviceapp(void) {
	g_GstPlayerOptionsServiceMP3 = new GstPlayerOptions();
	g_GstPlayerOptionsServiceGst = new GstPlayerOptions();
	g_GstPlayerOptionsUser = new GstPlayerOptions();

	g_ExtEplayer3OptionsServiceMP3 = new ExtEplayer3Options();
	g_ExtEplayer3OptionsServiceExt3 = new ExtEplayer3Options();
	g_ExtEplayer3OptionsUser = new ExtEplayer3Options();

	g_ServiceAppOptionsServiceMP3 = new eServiceAppOptions();
	g_ServiceAppOptionsServiceExt3 = new eServiceAppOptions();
	g_ServiceAppOptionsServiceGst = new eServiceAppOptions();
	g_ServiceAppOptionsUser = new eServiceAppOptions();

	SSL_load_error_strings();
	SSL_library_init();
	return PyModule_Create(&moduledef);
}
