#ifndef __extplayer_h
#define __extplayer_h

#include <lib/base/ebase.h>
#include <lib/base/message.h>
#include <lib/base/thread.h>
#include <lib/python/connections.h>

#include <atomic>
#include <climits>
#include <map>
#include <optional>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

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
/* a length in seconds (PLAYBACK_LENGTH) as ms: float * 1000 to int overflowed
 * (undefined) for garbage values; NaN and negative are "no length" */
static inline int lengthToMs(double seconds)
{
	double ms = seconds * 1000.0;
	if (!(ms > 0.0))
		return 0;
	if (ms >= (double)INT_MAX)
		return INT_MAX;
	return (int)ms;
}
static inline std::string cJsonGetStr(cJSON *obj, const char *key)
{
	cJSON *n = obj ? cJSON_GetObjectItem(obj, key) : NULL;
	return (n && n->type == cJSON_String && n->valuestring) ? std::string(n->valuestring) : std::string();
}

#ifndef eLog
 #define eLog(lvl,...)
#endif

/* the signal that ended the player: eConsoleContainer reports 128 + signal
 * number (as a shell does), 0 for a normal exit */
static inline int exitSignal(int exitCode)
{
	return (exitCode > 128 && exitCode <= 128 + 64) ? exitCode - 128 : 0;
}

enum
{
	STD_OUTPUT,
	STD_ERROR,
};

class PlayerApp: public sigc::trackable
{
	ePtr<eConsoleContainer> console;
	std::string jsonstr; // the start of a line whose '\n' has not come yet
	unsigned int parseOutput;
	bool mDiscardLine; // an overlong line is dropped up to its '\n'
	void stdoutData(const char *data, int len);
	void stderrData(const char *data, int len);
	void appClosed(int retval);
	void handleOutput(const char *data, size_t len);
	void handleLine(const char *data, size_t len);
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
		mDiscardLine(false){}
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
	int sar_num, sar_den; /* sample aspect ratio ("an", "ad"), 0 unknown */
	videoStream(): id(-1), width(-1), height(-1), framerate(-1), progressive(-1), gamma(-1), bit_depth(0), bitrate(0), dolby_vision(0), sar_num(0), sar_den(0){};
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
	// byte counters of the selected video / audio track (live bit rate)
	virtual int sendUpdateBitrate(){ return -1;}
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
	// compressed bytes of the selected tracks so far (32 bit, wrapping), -1: no such track
	virtual void recvBitrate(int64_t videoBytes, int64_t audioBytes){};
};


class BasePlayer: public iPlayerSend, public iPlayerCallback
{
	iPlayerCallback *pCallback = nullptr;

protected:
	std::string mPath;
	std::map<std::string, std::string> mHeaders;
	// the player announced that it can wind; the EPLAYER3_EXTENDED fields are
	// written by the backend thread and read by the GUI: atomic
	std::atomic<bool> mCanWind{false};

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
	void recvBitrate(int64_t videoBytes, int64_t audioBytes){pCallback->recvBitrate(videoBytes, audioBytes);};
	// the player announced the dual mono selection (am<n>)
	std::atomic<bool> mCanSelectChannel{false};
	// the player announced "forcedonly" (so1 / so0)
	std::atomic<bool> mCanForcedOnly{false};
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
			tPositionInterval,
		};
		Message(int type)
			:type(type), dataInt(0) {}
		Message(int type, int dataInt)
			:type(type), dataInt(dataInt) {}
	};

	/* written by the backend thread, read by the GUI without a lock: atomic */
	std::atomic<int> mPositionInMs, mLengthInMs;
	std::atomic<int> mExitCode; // of the player process, -1 while it runs
	std::atomic<int> mAudioChannel; // dual mono selection, 1 stereo
	std::atomic<bool> playbackStarted;
	std::atomic<bool> mThreadRunning;
	bool mDebug;

	BasePlayer *pPlayer;

	std::optional<audioStream> pCurrentAudio;
	std::optional<videoStream> pCurrentVideo;
	std::optional<subtitleStream> pCurrentSubtitle;
	std::optional<errorMessage> pErrorMessage;

	std::vector<audioStream> mAudioStreams;
	std::vector<subtitleStream> mSubtitleStreams;
	std::queue<subtitleMessage> mSubtitles;
	// last live channels/codec per track id from "ac", the track list is the
	// player's open-time snapshot
	std::map<int, audioStream> mLiveAudio;
	unsigned int mAudioCheckCounter;
	// live bit rate from the byte counters of the player ("BR"): the last
	// reading (backend thread) and the kbit/s for the GUI, -1 not available
	unsigned int mBitrateCheckCounter;
	int64_t mBitrateLastMs, mBitrateLastVideoBytes, mBitrateLastAudioBytes;
	std::atomic<int> mVideoKbps, mAudioKbps;

	eFixedMessagePump<Message> mMessageMain, mMessageThread;
	ePtr<eTimer> mTimer;
	// position poll interval (ms): 500, 100 while subtitles are shown
	std::atomic<unsigned int> mTimerDelay;
	bool mTimerActive; // backend thread only: the position poll runs

	eSingleLock mSubLock;
	// protects mAudioStreams/mSubtitleStreams/pCurrent*/pErrorMessage (parser thread vs e2 main thread)
	eSingleLock mStreamsLock;
	// a list came from the player in this playback (under mStreamsLock)
	bool mAudioListKnown, mSubtitleListKnown;

	/* a GUI caller waits for the answer to its list request: one flag per
	 * list, an s_l must not end the wait for an a_l (stale track count) */
	pthread_mutex_t mWaitMutex;
	pthread_cond_t mWaitCond;
	bool mWaitForAudioList;
	bool mWaitForSubtitleList;

	pthread_mutex_t mWaitForStopMutex;
	pthread_cond_t mWaitForStopCond;
	bool mWaitForStop;
	// the player process has ended (recvStopped, or it never started);
	// set under mWaitForStopMutex, read without it by the send paths
	std::atomic<bool> mProcessEnded;
	// stop() was called: a signal that ends the player then is ours
	std::atomic<bool> mStopRequested;
	// the player answered "l" with length 0 (live): ask less often
	std::atomic<bool> mLengthZero;
	int64_t mLastLengthAsk; // main thread only, monotonic ms

	void gotMessage(const Message &message);
	void _updatePosition();

	// the backend loop runs and the player has not ended
	bool canPost() const { return mThreadRunning && !mProcessEnded; }
	// a t* message to the backend loop, false (not sent) without a player
	bool postToPlayer(const Message& m);
	// a list request, waiting at most timeout ms for the answer (flag)
	void sendListRequest(const Message& m, bool& waitFlag, int timeout);
	// an answer came: wakes its waiter; true when one was waiting
	bool recvListAnswer(bool& waitFlag);
	void startPositionTimer();

	// eThread
	void thread();
	void thread_finished();

	// iPlayerCallback
	void recvStarted(int status);
	void recvStopped(int status);
	void recvPaused(int status);
	void recvResumed(int status);
	void recvLength(int status, int mseconds){
		if (status)
			return;
		mLengthInMs = mseconds;
		mLengthZero = mseconds <= 0;
	}
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
			pErrorMessage = message;
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
	void recvBitrate(int64_t videoBytes, int64_t audioBytes);

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
		mAudioCheckCounter(0),
		mBitrateCheckCounter(0),
		mBitrateLastMs(0),
		mBitrateLastVideoBytes(-1),
		mBitrateLastAudioBytes(-1),
		mVideoKbps(-1),
		mAudioKbps(-1),
		mMessageMain(eApp, 1, "extplayer"),
		mMessageThread(this, 1, "extplayer"),
		mTimerDelay(500), // updated play position timer, see setPositionInterval
		mTimerActive(false),
		mAudioListKnown(false),
		mSubtitleListKnown(false),
		mWaitForAudioList(false),
		mWaitForSubtitleList(false),
		mWaitForStop(false),
		mProcessEnded(false),
		mStopRequested(false),
		mLengthZero(false),
		mLastLengthAsk(0)

	{
		pPlayer->setCallback(this);
		CONNECT(mMessageThread.recv_msg, PlayerBackend::gotMessage);
		CONNECT(mMessageMain.recv_msg, PlayerBackend::gotMessage);
		/* the timed waits use CLOCK_MONOTONIC: a step of the wall clock
		 * (DVB TDT, NTP) neither cuts them short nor stretches them */
		pthread_condattr_t attr;
		pthread_condattr_init(&attr);
		pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
		pthread_mutex_init(&mWaitMutex, NULL);
		pthread_cond_init(&mWaitCond, &attr);
		pthread_mutex_init(&mWaitForStopMutex, NULL);
		pthread_cond_init(&mWaitForStopCond, &attr);
		pthread_condattr_destroy(&attr);
	}
	~PlayerBackend()
	{
		// join the backend thread first: its recv* callbacks use and
		// replace the pCurrent... / pErrorMessage objects (they are
		// members and go only after this body)
		stop();
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
		return postToPlayer(Message(Message::tSubtitleForcedOnly, on ? 1 : 0)) ? 0 : -1;
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
	/* live bit rate of the selected video / audio track in kbit/s, -1 when
	 * the player does not report its byte counters */
	int getVideoBitrate() const { return mVideoKbps; }
	int getAudioBitrate() const { return mAudioKbps; }
	int getSubtitles(std::queue<subtitleMessage>&);
	/* how often the play position is polled: 500 ms by default (the GUI's
	 * seek bar), 100 ms while subtitles are shown (their timing) */
	void setPositionInterval(int ms);
	/* timeout < 0: the cached count only; 0: also ask the player without
	 * waiting (subtitles: only while no list is known); > 0: wait (at most
	 * 200 ms) only while no list is known */
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

/* a timed wait in a thread of its own; PlayerBackend waits on the calling
 * thread now, ResolveUrl (scriptrun) still uses this. Its cond is made with
 * the default clock, CLOCK_REALTIME. */
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
