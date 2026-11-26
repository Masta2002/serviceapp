#ifndef __exteplayer3_h
#define __exteplayer3_h
#include <lib/base/eerror.h>
#include "extplayer.h"
#include "common.h"
#include <map>

extern const std::string EXT3_SW_DECODING_AAC;
extern const std::string EXT3_SW_DECODING_AC3;
extern const std::string EXT3_SW_DECODING_EAC3;
extern const std::string EXT3_SW_DECODING_DTS;
extern const std::string EXT3_SW_DECODING_MP3;
extern const std::string EXT3_SW_DECODING_WMA;
extern const std::string EXT3_DOWNMIX;
extern const std::string EXT3_LPCM_INJECTION;
extern const std::string EXT3_NO_PCM_RESAMPLING;
extern const std::string EXT3_FLV2MPEG4_CONVERTER;
extern const std::string EXT3_PLAYBACK_PROGRESSIVE;
extern const std::string EXT3_PLAYBACK_INIFITY_LOOP;
extern const std::string EXT3_PLAYBACK_LIVETS;
extern const std::string EXT3_PLAYBACK_AUDIO_TRACK_ID;
extern const std::string EXT3_PLAYBACK_SUBTITLE_TRACK_ID;
extern const std::string EXT3_PLAYBACK_AUDIO_URI;
extern const std::string EXT3_PLAYBACK_DASH_VIDEO_ID;
extern const std::string EXT3_PLAYBACK_DASH_AUDIO_ID;
extern const std::string EXT3_PLAYBACK_MPEGTS_PROGRAM;
extern const std::string EXT3_RTMP_PROTOCOL;
extern const std::string EXT3_NICE_VALUE;
extern const std::string EXT3_FFMPEG_SETTING_STRING;
extern const std::string EXT3_ARG_LCASE_B;
extern const std::string EXT3_ARG_5;
extern const std::string EXT3_ARG_6;
extern const std::string EXT3_ARG_7;
extern const std::string EXT3_TRANSCODE_DTS;
extern const std::string EXT3_TRANSCODE_DTS_EAC3;
extern const std::string EXT3_TRANSCODE_TRUEHD;
extern const std::string EXT3_TRANSCODE_TRUEHD_EAC3;
extern const std::string EXT3_TRANSCODE_HEAAC;
extern const std::string EXT3_HLS_QUALITY;
extern const std::string EXT3_HLS_DEFAULT_AUDIO;
extern const std::string EXT3_COVER;
extern const std::string EXT3_KEY;
extern const std::string EXT3_GRAPHIC_SUBS;

/* the window exteplayer3 scales graphic subtitles for (-W, -H) */
#define EXT3_GRAPHIC_SUB_WIDTH  1280
#define EXT3_GRAPHIC_SUB_HEIGHT 720

struct ExtEplayer3Options : public IOption
{
	SettingMap settingMap;
	SettingMap &GetSettingMap();

	ExtEplayer3Options();
	void print() const;
	int update(const std::string&, const std::string&);
};

class ExtEplayer3: public PlayerApp, public BasePlayer
{
	ExtEplayer3Options mPlayerOptions;
	// current video track as passed on, and what the decoder reported
	// for it with "v_e" (-1 unknown)
	videoStream mVideo, mDecoderVideo;
	// the player sent FF_ERROR
	bool mErrorReported;
	// "seekms" in EPLAYER3_EXTENDED: gf<s.ms>
	bool mSeekMs;
	// a live stream: FFmpeg reconnects an HTTP stream without a length itself
	bool mLiveStream;
	// graphic subtitles: the PNG directory of this player (-G), empty: off
	std::string mGraphicSubDir;
	void removeGraphicSubDir();
	void handleProcessStopped(int retval);
	void handleJsonOutput(cJSON* json);
	std::vector<std::string> buildCommand();
	public:
	ExtEplayer3(ExtEplayer3Options& options);
	~ExtEplayer3();
	int start(eMainloop *context);

	int sendStop();
	int sendForceStop();
	int sendKill();
	int sendPause();
	int sendResume();
	int sendUpdateLength();
	int sendUpdatePosition();
	int sendUpdateAudioTracksList();
	int sendUpdateAudioTrackCurrent();
	int sendPollAudioTrackCurrent();
	int sendAudioSelectTrack(int trackId);
	int sendUpdateSubtitleTracksList();
	int sendUpdateSubtitleTrackCurrent();
	int sendSubtitleSelectTrack(int trackId);
	int sendSubtitleDisable();
	int sendSeekTo(int seconds);
	int sendSeekToMs(int mseconds);
	int sendSeekRelative(int seconds);
	int sendFastForward(int speed);
	int sendAudioChannel(int channel);
	void setLiveStream(bool live){mLiveStream = live;}
};
#endif
