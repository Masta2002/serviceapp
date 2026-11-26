#ifndef __extplayer_h
#define __extplayer_h

#include <lib/base/ebase.h>
#include <lib/base/message.h>
#include <lib/base/thread.h>
#include <lib/python/connections.h>

#include <map>

#include "cJSON/cJSON.h"
#include "myconsole.h"
#include "subtitles/subtitles.h"

/* null and type safe cJSON reads: the external player can crash or send
 * incomplete JSON, a missing key or a non-string value must not crash */
static inline int cJsonGetInt(cJSON *obj, const char *key, int def = 0)
{
	cJSON *n = obj ? cJSON_GetObjectItem(obj, key) : NULL;
	return (n && n->type == cJSON_Number) ? n->valueint : def;
}
static inline double cJsonGetDouble(cJSON *obj, const char *key, double def = 0.0)
{
	cJSON *n = obj ? cJSON_GetObjectItem(obj, key) : NULL;
	return (n && n->type == cJSON_Number) ? n->valuedouble : def;
}
static inline std::string cJsonGetStr(cJSON *obj, const char *key)
{
	cJSON *n = obj ? cJSON_GetObjectItem(obj, key) : NULL;
	return (n && n->type == cJSON_String && n->valuestring) ? std::string(n->valuestring) : std::string();
}

#ifndef eLog
 #define eLog(lvl,...)
#endif

enum
{
	STD_OUTPUT,
	STD_ERROR,
};

class PlayerApp: public sigc::trackable
{
	ePtr<eConsoleContainer> console;
	std::string jsonstr;
	unsigned int parseOutput;
	unsigned int truncated;
	void stdoutAvail(const char *data);
	void stderrAvail(const char *data);
	void appClosed(int retval);
	void handleOutput(const std::string& data);
	void handleJsonStr(const std::string& data);
	void handleAppClosed(int retval);
protected:
	virtual std::vector<std::string> buildCommand() = 0;
	virtual void handleJsonOutput(cJSON *json) = 0;
	virtual void handleProcessStopped(int retval) = 0;
	int processStart(eMainloop *context);
	int processSend(const std::string& data);
	void processKill();
	void processForceKill();
	bool processRunning();
public:
	PlayerApp(int parseOutput=STD_ERROR):
		parseOutput(parseOutput),
		truncated(0){}
	~PlayerApp(){}
};


struct PlayerMessage
{
	enum
	{
		start,
		stop,
		pause,
		resume,
		error,
		videoSizeChanged,
		videoProgressiveChanged,
		videoGammaChanged,
		videoFramerateChanged,
		subtitleAvailable,
		audioTracksAvailable,
		audioChanged,
		windStart,
		windEnd,
		subtitleFlush,         // the player reset its subtitle output (seek, switch)
		subtitleTracksChanged, // a track list the player sent by itself (closed captions)
		metadataChanged,       // title / artist of a radio stream
		chaptersChanged,       // the chapters of the input
		coverAvailable,        // the embedded cover was written (--cover)
	};
};


struct audioStream
{
	int id;
	std::string language_code; /* iso-639, if available. */
	std::string description; /* clear text codec description */
	std::string codec_name; /* e.g. "Dolby Atmos", "HE-AAC v2", if the player sends it */
	int channels; /* channel count, if the player sends it */
	std::string channel_layout; /* e.g. "5.1(side)", if the player sends it */
	std::string title; /* track title ("Commentary"), if the player sends it */
	int sample_rate; /* Hz, 0 unknown */
	int bitrate; /* bit/s, 0 unknown */
	audioStream(): id(-1), channels(-1), sample_rate(0), bitrate(0){};
};


struct videoStream
{
	int id;
	std::string language_code; /* iso-639, if available. */
	std::string description; /* clear text codec description */
	int width;
	int height;
	int framerate;
	int progressive;
	int gamma; /* 0 SDR, 1 HDR, 2 HDR10, 3 HLG as iServiceInformation::sGamma, -1 unknown */
	std::string profile; /* "Main 10@5.1", if the player sends it */
	int bit_depth; /* 0 unknown */
	int bitrate; /* bit/s, 0 unknown */
	int dolby_vision; /* Dolby Vision profile, 0 none */
	videoStream(): id(-1), width(-1), height(-1), framerate(-1), progressive(-1), gamma(-1), bit_depth(0), bitrate(0), dolby_vision(0){};
};


/* a chapter of the input, exteplayer3 "c_l" (ms from the start of the media) */
struct chapterEntry
{
	int start_ms;
	int end_ms;
	std::string title;
	chapterEntry(): start_ms(0), end_ms(0){};
};


struct errorMessage
{
	int code;
	std::string message;
	errorMessage():code(-1){}
};


class iPlayerSend
{
public:
	virtual int sendStop() { return -1;}
	virtual int sendForceStop() { return -1;}
	virtual int sendKill() { return -1;}
	virtual int sendPause(){ return -1;}
	virtual int sendResume(){ return -1;}
	virtual int sendUpdateLength(){ return -1;}
	virtual int sendUpdatePosition(){ return -1;}
	virtual int sendUpdateAudioTracksList(){ return -1;}
	virtual int sendUpdateAudioTrackCurrent(){ return -1;}
	// periodic re-check of the current audio track, only players that report
	// live channel count / codec changes
	virtual int sendPollAudioTrackCurrent(){ return -1;}
	virtual int sendAudioSelectTrack(int trackId){ return -1;}
	virtual int sendUpdateSubtitleTracksList(){ return -1;}
	virtual int sendUpdateSubtitleTrackCurrent(){ return -1;}
	virtual int sendSubtitleSelectTrack(int trackId){ return -1;}
	// stop sending subtitles, only players that support it
	virtual int sendSubtitleDisable(){ return -1;}
	virtual int sendSeekTo(int seconds){ return -1;}
	// a player without ms seeks takes whole seconds
	virtual int sendSeekToMs(int mseconds){ return sendSeekTo(mseconds / 1000);}
	virtual int sendSeekRelative(int seconds){ return -1;}
	// speed > 1 winds forwards, < 0 backwards
	virtual int sendFastForward(int speed){ return -1;}
	// dual mono: 0 left, 1 stereo, 2 right (iAudioChannelSelection values)
	virtual int sendAudioChannel(int channel){ return -1;}
	// only the forced cues of the selected subtitle track (PGS forced pictures)
	virtual int sendSubtitleForcedOnly(bool on){ return -1;}
};


class iPlayerCallback
{
public:
	virtual void recvStarted(int status){};
	virtual void recvStopped(int status){};
	virtual void recvPaused(int status){};
	virtual void recvResumed(int status){};
	virtual void recvLength(int status, int mseconds){};
	virtual void recvPosition(int status, int mseconds){};
	virtual void recvSeekTo(int status, int seconds){};
	virtual void recvSeekRelative(int status, int seconds){};
	virtual void recvAudioTracksList(int status, std::vector<audioStream>&){};
	virtual void recvAudioTrackCurrent(int status, audioStream&){};
	virtual void recvAudioTrackSelected(int status, int trackId){};
	virtual void recvSubtitleTracksList(int status, std::vector<subtitleStream>&){};
	virtual void recvSubtitleTrackCurrent(int status, subtitleStream&){};
	virtual void recvSubtitleTrackSelected(int status, int trackId){};
	virtual void recvSubtitleMessage(subtitleMessage&){};
	virtual void recvVideoTrackCurrent(int status, videoStream&){};
	virtual void recvErrorMessage(errorMessage&){};
	// winding reached the start (direction < 0) or the end
	virtual void recvWindEnd(int direction){};
	virtual void recvSubtitleFlush(){};
	virtual void recvAudioChannel(int status, int channel){};
	// ICY / radio text of audio-only playback
	virtual void recvMetadata(const std::string& title, const std::string& artist, const std::string& text){};
	virtual void recvChapters(std::vector<chapterEntry>& chapters){};
	// the embedded cover was written to the --cover file, empty: none
	virtual void recvCover(const std::string& file){};
};


class BasePlayer: public iPlayerSend, public iPlayerCallback
{
	iPlayerCallback *pCallback = nullptr;

protected:
	std::string mPath;
	std::map<std::string, std::string> mHeaders;
	// the player announced that it can wind
	bool mCanWind = false;

	void recvStarted(int status){pCallback->recvStarted(status);};
	void recvStopped(int status){pCallback->recvStopped(status);};
	void recvPaused(int status){pCallback->recvPaused(status);};
	void recvResumed(int status){pCallback->recvResumed(status);};
	void recvLength(int status, int mseconds){pCallback->recvLength(status, mseconds);};
	void recvPosition(int status, int mseconds){pCallback->recvPosition(status, mseconds);};
	void recvAudioTracksList(int status, std::vector<audioStream>& streams){pCallback->recvAudioTracksList(status, streams);};
	void recvAudioTrackCurrent(int status, audioStream& stream){pCallback->recvAudioTrackCurrent(status, stream);};
	void recvAudioTrackSelected(int status, int trackId){pCallback->recvAudioTrackSelected(status, trackId);};
	void recvSubtitleMessage(subtitleMessage& sub){pCallback->recvSubtitleMessage(sub);};
	void recvSubtitleTracksList(int status, std::vector<subtitleStream>& streams){pCallback->recvSubtitleTracksList(status, streams);};
	void recvSubtitleTrackCurrent(int status, subtitleStream& stream){pCallback->recvSubtitleTrackCurrent(status, stream);};
	void recvSubtitleTrackSelected(int status, int trackId){pCallback->recvSubtitleTrackSelected(status, trackId);};
	void recvVideoTrackCurrent(int status, videoStream& stream){pCallback->recvVideoTrackCurrent(status, stream);};
	void recvSeekTo(int status, int seconds){pCallback->recvSeekTo(status, seconds);};
	void recvSeekRelative(int status, int seconds){pCallback->recvSeekRelative(status, seconds);};
	void recvErrorMessage(errorMessage& message){pCallback->recvErrorMessage(message);};
	void recvWindEnd(int direction){pCallback->recvWindEnd(direction);};
	void recvSubtitleFlush(){pCallback->recvSubtitleFlush();};
	void recvAudioChannel(int status, int channel){pCallback->recvAudioChannel(status, channel);};
	void recvMetadata(const std::string& title, const std::string& artist, const std::string& text){pCallback->recvMetadata(title, artist, text);};
	void recvChapters(std::vector<chapterEntry>& chapters){pCallback->recvChapters(chapters);};
	void recvCover(const std::string& file){pCallback->recvCover(file);};
	// the player announced the dual mono selection (am<n>)
	bool mCanSelectChannel = false;
	// the player announced "forcedonly" (so1 / so0)
	bool mCanForcedOnly = false;
public:
	virtual ~BasePlayer(){}

	void setCallback(iPlayerCallback *cb){pCallback = cb;}
	void setPath(const std::string& path){mPath = path;}
	void setHttpHeaders(const std::map<std::string, std::string>& headers){mHeaders = headers;}
	bool canWind() const {return mCanWind;}
	bool canSelectChannel() const {return mCanSelectChannel;}
	bool canForcedOnly() const {return mCanForcedOnly;}
	// the stream is known to be live (it ended by itself without a length)
	virtual void setLiveStream(bool live){}

	virtual int start(eMainloop *context) = 0;

};


class PlayerBackend: public sigc::trackable, public eThread, public eMainloop, public iPlayerCallback
{
	struct Message
	{
		int type;
		int dataInt;
		enum
		{
			start,
			tStart,
			stop,
			tStop,
			tKill,
			tForceKill,
			pause,
			tPause,
			resume,
			tResume,
			seekTo,
			tSeekTo,
			seekRelative,
			tSeekRelative,
			tFastForward,
			audioSelect,
			tAudioSelect,
			audioList,
			tAudioList,
			subtitleSelect,
			tSubtitleSelect,
			subtitleList,
			tSubtitleList,
			tGetLength,
			tGetPosition,
			videoSizeChanged,
			videoFramerateChanged,
			videoProgressiveChanged,
			videoGammaChanged,
			subtitleAvailable,
			audioChanged,
			windStart,
			windEnd,
			error,
			tAudioChannel,
			subtitleFlush,
			subtitleTracksChanged,
			metadataChanged,
			tSeekToMs,
			chaptersChanged,
			coverAvailable,
			tSubtitleForcedOnly,
		};
		Message(int type)
			:type(type), dataInt(0) {}
		Message(int type, int dataInt)
			:type(type), dataInt(dataInt) {}
	};

	int mPositionInMs, mLengthInMs;
	int mExitCode; // of the player process, -1 while it runs
	int mAudioChannel; // dual mono selection, 1 stereo
	bool playbackStarted;
	bool mThreadRunning;
	bool mDebug;

	BasePlayer *pPlayer;

	audioStream *pCurrentAudio;
	videoStream *pCurrentVideo;
	subtitleStream *pCurrentSubtitle;
	errorMessage *pErrorMessage;

	std::vector<audioStream> mAudioStreams;
	std::vector<subtitleStream> mSubtitleStreams;
	std::queue<subtitleMessage> mSubtitles;
	// last live channels/codec per track id from "ac", the track list is the
	// player's open-time snapshot
	std::map<int, audioStream> mLiveAudio;
	unsigned int mAudioCheckCounter;

	eFixedMessagePump<Message> mMessageMain, mMessageThread;
	ePtr<eTimer> mTimer;
	unsigned int mTimerDelay;

	eSingleLock mSubLock;
	// protects mAudioStreams/mSubtitleStreams/pCurrent*/pErrorMessage (parser thread vs e2 main thread)
	eSingleLock mStreamsLock;
	
	pthread_mutex_t mWaitMutex;
	pthread_cond_t mWaitCond;
	bool mWaitForUpdate;

	pthread_mutex_t mWaitForStopMutex;
	pthread_cond_t mWaitForStopCond;
	bool mWaitForStop;

	void gotMessage(const Message &message);
	void _updatePosition();

	void sendMessage(const Message& m, int timeout=0);
	void recvMessage();

	// eThread
	void thread();
	void thread_finished();

	// iPlayerCallback
	void recvStarted(int status);
	void recvStopped(int status);
	void recvPaused(int status);
	void recvResumed(int status);
	void recvLength(int status, int mseconds){ if (!status) mLengthInMs = mseconds; }
	void recvPosition(int status, int mseconds){ if (!status) mPositionInMs = mseconds; }
	void recvAudioTracksList(int status, std::vector<audioStream>& streams);
	void recvAudioTrackCurrent(int status, audioStream& stream);
	void recvAudioTrackSelected(int status, int trackId);
	void recvSubtitleTracksList(int status, std::vector<subtitleStream>& streams);
	void recvSubtitleTrackCurrent(int status, subtitleStream& stream);
	void recvSubtitleTrackSelected(int status, int trackId);
	void recvVideoTrackCurrent(int status, videoStream& stream);
	void recvSeekTo(int status, int seconds){eDebug("PlayerBackend::recvSeekTo %ds", seconds);}
	void recvSeekRelative(int status, int seconds){eDebug("PlayerBackend::recvSeekRelative %ds", seconds);}
	void recvErrorMessage(errorMessage& message){
		{
			eSingleLocker l_(mStreamsLock);
			if (pErrorMessage) delete pErrorMessage;
			pErrorMessage = new errorMessage(message);
		}
		// evUser+12 in the main thread, the text is sUser+12
		mMessageMain.send(Message(Message::error));
	};
	void recvSubtitleMessage(subtitleMessage& sub);
	void recvWindEnd(int direction){mMessageMain.send(Message(direction < 0 ? Message::windStart : Message::windEnd));}
	void recvSubtitleFlush();
	void recvAudioChannel(int status, int channel){ if (!status && channel >= 0 && channel <= 2) mAudioChannel = channel; }
	void recvMetadata(const std::string& title, const std::string& artist, const std::string& text){
		{
			eSingleLocker l_(mStreamsLock);
			mMetaTitle = title;
			mMetaArtist = artist;
			mMetaText = text;
		}
		mMessageMain.send(Message(Message::metadataChanged));
	}
	std::string mMetaTitle, mMetaArtist, mMetaText;
	std::vector<chapterEntry> mChapters;
	void recvChapters(std::vector<chapterEntry>& chapters){
		{
			eSingleLocker l_(mStreamsLock);
			mChapters = chapters;
		}
		mMessageMain.send(Message(Message::chaptersChanged));
	}
	void recvCover(const std::string& file){ if (!file.empty()) mMessageMain.send(Message(Message::coverAvailable)); }

public:
	PlayerBackend(BasePlayer* extplayer):
		mPositionInMs(0),
		mLengthInMs(0),
		mExitCode(-1),
		mAudioChannel(1),
		playbackStarted(false),
		mThreadRunning(false),
		mDebug(false),
		pPlayer(extplayer),
		pCurrentAudio(NULL),
		pCurrentVideo(NULL),
		pCurrentSubtitle(NULL),
		pErrorMessage(NULL),
		mAudioCheckCounter(0),
		mMessageMain(eApp, 1),
		mMessageThread(this, 1),
		mTimerDelay(100), // updated play position timer
		mWaitForUpdate(false),
		mWaitForStop(false)

	{
		pPlayer->setCallback(this);
		CONNECT(mMessageThread.recv_msg, PlayerBackend::gotMessage);
		CONNECT(mMessageMain.recv_msg, PlayerBackend::gotMessage);
		pthread_mutex_init(&mWaitMutex, NULL);
		pthread_cond_init(&mWaitCond, NULL);
		pthread_mutex_init(&mWaitForStopMutex, NULL);
		pthread_cond_init(&mWaitForStopCond, NULL);
	}
	~PlayerBackend()
	{
		// join the backend thread first: its recv* callbacks use and
		// replace the pCurrent... / pErrorMessage objects
		stop();
		if (pErrorMessage != NULL)
			delete pErrorMessage;
		if (pCurrentVideo != NULL)
			delete pCurrentVideo;
		if (pCurrentAudio != NULL)
			delete pCurrentAudio;
		if (pCurrentSubtitle != NULL)
			delete pCurrentSubtitle;
		pthread_mutex_destroy(&mWaitMutex);
		pthread_cond_destroy(&mWaitCond);
		pthread_mutex_destroy(&mWaitForStopMutex);
		pthread_cond_destroy(&mWaitForStopCond);
	}
	int start(const std::string& path, const std::map<std::string,std::string>& headers);
	int stop();
	int pause();
	int resume();
	int seekTo(int seconds);
	int seekRelative(int seconds);
	int fastForward(int speed);
	bool canWind(){return pPlayer->canWind();}
	/* dual mono: 0 left, 1 stereo, 2 right */
	bool canSelectAudioChannel(){return pPlayer->canSelectChannel();}
	/* only the forced cues of the subtitle track, exteplayer3 "forcedonly" */
	bool canSubtitleForcedOnly(){return pPlayer->canForcedOnly();}
	int subtitleForcedOnly(bool on){
		if (!pPlayer->canForcedOnly())
			return -1;
		mMessageThread.send(Message(Message::tSubtitleForcedOnly, on ? 1 : 0));
		return 0;
	}
	int audioSelectChannel(int channel);
	int audioGetChannel() const { return mAudioChannel; }
	void getChapters(std::vector<chapterEntry>& chapters){
		eSingleLocker l_(mStreamsLock);
		chapters = mChapters;
	}
	int seekToMs(int mseconds);
	/* ICY / radio text: title, artist and the whole line ("Artist - Title") */
	void getMetadata(std::string& title, std::string& artist, std::string& text){
		eSingleLocker l_(mStreamsLock);
		title = mMetaTitle;
		artist = mMetaArtist;
		text = mMetaText;
	}
	int getLength(int& mseconds);
	int getPlayPosition(int& mseconds);
	int getErrorMessage(errorMessage& error);
	/* length the player reported, 0 = none (live stream) */
	int lengthMs() const { return mLengthInMs; }
	/* exit code of the player process, -1 while it runs */
	int exitCode() const { return mExitCode; }
	int getSubtitles(std::queue<subtitleMessage>&);
	int audioGetNumberOfTracks(int timeout=0);
	int audioSelectTrack(int trackId);
	int audioGetTrackInfo(audioStream& trackInfo, int trackId);
	int audioGetCurrentTrackNum();
	int subtitleGetNumberOfTracks(int timeout=0);
	int subtitleSelectTrack(int trackId);
	int subtitleGetTrackInfo(subtitleStream& trackInfo, int trackId);
	int subtitleGetCurrentTrackNum();
	int videoGetTrackInfo(videoStream& trackInfo, int trackId);
	void setDebug(bool debug){mDebug = debug;}

	PSignal1<void,int> gotPlayerMessage;
};

class WaitThread: public eThread
{
	bool& waitForUpdate;
	pthread_mutex_t& mutex;
	pthread_cond_t& cond;
	long timeout;
	void thread();
	bool timedOut;
public:
	WaitThread(pthread_mutex_t& mutex, pthread_cond_t& cond, bool& waitForUpdate, long timeout):
		waitForUpdate(waitForUpdate),
		mutex(mutex),
		cond(cond),
		timeout(timeout),
		timedOut(false)
	{
	}
	bool isTimedOut(){return timedOut;}
};

#endif
