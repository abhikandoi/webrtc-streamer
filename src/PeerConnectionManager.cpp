/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** PeerConnectionManager.cpp
**
** -------------------------------------------------------------------------*/

#include <iostream>
#include <fstream>
#include <utility>
#include <vector>

#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "modules/video_capture/video_capture_factory.h"
#include "media/engine/webrtcvideocapturerfactory.h"

#include "modules/audio_device/include/fake_audio_device.h"

#include "test/fake_audio_device.h"

#include "PeerConnectionManager.h"
#include "CivetServer.h"

#ifdef HAVE_LIVE555
#include "rtspvideocapturer.h"
#endif

#include "zmqframereader.h"

const char kVideoLabel[] = "video_label";

// Names used for a IceCandidate JSON object.
const char kCandidateSdpMidName[] = "sdpMid";
const char kCandidateSdpMlineIndexName[] = "sdpMLineIndex";
const char kCandidateSdpName[] = "candidate";

// Names used for a SessionDescription JSON object.
const char kSessionDescriptionTypeName[] = "type";
const char kSessionDescriptionSdpName[] = "sdp";

// Names used for stun and turn urls
const char kStunURLTypeName[] = "stunurl";
const char kTurnURLTypeName[] = "turnurl";

/* ---------------------------------------------------------------------------
**  Constructor
** -------------------------------------------------------------------------*/
PeerConnectionManager::PeerConnectionManager(
	const std::string & stunurl,
	const std::string & turnurl,
	const webrtc::AudioDeviceModule::AudioLayer audioLayer
	): audioDeviceModule_(webrtc::FakeAudioDeviceModule::Create(0, audioLayer)),
	audioDecoderfactory_(webrtc::CreateBuiltinAudioDecoderFactory()),
	peer_connection_factory_(
		webrtc::CreatePeerConnectionFactory(
			NULL,
            rtc::Thread::Current(),
            NULL,
            audioDeviceModule_,
            webrtc::CreateBuiltinAudioEncoderFactory(),
            audioDecoderfactory_,
            NULL,
            NULL
        )
	), stunurl_(stunurl),
	turnurl_(turnurl)
{
	if (turnurl_.length() > 0)
	{
		std::size_t pos = turnurl_.find('@');
		if (pos != std::string::npos)
		{
			std::string credentials = turnurl_.substr(0, pos);
			turnurl_ = turnurl_.substr(pos + 1);
			pos = credentials.find(':');
			if (pos == std::string::npos)
			{
				turnuser_ = credentials;
			}
			else
			{
				turnuser_ = credentials.substr(0, pos);
				turnpass_ = credentials.substr(pos + 1);
			}
		}
	}
}

/* ---------------------------------------------------------------------------
**  Destructor
** -------------------------------------------------------------------------*/
PeerConnectionManager::~PeerConnectionManager()
{
}


/* ---------------------------------------------------------------------------
**  return deviceList as JSON vector
** -------------------------------------------------------------------------*/
const Json::Value PeerConnectionManager::getMediaList()
{
	Json::Value value(Json::arrayValue);

	return value;
}

/* ---------------------------------------------------------------------------
**  return deviceList as JSON vector
** -------------------------------------------------------------------------*/
const Json::Value PeerConnectionManager::getVideoDeviceList()
{
	Json::Value value(Json::arrayValue);

	std::unique_ptr<webrtc::VideoCaptureModule::DeviceInfo> info(webrtc::VideoCaptureFactory::CreateDeviceInfo());
	if (info)
	{
		int num_videoDevices = info->NumberOfDevices();
		RTC_LOG(INFO) << "nb video devices:" << num_videoDevices;
		for (int i = 0; i < num_videoDevices; ++i)
		{
			const uint32_t kSize = 256;
			char name[kSize] = {0};
			char id[kSize] = {0};
			if (info->GetDeviceName(i, name, kSize, id, kSize) != -1)
			{
				RTC_LOG(INFO) << "video device name:" << name << " id:" << id;
				value.append(name);
			}
		}
	}

	return value;
}

/* ---------------------------------------------------------------------------
**  return deviceList as JSON vector
** -------------------------------------------------------------------------*/
const Json::Value PeerConnectionManager::getAudioDeviceList()
{
	Json::Value value(Json::arrayValue);

	int16_t num_audioDevices = audioDeviceModule_->RecordingDevices();
	RTC_LOG(INFO) << "nb audio devices:" << num_audioDevices;

	std::map<std::string,std::string> deviceMap;
	for (int i = 0; i < num_audioDevices; ++i)
	{
		char name[webrtc::kAdmMaxDeviceNameSize] = {0};
		char id[webrtc::kAdmMaxGuidSize] = {0};
		if (audioDeviceModule_->RecordingDeviceName(i, name, id) != -1)
		{
			RTC_LOG(INFO) << "audio device name:" << name << " id:" << id;
			deviceMap[name]=id;
		}
	}
	for (auto& pair : deviceMap) {
		value.append(pair.first);
	}

	return value;
}

#include <net/if.h>
#include <ifaddrs.h>
std::string getServerIpFromClientIp(int clientip)
{
	std::string serverAddress;
	char host[NI_MAXHOST];
	struct ifaddrs *ifaddr = NULL;
	if (getifaddrs(&ifaddr) == 0) 
	{
		for (struct ifaddrs* ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
		{
			if ( (ifa->ifa_netmask != NULL) && (ifa->ifa_netmask->sa_family == AF_INET) && (ifa->ifa_addr != NULL) && (ifa->ifa_addr->sa_family == AF_INET) )  
			{
				struct sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
				struct sockaddr_in* mask = (struct sockaddr_in*)ifa->ifa_netmask;
				if ( (addr->sin_addr.s_addr & mask->sin_addr.s_addr) == (clientip & mask->sin_addr.s_addr) )
				{
					if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, sizeof(host), NULL, 0, NI_NUMERICHOST) == 0)
					{
						serverAddress = host;
						break;
					}
				}
			}
		}
	}
	freeifaddrs(ifaddr);
	return serverAddress;
}

/* ---------------------------------------------------------------------------
**  return iceServers as JSON vector
** -------------------------------------------------------------------------*/
const Json::Value PeerConnectionManager::getIceServers(const std::string& clientIp)
{
	Json::Value url;
	std::string stunurl("stun:");
	if (stunurl_.find("0.0.0.0:") == 0) {
		// answer with ip that is on same network as client
		stunurl += getServerIpFromClientIp(inet_addr(clientIp.c_str()));
		stunurl += stunurl_.substr(stunurl_.find_first_of(':'));
	} else {
		stunurl += stunurl_;
	}
	url["url"] = stunurl;

	Json::Value urls;
	urls.append(url);

	if (turnurl_.length() > 0)
	{
		Json::Value turn;
		turn["url"] = "turn:" + turnurl_;
		if (turnuser_.length() > 0) turn["username"] = turnuser_;
		if (turnpass_.length() > 0) turn["credential"] = turnpass_;
		urls.append(turn);
	}

	Json::Value iceServers;
	iceServers["iceServers"] = urls;

	return iceServers;
}

/* ---------------------------------------------------------------------------
**  add ICE candidate to a PeerConnection
** -------------------------------------------------------------------------*/
const Json::Value PeerConnectionManager::addIceCandidate(const std::string& peerid, const Json::Value& jmessage)
{
	bool result = false;
	std::string sdp_mid;
	int sdp_mlineindex = 0;
	std::string sdp;
	if (  !rtc::GetStringFromJsonObject(jmessage, kCandidateSdpMidName, &sdp_mid)
	   || !rtc::GetIntFromJsonObject(jmessage, kCandidateSdpMlineIndexName, &sdp_mlineindex)
	   || !rtc::GetStringFromJsonObject(jmessage, kCandidateSdpName, &sdp))
	{
		RTC_LOG(WARNING) << "Can't parse received message:" << jmessage;
	}
	else
	{
		std::unique_ptr<webrtc::IceCandidateInterface> candidate(webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, NULL));
		if (!candidate.get())
		{
			RTC_LOG(WARNING) << "Can't parse received candidate message.";
		}
		else
		{
			std::map<std::string, PeerConnectionObserver* >::iterator  it = peer_connectionobs_map_.find(peerid);
			if (it != peer_connectionobs_map_.end())
			{
				rtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection = it->second->getPeerConnection();
				if (!peerConnection->AddIceCandidate(candidate.get()))
				{
					RTC_LOG(WARNING) << "Failed to apply the received candidate";
				}
				else
				{
					result = true;
				}
			}
		}
	}
	Json::Value answer;
	if (result) {
		answer = result;
	}
	return answer;
}

/* ---------------------------------------------------------------------------
** create an offer for a call
** -------------------------------------------------------------------------*/
const Json::Value
PeerConnectionManager::createOffer(
	const std::string &peerid,
	const std::string & videourl,
	const std::string & audiourl,
	const std::string & options)
{
	Json::Value offer;
	RTC_LOG(INFO) << __FUNCTION__;
	webrtc::PeerConnectionInterface::RTCConfiguration config;

	PeerConnectionObserver* peerConnectionObserver = this->CreatePeerConnection(peerid, config);
	if (!peerConnectionObserver)
	{
		RTC_LOG(LERROR) << "Failed to initialize PeerConnection";
	}
	else
	{
		rtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection = peerConnectionObserver->getPeerConnection();
		
		// set bandwidth
		std::string tmp;
		if (CivetServer::getParam(options, "bitrate", tmp)) {
			int bitrate = std::stoi(tmp);
			
			webrtc::PeerConnectionInterface::BitrateParameters bitrateParam;
			bitrateParam.min_bitrate_bps = rtc::Optional<int>(bitrate/2);
			bitrateParam.current_bitrate_bps = rtc::Optional<int>(bitrate);
			bitrateParam.max_bitrate_bps = rtc::Optional<int>(bitrate*2);
			peerConnection->SetBitrate(bitrateParam);			
			
			RTC_LOG(WARNING) << "set bitrate:" << bitrate;
		}			
		
		if (!this->AddStream(peerConnectionObserver->getPeerConnection(), videourl, audiourl, options))
		{
			RTC_LOG(WARNING) << "Can't add stream";
		}

		// register peerid
		peer_connectionobs_map_.insert(std::pair<std::string, PeerConnectionObserver* >(peerid, peerConnectionObserver));

		// ask to create offer
		peerConnectionObserver->getPeerConnection()->CreateOffer(CreateSessionDescriptionObserver::Create(peerConnectionObserver->getPeerConnection()), NULL);

		// waiting for offer
		int count=10;
		while ( (peerConnectionObserver->getPeerConnection()->local_description() == NULL) && (--count > 0) )
		{
			usleep(1000);
		}

		// answer with the created offer
		const webrtc::SessionDescriptionInterface* desc = peerConnectionObserver->getPeerConnection()->local_description();
		if (desc)
		{
			std::string sdp;
			desc->ToString(&sdp);

			offer[kSessionDescriptionTypeName] = desc->type();
			offer[kSessionDescriptionSdpName] = sdp;
		}
		else
		{
			RTC_LOG(LERROR) << "Failed to create offer";
		}
	}
	return offer;
}

/* ---------------------------------------------------------------------------
** set answer to a call initiated by createOffer
** -------------------------------------------------------------------------*/
void PeerConnectionManager::setAnswer(const std::string &peerid, const Json::Value& jmessage)
{
	RTC_LOG(INFO) << jmessage;

	std::string type;
	std::string sdp;
	if (  !rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionTypeName, &type)
	   || !rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionSdpName, &sdp))
	{
		RTC_LOG(WARNING) << "Can't parse received message.";
	}
	else
	{
		webrtc::SessionDescriptionInterface* session_description(webrtc::CreateSessionDescription(type, sdp, NULL));
		if (!session_description)
		{
			RTC_LOG(WARNING) << "Can't parse received session description message.";
		}
		else
		{
			RTC_LOG(LERROR) << "From peerid:" << peerid << " received session description :" << session_description->type();

			std::map<std::string, PeerConnectionObserver* >::iterator  it = peer_connectionobs_map_.find(peerid);
			if (it != peer_connectionobs_map_.end())
			{
				rtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection = it->second->getPeerConnection();
				peerConnection->SetRemoteDescription(SetSessionDescriptionObserver::Create(peerConnection), session_description);
			}
		}
	}
}

void extractturnserver(std::string *turnurl, std::string *turnuser, std::string *turnpass) {
	if ((*turnurl).length() > 0)
	{
		std::size_t pos = (*turnurl).find('@');
		if (pos != std::string::npos)
		{
			std::string credentials = (*turnurl).substr(0, pos);
			*turnurl = (*turnurl).substr(pos + 1);
			pos = credentials.find(':');
			if (pos == std::string::npos)
			{
				*turnuser = credentials;
			}
			else
			{
				*turnuser = credentials.substr(0, pos);
				*turnpass = credentials.substr(pos + 1);
			}
		}
	}
}

/* ---------------------------------------------------------------------------
**  auto-answer to a call
** -------------------------------------------------------------------------*/
const Json::Value
PeerConnectionManager::call(
	const std::string &peerid,
	const std::string &videourl,
	const std::string &audiourl,
	const std::string &options,
	const Json::Value &jmessage)
{
	RTC_LOG(INFO) << __FUNCTION__;
	Json::Value answer;

	std::string type;
	std::string sdp;
	std::string stunurl;
	Json::Value turnurls;
	std::vector<std::string> turn_servers;

	webrtc::PeerConnectionInterface::RTCConfiguration config;

	if (!rtc::GetStringFromJsonObject(jmessage, kStunURLTypeName, &stunurl)) {
		RTC_LOG(WARNING) << "[peerid=" << peerid << "] No stunurl provided.";
	} else {
		RTC_LOG(WARNING) << "[peerid=" << peerid << "] stunurl is:" << stunurl;
		webrtc::PeerConnectionInterface::IceServer server;
		server.uri = "stun:" + stunurl;
		server.username = "";
		server.password = "";
		config.servers.push_back(server);
	}

	if (!rtc::GetValueFromJsonObject(jmessage, kTurnURLTypeName, &turnurls)) {
		RTC_LOG(WARNING) << "[peerid=" << peerid << "] No turnurls provided.";
	}
	else
	{
		if (!rtc::JsonArrayToStringVector(turnurls, &turn_servers))
		{
			RTC_LOG(WARNING) << "[peerid=" << peerid << "] turnurl field should be json array.";
		}
		else
		{
			RTC_LOG(WARNING) << "[peerid=" << peerid << "] turnurl count:" << turn_servers.size();
			for (std::vector<std::string>::iterator i = turn_servers.begin(); i != turn_servers.end(); ++i)
			{
				std::string turnurl = *i;
				std::string turnuser = "";
				std::string turnpass = "";
				extractturnserver(&turnurl, &turnuser, &turnpass);

				webrtc::PeerConnectionInterface::IceServer turnserver;
				turnserver.uri = "turn:" + turnurl;
				turnserver.username = turnuser;
				turnserver.password = turnpass;
				config.servers.push_back(turnserver);
				RTC_LOG(WARNING) << "[peerid=" << peerid << "] added turnurl:" << turnurl << ",username=" << turnuser << ",password=" << turnpass;
			}
		}
	}

	if (  !rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionTypeName, &type)
	   || !rtc::GetStringFromJsonObject(jmessage, kSessionDescriptionSdpName, &sdp))
	{
		RTC_LOG(WARNING) << "Can't parse received message.";
	}
	else
	{
		RTC_LOG(WARNING) << "here1";
		PeerConnectionObserver* peerConnectionObserver = this->CreatePeerConnection(peerid, config);
		RTC_LOG(WARNING) << "here222";
		if (!peerConnectionObserver)
		{
			RTC_LOG(WARNING) << "here111";
			RTC_LOG(LERROR) << "Failed to initialize PeerConnection";
		}
		else
		{
			RTC_LOG(WARNING) << "here2";
			rtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection = peerConnectionObserver->getPeerConnection();
			
			// set bandwidth
			std::string tmp;
			if (CivetServer::getParam(options, "bitrate", tmp)) {
				int bitrate = std::stoi(tmp);
				
				webrtc::PeerConnectionInterface::BitrateParameters bitrateParam;
				bitrateParam.min_bitrate_bps = rtc::Optional<int>(bitrate/2);
				bitrateParam.current_bitrate_bps = rtc::Optional<int>(bitrate);
				bitrateParam.max_bitrate_bps = rtc::Optional<int>(bitrate*2);
				peerConnection->SetBitrate(bitrateParam);			
				
				RTC_LOG(WARNING) << "set bitrate:" << bitrate;
			}			
			
			RTC_LOG(WARNING) << "here3";
			
			RTC_LOG(INFO) << "nbStreams local:" << peerConnection->local_streams()->count() << " remote:" << peerConnection->remote_streams()->count() << " localDescription:" << peerConnection->local_description();

			// register peerid
			peer_connectionobs_map_.insert(std::pair<std::string, PeerConnectionObserver* >(peerid, peerConnectionObserver));

			RTC_LOG(WARNING) << "here4";

			// set remote offer
			webrtc::SessionDescriptionInterface* session_description(webrtc::CreateSessionDescription(type, sdp, NULL));
			if (!session_description)
			{
				RTC_LOG(WARNING) << "Can't parse received session description message.";
			}
			else
			{
				RTC_LOG(WARNING) << "here5";
				peerConnection->SetRemoteDescription(SetSessionDescriptionObserver::Create(peerConnection), session_description);
			}
			
			// waiting for remote description
			int count=10;
			while ( (peerConnection->remote_description() == NULL) && (--count > 0) )
			{
				usleep(1000);
			}

			RTC_LOG(WARNING) << "here6";

			if (peerConnection->remote_description() == NULL) {
				RTC_LOG(WARNING) << "remote_description is NULL";
			}

			RTC_LOG(WARNING) << "here7";

			// add local stream
			if (!this->AddStream(peerConnection, videourl, audiourl, options))
			{
				RTC_LOG(WARNING) << "Can't add stream";
			}

			RTC_LOG(WARNING) << "here8";

			// create answer
			webrtc::FakeConstraints constraints;
			constraints.AddMandatory(webrtc::MediaConstraintsInterface::kOfferToReceiveVideo, "false");
			constraints.AddMandatory(webrtc::MediaConstraintsInterface::kOfferToReceiveAudio, "false");
			peerConnection->CreateAnswer(CreateSessionDescriptionObserver::Create(peerConnection), &constraints);

			RTC_LOG(INFO) << "nbStreams local:" << peerConnection->local_streams()->count() << " remote:" << peerConnection->remote_streams()->count()
					<< " localDescription:" << peerConnection->local_description()
					<< " remoteDescription:" << peerConnection->remote_description();

			// waiting for answer
			count=10;
			while ( (peerConnection->local_description() == NULL) && (--count > 0) )
			{
				usleep(1000);
			}

			RTC_LOG(WARNING) << "here9";

			RTC_LOG(INFO) << "nbStreams local:" << peerConnection->local_streams()->count() << " remote:" << peerConnection->remote_streams()->count()
					<< " localDescription:" << peerConnection->local_description()
					<< " remoteDescription:" << peerConnection->remote_description();

			RTC_LOG(WARNING) << "here10";

			// return the answer
			const webrtc::SessionDescriptionInterface* desc = peerConnection->local_description();
			if (desc)
			{
				RTC_LOG(WARNING) << "here11";
				std::string sdp;
				desc->ToString(&sdp);

				answer[kSessionDescriptionTypeName] = desc->type();
				answer[kSessionDescriptionSdpName] = sdp;

				RTC_LOG(WARNING) << "here12";
			}
			else
			{
				RTC_LOG(WARNING) << "here13";
				RTC_LOG(LERROR) << "Failed to create answer";
			}
		}
	}

	RTC_LOG(WARNING) << "[peerid=" << peerid << "] returning answer:" << answer;
	return answer;
}

bool PeerConnectionManager::streamStillUsed(const std::string & streamLabel)
{
	bool stillUsed = false;
	for (auto it: peer_connectionobs_map_)
	{
		rtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection = it.second->getPeerConnection();
		rtc::scoped_refptr<webrtc::StreamCollectionInterface> localstreams (peerConnection->local_streams());
		for (unsigned int i = 0; i<localstreams->count(); i++)
		{
			if (localstreams->at(i)->label() == streamLabel)
			{
				stillUsed = true;
				break;
			}
		}
	}
	return stillUsed;
}

/* ---------------------------------------------------------------------------
**  hangup a call
** -------------------------------------------------------------------------*/
const Json::Value PeerConnectionManager::hangUp(const std::string &peerid)
{
	bool result = false;
	RTC_LOG(INFO) << __FUNCTION__ << " " << peerid;

	std::map<std::string, PeerConnectionObserver* >::iterator  it = peer_connectionobs_map_.find(peerid);
	if (it != peer_connectionobs_map_.end())
	{
		RTC_LOG(LS_ERROR) << "Close PeerConnection";
		PeerConnectionObserver* pcObserver = it->second;
		rtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection = pcObserver->getPeerConnection();
		peer_connectionobs_map_.erase(it);

		rtc::scoped_refptr<webrtc::StreamCollectionInterface> localstreams (peerConnection->local_streams());
		Json::Value streams;
		
		RTC_LOG(LS_ERROR) << "peerConnection->Close()";
		peerConnection->Close();
		delete pcObserver;
		RTC_LOG(LS_ERROR) << "done peerConnection->Close()";

		for (unsigned int i = 0; i<localstreams->count(); i++)
		{
			std::string streamLabel = localstreams->at(i)->label();

			bool stillUsed = this->streamStillUsed(streamLabel);
			if (!stillUsed)
			{
				RTC_LOG(LS_ERROR) << "Close PeerConnection no more used " << streamLabel;
				std::map<std::string, rtc::scoped_refptr<webrtc::MediaStreamInterface> >::iterator it = stream_map_.find(streamLabel);
				RTC_LOG(LS_ERROR) << "herehere";
				if (it != stream_map_.end())
				{

					RTC_LOG(LS_ERROR) << "herehere2";
					// remove video tracks
					while (it->second->GetVideoTracks().size() > 0)
					{
						RTC_LOG(LS_ERROR) << "herehere123";
						it->second->RemoveTrack(it->second->GetVideoTracks().at(0));
						RTC_LOG(LS_ERROR) << "herehere1233124";
					}

					RTC_LOG(LS_ERROR) << "herehere3";
					// remove audio tracks
					while (it->second->GetAudioTracks().size() > 0)
					{
						it->second->RemoveTrack(it->second->GetAudioTracks().at(0));
					}

					RTC_LOG(LS_ERROR) << "herehere4";

					it->second.release();
					RTC_LOG(LS_ERROR) << "herehere5";
					stream_map_.erase(it);
					RTC_LOG(LS_ERROR) << "herehere6";
				}

				RTC_LOG(LS_ERROR) << "herehere7";
			}

			RTC_LOG(LS_ERROR) << "herehere8";
		}

		RTC_LOG(LS_ERROR) << "herehere9";

		result = true;

		RTC_LOG(LS_ERROR) << "herehere10";
	}
	Json::Value answer;

	RTC_LOG(LS_ERROR) << "herehere11";
	if (result) {
		answer = result;
	}

	RTC_LOG(LS_ERROR) << "herehere12";

	return answer;
}


/* ---------------------------------------------------------------------------
**  get list ICE candidate associayed with a PeerConnection
** -------------------------------------------------------------------------*/
const Json::Value PeerConnectionManager::getIceCandidateList(const std::string &peerid)
{
	RTC_LOG(INFO) << __FUNCTION__;
	
	Json::Value value;
	std::map<std::string, PeerConnectionObserver* >::iterator  it = peer_connectionobs_map_.find(peerid);
	if (it != peer_connectionobs_map_.end())
	{
		PeerConnectionObserver* obs = it->second;
		if (obs)
		{
			value = obs->getIceCandidateList();
		}
		else
		{
			RTC_LOG(LS_ERROR) << "No observer for peer:" << peerid;
		}
	} else {
		RTC_LOG(WARNING) << __FUNCTION__ << "failed to getIceCandidateList";
	}
	return value;
}

/* ---------------------------------------------------------------------------
**  get PeerConnection list
** -------------------------------------------------------------------------*/
const Json::Value PeerConnectionManager::getPeerConnectionList()
{
	Json::Value value(Json::arrayValue);
	for (auto it : peer_connectionobs_map_)
	{
		Json::Value content;

		// get local SDP
		rtc::scoped_refptr<webrtc::PeerConnectionInterface> peerConnection = it.second->getPeerConnection();
		if ( (peerConnection) && (peerConnection->local_description()) ) {
			std::string sdp;
			peerConnection->local_description()->ToString(&sdp);
			content["sdp"] = sdp;

			Json::Value streams;
			rtc::scoped_refptr<webrtc::StreamCollectionInterface> localstreams (peerConnection->local_streams());
			if (localstreams) {
				for (unsigned int i = 0; i<localstreams->count(); i++) {
					if (localstreams->at(i)) {
						Json::Value tracks;
						
						const webrtc::VideoTrackVector& videoTracks = localstreams->at(i)->GetVideoTracks();
						for (unsigned int j=0; j<videoTracks.size() ; j++)
						{
							Json::Value track;
							tracks[videoTracks.at(j)->kind()].append(videoTracks.at(j)->id());
						}
						const webrtc::AudioTrackVector& audioTracks = localstreams->at(i)->GetAudioTracks();
						for (unsigned int j=0; j<audioTracks.size() ; j++)
						{
							Json::Value track;
							tracks[audioTracks.at(j)->kind()].append(audioTracks.at(j)->id());
						}
						
						Json::Value stream;
						stream[localstreams->at(i)->label()] = tracks;
						
						streams.append(stream);						
					}
				}
			}
			content["streams"] = streams;
		}
		
		// get Stats
		content["stats"] = it.second->getStats();

		Json::Value pc;
		pc[it.first] = content;
		value.append(pc);
	}
	return value;
}

/* ---------------------------------------------------------------------------
**  get StreamList list
** -------------------------------------------------------------------------*/
const Json::Value PeerConnectionManager::getStreamList()
{
	Json::Value value(Json::arrayValue);
	for (auto it : stream_map_)
	{
		value.append(it.first);
	}
	return value;
}

/* ---------------------------------------------------------------------------
**  check if factory is initialized
** -------------------------------------------------------------------------*/
bool PeerConnectionManager::InitializePeerConnection()
{
	return (peer_connection_factory_.get() != NULL);
}

/* ---------------------------------------------------------------------------
**  create a new PeerConnection
** -------------------------------------------------------------------------*/
PeerConnectionManager::PeerConnectionObserver*
PeerConnectionManager::CreatePeerConnection(
	const std::string& peerid,
	webrtc::PeerConnectionInterface::RTCConfiguration &config
	)
{
	RTC_LOG(WARNING) << __FUNCTION__ << "CreatePeerConnection called";
	webrtc::FakeConstraints constraints;
	constraints.AddOptional(webrtc::MediaConstraintsInterface::kEnableDtlsSrtp, "true");

	RTC_LOG(WARNING) << __FUNCTION__ << "here1010";

	PeerConnectionObserver* obs = new PeerConnectionObserver(this, peerid, config, constraints);

	RTC_LOG(WARNING) << __FUNCTION__ << "here2323010";

	if (!obs)
	{
		RTC_LOG(LERROR) << __FUNCTION__ << "CreatePeerConnection failed";
	}

	RTC_LOG(WARNING) << __FUNCTION__ << "here23452";
	return obs;
}

/* ---------------------------------------------------------------------------
**  get the capturer from its URL
** -------------------------------------------------------------------------*/
rtc::scoped_refptr<webrtc::VideoTrackInterface>
PeerConnectionManager::CreateVideoTrack(
	const std::string &pipename,
	const std::string &options)
{
	RTC_LOG(INFO) << "pipename:" << pipename << " options:" << options;
	rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track;

	std::unique_ptr<cricket::VideoCapturer> capturer;
	RTC_LOG(INFO) << "Using pipename for ZMQFrameReader:" << pipename;

	// int timeout = 10;
	// std::string tmp;
	// if (CivetServer::getParam(options, "timeout", tmp)) {
	// 	timeout = std::stoi(tmp);
	// }
	// std::string rtptransport;
	// CivetServer::getParam(options, "rtptransport", rtptransport);
	// capturer.reset(new RTSPVideoCapturer("rtsp://b1.dnsdojo.com:1935/live/sys3.stream", timeout, rtptransport));
	capturer.reset(new ZMQFrameReader(pipename));

	if (!capturer)
	{
		RTC_LOG(LS_ERROR) << "Cannot create capturer video for pipename:" << pipename;
	}
	else
	{
		rtc::scoped_refptr<webrtc::VideoTrackSourceInterface> videoSource = peer_connection_factory_->CreateVideoSource(std::move(capturer), NULL);
		video_track = peer_connection_factory_->CreateVideoTrack(kVideoLabel, videoSource);
	}
	return video_track;
}


rtc::scoped_refptr<webrtc::AudioTrackInterface>
PeerConnectionManager::CreateAudioTrack(
	const std::string &audiourl,
	const std::string &options)
{
	RTC_LOG(INFO) << "audiourl:" << audiourl << " options:" << options;

	rtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track;
	// nothing here, since we don't need audio for now
	return audio_track;
}
  
/* ---------------------------------------------------------------------------
**  Add a stream to a PeerConnection
** -------------------------------------------------------------------------*/
bool
PeerConnectionManager::AddStream(
	webrtc::PeerConnectionInterface* peer_connection,
	const std::string &videourl,
	const std::string &audiourl,
	const std::string &options)
{
	bool ret = false;

	std::string pipename = videourl;
	std::string audio = audiourl;
		
	// compute stream label removing space because SDP use label
	std::string streamLabel = pipename;
	streamLabel.erase(std::remove_if(streamLabel.begin(), streamLabel.end(), isspace), streamLabel.end());

	std::map<std::string, rtc::scoped_refptr<webrtc::MediaStreamInterface> >::iterator it = stream_map_.find(streamLabel);
	if (it == stream_map_.end())
	{
		// need to create the stream
		rtc::scoped_refptr<webrtc::VideoTrackInterface> video_track(this->CreateVideoTrack(pipename, options));
		rtc::scoped_refptr<webrtc::MediaStreamInterface> stream = peer_connection_factory_->CreateLocalMediaStream(streamLabel);
		if (!stream.get())
		{
			RTC_LOG(LS_ERROR) << "Cannot create stream";
		}
		else
		{
			if ( (video_track) && (!stream->AddTrack(video_track)) )
			{
				RTC_LOG(LS_ERROR) << "Adding VideoTrack to MediaStream failed";
			} 

			RTC_LOG(INFO) << "Adding Stream to map";
			stream_map_[streamLabel] = stream;
		}
	}


	it = stream_map_.find(streamLabel);
	if (it != stream_map_.end())
	{
		if (!peer_connection->AddStream(it->second))
		{
			RTC_LOG(LS_ERROR) << "Adding stream to PeerConnection failed";
		}
		else
		{
			RTC_LOG(INFO) << "stream added to PeerConnection";
			ret = true;
		}
	}
	else
	{
		RTC_LOG(LS_ERROR) << "Cannot find stream";
	}

	return ret;
}

/* ---------------------------------------------------------------------------
**  ICE callback
** -------------------------------------------------------------------------*/
void PeerConnectionManager::PeerConnectionObserver::OnIceCandidate(const webrtc::IceCandidateInterface* candidate)
{
	RTC_LOG(INFO) << __FUNCTION__ << " " << candidate->sdp_mline_index();

	std::string sdp;
	if (!candidate->ToString(&sdp))
	{
		RTC_LOG(LS_ERROR) << "Failed to serialize candidate";
	}
	else
	{
		RTC_LOG(INFO) << sdp;

		Json::Value jmessage;
		jmessage[kCandidateSdpMidName] = candidate->sdp_mid();
		jmessage[kCandidateSdpMlineIndexName] = candidate->sdp_mline_index();
		jmessage[kCandidateSdpName] = sdp;
		iceCandidateList_.append(jmessage);
	}
}


