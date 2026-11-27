#include <sstream>
#include <cctype>
#include <cstdio>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#include <lib/base/eerror.h>
#include "exteplayer3.h"

const std::string  EXT3_SW_DECODING_AAC            = "aac_swdec";
const std::string  EXT3_SW_DECODING_AC3            = "ac3_swdec";
const std::string  EXT3_SW_DECODING_EAC3           = "eac3_swdec";
const std::string  EXT3_SW_DECODING_DTS            = "dts_swdec";
const std::string  EXT3_SW_DECODING_MP3            = "mp3_swdec";
const std::string  EXT3_SW_DECODING_WMA            = "wma_swdec";
const std::string  EXT3_DOWNMIX                    = "downmix";
const std::string  EXT3_LPCM_INJECTION             = "lpcm_injection";
const std::string  EXT3_NO_PCM_RESAMPLING          = "no_pcm_resampling";
const std::string  EXT3_FLV2MPEG4_CONVERTER        = "flv2mpeg4";
const std::string  EXT3_PLAYBACK_PROGRESSIVE       = "progressive";
const std::string  EXT3_PLAYBACK_INIFITY_LOOP      = "loop";
const std::string  EXT3_PLAYBACK_LIVETS            = "live_ts";
const std::string  EXT3_PLAYBACK_AUDIO_TRACK_ID    = "audio_id";
const std::string  EXT3_PLAYBACK_SUBTITLE_TRACK_ID = "subtitle_id";
const std::string  EXT3_PLAYBACK_AUDIO_URI         = "audio_uri";
const std::string  EXT3_PLAYBACK_DASH_VIDEO_ID     = "dash_video_id";
const std::string  EXT3_PLAYBACK_DASH_AUDIO_ID     = "dash_audio_id";
const std::string  EXT3_PLAYBACK_MPEGTS_PROGRAM    = "mpegts_program_id";
const std::string  EXT3_RTMP_PROTOCOL              = "rtmpproto";
const std::string  EXT3_NICE_VALUE                 = "nice";
const std::string  EXT3_FFMPEG_SETTING_STRING      = "ffmpeg_option";
const std::string  EXT3_ARG_LCASE_B                = "b";
const std::string  EXT3_ARG_5                      = "5";
const std::string  EXT3_ARG_6                      = "6";
const std::string  EXT3_ARG_7                      = "7";
// exteplayer3 186 and newer
const std::string  EXT3_TRANSCODE_DTS              = "dts_ac3";
const std::string  EXT3_TRANSCODE_DTS_EAC3         = "dts_eac3";
const std::string  EXT3_TRANSCODE_TRUEHD           = "truehd_ac3";
const std::string  EXT3_TRANSCODE_TRUEHD_EAC3      = "truehd_eac3";
const std::string  EXT3_TRANSCODE_HEAAC            = "heaac_aaclc";
const std::string  EXT3_HLS_QUALITY                = "hls_quality";
const std::string  EXT3_HLS_DEFAULT_AUDIO          = "hls_default_audio";
const std::string  EXT3_COVER                      = "cover";
const std::string  EXT3_KEY                        = "key"; // #sapp_key=kid:key[,kid:key...]
const std::string  EXT3_GRAPHIC_SUBS               = "graphic_subs"; // -G base directory, set by the plugin only
const std::string  EXT3_ARGS_LIST                  = "args"; // cmdline like list of exteplayer3 arguments in a form "-arg1=value1-arg2=value2"

SettingMap &ExtEplayer3Options::GetSettingMap()
{
	return settingMap;
}

ExtEplayer3Options::ExtEplayer3Options()
{
	settingMap[EXT3_SW_DECODING_AAC]            = SettingEntry ("-a", "int");
	settingMap[EXT3_SW_DECODING_EAC3]           = SettingEntry ("-e", "bool");
	settingMap[EXT3_SW_DECODING_AC3]            = SettingEntry ("-3", "bool");
	settingMap[EXT3_SW_DECODING_DTS]            = SettingEntry ("-d", "bool");
	settingMap[EXT3_SW_DECODING_MP3]            = SettingEntry ("-m", "bool");
	settingMap[EXT3_SW_DECODING_WMA]            = SettingEntry ("-w", "bool");
	settingMap[EXT3_LPCM_INJECTION]             = SettingEntry ("-l", "bool");
	settingMap[EXT3_DOWNMIX]                    = SettingEntry ("-s", "bool");
	settingMap[EXT3_NO_PCM_RESAMPLING]          = SettingEntry ("-r", "bool");
	settingMap[EXT3_FLV2MPEG4_CONVERTER]        = SettingEntry ("-4", "int");
	settingMap[EXT3_PLAYBACK_INIFITY_LOOP]      = SettingEntry ("-i", "bool");
	settingMap[EXT3_PLAYBACK_LIVETS]            = SettingEntry ("-v", "bool");
	settingMap[EXT3_RTMP_PROTOCOL]              = SettingEntry ("-n", "int");
	settingMap[EXT3_PLAYBACK_PROGRESSIVE]       = SettingEntry ("-o", "int");
	settingMap[EXT3_NICE_VALUE]                 = SettingEntry ("-p", "int");
	settingMap[EXT3_PLAYBACK_MPEGTS_PROGRAM]    = SettingEntry ("-P", "int");
	settingMap[EXT3_PLAYBACK_AUDIO_TRACK_ID]    = SettingEntry ("-t", "int");
	settingMap[EXT3_PLAYBACK_SUBTITLE_TRACK_ID] = SettingEntry ("-9", "int");
	settingMap[EXT3_PLAYBACK_AUDIO_URI]         = SettingEntry ("-x", "string");
	settingMap[EXT3_PLAYBACK_DASH_VIDEO_ID]     = SettingEntry ("-0", "int");
	settingMap[EXT3_PLAYBACK_DASH_AUDIO_ID]     = SettingEntry ("-1", "int");
	settingMap[EXT3_FFMPEG_SETTING_STRING]      = SettingEntry ("-f", "string");
	settingMap[EXT3_ARG_LCASE_B]                = SettingEntry ("-b", "int");
	settingMap[EXT3_ARG_5]                      = SettingEntry ("-5", "string");
	settingMap[EXT3_ARG_6]                      = SettingEntry ("-6", "string");
	settingMap[EXT3_ARG_7]                      = SettingEntry ("-7", "string");
	/* exteplayer3 187 ("xcode" in EPLAYER3_EXTENDED): the long names, -D and -Q differ between builds */
	settingMap[EXT3_TRANSCODE_DTS]              = SettingEntry ("--dts-ac3", "bool");
	settingMap[EXT3_TRANSCODE_DTS_EAC3]         = SettingEntry ("--dts-eac3", "bool");
	settingMap[EXT3_TRANSCODE_TRUEHD]           = SettingEntry ("--truehd-ac3", "bool");
	settingMap[EXT3_TRANSCODE_TRUEHD_EAC3]      = SettingEntry ("--truehd-eac3", "bool");
	settingMap[EXT3_TRANSCODE_HEAAC]            = SettingEntry ("--heaac-aaclc", "bool");
	/* exteplayer3 185 and newer: -Q 0|1|2 (auto, lowest, highest), -D 0|1 (all or only the default audio) */
	settingMap[EXT3_HLS_QUALITY]                = SettingEntry ("-Q", "nonzero");
	settingMap[EXT3_HLS_DEFAULT_AUDIO]          = SettingEntry ("-D", "nonzero");
	/* "cover" in EPLAYER3_EXTENDED: the embedded cover to this file (servicemp3's path) */
	settingMap[EXT3_COVER]                      = SettingEntry ("--cover", "string");
	/* "drmkey" & 1: CENC keys per KID, one --key each (MP4, DASH, HLS fMP4) */
	settingMap[EXT3_KEY]                        = SettingEntry ("--key", "keylist");
	/* graphic subtitles (PGS, DVB, XSUB) as PNG files, -G (exteplayer3 63 and newer).
	 * Own type: update() does not take it, a url cannot point it elsewhere */
	settingMap[EXT3_GRAPHIC_SUBS]               = SettingEntry ("-G", "subdir");
	settingMap[EXT3_ARGS_LIST]                  = SettingEntry ("al", "argslist");
}

int ExtEplayer3Options::update(const std::string &key, const std::string &val)
{
	int ret = 0;
	if (settingMap.find(key) != settingMap.end())
	{
		SettingEntry &entry = settingMap[key];
		if (entry.getType() == "bool")
		{
			if (val == "1")
				entry.setValue(1);
			else if (val == "0")
				entry.setValue(0);
			else
			{
				eWarning("ExtEplayer3Options::update - invalid value '%s' for '%s' setting, allowed values are 0|1", val.c_str(), key.c_str());
				ret = -2;
			}
		}
		else if (entry.getType() == "int" || entry.getType() == "nonzero")
		{
			char *endptr = NULL;
			int intval = -1;
			intval = strtol(val.c_str(), &endptr , 10);
			if (!*endptr && intval >= 0)
			{
				if (key == EXT3_SW_DECODING_AAC || key == EXT3_RTMP_PROTOCOL)
				{
					if (intval < 3)
					{
						entry.setValue(intval);
					}
					else
					{
						eWarning("ExtEplayer3Options::update - invalid value '%s' for '%s' setting, allowed values <0,2>", val.c_str(), key.c_str());
						ret = -2;
					}
				}
				else
				{
					entry.setValue(intval);
				}
			}
			else
			{
				eWarning("ExtEplayer3Options::update - invalid value '%s' for '%s' setting, allowed values are >= 0", val.c_str(), key.c_str());
				ret = -2;
			}
		}
		else if (entry.getType() == "string")
		{
			entry.setValue(val);
		}
		else if (entry.getType() == "argslist")
		{
                     entry.setValue(val);
		}
		else if (entry.getType() == "keylist")
		{
			/* kid:key pairs separated by "," or ";": hex digits, dashes (UUID
			 * form) and base64 characters, nothing else goes to the player */
			bool ok = !val.empty();
			for (size_t i = 0; ok && i < val.size(); i++)
			{
				char c = val[i];
				ok = isalnum((unsigned char)c) || c == ':' || c == ',' || c == ';' || c == '-' || c == '_' || c == '+' || c == '/' || c == '=';
			}
			if (ok)
				entry.setValue(val);
			else
			{
				eWarning("ExtEplayer3Options::update - invalid value for '%s'", key.c_str());
				ret = -2;
			}
		}
	}
	else
	{
		eWarning("ExtEplayer3Options::update - not recognized setting '%s'", key.c_str());
		ret = -1;
	}
	return ret;
}

void ExtEplayer3Options::print() const
{
	for (SettingIter it(settingMap.begin()); it != settingMap.end(); it++)
	{
		eDebug(" %-30s = %s", it->first.c_str(), it->second.toString().c_str());
	}
}

ExtEplayer3::ExtEplayer3(ExtEplayer3Options& options): PlayerApp(STD_ERROR), mErrorReported(false), mSeekMs(false), mLiveStream(false), mCanBitrate(false), mBitrateProbes(0)
{
	mPlayerOptions = options;
	eDebug("ExtEplayer3::ExtEplayer3 initializing with options:");
	mPlayerOptions.print();
}

ExtEplayer3::~ExtEplayer3()
{
	removeGraphicSubDir();
}

/* graphic subtitles: the pictures of this player go to a directory of its own
 * below the base directory, the next player (zap, live reconnect) may start
 * before this one has ended. It is emptied at the start and removed when the
 * player has stopped; eServiceApp deletes each file once its cue is over. */
static void clearGraphicSubDir(const std::string &dir, bool remove)
{
	DIR *d = opendir(dir.c_str());
	if (d)
	{
		struct dirent *e;
		while ((e = readdir(d)) != NULL)
		{
			std::string n(e->d_name);
			if (n == "." || n == "..")
				continue;
			unlink((dir + "/" + n).c_str()); /* NNN_PTS_I.png and its .tmp while written */
		}
		closedir(d);
	}
	if (remove)
		rmdir(dir.c_str());
}

void ExtEplayer3::removeGraphicSubDir()
{
	if (!mGraphicSubDir.empty())
		clearGraphicSubDir(mGraphicSubDir, true);
}

/* an exteplayer3 binary in /usr/bin or /usr/local/bin (exteplayer3, exteplayer3-test ...) */
static bool isInstalledPlayer(const std::string &path)
{
	static const char *dirs[] = {"/usr/bin/", "/usr/local/bin/"};
	for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
	{
		std::string dir(dirs[i]);
		if (path.compare(0, dir.size(), dir) != 0)
			continue;
		std::string name = path.substr(dir.size());
		if (name.compare(0, 11, "exteplayer3") != 0 || name.find('/') != std::string::npos)
			return false;
		for (size_t j = 0; j < name.size(); j++)
		{
			char c = name[j];
			if (!isalnum((unsigned char)c) && c != '_' && c != '.' && c != '-')
				return false;
		}
		return access(path.c_str(), X_OK) == 0;
	}
	return false;
}

std::vector<std::string> ExtEplayer3::buildCommand()
{
	// TODO add all options
	std::vector<std::string> args;
	args.push_back("exteplayer3");
	size_t pos = mPath.find("&suburi=");
	size_t pos1 = mPath.find("&cenc_decryption_key=");
	if (pos1 != std::string::npos)
	{
		args.push_back(mPath.substr(0, pos1));
		args.push_back("-f");
		args.push_back(mPath.substr((pos1) + 1,52));
		if (pos != std::string::npos)
		{
			args.push_back(mPath.substr(0, pos));
			args.push_back("-x");
			args.push_back(mPath.substr(pos + 8));
		}
	}
	else if (pos != std::string::npos)
	{
		args.push_back(mPath.substr(0, pos));
		args.push_back("-x");
		args.push_back(mPath.substr(pos + 8));
	}
	else
	{
		args.push_back(mPath);
	}
	std::map<std::string,std::string>::const_iterator i(mHeaders.find("User-Agent"));
	if (i != mHeaders.end())
	{
		args.push_back("-u");
		args.push_back(i->second);
	}
	std::string headersStr;
	for (std::map<std::string,std::string>::const_iterator i(mHeaders.begin()); i != mHeaders.end(); i++)
	{
		if (i->first.compare("User-Agent") == 0)
			continue;
		headersStr +=  i->first + ":" + i->second + "\r\n";
	}
	if (!headersStr.empty())
	{
		args.push_back("-h");
		args.push_back(headersStr);
	}
	/* a stream known to be live (reconnected by ServiceApp): FFmpeg's http
	 * reconnects a stream without a length only with reconnect_streamed,
	 * which exteplayer3 sets only in -v mode. Not for the first start: a VOD
	 * without a length would restart at its end from byte 0. The delays
	 * 0+1+3+7+15 s stay below the 30 s after which enigma2 restarts. */
	if (mLiveStream && (mPath.compare(0, 7, "http://") == 0 || mPath.compare(0, 8, "https://") == 0))
	{
		args.push_back("-f");
		args.push_back("reconnect_streamed=1");
		args.push_back("-f");
		args.push_back("reconnect_delay_max=15");
	}
	for (SettingIter i(mPlayerOptions.GetSettingMap().begin()); i != mPlayerOptions.GetSettingMap().end(); i++)
	{
		if (!i->second.isSet())
		{
			continue;
		}
		if (i->second.getType() == "bool" && i->second.getValueInt())
		{
			args.push_back(i->second.getAppArg());
		}
		if (i->second.getType() == "nonzero" && i->second.getValueInt() > 0)
		{
			/* the player default is 0, older players do not know the option */
			args.push_back(i->second.getAppArg());
			args.push_back(i->second.getValue());
		}
		if (i->second.getType() == "keylist")
		{
			std::string list = i->second.getValue();
			size_t start = 0;
			while (start <= list.size())
			{
				size_t end = list.find_first_of(",;", start);
				if (end == std::string::npos)
					end = list.size();
				if (end > start)
				{
					args.push_back(i->second.getAppArg());
					args.push_back(list.substr(start, end - start));
				}
				start = end + 1;
			}
		}
		if (i->second.getType() == "int" || i->second.getType() == "string")
		{
            args.push_back(i->second.getAppArg()); //add arg name to list of args
			args.push_back(i->second.getValue());  //add value to list of args
		}
		if (i->second.getType() == "argslist")
                {
                        std::stringstream ss(i->second.getValue());
                        std::string item;

                        while (std::getline(ss, item, '-'))
                        {
                                if (!item.empty())
                                {
                                        size_t eq = item.find('=');
                                        /* only a bare item names the player: a value (an url,
                                         * a path) that contains "exteplayer3" is an argument */
                                        if (eq == std::string::npos && item.find("exteplayer3") != std::string::npos)
                                        {
                                                /* the args come with the url (#sapp_args=): only another
                                                 * installed exteplayer3 build, never any program */
                                                if (isInstalledPlayer(item))
                                                        args[0] = item;
                                                else
                                                        eWarning("ExtEplayer3::buildCommand - sapp_args: player '%s' refused", item.c_str());
                                        }
                                        else
                                        {
                                                // split arg=value
                                                if (eq != std::string::npos)
                                                {
                                                        std::string key = item.substr(0, eq);
                                                        std::string value = item.substr(eq + 1);
                                                        /* -G/-W/-H (graphic subtitle directory and window)
                                                         * come only from the plugin: with a url the root
                                                         * player would write its PNG files anywhere */
                                                        if (key == "G" || key == "W" || key == "H")
                                                        {
                                                                eWarning("ExtEplayer3::buildCommand - sapp_args: option -%s refused", key.c_str());
                                                                continue;
                                                        }
                                                        args.push_back("-" + key);
                                                        args.push_back(value);
                                                }
                                                else
                                                {
                                                        args.push_back(item);
                                                }
                                        }
                                }
                        }
                }
			if (i->second.getType() == "subdir")
			{
				/* only below /tmp (RAM): the directory is emptied */
				std::string base = i->second.getValue();
				if (base.compare(0, 5, "/tmp/") != 0 || base.find("..") != std::string::npos)
				{
					eWarning("ExtEplayer3::buildCommand - graphic subtitle directory '%s' refused", base.c_str());
					continue;
				}
				if (mGraphicSubDir.empty())
				{
					static unsigned int instance = 0;
					std::ostringstream dir;
					dir << base << "/" << ++instance;
					mGraphicSubDir = dir.str();
				}
				mkdir(base.c_str(), 0755);
				mkdir(mGraphicSubDir.c_str(), 0755);
				clearGraphicSubDir(mGraphicSubDir, false);
				args.push_back(i->second.getAppArg());
				args.push_back(mGraphicSubDir);
				args.push_back("-W");
				args.push_back(std::to_string(EXT3_GRAPHIC_SUB_WIDTH));
				args.push_back("-H");
				args.push_back(std::to_string(EXT3_GRAPHIC_SUB_HEIGHT));
			}
	}
	return args;
}
int ExtEplayer3::start(eMainloop *context)
{
	mVideo = videoStream();
	mDecoderVideo = videoStream();
	mErrorReported = false;
	return processStart(context);
}


int ExtEplayer3::sendStop(){ return processSend(std::string("q\n"));}
int ExtEplayer3::sendForceStop(){ processKill(); return 0;}
int ExtEplayer3::sendKill(){ processForceKill(); return 0;}
int ExtEplayer3::sendPause(){ return processSend(std::string("p\n"));}
int ExtEplayer3::sendResume(){ return processSend(std::string("c\n"));}
int ExtEplayer3::sendFastForward(int speed)
{
	std::stringstream sstm;
	if (speed < 0)
		sstm << "b" << -speed << std::endl;
	else
		sstm << "f" << speed << std::endl;
	return processSend(sstm.str());
}
int ExtEplayer3::sendUpdateLength(){ return processSend(std::string("l\n"));}
int ExtEplayer3::sendUpdatePosition(){ return processSend(std::string("j\n"));}
int ExtEplayer3::sendUpdateAudioTracksList(){ return processSend(std::string("al\n"));}
int ExtEplayer3::sendUpdateAudioTrackCurrent(){ return processSend(std::string("ac\n"));}
/* exteplayer3 keeps channels and codec name of the current track live */
int ExtEplayer3::sendPollAudioTrackCurrent(){ return sendUpdateAudioTrackCurrent();}

int ExtEplayer3::sendAudioSelectTrack(int trackId)
{
	std::stringstream sstm;
	sstm << "a" << trackId << std::endl;
	return processSend(sstm.str());
}

int ExtEplayer3::sendUpdateSubtitleTracksList(){ return processSend(std::string("sl\n"));}
int ExtEplayer3::sendUpdateSubtitleTrackCurrent(){ return processSend(std::string("sc\n"));}

int ExtEplayer3::sendSubtitleSelectTrack(int trackId)
{
	std::stringstream sstm;
	sstm << "s" << trackId << std::endl;
	return processSend(sstm.str());
}

/* exteplayer3 takes track -1 as "no subtitles" */
int ExtEplayer3::sendSubtitleDisable(){ return processSend(std::string("s-1\n"));}

/* exteplayer3 187 ("forcedonly"): so1 only the forced cues, so0 all again */
int ExtEplayer3::sendSubtitleForcedOnly(bool on)
{
	if (!mCanForcedOnly)
		return -1;
	return processSend(std::string(on ? "so1\n" : "so0\n"));
}

/* dual mono, exteplayer3 187 ("achannel"): am0 left, am1 stereo, am2 right */
int ExtEplayer3::sendAudioChannel(int channel)
{
	if (!mCanSelectChannel)
		return -1;
	std::stringstream sstm;
	sstm << "am" << channel << std::endl;
	return processSend(sstm.str());
}

/* byte counters of the selected tracks, answered with {"BR":{"v":<bytes>,
 * "a":<bytes>}}. Players that announce "br" get it all the time; the others
 * get three requests and keep it only when they answer one of them (no
 * player announces it yet). Players without it ignore "BR" (no 'B'
 * command; lowercase 'b' would wind backwards). */
int ExtEplayer3::sendUpdateBitrate()
{
	if (!mCanBitrate)
	{
		if (mBitrateProbes >= 3)
			return -1;
		mBitrateProbes++;
	}
	return processSend(std::string("BR\n"));
}

int ExtEplayer3::sendSeekTo(int seconds)
{
	std::stringstream sstm;
	sstm << "gc" << seconds << std::endl;
	return processSend(sstm.str());
}

/* exteplayer3 187 ("seekms"): gf takes a fraction and has not the 10 s
 * limits of gc (length >= 10 s, at most 10 s before the end) */
int ExtEplayer3::sendSeekToMs(int mseconds)
{
	if (!mSeekMs)
		return sendSeekTo(mseconds / 1000);
	if (mseconds < 0)
		mseconds = 0;
	char buf[32];
	snprintf(buf, sizeof(buf), "gf%d.%03d\n", mseconds / 1000, mseconds % 1000);
	return processSend(std::string(buf));
}

int ExtEplayer3::sendSeekRelative(int seconds)
{
	std::stringstream sstm;
	sstm << "kc" << seconds << std::endl;
	return processSend(sstm.str());
}

void ExtEplayer3::handleProcessStopped(int retval)
{
	/* exteplayer3 exit codes: 0 end or stop, 1 usage (an option it does not
	 * know), 10 the input could not be opened, 11 stop during the open */
	if (retval == 1 && !mErrorReported)
	{
		errorMessage e;
		e.code = 1;
		e.message = "exteplayer3 did not accept its options (exit code 1)";
		recvErrorMessage(e);
	}
	/* no more pictures come: the files still there are not needed any more */
	removeGraphicSubDir();
	recvStopped(retval);
}

/* fields that only newer exteplayer3 versions send */
static void readAudioExtras(cJSON *item, audioStream &a)
{
	a.channels = cJsonGetInt(item, "c", a.channels);
	a.codec_name = cJsonGetStr(item, "d");
	a.channel_layout = cJsonGetStr(item, "cl");
	a.title = cJsonGetStr(item, "t");
	a.sample_rate = cJsonGetInt(item, "sr");
	a.bitrate = cJsonGetInt(item, "br");
}

static void readSubtitleExtras(cJSON *item, subtitleStream &s)
{
	s.forced = cJsonGetInt(item, "f", s.forced);
	s.title = cJsonGetStr(item, "t");
}

/* "f" of "v_e" is the frame rate as the video driver reports it: frames per
 * 1000 s like "f" of "v_c" (DVB API). A driver that reports plain frames per
 * second is brought to the same unit. */
static int decoderFramerate(int framerate)
{
	if (framerate <= 0)
		return -1;
	return framerate < 1000 ? framerate * 1000 : framerate;
}

void ExtEplayer3::handleJsonOutput(cJSON *json)
{
	if (!json->child)
	{
		return;
	}
	const char *key = json->child->string;
	if (!key)
		return;
	cJSON* value = cJSON_GetObjectItem(json, key);

	if (!strcmp(key, "PLAYBACK_PLAY"))
	{
		if (!cJsonGetInt(value, "sts"))
			recvStarted(0);
	}
	else if (!strcmp(key, "v_c"))
	{
		videoStream v;
		v.id = cJsonGetInt(value, "id");
		v.description = cJsonGetStr(value, "e");
		v.language_code = cJsonGetStr(value, "n");
		v.width = cJsonGetInt(value, "w");
		v.height = cJsonGetInt(value, "h");
		v.framerate = cJsonGetInt(value, "f");
		cJSON *progressive = value ? cJSON_GetObjectItem(value, "p") : NULL;
		if (progressive != NULL)
		{
			v.progressive = progressive->valueint;
		}
		/* exteplayer3: 0 SDR, 1 HDR10, 2 HLG, 3 other HDR */
		int hdr = cJsonGetInt(value, "hdr", -1);
		if (hdr >= 0 && hdr <= 3)
		{
			static const int gamma[] = {0, 2, 3, 1};
			v.gamma = gamma[hdr];
		}
		/* exteplayer3 187: profile@level, bit depth, bit rate, Dolby Vision profile */
		v.profile = cJsonGetStr(value, "pr");
		v.bit_depth = cJsonGetInt(value, "bd");
		v.bitrate = cJsonGetInt(value, "br");
		v.dolby_vision = cJsonGetInt(value, "dv");
		v.sar_num = cJsonGetInt(value, "an");
		v.sar_den = cJsonGetInt(value, "ad");
		if (v.id >= 0 && (v.width <= 0 || v.height <= 0))
		{
			/* live TS, HLS: FFmpeg has not seen the picture, "w" and "h"
			 * are 0 and "f" is a guess. Take what the decoder reported,
			 * else keep the size already known for this track. */
			if (mDecoderVideo.width > 0 && mDecoderVideo.height > 0)
			{
				v.width = mDecoderVideo.width;
				v.height = mDecoderVideo.height;
			}
			else if (v.id == mVideo.id && mVideo.width > 0 && mVideo.height > 0)
			{
				v.width = mVideo.width;
				v.height = mVideo.height;
			}
			if (mDecoderVideo.framerate > 0)
				v.framerate = mDecoderVideo.framerate;
		}
		mVideo = v;
		recvVideoTrackCurrent(0, v);
	}
	else if (!strcmp(key, "v_e"))
	{
		/* events of the video decoder (size, frame rate, progressive):
		 * {"v_e":{"w":1280,"h":720,"a":1,"f":25000,"p":1}}, sent once the
		 * driver has reported all of them */
		int width = cJsonGetInt(value, "w", -1);
		int height = cJsonGetInt(value, "h", -1);
		int framerate = decoderFramerate(cJsonGetInt(value, "f", -1));
		int progressive = cJsonGetInt(value, "p", -1);
		if (width > 0 && height > 0)
		{
			mDecoderVideo.width = width;
			mDecoderVideo.height = height;
		}
		if (framerate > 0)
			mDecoderVideo.framerate = framerate;
		if (progressive >= 0)
			mDecoderVideo.progressive = progressive;

		videoStream v = mVideo;
		if (mDecoderVideo.width > 0 && mDecoderVideo.height > 0)
		{
			v.width = mDecoderVideo.width;
			v.height = mDecoderVideo.height;
		}
		if (mDecoderVideo.framerate > 0)
			v.framerate = mDecoderVideo.framerate;
		if (mDecoderVideo.progressive >= 0)
			v.progressive = mDecoderVideo.progressive;
		mVideo = v;
		recvVideoTrackCurrent(0, v);
	}
	else if (!strcmp(key, "a_s"))
	{
		if (!cJsonGetInt(value, "sts"))
		{
			int s = cJsonGetInt(value, "id");
			recvAudioTrackSelected(0, s);
			return;
		}
		recvAudioTrackSelected(1, 0);
	}
	else if (!strcmp(key, "a_c"))
	{
		audioStream a;
		a.id = cJsonGetInt(value, "id");
		a.description = cJsonGetStr(value, "e");
		a.language_code = cJsonGetStr(value, "n");
		readAudioExtras(value, a);
		recvAudioTrackCurrent(0, a);
	}
	else if (!strcmp(key, "a_l"))
	{
		std::vector<audioStream> streams;
		for (int i=0; i<cJSON_GetArraySize(value); i++)
		{
			cJSON *subitem=cJSON_GetArrayItem(value,i);
			audioStream a;
			a.id = cJsonGetInt(subitem, "id");
			a.description = cJsonGetStr(subitem, "e");
			a.language_code = cJsonGetStr(subitem, "n");
			readAudioExtras(subitem, a);
			streams.push_back(a);
		}
		recvAudioTracksList(0, streams);
	}
	else if (!strcmp(key, "s_s"))
	{
		if (!cJsonGetInt(value, "sts"))
		{
			int s = cJsonGetInt(value, "id");
			recvSubtitleTrackSelected(0, s);
			return;
		}
		recvSubtitleTrackSelected(1, -1);
	}
	else if (!strcmp(key, "s_c"))
	{
		subtitleStream s;
		s.id = cJsonGetInt(value, "id");
		s.description = cJsonGetStr(value, "e");
		s.language_code = cJsonGetStr(value, "n");
		readSubtitleExtras(value, s);
		recvSubtitleTrackCurrent(0, s);
	}
	else if (!strcmp(key, "s_l"))
	{
		std::vector<subtitleStream> streams;
		for (int i=0; i<cJSON_GetArraySize(value); i++)
		{
			cJSON *subitem=cJSON_GetArrayItem(value,i);
			subtitleStream s;
			s.id = cJsonGetInt(subitem, "id");
			s.description = cJsonGetStr(subitem, "e");
			s.language_code = cJsonGetStr(subitem, "n");
			readSubtitleExtras(subitem, s);
			streams.push_back(s);
		}
		recvSubtitleTracksList(0, streams);
	}
	else if (!strcmp(key, "s_a"))
	{
		subtitleMessage s;
		s.start_ms = cJsonGetInt(value, "s");
		s.end_ms = cJsonGetInt(value, "e");
		s.text = cJsonGetStr(value, "t");
		cJSON *rects = value ? cJSON_GetObjectItem(value, "r") : NULL;
		if (rects && rects->type == cJSON_Array)
		{
			/* graphic subtitle (-G): {"s_a":{"id":3,"s":1000,"e":null,"r":[{"x":..,"y":..,
			 * "w":..,"h":..,"f":"3_1000_0.png"}]}}, the files are in the -G directory.
			 * "e" null: the end is not known, the next page ends it (eServiceApp cuts
			 * it there), 30 s at most as in the DreamOS port. No rectangles: a clear. */
			s.graphic = true;
			cJSON *end = cJSON_GetObjectItem(value, "e");
			if (!end || end->type != cJSON_Number)
				s.end_ms = s.start_ms + 30000;
			for (int i = 0; i < cJSON_GetArraySize(rects); i++)
			{
				cJSON *r = cJSON_GetArrayItem(rects, i);
				std::string f = cJsonGetStr(r, "f");
				/* a file name in the directory, nothing else */
				if (f.empty() || f.find('/') != std::string::npos || f[0] == '.' || mGraphicSubDir.empty())
					continue;
				subtitleImage img;
				img.x = cJsonGetInt(r, "x");
				img.y = cJsonGetInt(r, "y");
				img.w = cJsonGetInt(r, "w");
				img.h = cJsonGetInt(r, "h");
				img.file = mGraphicSubDir + "/" + f;
				s.images.push_back(img);
			}
			if (s.end_ms < s.start_ms)
				s.end_ms = s.start_ms;
		}
		s.duration_ms = s.end_ms - s.start_ms;
		recvSubtitleMessage(s);
	}
	else if (!strcmp(key, "PLAYBACK_LENGTH"))
	{
		if (!cJsonGetInt(value, "sts"))
		{
			recvLength(0, lengthToMs(cJsonGetDouble(value, "length")));
		}
	}
	else if (!strcmp(key, "J"))
	{
		int positionInMs = cJsonGetInt(value, "ms");
		recvPosition(0, positionInMs);
	}
	else if (!strcmp(key, "PLAYBACK_STOP"))
	{
		if (!cJsonGetInt(value, "sts"))
		{
			//recvStopped(0);
			return;
		}
		//recvStopped(1);
	}
	else if (!strcmp(key, "PLAYBACK_CONTINUE"))
	{
		if (!cJsonGetInt(value, "sts"))
		{
			recvResumed(0);
			return;
		}
		recvResumed(1);
	}
	else if (!strcmp(key, "PLAYBACK_PAUSE"))
	{
		if (!cJsonGetInt(value, "sts"))
		{
			recvPaused(0);
			return;
		}
		recvPaused(1);
	}
	else if (!strcmp(key, "PLAYBACK_FASTFORWARD"))
	{
		if (cJsonGetInt(value, "sts"))
		{
		}
	}
	else if (!strcmp(key, "PLAYBACK_SEEK_ABS"))
	{
		if (!cJsonGetInt(value, "sts"))
		{
			recvSeekTo(0, 0);
			return;
		}
		recvSeekTo(1, 0);
	}
	else if (!strcmp(key, "PLAYBACK_SEEK"))
	{
		if (!cJsonGetInt(value, "sts"))
		{
			recvSeekRelative(0, 0);
			return;
		}
		recvSeekRelative(1, 0);
	}
	else if (!strcmp(key, "PLAYBACK_TRICK_END"))
	{
		recvWindEnd(cJsonGetInt(value, "dir"));
	}
	else if (!strcmp(key, "EPLAYER3_EXTENDED"))
	{
		mCanWind = cJsonGetInt(value, "trick") != 0;
		mCanSelectChannel = cJsonGetInt(value, "achannel") != 0;
		mSeekMs = cJsonGetInt(value, "seekms") != 0;
		mCanForcedOnly = cJsonGetInt(value, "forcedonly") != 0;
		if (cJsonGetInt(value, "br") != 0)
			mCanBitrate = true;
	}
	else if (!strcmp(key, "BR"))
	{
		/* byte counters: {"BR":{"v":<bytes>,"a":<bytes>}}, -1 without the track */
		mCanBitrate = true;
		recvBitrate((int64_t)cJsonGetDouble(value, "v", -1), (int64_t)cJsonGetDouble(value, "a", -1));
	}
	else if (!strcmp(key, "c_l"))
	{
		/* chapters: {"c_l":[{"id":0,"s":0,"e":4321,"t":"Intro"}, ...]} (ms) */
		std::vector<chapterEntry> chapters;
		for (int i = 0; value && i < cJSON_GetArraySize(value); i++)
		{
			cJSON *item = cJSON_GetArrayItem(value, i);
			chapterEntry c;
			c.start_ms = cJsonGetInt(item, "s");
			c.end_ms = cJsonGetInt(item, "e");
			c.title = cJsonGetStr(item, "t");
			chapters.push_back(c);
		}
		recvChapters(chapters);
	}
	else if (!strcmp(key, "cover"))
	{
		/* --cover: {"cover":{"file":"/tmp/.id3coverart","type":"jpeg",...}}, {} without one */
		recvCover(cJsonGetStr(value, "file"));
	}
	else if (!strcmp(key, "s_f"))
	{
		/* the player reset its subtitle output (seek, track switch) */
		recvSubtitleFlush();
	}
	else if (!strcmp(key, "m_t"))
	{
		/* ICY / radio text of audio-only playback:
		 * {"m_t":{"rt":"Artist - Title","t":"Title","a":"Artist"}} */
		recvMetadata(cJsonGetStr(value, "t"), cJsonGetStr(value, "a"), cJsonGetStr(value, "rt"));
	}
	else if (!strcmp(key, "a_m"))
	{
		/* dual mono: {"a_m":{"m":2,"sts":0}} */
		recvAudioChannel(cJsonGetInt(value, "sts"), cJsonGetInt(value, "m", 1));
	}
	else if (!strcmp(key, "FF_ERROR"))
	{
		/* open or read error: {"FF_ERROR":{"msg":"Connection timed out","code":-110}};
		 * sent before the stop, so a lost connection differs from the end */
		errorMessage e;
		e.code = cJsonGetInt(value, "code", -1);
		e.message = cJsonGetStr(value, "msg");
		eWarning("ExtEPlayer3::handleJsonOutput - player error %d: %s", e.code, e.message.c_str());
		mErrorReported = true;
		recvErrorMessage(e);
	}
	else
	{
		eDebug("ExtEPlayer3::handleJsonOutput - unhandled key \"%s\"", key);
	}
}
