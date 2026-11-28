#ifndef __hlsexplorer_h
#define __hlsexplorer_h

#include <lib/base/ebase.h>
#include <lib/base/message.h>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common.h"
#include "m3u8.h"

/* Reads the variants of an HLS master playlist (M3U8VariantsExplorer) in a
 * thread of its own and hands them to the main thread. The fetch blocks for
 * up to M3U8_FETCH_TIMEOUT_MS plus the DNS lookup, and getaddrinfo() has no
 * timeout at all: on the enigma2 main thread that froze the GUI at every
 * start of an HLS stream with a slow or dead server.
 *
 * The thread is detached and never joined. Everything it touches is in the
 * job, which it shares (shared_ptr) with this object; cancel() and the
 * destructor only disconnect the job from the message pump, under the job's
 * mutex, and return at once. A fetch that is still running (or hanging in
 * DNS) ends in the background, its result is dropped and the job is freed by
 * whichever side lets go of it last. Joining it instead could block the GUI
 * for as long as the lookup takes. */
class HlsExplorer : public sigc::trackable {
	struct Job {
		std::string url;
		HeaderMap headers;
		std::vector<M3U8StreamInfo> streams;
		/* guards pump: the worker sends under it, cancel() clears it under it */
		std::mutex lock;
		eFixedMessagePump<std::shared_ptr<Job>>* pump;
		Job(const std::string& url, const HeaderMap& headers) : url(url), headers(headers), pump(NULL) {}
	};
	eFixedMessagePump<std::shared_ptr<Job>> m_pump;
	/* the running job; a result of any other job is a stale one */
	std::shared_ptr<Job> m_job;

	static void run(std::shared_ptr<Job> job);
	void gotResult(const std::shared_ptr<Job>& job);

public:
	HlsExplorer();
	~HlsExplorer();
	/* starts reading url, unless that url is being read already; false if
	 * the thread could not be started (nothing will be reported then) */
	bool start(const std::string& url, const HeaderMap& headers);
	/* drops the running job: its result is never reported */
	void cancel();
	bool running() const { return m_job != nullptr; }

	/* on the main thread: the variants found, empty if the fetch failed or
	 * the playlist has none */
	sigc::signal<void(const std::vector<M3U8StreamInfo>&)> finished;
};

#endif
