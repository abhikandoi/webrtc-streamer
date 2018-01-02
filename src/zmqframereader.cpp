#define _GLIBCXX_USE_CXX11_ABI 0

#include "rtc_base/timeutils.h"
#include "rtc_base/logging.h"

#include <zmq.hpp>
#include <opencv2/opencv.hpp>
 
#include "api/video/i420_buffer.h"

#include "libyuv/video_common.h"
#include "libyuv/convert.h"

#include <vector>
#include <ctime>
#include <string>
#include <chrono>

#include "zmqframereader.h"

static const std::string base64_chars =
"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
"abcdefghijklmnopqrstuvwxyz"
"0123456789+/";


static inline bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

// helper func.
std::string base64_decode(std::string const& encoded_string) {
    int in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (i = 0; (i < 3); i++)
                ret += char_array_3[i];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 4; j++)
            char_array_4[j] = 0;

        for (j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);

        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

        for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
    }

    return ret;
}

ZMQFrameReader::ZMQFrameReader(const std::string &pipename): m_zmqctx(1), m_zmqsocket(m_zmqctx, ZMQ_SUB) {
	RTC_LOG(INFO) << "ZMQFrameReader" << pipename ;
	this->pipename = pipename;
	m_zmqsocket.connect (pipename);
    m_zmqsocket.setsockopt(ZMQ_SUBSCRIBE, "", 0);
}

ZMQFrameReader::~ZMQFrameReader() {
	rtc::Thread::Stop();
}

cricket::CaptureState ZMQFrameReader::Start(const cricket::VideoFormat& format)
{
	SetCaptureFormat(&format);
	SetCaptureState(cricket::CS_RUNNING);
	this->running = true;
	rtc::Thread::Start();
	return cricket::CS_RUNNING;
}

void ZMQFrameReader::Stop()
{
	this->running = false;
	rtc::Thread::Stop();
	SetCaptureFormat(NULL);
	SetCaptureState(cricket::CS_STOPPED);
}

void ZMQFrameReader::Run()
{
	// do here
    cv::Mat frame;
    int res = 0;
    auto start = std::chrono::high_resolution_clock::now();
    int64_t ts;

    while(this->running) {
    	zmq::message_t msg;
	    bool recvd = m_zmqsocket.recv(&msg, ZMQ_NOBLOCK);
	    if(recvd) {
	    	RTC_LOG(LS_VERBOSE) << "ZMQFrameReader::Run " << "recvd frame for pipename=" << this->pipename;

	    	auto elapsed = std::chrono::high_resolution_clock::now() - start;
	    	ts = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
	        std::string encoded_string = std::string(static_cast<char *>(msg.data()), msg.size());
	        std::string decoded_string = base64_decode(encoded_string);
	        std::vector<uchar> data(decoded_string.begin(), decoded_string.end());

	        cv::Mat frame = cv::imdecode(data, cv::IMREAD_UNCHANGED);
	        cv::Mat bgra(frame.rows, frame.cols, CV_8UC4);
	        //opencv reads the stream in BGR format by default
	        cv::cvtColor(frame, bgra, CV_BGR2BGRA);
	        
	        int32_t width = bgra.cols;
			int32_t height = bgra.rows;

			int stride_y = width;
			int stride_uv = (width + 1) / 2;

			rtc::scoped_refptr<webrtc::I420Buffer> I420buffer = webrtc::I420Buffer::Create(width, height, stride_y, stride_uv, stride_uv);
			const int conversionResult = libyuv::ConvertToI420((const uint8*)bgra.ptr(), 0,
							(uint8*)I420buffer->DataY(), I420buffer->StrideY(),
							(uint8*)I420buffer->DataU(), I420buffer->StrideU(),
							(uint8*)I420buffer->DataV(), I420buffer->StrideV(),
							0, 0,
							width, height,
							width, height,
							libyuv::kRotate0, ::libyuv::FOURCC_ARGB);									

			if (conversionResult >= 0) {
				webrtc::VideoFrame frame(I420buffer, 0, ts * 1000, webrtc::kVideoRotation_0);
				this->Decoded(frame);
			} else {
				RTC_LOG(LS_ERROR) << "ZMQFrameReader:Run decoder error:" << conversionResult;
				res = -1;
			}
	    } else {
	    	RTC_LOG(LS_VERBOSE) << "ZMQFrameReader::Run " << "no frame recvd for pipename=" << this->pipename;
	    }
    }

    return;
}


int32_t ZMQFrameReader::Decoded(webrtc::VideoFrame& decodedImage)
{
	if (decodedImage.timestamp_us() == 0) {
		decodedImage.set_timestamp_us(decodedImage.timestamp());
	}
	RTC_LOG(LS_VERBOSE) << "ZMQFrameReader::Decoded " << decodedImage.size() << " " << decodedImage.timestamp_us() << " " << decodedImage.timestamp() << " " << decodedImage.ntp_time_ms() << " " << decodedImage.render_time_ms();
	this->OnFrame(decodedImage, decodedImage.height(), decodedImage.width());
	return true;
}


bool ZMQFrameReader::GetPreferredFourccs(std::vector<unsigned int>* fourccs)
{
	return true;
}