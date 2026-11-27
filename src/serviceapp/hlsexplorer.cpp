#include "hlsexplorer.h"

#include <system_error>
#include <thread>

HlsExplorer::HlsExplorer() : m_pump(eApp, 1, "hlsexplorer") {
	CONNECT(m_pump.recv_msg, HlsExplorer::gotResult);
}

HlsExplorer::~HlsExplorer() {
	/* before m_pump goes: a worker still running must not send to it */
	cancel();
}

void HlsExplorer::run(std::shared_ptr<Job> job) {
	/* only the job's own copies of url and headers: the service may be gone */
	M3U8VariantsExplorer ve(job->url, job->headers);
	std::vector<M3U8StreamInfo> streams = ve.getStreams();
	std::lock_guard<std::mutex> guard(job->lock);
	if (!job->pump)
		return; // cancelled while fetching
	job->streams.swap(streams);
	job->pump->send(job);
}

bool HlsExplorer::start(const std::string& url, const HeaderMap& headers) {
	if (m_job && m_job->url == url)
		return true; // the same playlist is read already, its result comes
	cancel();
	std::shared_ptr<Job> job = std::make_shared<Job>(url, headers);
	job->pump = &m_pump;
	try {
		std::thread(HlsExplorer::run, job).detach();
	} catch (const std::system_error& e) {
		eWarning("HlsExplorer::start - cannot start the thread: %s", e.what());
		return false;
	}
	m_job = job;
	return true;
}

void HlsExplorer::cancel() {
	if (!m_job)
		return;
	{
		std::lock_guard<std::mutex> guard(m_job->lock);
		m_job->pump = NULL;
	}
	m_job.reset();
}

void HlsExplorer::gotResult(const std::shared_ptr<Job>& job) {
	/* a result sent just before a cancel() (or of a job replaced by a new
	 * start()) may still be in the pump's queue */
	if (job != m_job)
		return;
	m_job.reset();
	finished(job->streams);
}
