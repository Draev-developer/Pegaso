// Pegaso: BitTorrent engine. See TorrentEngine.h.
// Compiled without the precompiled header and without MFC.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <set>
#include <thread>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/write_resume_data.hpp>

#include "TorrentEngine.h"

namespace
{
	std::string ToUtf8(const std::wstring &w)
	{
		if (w.empty())
			return std::string();
		int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
		std::string s((size_t)n, '\0');
		::WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, NULL, NULL);
		return s;
	}

	std::wstring FromUtf8(const std::string &s)
	{
		if (s.empty())
			return std::wstring();
		int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
		std::wstring w((size_t)n, L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
		return w;
	}

	std::string Hex(const lt::sha1_hash &h)
	{
		static const char digits[] = "0123456789abcdef";
		std::string s;
		for (unsigned char c : h) {
			s += digits[c >> 4];
			s += digits[c & 15];
		}
		return s;
	}

	std::string IdOf(const lt::torrent_handle &h)
	{
		return h.is_valid() ? Hex(h.info_hashes().get_best()) : std::string();
	}

	bool ReadFile(const std::wstring &path, std::vector<char> &data, size_t maxSize)
	{
		FILE *f = _wfopen(path.c_str(), L"rb");
		if (f == NULL)
			return false;
		bool ok = false;
		if (_fseeki64(f, 0, SEEK_END) == 0) {
			const long long size = _ftelli64(f);
			if (size >= 0 && (unsigned long long)size <= maxSize && _fseeki64(f, 0, SEEK_SET) == 0) {
				data.resize((size_t)size);
				ok = size == 0 || fread(data.data(), 1, (size_t)size, f) == (size_t)size;
			}
		}
		fclose(f);
		return ok;
	}

	// Writes through a temporary file, so a crash never leaves a truncated state file behind.
	bool WriteFile(const std::wstring &path, const std::vector<char> &data)
	{
		const std::wstring tmp(path + L".tmp");
		FILE *f = _wfopen(tmp.c_str(), L"wb");
		if (f == NULL)
			return false;
		const bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
		fclose(f);
		if (!ok || !::MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
			::DeleteFileW(tmp.c_str());
			return false;
		}
		return true;
	}

	const size_t MAX_TORRENT_FILE = 64 * 1024 * 1024;

	bool HasExtension(const std::string &path, const char *ext)
	{
		const size_t n = strlen(ext);
		if (path.size() < n)
			return false;
		for (size_t i = 0; i < n; ++i)
			if (tolower((unsigned char)path[path.size() - n + i]) != ext[i])
				return false;
		return true;
	}

	// Internet shortcuts packed into torrents are advertising more often than not:
	// they are not downloaded unless the user asks for them.
	void SkipLinkFiles(const lt::torrent_handle &h)
	{
		const std::shared_ptr<const lt::torrent_info> ti = h.torrent_file();
		if (!ti)
			return;
		const lt::file_storage &fs = ti->layout();
		std::vector<lt::download_priority_t> prio = h.get_file_priorities();
		prio.resize((size_t)fs.num_files(), lt::default_priority);
		bool changed = false;
		for (const lt::file_index_t i : fs.file_range())
			if (!fs.pad_file_at(i) && HasExtension(fs.file_path(i), ".url")) {
				prio[(size_t)static_cast<int>(i)] = lt::dont_download;
				changed = true;
			}
		if (changed)
			h.prioritize_files(prio);
	}
	const lt::resume_data_flags_t RESUME_FLAGS = lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified;
}

struct CTorrentEngine::Impl
{
	std::unique_ptr<lt::session> ses;
	STorrentSettings settings;
	std::chrono::steady_clock::time_point lastSave;
	int pendingSaves = 0;	// save_resume_data requests without an answer yet
	std::map<std::string, std::string> names;	// id -> name, also for torrents which are gone already
	std::set<std::string> fresh;	// torrents added by the user in this session, as opposed to restored ones

	// Name of a torrent; libtorrent 2.1 alerts do not carry it any more.
	std::string NameOf(const lt::torrent_handle &h)
	{
		const std::string id(IdOf(h));
		if (h.is_valid()) {
			try {
				const std::string name(h.status(lt::torrent_handle::query_name).name);
				if (!name.empty())
					names[id] = name;
			} catch (const std::exception&) {
			}
		}
		const auto it = names.find(id);
		return it != names.end() ? it->second : id;
	}

	std::wstring ResumePath(const std::string &id) const
	{
		return settings.stateDir + FromUtf8(id) + L".fastresume";
	}

	lt::torrent_handle Find(const std::string &id) const
	{
		if (ses)
			for (const lt::torrent_handle &h : ses->get_torrents())
				if (IdOf(h) == id)
					return h;
		return lt::torrent_handle();
	}

	void ApplyLimits(lt::settings_pack &pack) const
	{
		pack.set_int(lt::settings_pack::download_rate_limit, settings.downloadLimit);
		pack.set_int(lt::settings_pack::upload_rate_limit, settings.uploadLimit);
	}

	bool Add(lt::add_torrent_params atp, std::wstring &error)
	{
		const lt::sha1_hash best = atp.info_hashes.get_best();
		for (const lt::torrent_handle &h : ses->get_torrents())
			if (h.info_hashes().get_best() == best) {
				error = L"this torrent is already in the list";
				return false;
			}
		fresh.insert(Hex(best));
		atp.save_path = ToUtf8(settings.savePath);
		atp.flags |= lt::torrent_flags::auto_managed;
		atp.flags &= ~lt::torrent_flags::paused;
		ses->async_add_torrent(std::move(atp));
		return true;
	}

	void RequestSave(bool onlyModified)
	{
		for (const lt::torrent_handle &h : ses->get_torrents()) {
			if (!h.is_valid())
				continue;
			h.save_resume_data(onlyModified ? RESUME_FLAGS : lt::torrent_handle::save_info_dict);
			++pendingSaves;
		}
		lastSave = std::chrono::steady_clock::now();
	}

	void SaveSessionState() const
	{
		WriteFile(settings.stateDir + L"session.dat", lt::write_session_params_buf(ses->session_state(lt::session::save_dht_state)));
	}

	void Handle(lt::alert *a, std::vector<STorrentEvent> *events)
	{
		auto push = [events](ETorrentEvent kind, const std::string &id, const std::string &name, const std::string &text) {
			if (events)
				events->push_back(STorrentEvent{kind, id, FromUtf8(name), FromUtf8(text)});
		};

		if (auto *p = lt::alert_cast<lt::save_resume_data_alert>(a)) {
			--pendingSaves;
			const std::string id(Hex(p->params.info_hashes.get_best()));
			WriteFile(ResumePath(id), lt::write_resume_data_buf(p->params));
		} else if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
			--pendingSaves;		// includes "nothing has changed"
		} else if (auto *p = lt::alert_cast<lt::add_torrent_alert>(a)) {
			if (p->error)
				push(TE_ERROR, std::string(), p->params.name, p->error.message());
			else {
				push(TE_ADDED, IdOf(p->handle), NameOf(p->handle), std::string());
				if (fresh.count(IdOf(p->handle)) != 0)
					SkipLinkFiles(p->handle);	// a .torrent file brings its file list along
				p->handle.save_resume_data(lt::torrent_handle::save_info_dict);	// keep it across a restart right away
				++pendingSaves;
			}
		} else if (auto *p = lt::alert_cast<lt::metadata_received_alert>(a)) {
			push(TE_METADATA, IdOf(p->handle), NameOf(p->handle), std::string());
			SkipLinkFiles(p->handle);
			p->handle.save_resume_data(lt::torrent_handle::save_info_dict);
			++pendingSaves;
		} else if (auto *p = lt::alert_cast<lt::torrent_finished_alert>(a)) {
			push(TE_FINISHED, IdOf(p->handle), NameOf(p->handle), std::string());
			p->handle.save_resume_data(lt::torrent_handle::save_info_dict);
			++pendingSaves;
		} else if (auto *p = lt::alert_cast<lt::torrent_error_alert>(a)) {
			push(TE_ERROR, IdOf(p->handle), NameOf(p->handle), p->error.message());
		} else if (auto *p = lt::alert_cast<lt::torrent_removed_alert>(a)) {
			const std::string id(Hex(p->info_hashes.get_best()));
			::DeleteFileW(ResumePath(id).c_str());
			const auto it = names.find(id);
			push(TE_REMOVED, id, it != names.end() ? it->second : id, std::string());
			if (it != names.end())
				names.erase(it);
		} else if (auto *p = lt::alert_cast<lt::listen_succeeded_alert>(a)) {
			if (p->socket_type == lt::socket_type_t::tcp)
				push(TE_INFO, std::string(), std::string(), "listening on " + p->address.to_string() + ":" + std::to_string(p->port));
		} else if (auto *p = lt::alert_cast<lt::listen_failed_alert>(a)) {
			push(TE_ERROR, std::string(), std::string(), "cannot listen on " + p->address.to_string() + ":" + std::to_string(p->port) + " - " + p->error.message());
		}
	}

	void Drain(std::vector<STorrentEvent> *events)
	{
		std::vector<lt::alert*> alerts;
		ses->pop_alerts(&alerts);
		for (lt::alert *a : alerts)
			Handle(a, events);
	}
};

CTorrentEngine::CTorrentEngine()
	: m(new Impl)
{
}

CTorrentEngine::~CTorrentEngine()
{
	Stop();
}

bool CTorrentEngine::IsRunning() const
{
	return static_cast<bool>(m->ses);
}

bool CTorrentEngine::Start(const STorrentSettings &settings, std::wstring &error)
{
	if (m->ses)
		return true;
	m->settings = settings;
	for (std::wstring *dir : {&m->settings.savePath, &m->settings.stateDir}) {
		if (!dir->empty() && dir->back() != L'\\' && dir->back() != L'/')
			*dir += L'\\';
		::CreateDirectoryW(dir->c_str(), NULL);
	}

	try {
		lt::session_params params;
		std::vector<char> state;
		if (ReadFile(m->settings.stateDir + L"session.dat", state, MAX_TORRENT_FILE) && !state.empty()) {
			lt::error_code ec;
			lt::session_params loaded = lt::read_session_params(state, lt::session::save_dht_state);
			params.dht_state = std::move(loaded.dht_state);
		}

		lt::settings_pack &pack = params.settings;
		pack.set_str(lt::settings_pack::user_agent, m->settings.userAgent);
		pack.set_str(lt::settings_pack::peer_fingerprint, "-PG0100-");
		const std::string port(std::to_string(m->settings.listenPort));
		if (m->settings.bindAddress.empty())
			pack.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:" + port + ",[::]:" + port);
		else {
			// tied to one adapter: listen there and send from there, nowhere else
			pack.set_str(lt::settings_pack::listen_interfaces, m->settings.bindAddress + ":" + port);
			pack.set_str(lt::settings_pack::outgoing_interfaces, m->settings.bindAddress);
		}
		pack.set_int(lt::settings_pack::alert_mask, lt::alert_category::status | lt::alert_category::error | lt::alert_category::storage);
		pack.set_bool(lt::settings_pack::enable_dht, true);
		pack.set_bool(lt::settings_pack::enable_lsd, true);
		pack.set_bool(lt::settings_pack::enable_upnp, true);
		pack.set_bool(lt::settings_pack::enable_natpmp, true);
		const int policy = m->settings.requireEncryption ? lt::settings_pack::pe_forced : lt::settings_pack::pe_enabled;
		pack.set_int(lt::settings_pack::out_enc_policy, policy);
		pack.set_int(lt::settings_pack::in_enc_policy, policy);
		m->ApplyLimits(pack);

		m->ses.reset(new lt::session(std::move(params)));
	} catch (const std::exception &e) {
		error = FromUtf8(e.what());
		m->ses.reset();
		return false;
	}
	m->lastSave = std::chrono::steady_clock::now();
	m->pendingSaves = 0;

	// bring back the torrents of the last session
	WIN32_FIND_DATAW fd;
	HANDLE hFind = ::FindFirstFileW((m->settings.stateDir + L"*.fastresume").c_str(), &fd);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			std::vector<char> data;
			if (!ReadFile(m->settings.stateDir + fd.cFileName, data, MAX_TORRENT_FILE))
				continue;
			lt::error_code ec;
			lt::add_torrent_params atp = lt::read_resume_data(data, ec);
			if (ec)
				continue;
			if (atp.save_path.empty())
				atp.save_path = ToUtf8(m->settings.savePath);
			m->ses->async_add_torrent(std::move(atp));
		} while (::FindNextFileW(hFind, &fd));
		::FindClose(hFind);
	}
	return true;
}

void CTorrentEngine::Stop()
{
	if (!m->ses)
		return;
	try {
		m->ses->pause();
		m->Drain(NULL);
		m->pendingSaves = 0;
		m->RequestSave(false);
		// wait for the answers, but never hang the shutdown
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
		while (m->pendingSaves > 0 && std::chrono::steady_clock::now() < deadline) {
			m->ses->wait_for_alert(std::chrono::milliseconds(200));
			m->Drain(NULL);
		}
		m->SaveSessionState();
	} catch (const std::exception&) {
	}
	m->ses.reset();
}

bool CTorrentEngine::IsTorrentMagnet(const std::string &uri)
{
	std::string s(uri);
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
	return s.compare(0, 8, "magnet:?") == 0 && (s.find("urn:btih:") != std::string::npos || s.find("urn:btmh:") != std::string::npos);
}

bool CTorrentEngine::AddMagnet(const std::string &uri, std::wstring &error)
{
	if (!m->ses) {
		error = L"the BitTorrent engine is not running";
		return false;
	}
	lt::error_code ec;
	lt::add_torrent_params atp = lt::parse_magnet_uri(uri, ec);
	if (ec) {
		error = FromUtf8(ec.message());
		return false;
	}
	return m->Add(std::move(atp), error);
}

bool CTorrentEngine::AddTorrentFile(const std::wstring &path, std::wstring &error)
{
	if (!m->ses) {
		error = L"the BitTorrent engine is not running";
		return false;
	}
	std::vector<char> data;
	if (!ReadFile(path, data, MAX_TORRENT_FILE) || data.empty()) {
		error = L"cannot read the torrent file";
		return false;
	}
	try {
		return m->Add(lt::load_torrent_buffer(data), error);
	} catch (const std::exception &e) {
		error = FromUtf8(e.what());
		return false;
	}
}

void CTorrentEngine::Pause(const std::string &id)
{
	lt::torrent_handle h = m->Find(id);
	if (h.is_valid()) {
		h.unset_flags(lt::torrent_flags::auto_managed);
		h.pause();
		h.save_resume_data(lt::torrent_handle::save_info_dict);
		++m->pendingSaves;
	}
}

void CTorrentEngine::Resume(const std::string &id)
{
	lt::torrent_handle h = m->Find(id);
	if (h.is_valid()) {
		h.clear_error();
		h.set_flags(lt::torrent_flags::auto_managed);
		h.resume();
	}
}

void CTorrentEngine::Remove(const std::string &id, bool deleteFiles)
{
	lt::torrent_handle h = m->Find(id);
	if (h.is_valid())
		m->ses->remove_torrent(h, deleteFiles ? lt::session::delete_files : lt::remove_flags_t{});
}

void CTorrentEngine::PauseSession(bool pause)
{
	if (!m->ses)
		return;
	if (pause)
		m->ses->pause();
	else
		m->ses->resume();
}

void CTorrentEngine::SetLimits(int downloadLimit, int uploadLimit)
{
	if (m->settings.downloadLimit == downloadLimit && m->settings.uploadLimit == uploadLimit)
		return;
	m->settings.downloadLimit = downloadLimit;
	m->settings.uploadLimit = uploadLimit;
	if (m->ses) {
		lt::settings_pack pack;
		m->ApplyLimits(pack);
		m->ses->apply_settings(pack);
	}
}

std::vector<STorrentFile> CTorrentEngine::GetFiles(const std::string &id)
{
	std::vector<STorrentFile> out;
	const lt::torrent_handle h = m->Find(id);
	if (!h.is_valid())
		return out;
	const std::shared_ptr<const lt::torrent_info> ti = h.torrent_file();
	if (!ti)
		return out;
	const lt::file_storage &fs = ti->layout();
	const std::vector<lt::download_priority_t> prio = h.get_file_priorities();
	for (const lt::file_index_t i : fs.file_range()) {
		if (fs.pad_file_at(i))
			continue;	// filler between files, of no interest to the user
		STorrentFile f;
		f.index = static_cast<int>(i);
		f.path = FromUtf8(fs.file_path(i));
		f.size = fs.file_size(i);
		f.wanted = (size_t)f.index >= prio.size() || prio[(size_t)f.index] != lt::dont_download;
		out.push_back(std::move(f));
	}
	return out;
}

void CTorrentEngine::SetFilesWanted(const std::string &id, const std::vector<std::pair<int, bool> > &wanted)
{
	const lt::torrent_handle h = m->Find(id);
	if (!h.is_valid())
		return;
	const std::shared_ptr<const lt::torrent_info> ti = h.torrent_file();
	if (!ti)
		return;
	std::vector<lt::download_priority_t> prio = h.get_file_priorities();
	prio.resize((size_t)ti->layout().num_files(), lt::default_priority);
	for (const std::pair<int, bool> &w : wanted)
		if (w.first >= 0 && (size_t)w.first < prio.size()) {
			if (!w.second)
				prio[(size_t)w.first] = lt::dont_download;
			else if (prio[(size_t)w.first] == lt::dont_download)
				prio[(size_t)w.first] = lt::default_priority;
		}
	h.prioritize_files(prio);
	h.save_resume_data(lt::torrent_handle::save_info_dict);
	++m->pendingSaves;
}

void CTorrentEngine::Poll(std::vector<STorrentEvent> &events)
{
	if (!m->ses)
		return;
	m->Drain(&events);
	// progress is written down regularly, so a crash costs little
	if (std::chrono::steady_clock::now() - m->lastSave > std::chrono::minutes(2)) {
		m->RequestSave(true);
		m->SaveSessionState();
	}
}

std::vector<STorrentStatus> CTorrentEngine::GetStatus()
{
	std::vector<STorrentStatus> out;
	if (!m->ses)
		return out;
	for (const lt::torrent_handle &h : m->ses->get_torrents()) {
		if (!h.is_valid())
			continue;
		const lt::torrent_status st = h.status(lt::torrent_handle::query_name);
		STorrentStatus s;
		s.id = Hex(st.info_hashes.get_best());
		s.name = FromUtf8(st.name);
		if (!st.name.empty())
			m->names[s.id] = st.name;
		s.size = st.total_wanted;
		s.done = st.total_wanted_done;
		s.downloadRate = st.download_payload_rate;
		s.uploadRate = st.upload_payload_rate;
		s.peers = st.num_peers;
		s.seeds = st.num_seeds;
		if (st.errc) {
			s.state = TS_ERROR;
			s.error = FromUtf8(st.errc.message());
		} else if ((st.flags & lt::torrent_flags::paused) && !(st.flags & lt::torrent_flags::auto_managed))
			s.state = TS_PAUSED;
		else
			switch (st.state) {
			case lt::torrent_status::downloading_metadata:
				s.state = TS_METADATA;
				break;
			case lt::torrent_status::checking_files:
			case lt::torrent_status::checking_resume_data:
				s.state = TS_CHECKING;
				break;
			case lt::torrent_status::finished:
			case lt::torrent_status::seeding:
				s.state = TS_SEEDING;
				break;
			default:
				s.state = TS_DOWNLOADING;
			}
		out.push_back(std::move(s));
	}
	return out;
}
