#include <algorithm>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <extplayer.h>
#include <cJSON/cJSON.h>
#include <error.h>
#include "common.h"

void PlayerApp::handleJsonStr(const std::string& data)
{
	eLog(5, "PlayerApp::handleJsonStr: %s", data.c_str());
	cJSON *json = cJSON_Parse(data.c_str());
	if (!json)
	{
		eDebug("Error before: [%s]", cJSON_GetErrorPtr());
		return;
	}
	handleJsonOutput(json);
	cJSON_Delete(json);
}
/* a line longer than this is no message of the player: it is dropped and
 * the parser starts again after its end */
static const size_t MAX_LINE_LENGTH = 256 * 1024;

void PlayerApp::handleLine(const char *data, size_t len)
{
	/* a line that is not JSON (e.g. debug text on the same stderr channel)
	 * is dropped alone */
	if (len > 0 && data[0] == '{')
		handleJsonStr(std::string(data, len));
}

void PlayerApp::handleOutput(const char *data, size_t len)
{
	/* only complete lines are parsed: a chunk that ended after an inner '}'
	 * of a long line was taken for a whole message before, and the line was
	 * lost. The rest of a line is kept until its '\n' comes. */
	size_t start = 0;
	while (start < len)
	{
		const char *nl = (const char *)memchr(data + start, '\n', len - start);
		if (!nl)
			break;
		size_t end = nl - data;
		if (mDiscardLine)
		{
			/* the end of an overlong line: in sync again */
			mDiscardLine = false;
		}
		else if (jsonstr.empty())
		{
			handleLine(data + start, end - start);
		}
		else
		{
			jsonstr.append(data + start, end - start);
			handleLine(jsonstr.data(), jsonstr.size());
		}
		jsonstr.clear();
		start = end + 1;
	}
	if (start < len && !mDiscardLine)
	{
		if (jsonstr.size() + (len - start) > MAX_LINE_LENGTH)
		{
			eWarning("PlayerApp::handleOutput - line longer than %zu bytes dropped", MAX_LINE_LENGTH);
			jsonstr.clear();
			mDiscardLine = true;
		}
		else
		{
			jsonstr.append(data + start, len - start);
		}
	}
}

void PlayerApp::stderrData(const char *data, int len)
{
	eLog(5, "PlayerApp::stderrData: %.*s", len, data);
	if (parseOutput == STD_ERROR && len > 0)
	{
		handleOutput(data, len);
	}
}

void PlayerApp::stdoutData(const char *data, int len)
{
	eLog(5, "PlayerApp::stdoutData: %.*s", len, data);
	if (parseOutput == STD_OUTPUT && len > 0)
	{
		handleOutput(data, len);
	}
}

void PlayerApp::appClosed(int retval)
{
	/* the last line of a player that ended without its '\n' */
	if (!mDiscardLine && !jsonstr.empty())
		handleLine(jsonstr.data(), jsonstr.size());
	jsonstr.clear();
	mDiscardLine = false;
	handleProcessStopped(retval);
}

int PlayerApp::processStart(eMainloop *context)
{
	jsonstr.clear();
	mDiscardLine = false;
	console = new eConsoleContainer();
	CONNECT(console->appClosed, PlayerApp::appClosed);
	CONNECT(console->stdoutData, PlayerApp::stdoutData);
	CONNECT(console->stderrData, PlayerApp::stderrData);
	const std::vector<std::string> args = buildCommand();
	eDebugNoNewLine("PlayerApp::processStart: ");
	std::vector<const char *> cargs(args.size()+1);
	for (size_t i=0; i <= args.size(); i++)
	{
		// execvp needs args array terminated with NULL
		if (i == args.size())
		{
			cargs[i] = NULL;
			eDebugNoNewLine("\n");
		}
		else
		{
			cargs[i] = args[i].c_str();
			if (i != 0 && cargs[i][0] != '-')
				eDebugNoNewLine("\"%s\" ", cargs[i]);
			else
				eDebugNoNewLine("%s ", cargs[i]);
		}
	}
	int ret = console->execute(context, cargs[0], cargs.data());
	return ret;
}

int PlayerApp::processSend(const std::string& data)
{
	if (console && console->running())
	{
		eLog(5, "sending command \"%s\" ", data.c_str());
		/* position, length and live audio polls: the next tick asks again,
		 * they may go while the player does not read its stdin */
		bool poll = data == "j\n" || data == "l\n" || data == "ac\n";
		console->write(data.c_str(), data.length(), poll);
		return 0;
	}
	return -1;
}

void PlayerApp::processKill()
{
	if (console && console->running())
	{
		console->sendCtrlC();
	}
}

void PlayerApp::processForceKill()
{
	if (console && console->running())
	{
		console->sendKill();
	}
}

bool PlayerApp::processRunning()
{
	return console ? console->running() : false;
}


static int64_t monotonicMs()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Waits on the calling thread until the backend thread clears flag (and
 * signals cond), at most timeout ms; false on a timeout, the flag is cleared
 * then, so a late answer finds nobody waiting. A loop on the flag: a single
 * pthread_cond_timedwait took a spurious wakeup for "in time" (stop() then
 * joined a running player, the GUI froze). The conds use CLOCK_MONOTONIC.
 * Before, a pthread was created for every wait. */
static bool waitForClear(pthread_mutex_t &mutex, pthread_cond_t &cond, bool &flag, long timeout, clockid_t clock = CLOCK_MONOTONIC)
{
	struct timespec ts;
	clock_gettime(clock, &ts);
	ts.tv_sec += timeout / 1000;
	ts.tv_nsec += (timeout % 1000) * 1000000L;
	// pthread_cond_timedwait requires tv_nsec in [0, 1e9)
	if (ts.tv_nsec >= 1000000000L)
	{
		ts.tv_sec += ts.tv_nsec / 1000000000L;
		ts.tv_nsec %= 1000000000L;
	}
	bool cleared = true;
	pthread_mutex_lock(&mutex);
	while (flag)
	{
		int ret = pthread_cond_timedwait(&cond, &mutex, &ts);
		if (ret != 0 && ret != EINTR && flag)
		{
			// ETIMEDOUT (or an error that would only repeat)
			flag = false;
			cleared = false;
			break;
		}
	}
	pthread_mutex_unlock(&mutex);
	return cleared;
}

void WaitThread::thread()
{
	hasStarted();
	/* in a loop on the flag as well: a spurious wakeup is no answer */
	timedOut = !waitForClear(mutex, cond, waitForUpdate, timeout, CLOCK_REALTIME);
}

bool PlayerBackend::postToPlayer(const Message& m)
{
	/* the loop of an ended player takes no more messages: they piled up in
	 * mMessageThread for good (a live stream's getLength on every poll) */
	if (!canPost())
		return false;
	mMessageThread.send(m);
	return true;
}

void PlayerBackend::sendListRequest(const Message& m, bool& waitFlag, int timeout)
{
	if (!canPost())
		return;
	if (timeout > 0)
	{
		pthread_mutex_lock(&mWaitMutex);
		waitFlag = true;
		pthread_mutex_unlock(&mWaitMutex);
		mMessageThread.send(m);
		bool inTime = waitForClear(mWaitMutex, mWaitCond, waitFlag, timeout);
		if (mDebug)
			eDebug("PlayerBackend::sendListRequest - answer %s", inTime ? "in time" : "timed out");
	}
	else
	{
		mMessageThread.send(m);
	}
}

bool PlayerBackend::recvListAnswer(bool& waitFlag)
{
	pthread_mutex_lock(&mWaitMutex);
	bool waiting = waitFlag;
	if (waiting)
	{
		waitFlag = false;
		pthread_cond_broadcast(&mWaitCond);
	}
	pthread_mutex_unlock(&mWaitMutex);
	return waiting;
}

void PlayerBackend::startPositionTimer()
{
	mTimer->start(mTimerDelay, false);
	mTimerActive = true;
}

void PlayerBackend::setPositionInterval(int ms)
{
	if (ms < 20)
		ms = 20;
	if ((unsigned int)ms == mTimerDelay)
		return;
	mTimerDelay = ms;
	/* the timer belongs to the backend thread: it restarts it there */
	postToPlayer(Message(Message::tPositionInterval, ms));
}

void PlayerBackend::_updatePosition()
{
	pPlayer->sendUpdatePosition();

	/* about every 2 s (whatever the poll interval) re-check the current
	 * audio track: an ad break or programme change can switch 5.1 to 2.0 or
	 * DD+ to Atmos on the same track, see recvAudioTrackCurrent() */
	if (++mAudioCheckCounter * mTimerDelay >= 2000)
	{
		mAudioCheckCounter = 0;
		pPlayer->sendPollAudioTrackCurrent();
	}
}

int PlayerBackend::start(const std::string& path, const std::map<std::string,std::string>& headers)
{
	pPlayer->setPath(path);
	pPlayer->setHttpHeaders(headers);
	// start player only when mainloop in player thread has started
	mMessageThread.send(Message(Message::tStart));
	run();
	return 0;
}
/* arms the stop wait unless the player has ended already; false: ended */
static bool armStopWait(pthread_mutex_t &mutex, bool &waitForStop, const std::atomic<bool> &ended)
{
	pthread_mutex_lock(&mutex);
	bool arm = !ended;
	waitForStop = arm;
	pthread_mutex_unlock(&mutex);
	return arm;
}

int PlayerBackend::stop()
{
	mStopRequested = true;
	/* a player that has ended already (recvStopped ran, the loop is about to
	 * quit) answers no "q": the 3 + 2 s waits only blocked the GUI */
	if (mThreadRunning && armStopWait(mWaitForStopMutex, mWaitForStop, mProcessEnded))
	{
		// wait 3 seconds for a normal exit, then SIGINT, then SIGKILL
		mMessageThread.send(Message(Message::tStop));
		if (!waitForClear(mWaitForStopMutex, mWaitForStopCond, mWaitForStop, 3000)
			&& armStopWait(mWaitForStopMutex, mWaitForStop, mProcessEnded))
		{
			/* the join in kill() blocks the main loop until the app ends,
			 * a SIGKILL follows when it ignores SIGINT */
			mMessageThread.send(Message(Message::tKill));
			if (!waitForClear(mWaitForStopMutex, mWaitForStopCond, mWaitForStop, 2000))
			{
				mMessageThread.send(Message(Message::tForceKill));
			}
		}
	}
	kill();
	return 0;
}

int PlayerBackend::pause()
{
	if (!playbackStarted)
		return -1;
	return postToPlayer(Message(Message::tPause)) ? 0 : -1;
}

int PlayerBackend::fastForward(int speed)
{
	if (!playbackStarted || !pPlayer->canWind())
		return -1;
	return postToPlayer(Message(Message::tFastForward, speed)) ? 0 : -1;
}

int PlayerBackend::resume()
{
	if (!playbackStarted)
		return -1;
	return postToPlayer(Message(Message::tResume)) ? 0 : -1;
}

int PlayerBackend::seekTo(int seconds)
{
	if (!playbackStarted)
		return -1;
	return postToPlayer(Message(Message::tSeekTo, seconds)) ? 0 : -1;
}

int PlayerBackend::seekToMs(int mseconds)
{
	if (!playbackStarted)
		return -1;
	return postToPlayer(Message(Message::tSeekToMs, mseconds)) ? 0 : -1;
}

int PlayerBackend::seekRelative(int seconds)
{
	if (!playbackStarted)
		return -1;
	return postToPlayer(Message(Message::tSeekRelative, seconds)) ? 0 : -1;
}

int PlayerBackend::getPlayPosition(int& mseconds)
{
	if (!playbackStarted)
	{
		return -1;
	}
	if (!mPositionInMs) 
	{
		//mMessageThread.send(Message(Message::tGetPosition));
		return -2;
	}
	mseconds = mPositionInMs;
	return 0;
}

int PlayerBackend::getLength(int& mseconds)
{
	if (!playbackStarted)
	{
		return -1;
	}
	int length = mLengthInMs;
	if (!length)
	{
		/* the GUI polls this several times a second; a live stream has no
		 * length: once the player answered 0, ask only every 5 s (a length
		 * may still come), before the first answer every 0.5 s */
		int64_t now = monotonicMs();
		if (now - mLastLengthAsk >= (mLengthZero ? 5000 : 500) && postToPlayer(Message(Message::tGetLength)))
			mLastLengthAsk = now;
		return -2;
	}
	mseconds = length;
	return 0;
}

int PlayerBackend::getErrorMessage(errorMessage& error)
{
	// also before the start: the reason a stream could not be opened
	eSingleLocker l_(mStreamsLock);
	if (!pErrorMessage)
	{
		return -1;
	}
	error = *pErrorMessage;
	return 0;
}

/* The GUI asks for the track counts often (skin converters, the InfoBar on
 * every evUpdatedInfo); a wait blocked it up to 500 ms each time. The player
 * sends both lists at the start and again when they change (a_l, s_l), so
 * the cached count is the answer; only while no list is known yet and the
 * player runs the caller waits, at most 200 ms. */
static const int LIST_WAIT_MAX_MS = 200;

int PlayerBackend::audioGetNumberOfTracks(int timeout)
{
	size_t count;
	bool known;
	{
		eSingleLocker l_(mStreamsLock);
		count = mAudioStreams.size();
		known = mAudioListKnown;
	}
	if (timeout < 0 || !canPost())
		return count;
	if (timeout == 0)
	{
		// asynchronous: the answer comes as audioTracksAvailable
		sendListRequest(Message(Message::tAudioList), mWaitForAudioList, 0);
		return count;
	}
	if (known || !playbackStarted)
		return count;
	pthread_mutex_lock(&mWaitMutex);
	bool waiting = mWaitForAudioList;
	pthread_mutex_unlock(&mWaitMutex);
	if (!waiting)
		sendListRequest(Message(Message::tAudioList), mWaitForAudioList, std::min(timeout, LIST_WAIT_MAX_MS));
	eSingleLocker l_(mStreamsLock);
	return mAudioStreams.size();
}

int PlayerBackend::audioGetCurrentTrackNum()
{
	int trackNum = 0, j=0;
	eSingleLocker l_(mStreamsLock);
	int trackId = pCurrentAudio ? pCurrentAudio->id : 0;
	for (std::vector<audioStream>::const_iterator i(mAudioStreams.begin()); i!=mAudioStreams.end(); i++, j++)
	{
		if (trackId == i->id)
		{
			trackNum = j;
			break;
		}
	}
	return trackNum;
}

int PlayerBackend::audioSelectChannel(int channel)
{
	if (channel < 0 || channel > 2 || !pPlayer->canSelectChannel())
		return -1;
	mAudioChannel = channel;
	return postToPlayer(Message(Message::tAudioChannel, channel)) ? 0 : -1;
}

int PlayerBackend::audioSelectTrack(int trackNum)
{
	eSingleLocker l_(mStreamsLock);
	if (trackNum >= 0 && trackNum < (int) mAudioStreams.size())
	{
		postToPlayer(Message(Message::tAudioSelect, mAudioStreams[trackNum].id));
		/* the new track at once: the audio selection asks getCurrentTrack()
		 * right after the switch, the answer a_s comes later and corrects
		 * this if the switch failed */
		pCurrentAudio = mAudioStreams[trackNum];
		return 0;
	}
	return -1;
}

int PlayerBackend::audioGetTrackInfo(audioStream& trackInfo, int trackNum)
{
	eSingleLocker l_(mStreamsLock);
	if (trackNum >= 0 && trackNum < (int) mAudioStreams.size())
	{
		trackInfo = mAudioStreams[trackNum];
		return 0;
	}
	return -1;
}

int PlayerBackend::subtitleGetNumberOfTracks(int timeout)
{
	size_t count;
	bool known;
	{
		eSingleLocker l_(mStreamsLock);
		count = mSubtitleStreams.size();
		known = mSubtitleListKnown;
	}
	if (timeout < 0 || !canPost())
		return count;
	if (timeout == 0)
	{
		/* asynchronous, only while no list is known (the player pushes its
		 * changes): an answer that changes the list comes as
		 * subtitleTracksChanged */
		if (!known)
			sendListRequest(Message(Message::tSubtitleList), mWaitForSubtitleList, 0);
		return count;
	}
	if (known || !playbackStarted)
		return count;
	pthread_mutex_lock(&mWaitMutex);
	bool waiting = mWaitForSubtitleList;
	pthread_mutex_unlock(&mWaitMutex);
	if (!waiting)
		sendListRequest(Message(Message::tSubtitleList), mWaitForSubtitleList, std::min(timeout, LIST_WAIT_MAX_MS));
	eSingleLocker l_(mStreamsLock);
	return mSubtitleStreams.size();
}

int PlayerBackend::subtitleGetCurrentTrackNum()
{
	int trackNum = 0, j=0;
	eSingleLocker l_(mStreamsLock);
	int trackId = pCurrentSubtitle ? pCurrentSubtitle->id : 0;
	for (std::vector<subtitleStream>::const_iterator i(mSubtitleStreams.begin()); i!=mSubtitleStreams.end(); i++, j++)
	{
		if (trackId == i->id)
		{
			trackNum = j;
			break;
		}
	}
	return trackNum;
}

int PlayerBackend::subtitleSelectTrack(int trackNum)
{
	eSingleLocker l_(mStreamsLock);
	if (trackNum == -1)
	{
		// subtitles off: the player stops decoding and sending them
		postToPlayer(Message(Message::tSubtitleSelect, -1));
		pCurrentSubtitle.reset();
		return 0;
	}
	if (trackNum >= 0 && trackNum < (int) mSubtitleStreams.size())
	{
		postToPlayer(Message(Message::tSubtitleSelect, mSubtitleStreams[trackNum].id));
		/* the new track at once: the GUI asks right after the selection,
		 * the answer s_s comes later; it corrects this if it differs */
		pCurrentSubtitle = mSubtitleStreams[trackNum];
		return 0;
	}
	return -1;
}

int PlayerBackend::subtitleGetTrackInfo(subtitleStream& trackInfo, int trackNum)
{
	eSingleLocker l_(mStreamsLock);
	if (trackNum >= 0 && trackNum < (int) mSubtitleStreams.size())
	{
		trackInfo = mSubtitleStreams[trackNum];
		return 0;
	}
	return -1;
}

int PlayerBackend::videoGetTrackInfo(videoStream& trackInfo, int trackNum)
{
	eSingleLocker l_(mStreamsLock);
	if (!pCurrentVideo)
		return -1;
	trackInfo = *pCurrentVideo;
	return 0;
}


void PlayerBackend::gotMessage(const PlayerBackend::Message& message)
{
	switch (message.type)
	{
		case Message::tStart:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tStart");
			if (pPlayer->start(this) < 0)
			{
				pthread_mutex_lock(&mWaitForStopMutex);
				mProcessEnded = true; // stop() has nothing to wait for
				pthread_mutex_unlock(&mWaitForStopMutex);
				quit(0);
				mMessageMain.send(Message(Message::stop));
			}
			else
			{
				mTimer = eTimer::create(this);
				CONNECT(mTimer->timeout, PlayerBackend::_updatePosition);
			}
			break;
		case Message::tStop:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tStop");
			if (mTimer)
				mTimer->stop();
			mTimerActive = false;
			pPlayer->sendStop();
			break;
		case Message::tPositionInterval:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tPositionInterval %d", message.dataInt);
			// a stopped (paused) timer starts with the new interval on resume
			if (mTimer && mTimerActive)
				startPositionTimer();
			break;
		case Message::tKill:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tKill");
			pPlayer->sendForceStop();
			break;
		case Message::tForceKill:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tForceKill");
			pPlayer->sendKill();
			break;
		case Message::tPause:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tPause");
			pPlayer->sendPause();
			break;
		case Message::tResume:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tUnpause");
			pPlayer->sendResume();
			break;
		case Message::tFastForward:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tFastForward");
			pPlayer->sendFastForward(message.dataInt);
			break;
		case Message::tSeekTo:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tSeekTo");
			pPlayer->sendSeekTo(message.dataInt);
			break;
		case Message::tSeekRelative:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tSeekRelative");
			pPlayer->sendSeekRelative(message.dataInt);
			break;
		case Message::tAudioSelect:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tAudioSelect");
			pPlayer->sendAudioSelectTrack(message.dataInt);
			break;
		case Message::tAudioList:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tAudioList");
			pPlayer->sendUpdateAudioTracksList();
			break;
		case Message::tSubtitleSelect:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tSubtitleSelect");
			if (message.dataInt < 0)
				pPlayer->sendSubtitleDisable();
			else
				pPlayer->sendSubtitleSelectTrack(message.dataInt);
			break;
		case Message::tSubtitleList:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tSubtitleList");
			pPlayer->sendUpdateSubtitleTracksList();
			break;
		case Message::tAudioChannel:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tAudioChannel %d", message.dataInt);
			pPlayer->sendAudioChannel(message.dataInt);
			break;
		case Message::subtitleFlush:
			gotPlayerMessage(PlayerMessage::subtitleFlush);
			break;
		case Message::subtitleTracksChanged:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - subtitleTracksChanged");
			gotPlayerMessage(PlayerMessage::subtitleTracksChanged);
			break;
		case Message::metadataChanged:
			gotPlayerMessage(PlayerMessage::metadataChanged);
			break;
		case Message::tSeekToMs:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tSeekToMs %d", message.dataInt);
			pPlayer->sendSeekToMs(message.dataInt);
			break;
		case Message::chaptersChanged:
			gotPlayerMessage(PlayerMessage::chaptersChanged);
			break;
		case Message::tSubtitleForcedOnly:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tSubtitleForcedOnly %d", message.dataInt);
			pPlayer->sendSubtitleForcedOnly(message.dataInt != 0);
			break;
		case Message::coverAvailable:
			gotPlayerMessage(PlayerMessage::coverAvailable);
			break;
		case Message::tGetLength:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - tGetLength");
			pPlayer->sendUpdateLength();
			break;
		case Message::start:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - start");
			gotPlayerMessage(PlayerMessage::start);
			break;
		case Message::stop:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - stop");
			gotPlayerMessage(PlayerMessage::stop);
			break;
		case Message::pause:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - pause");
			gotPlayerMessage(PlayerMessage::pause);
			break;
		case Message::resume:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - resume");
			gotPlayerMessage(PlayerMessage::resume);
			break;
		case Message::videoSizeChanged:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - videoSizeChanged");
			gotPlayerMessage(PlayerMessage::videoSizeChanged);
			break;
		case Message::videoFramerateChanged:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - videoFramerateChanged");
			gotPlayerMessage(PlayerMessage::videoFramerateChanged);
			break;
		case Message::videoProgressiveChanged:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - videoProgressiveChanged");
			gotPlayerMessage(PlayerMessage::videoProgressiveChanged);
			break;
		case Message::videoGammaChanged:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - videoGammaChanged");
			gotPlayerMessage(PlayerMessage::videoGammaChanged);
			break;
		case Message::audioSelect:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - audioSelect");
			break;
		case Message::audioList:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - audioList");
			gotPlayerMessage(PlayerMessage::audioTracksAvailable);
			break;
		case Message::audioChanged:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - audioChanged");
			gotPlayerMessage(PlayerMessage::audioChanged);
			break;
		case Message::error:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - error");
			gotPlayerMessage(PlayerMessage::error);
			break;
		case Message::windStart:
			gotPlayerMessage(PlayerMessage::windStart);
			break;
		case Message::windEnd:
			gotPlayerMessage(PlayerMessage::windEnd);
			break;
		case Message::subtitleAvailable:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - subtitleAvailable");
			gotPlayerMessage(PlayerMessage::subtitleAvailable);
			break;
		default:
			if(mDebug)
				eDebug("PlayerBackend::gotMessage - unhandled message");
			break;
	}
}

void PlayerBackend::thread()
{
	mThreadRunning = true;
	hasStarted();
	runLoop();
}

void PlayerBackend::thread_finished()
{
	if(mDebug)
		eDebug("PlayerBackend::thread_finished");
	mThreadRunning = false;
}

void PlayerBackend::recvStarted(int status)
{
	if(mDebug)
		eDebug("PlayerBackend::recvStart - status = %d", status);
	if (playbackStarted || status)
		return;
	playbackStarted = true;
	startPositionTimer();
	mMessageMain.send(Message(Message::start));
}

void PlayerBackend::recvStopped(int retval)
{
	pthread_mutex_lock(&mWaitForStopMutex);
	mProcessEnded = true;
	if (mWaitForStop)
	{
		mWaitForStop = false;
		pthread_cond_broadcast(&mWaitForStopCond);
	}
	pthread_mutex_unlock(&mWaitForStopMutex);
	if(mDebug)
		eDebug("PlayerBackend::recvStopped - retval = %d", retval);
	int sig = exitSignal(retval);
	if (sig && !mStopRequested)
	{
		/* killed by a signal (SIGSEGV, the OOM killer) without our stop: an
		 * error, not the end of the stream; a live stream counts it as a lost
		 * connection. An error the player reported itself is kept. */
		bool reported;
		{
			eSingleLocker l_(mStreamsLock);
			reported = pErrorMessage.has_value();
		}
		if (!reported)
		{
			errorMessage e;
			char buf[128];
			/* TRANSLATORS: %d is the number of the signal that ended the player (11 SIGSEGV, 9 SIGKILL) */
			snprintf(buf, sizeof(buf), SAPP_TR("player ended by signal %d"), sig);
			e.code = retval;
			e.message = buf;
			eWarning("PlayerBackend::recvStopped - player ended by signal %d", sig);
			recvErrorMessage(e);
		}
	}
	mExitCode = retval;
	quit(0);
	mMessageMain.send(Message(Message::stop));
}

void PlayerBackend::recvPaused(int status)
{
	if(mDebug)
		eDebug("PlayerBackend::recvPause - status = %d", status);
	if (!status)
	{
		mTimer->stop();
		mTimerActive = false;
		mMessageMain.send(Message(Message::pause));
	}
}

void PlayerBackend::recvResumed(int status)
{
	if(mDebug)
		eDebug("PlayerBackend::recvResume - status = %d", status);
	if (!status)
	{
		startPositionTimer();
		mMessageMain.send(Message(Message::resume));
	}
}

void PlayerBackend::recvAudioTracksList(int status, std::vector<audioStream>& streams)
{
	if(!status)
	{
		{
			eSingleLocker l_(mStreamsLock);
			mAudioStreams = streams;
			// keep what the player learned live, the list is the open-time snapshot
			for (std::vector<audioStream>::iterator i(mAudioStreams.begin()); i != mAudioStreams.end(); ++i)
			{
				std::map<int, audioStream>::const_iterator l(mLiveAudio.find(i->id));
				if (l == mLiveAudio.end())
					continue;
				if (l->second.channels > 0)
					i->channels = l->second.channels;
				if (!l->second.codec_name.empty())
					i->codec_name = l->second.codec_name;
			}
			mAudioListKnown = true;
		}
		mMessageMain.send(Message(Message::audioList));
	}
	recvListAnswer(mWaitForAudioList);
}

void PlayerBackend::recvAudioTrackCurrent(int status, audioStream& stream)
{ 
	if(mDebug)
		eDebug("PlayerBackend::recvAudioTrackCurrent - status = %d", status);
	if(!status)
	{
		bool changed = false;
		eSingleLocker l_(mStreamsLock);
		pCurrentAudio = stream;

		/* the answer to the periodic "ac" carries the live channel count and
		 * codec name; update the track list entry the track info reads */
		if (stream.channels > 0 || !stream.codec_name.empty())
		{
			audioStream &live = mLiveAudio[stream.id];
			if (stream.channels > 0)
				live.channels = stream.channels;
			if (!stream.codec_name.empty())
				live.codec_name = stream.codec_name;
			for (std::vector<audioStream>::iterator i(mAudioStreams.begin()); i != mAudioStreams.end(); ++i)
			{
				if (i->id != stream.id)
					continue;
				if (stream.channels > 0 && i->channels != stream.channels)
				{
					i->channels = stream.channels;
					changed = true;
				}
				if (!stream.codec_name.empty() && i->codec_name != stream.codec_name)
				{
					i->codec_name = stream.codec_name;
					changed = true;
				}
				break;
			}
		}
		if (changed)
			mMessageMain.send(Message(Message::audioChanged));
	}
} 

void PlayerBackend::recvAudioTrackSelected(int status, int trackId)
{
	if(mDebug)
		eDebug("PlayerBackend::recvAudioTrackSelected - status = %d, trackId = %d", status, trackId);
	if (!status)
	{
		eSingleLocker l_(mStreamsLock);
		for (std::vector<audioStream>::const_iterator i(mAudioStreams.begin()); i!=mAudioStreams.end(); i++)
		{
			if (trackId == i->id)
			{
				pCurrentAudio = *i;
				break;
			}
		}
	}
	else
	{
		/* the switch failed: ask for the real track instead of the one
		 * audioSelectTrack() assumed */
		pPlayer->sendUpdateAudioTrackCurrent();
	}
}

void PlayerBackend::recvSubtitleTracksList(int status, std::vector<subtitleStream>& streams)
{
	bool changed = false;
	if(!status)
	{
		eSingleLocker l_(mStreamsLock);
		changed = playbackStarted && streams.size() != mSubtitleStreams.size();
		mSubtitleStreams = streams;
		mSubtitleListKnown = true;
	}
	/* a waiting GUI caller reads the new list itself; only the subtitle wait
	 * counts, an audio list wait no longer hid this answer */
	bool asked = recvListAnswer(mWaitForSubtitleList);
	/* exteplayer3 sends the list again by itself when closed captions show
	 * up; also the answer to a request nobody waited for */
	if (changed && !asked)
		mMessageMain.send(Message(Message::subtitleTracksChanged));
}

void PlayerBackend::recvSubtitleFlush()
{
	{
		eSingleLocker s(mSubLock);
		while (!mSubtitles.empty())
		{
			/* the pictures of a dropped graphic cue are not shown any more */
			const std::vector<subtitleImage> &images = mSubtitles.front().images;
			for (size_t i = 0; i < images.size(); i++)
				unlink(images[i].file.c_str());
			mSubtitles.pop();
		}
	}
	mMessageMain.send(Message(Message::subtitleFlush));
}

void PlayerBackend::recvSubtitleTrackCurrent(int status, subtitleStream& stream)
{ 
	if(mDebug)
		eDebug("PlayerBackend::recvSubtitleTrackCurrent - status = %d", status);
	if(!status)
	{
		eSingleLocker l_(mStreamsLock);
		pCurrentSubtitle = stream;
	}
} 

void PlayerBackend::recvSubtitleTrackSelected(int status, int trackId)
{
	if(mDebug)
		eDebug("PlayerBackend::recvSubtitleTrackSelected - status = %d, trackId = %d", status, trackId);
	if (!status)
	{
		eSingleLocker l_(mStreamsLock);
		for (std::vector<subtitleStream>::const_iterator i(mSubtitleStreams.begin()); i!=mSubtitleStreams.end(); i++)
		{
			if (trackId == i->id)
			{
				pCurrentSubtitle = *i;
				break;
			}
		}
	}
	else
	{
		/* the switch failed: ask for the real track instead of the one
		 * subtitleSelectTrack() assumed */
		pPlayer->sendUpdateSubtitleTrackCurrent();
	}
}

void PlayerBackend::recvVideoTrackCurrent(int status, videoStream& stream)
{
	if(mDebug)
		eDebug("PlayerBackend::recvVideoTrackCurrent - status = %d", status);
	if (!status)
	{
		eSingleLocker l_(mStreamsLock);
		videoStream prev;
		if (pCurrentVideo)
			prev = *pCurrentVideo;
		pCurrentVideo = stream;
		if (stream.progressive >= 0 && prev.progressive != stream.progressive)
			mMessageMain.send(Message(Message::videoProgressiveChanged));
		if (stream.gamma >= 0 && prev.gamma != stream.gamma)
			mMessageMain.send(Message(Message::videoGammaChanged));
		if (stream.framerate > 0 && prev.framerate != stream.framerate)
			mMessageMain.send(Message(Message::videoFramerateChanged));
		// also when a known size gets unknown (other track): no stale size
		if ((prev.width != stream.width && (stream.width > 0 || prev.width > 0))
			|| (prev.height != stream.height && (stream.height > 0 || prev.height > 0)))
			mMessageMain.send(Message(Message::videoSizeChanged));
	}
}

void PlayerBackend::recvSubtitleMessage(subtitleMessage& sub)
{
	eSingleLocker s(mSubLock);
	mSubtitles.push(sub);
	mMessageMain.send(Message(Message::subtitleAvailable));
}

int PlayerBackend::getSubtitles(std::queue< subtitleMessage >& subtitles)
{
	eSingleLocker s(mSubLock);
	if (mSubtitles.empty())
	{
		return -1;
	}
	while (!mSubtitles.empty())
	{
		subtitles.push(mSubtitles.front());
		mSubtitles.pop();
	}
	return 0;
}
