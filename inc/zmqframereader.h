#ifndef ZMQFRAMEREADER_H_
#define ZMQFRAMEREADER_H_
#define _GLIBCXX_USE_CXX11_ABI 0
#include <string.h>

#include "rtc_base/thread.h"

#include "api/video_codecs/video_decoder.h"
#include "media/base/videocapturer.h"
#include "media/engine/internaldecoderfactory.h"

#include <zmq.hpp>

class ZMQFrameReader : public cricket::VideoCapturer, public rtc::Thread, public webrtc::DecodedImageCallback
{
	public:
		ZMQFrameReader(const std::string &pipename);
		virtual ~ZMQFrameReader();

		// overide webrtc::DecodedImageCallback
		virtual int32_t Decoded(webrtc::VideoFrame& decodedImage);
		
		// overide rtc::Thread
		virtual void Run();

		// overide cricket::VideoCapturer
		virtual cricket::CaptureState Start(const cricket::VideoFormat& format);
		virtual void Stop();
		virtual bool GetPreferredFourccs(std::vector<unsigned int>* fourccs);
		virtual bool IsScreencast() const { return false; };
		virtual bool IsRunning() { return this->capture_state() == cricket::CS_RUNNING; }

	private:
		std::vector<uint8_t>                  m_cfg;
		zmq::context_t                        m_zmqctx;
		zmq::socket_t                         m_zmqsocket;
		bool                                  running;
		std::string                           pipename;
};

#endif